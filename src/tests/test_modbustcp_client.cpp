#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <future>
#include <functional>

#include "driver_modbustcp/modbus_client.h"

namespace {

quint16 readUInt16BE(const QByteArray& data, int offset) {
    return static_cast<quint16>((static_cast<quint8>(data[offset]) << 8) |
                                static_cast<quint8>(data[offset + 1]));
}

QByteArray appendUInt16BE(QByteArray bytes, quint16 value) {
    bytes.append(static_cast<char>((value >> 8) & 0xFF));
    bytes.append(static_cast<char>(value & 0xFF));
    return bytes;
}

QByteArray buildModbusTcpResponse(const QByteArray& request, const QByteArray& pdu) {
    QByteArray response;
    response.append(request.left(2)); // Transaction ID
    response = appendUInt16BE(response, 0); // Protocol ID
    response = appendUInt16BE(response, static_cast<quint16>(pdu.size() + 1)); // Unit ID + PDU
    response.append(request[6]); // Unit ID
    response.append(pdu);
    return response;
}

struct ResponsePlan {
    QByteArray firstChunk;
    QByteArray secondChunk;
    int secondDelayMs = 0;
};

class FakeModbusTcpDevice {
public:
    FakeModbusTcpDevice() {
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                m_socket = socket;
                QObject::connect(socket, &QTcpSocket::readyRead, &m_server, [this, socket]() {
                    onReadyRead(socket);
                });
                QObject::connect(socket, &QTcpSocket::disconnected, &m_server, [this, socket]() {
                    if (m_socket == socket) {
                        m_socket.clear();
                    }
                    socket->deleteLater();
                });
            }
        });
    }

    ~FakeModbusTcpDevice() {
        if (m_socket) {
            m_socket->disconnectFromHost();
            m_socket->deleteLater();
        }
        m_server.close();
    }

    bool listen() {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const {
        return m_server.serverPort();
    }

    void setResponseBuilder(std::function<ResponsePlan(const QByteArray&)> builder) {
        m_responseBuilder = std::move(builder);
    }

private:
    void onReadyRead(QTcpSocket* socket) {
        if (!socket) {
            return;
        }

        m_requestBuffer.append(socket->readAll());
        while (m_requestBuffer.size() >= 7) {
            const quint16 length = readUInt16BE(m_requestBuffer, 4);
            const int frameLength = 6 + length;
            if (m_requestBuffer.size() < frameLength) {
                return;
            }

            const QByteArray request = m_requestBuffer.left(frameLength);
            m_requestBuffer.remove(0, frameLength);

            if (!m_responseBuilder) {
                continue;
            }

            const ResponsePlan plan = m_responseBuilder(request);
            if (!plan.firstChunk.isEmpty()) {
                socket->write(plan.firstChunk);
                socket->flush();
            }

            if (!plan.secondChunk.isEmpty()) {
                QTimer::singleShot(
                    plan.secondDelayMs,
                    &m_server,
                    [socketPtr = QPointer<QTcpSocket>(socket), second = plan.secondChunk]() {
                        if (!socketPtr) {
                            return;
                        }
                        socketPtr->write(second);
                        socketPtr->flush();
                    });
            }
        }
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    QByteArray m_requestBuffer;
    std::function<ResponsePlan(const QByteArray&)> m_responseBuilder;
};

template <typename T>
bool waitForFutureWithEvents(std::future<T>& future, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }

    return future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

} // namespace

class ModbusTcpClientFragmentedResponseTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_device.listen());
    }

    FakeModbusTcpDevice m_device;
};

TEST_F(ModbusTcpClientFragmentedResponseTest, SingleFrameReadHoldingRegistersSucceeds) {
    m_device.setResponseBuilder([](const QByteArray& request) {
        QByteArray pdu;
        pdu.append(static_cast<char>(static_cast<quint8>(modbus::FunctionCode::ReadHoldingRegisters)));
        pdu.append(static_cast<char>(4));
        pdu.append(static_cast<char>(0x12));
        pdu.append(static_cast<char>(0x34));
        pdu.append(static_cast<char>(0x56));
        pdu.append(static_cast<char>(0x78));

        ResponsePlan plan;
        plan.firstChunk = buildModbusTcpResponse(request, pdu);
        return plan;
    });

    auto future = std::async(std::launch::async, [port = m_device.port()]() {
        modbus::ModbusClient client(800);
        if (!client.connectToServer(QStringLiteral("127.0.0.1"), port)) {
            return modbus::ModbusResult{false, modbus::ExceptionCode::None, QStringLiteral("Connect failed"), {}, {}};
        }
        return client.readHoldingRegisters(0, 2);
    });

    ASSERT_TRUE(waitForFutureWithEvents(future, 2000));
    const modbus::ModbusResult result = future.get();
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    ASSERT_EQ(result.registers.size(), 2);
    EXPECT_EQ(result.registers[0], 0x1234);
    EXPECT_EQ(result.registers[1], 0x5678);
}

TEST_F(ModbusTcpClientFragmentedResponseTest, FragmentedReadHoldingRegistersWaitsForFullFrame) {
    m_device.setResponseBuilder([](const QByteArray& request) {
        QByteArray pdu;
        pdu.append(static_cast<char>(static_cast<quint8>(modbus::FunctionCode::ReadHoldingRegisters)));
        pdu.append(static_cast<char>(4));
        pdu.append(static_cast<char>(0x12));
        pdu.append(static_cast<char>(0x34));
        pdu.append(static_cast<char>(0x56));
        pdu.append(static_cast<char>(0x78));

        const QByteArray response = buildModbusTcpResponse(request, pdu);

        ResponsePlan plan;
        plan.firstChunk = response.left(8);
        plan.secondChunk = response.mid(8);
        plan.secondDelayMs = 25;
        return plan;
    });

    auto future = std::async(std::launch::async, [port = m_device.port()]() {
        modbus::ModbusClient client(800);
        if (!client.connectToServer(QStringLiteral("127.0.0.1"), port)) {
            return modbus::ModbusResult{false, modbus::ExceptionCode::None, QStringLiteral("Connect failed"), {}, {}};
        }
        return client.readHoldingRegisters(0, 2);
    });

    ASSERT_TRUE(waitForFutureWithEvents(future, 2000));
    const modbus::ModbusResult result = future.get();
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    ASSERT_EQ(result.registers.size(), 2);
    EXPECT_EQ(result.registers[0], 0x1234);
    EXPECT_EQ(result.registers[1], 0x5678);
}

TEST_F(ModbusTcpClientFragmentedResponseTest, FragmentedWriteSingleRegisterWaitsForFullFrame) {
    m_device.setResponseBuilder([](const QByteArray& request) {
        const QByteArray pdu = request.mid(7, 5); // Echo FC + address + value
        const QByteArray response = buildModbusTcpResponse(request, pdu);

        ResponsePlan plan;
        plan.firstChunk = response.left(8);
        plan.secondChunk = response.mid(8);
        plan.secondDelayMs = 25;
        return plan;
    });

    auto future = std::async(std::launch::async, [port = m_device.port()]() {
        modbus::ModbusClient client(800);
        if (!client.connectToServer(QStringLiteral("127.0.0.1"), port)) {
            return modbus::ModbusResult{false, modbus::ExceptionCode::None, QStringLiteral("Connect failed"), {}, {}};
        }
        return client.writeSingleRegister(3, 0x55AA);
    });

    ASSERT_TRUE(waitForFutureWithEvents(future, 2000));
    const modbus::ModbusResult result = future.get();
    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_TRUE(result.errorMessage.isEmpty());
}
