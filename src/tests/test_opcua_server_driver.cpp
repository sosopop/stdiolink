#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QTcpServer>
#include <QThread>

#include <atomic>

extern "C" {
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
}

#include "driver_opcua_server/handler.h"
#include "opcua_common.h"

namespace {

class MockResponder : public stdiolink::IResponder {
public:
    QString lastStatus;
    int lastCode = -1;
    QJsonObject lastData;
    QVector<QPair<QString, QJsonObject>> events;

    void done(int code, const QJsonValue& payload) override {
        QMutexLocker locker(&m_mutex);
        lastStatus = "done";
        lastCode = code;
        lastData = payload.toObject();
    }

    void error(int code, const QJsonValue& payload) override {
        QMutexLocker locker(&m_mutex);
        lastStatus = "error";
        lastCode = code;
        lastData = payload.toObject();
    }

    void event(int code, const QJsonValue& payload) override {
        Q_UNUSED(code);
        Q_UNUSED(payload);
    }

    void event(const QString& eventName, int code, const QJsonValue& data) override {
        Q_UNUSED(code);
        QMutexLocker locker(&m_mutex);
        events.append({eventName, data.toObject()});
    }

    void reset() {
        QMutexLocker locker(&m_mutex);
        lastStatus.clear();
        lastCode = -1;
        lastData = QJsonObject{};
    }

    int eventCount(const QString& name) {
        QMutexLocker locker(&m_mutex);
        int count = 0;
        for (const auto& pair : events) {
            if (pair.first == name) {
                ++count;
            }
        }
        return count;
    }

    QJsonObject latestEvent(const QString& name) {
        QMutexLocker locker(&m_mutex);
        for (int i = events.size() - 1; i >= 0; --i) {
            if (events.at(i).first == name) {
                return events.at(i).second;
            }
        }
        return {};
    }

private:
    QMutex m_mutex;
};

quint16 allocateLocalPort() {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return server.serverPort();
}

class UaClientHandle {
public:
    ~UaClientHandle() {
        if (m_client) {
            UA_Client_disconnect(m_client);
            UA_Client_delete(m_client);
        }
    }

    bool connect(const QString& host, quint16 port, QString* errorMessage = nullptr) {
        UA_ClientConfig config{};
        config.logging = opcua_common::silentLogger();
        UA_StatusCode statusCode = UA_ClientConfig_setDefault(&config);
        if (statusCode != UA_STATUSCODE_GOOD) {
            if (errorMessage) {
                *errorMessage = opcua_common::formatStatusMessage(
                    "UA_ClientConfig_setDefault failed",
                    statusCode);
            }
            return false;
        }

        m_client = UA_Client_newWithConfig(&config);
        if (!m_client) {
            if (errorMessage) {
                *errorMessage = "Failed to allocate OPC UA client";
            }
            return false;
        }

        const QString endpoint = QString("opc.tcp://%1:%2").arg(host).arg(port);
        const QByteArray endpointUtf8 = endpoint.toUtf8();
        statusCode = UA_Client_connect(m_client, endpointUtf8.constData());
        if (statusCode != UA_STATUSCODE_GOOD) {
            if (errorMessage) {
                *errorMessage = opcua_common::formatStatusMessage(
                    "UA_Client_connect failed",
                    statusCode);
            }
            return false;
        }
        return true;
    }

    UA_Client* client() const { return m_client; }

private:
    UA_Client* m_client = nullptr;
};

bool readDoubleValue(UA_Client* client, const QString& nodeIdText, double& value) {
    UA_NodeId nodeId;
    QString normalized;
    QString error;
    if (!opcua_common::parseNodeIdString(nodeIdText, nodeId, normalized, &error)) {
        return false;
    }
    UA_Variant variant;
    UA_Variant_init(&variant);
    const UA_StatusCode statusCode = UA_Client_readValueAttribute(client, nodeId, &variant);
    UA_NodeId_clear(&nodeId);
    if (statusCode != UA_STATUSCODE_GOOD || !UA_Variant_isScalar(&variant)
        || variant.type != &UA_TYPES[UA_TYPES_DOUBLE]) {
        UA_Variant_clear(&variant);
        return false;
    }
    value = *static_cast<UA_Double*>(variant.data);
    UA_Variant_clear(&variant);
    return true;
}

UA_StatusCode writeDoubleValue(UA_Client* client, const QString& nodeIdText, double value) {
    UA_NodeId nodeId;
    QString normalized;
    QString error;
    if (!opcua_common::parseNodeIdString(nodeIdText, nodeId, normalized, &error)) {
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }
    const UA_StatusCode statusCode =
        UA_Client_writeValueAttribute_scalar(client, nodeId, &value, &UA_TYPES[UA_TYPES_DOUBLE]);
    UA_NodeId_clear(&nodeId);
    return statusCode;
}

QJsonObject childByNodeId(const QJsonObject& node, const QString& nodeId) {
    const QJsonArray children = node.value("children").toArray();
    for (const QJsonValue& childValue : children) {
        const QJsonObject child = childValue.toObject();
        if (child.value("node_id").toString() == nodeId) {
            return child;
        }
    }
    return {};
}

class OpcUaServerHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int argc = 1;
        static char arg0[] = "test";
        static char* argv[] = {arg0};
        if (!QCoreApplication::instance()) {
            new QCoreApplication(argc, argv);
        }
        handler.setEventResponder(&responder);
        port = allocateLocalPort();
        ASSERT_GT(port, 0);
    }

    void TearDown() override {
        responder.reset();
        handler.handle("status", QJsonObject{}, responder);
        if (responder.lastStatus == "done" && responder.lastData.value("running").toBool()) {
            responder.reset();
            handler.handle("stop_server", QJsonObject{}, responder);
        }
    }

    void startServer() {
        responder.reset();
        handler.handle("start_server",
                       QJsonObject{{"bind_host", "127.0.0.1"}, {"listen_port", port}},
                       responder);
        ASSERT_EQ(responder.lastStatus, "done");
        ASSERT_EQ(responder.lastCode, 0);
    }

    QJsonArray sampleNodes(bool includeUInt64 = false) const {
        QJsonArray nodes{
            QJsonObject{
                {"node_id", "ns=1;s=Plant.Line1.Temp"},
                {"parent_node_id", "ns=1;s=Plant.Line1"},
                {"node_class", "variable"},
                {"browse_name", "Temp"},
                {"display_name", "Temp"},
                {"description", "Process temperature"},
                {"data_type", "double"},
                {"access", "read_only"},
                {"initial_value", 36.5}
            },
            QJsonObject{
                {"node_id", "ns=1;s=Plant.Line1.SetPoint"},
                {"parent_node_id", "ns=1;s=Plant.Line1"},
                {"node_class", "variable"},
                {"browse_name", "SetPoint"},
                {"display_name", "SetPoint"},
                {"description", "Temperature setpoint"},
                {"data_type", "double"},
                {"access", "read_write"},
                {"initial_value", 40.0}
            },
            QJsonObject{
                {"node_id", "ns=1;s=Plant.Line1"},
                {"parent_node_id", "ns=1;s=Plant"},
                {"node_class", "folder"},
                {"browse_name", "Line1"},
                {"display_name", "Line1"},
                {"description", "Line 1"}
            },
            QJsonObject{
                {"node_id", "ns=1;s=Plant"},
                {"parent_node_id", "i=85"},
                {"node_class", "folder"},
                {"browse_name", "Plant"},
                {"display_name", "Plant"},
                {"description", "Plant root"}
            }
        };

        if (includeUInt64) {
            nodes.append(QJsonObject{
                {"node_id", "ns=1;s=Plant.Line1.Counter"},
                {"parent_node_id", "ns=1;s=Plant.Line1"},
                {"node_class", "variable"},
                {"browse_name", "Counter"},
                {"display_name", "Counter"},
                {"description", "UInt64 counter"},
                {"data_type", "uint64"},
                {"access", "read_write"},
                {"initial_value", "18446744073709551615"}
            });
        }

        return nodes;
    }

    OpcUaServerHandler handler;
    MockResponder responder;
    quint16 port = 0;
};

} // namespace

TEST_F(OpcUaServerHandlerTest, StatusReflectsStartAndStopState) {
    handler.handle("status", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "done");
    EXPECT_FALSE(responder.lastData.value("running").toBool());

    startServer();

    responder.reset();
    handler.handle("status", QJsonObject{}, responder);
    EXPECT_TRUE(responder.lastData.value("running").toBool());
    EXPECT_EQ(responder.lastData.value("namespace_index").toInt(), 1);
    EXPECT_EQ(responder.lastData.value("event_mode").toString(), "write");
}

TEST_F(OpcUaServerHandlerTest, RunKeepsServerAliveWithoutTerminalDoneResponse) {
    const int startedBefore = responder.eventCount("started");

    responder.reset();
    handler.handle("run",
                   QJsonObject{
                       {"bind_host", "127.0.0.1"},
                       {"listen_port", port},
                       {"nodes", sampleNodes()}
                   },
                   responder);

    EXPECT_TRUE(responder.lastStatus.isEmpty());
    EXPECT_EQ(responder.lastCode, -1);
    EXPECT_EQ(responder.eventCount("started"), startedBefore + 1);

    const QJsonObject started = responder.latestEvent("started");
    ASSERT_FALSE(started.isEmpty());
    EXPECT_EQ(started.value("namespace_uri").toString(), "urn:stdiolink:opcua:nodes");
    EXPECT_EQ(started.value("namespace_index").toInt(), 1);
    EXPECT_EQ(started.value("event_mode").toString(), "write");
    EXPECT_GE(started.value("node_count").toInt(), sampleNodes().size());
    EXPECT_EQ(started.value("upserted").toArray().size(), sampleNodes().size());

    UaClientHandle client;
    QString errorMessage;
    ASSERT_TRUE(client.connect("127.0.0.1", port, &errorMessage)) << errorMessage.toStdString();

    double value = 0.0;
    ASSERT_TRUE(readDoubleValue(client.client(), "ns=1;s=Plant.Line1.Temp", value));
    EXPECT_DOUBLE_EQ(value, 36.5);
}

TEST_F(OpcUaServerHandlerTest, RunReturnsTerminalErrorWhenBootstrapNodesFail) {
    responder.reset();
    handler.handle("run",
                   QJsonObject{
                       {"bind_host", "127.0.0.1"},
                       {"listen_port", port},
                       {"nodes", QJsonArray{
                           QJsonObject{
                               {"node_id", "ns=1;s=Plant.LineX.Temp"},
                               {"parent_node_id", "ns=1;s=MissingParent"},
                               {"node_class", "variable"},
                               {"browse_name", "Temp"},
                               {"display_name", "Temp"},
                               {"data_type", "double"},
                               {"access", "read_only"},
                               {"initial_value", 1.0}
                           }
                       }}
                   },
                   responder);

    ASSERT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 2);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("unresolved parent_node_id"));

    responder.reset();
    handler.handle("status", QJsonObject{}, responder);
    ASSERT_EQ(responder.lastStatus, "done");
    EXPECT_FALSE(responder.lastData.value("running").toBool());
}

TEST_F(OpcUaServerHandlerTest, UpsertNodesSupportsUnorderedPayloadAndSnapshot) {
    startServer();

    responder.reset();
    handler.handle("upsert_nodes", QJsonObject{{"nodes", sampleNodes()}}, responder);
    ASSERT_EQ(responder.lastStatus, "done");
    ASSERT_EQ(responder.lastCode, 0);
    ASSERT_EQ(responder.lastData.value("results").toArray().size(), 4);

    responder.reset();
    handler.handle("snapshot_nodes", QJsonObject{}, responder);
    ASSERT_EQ(responder.lastStatus, "done");
    const QJsonObject root = responder.lastData.value("node").toObject();
    const QJsonObject plant = childByNodeId(root, "ns=1;s=Plant");
    ASSERT_FALSE(plant.isEmpty());
    const QJsonObject line1 = childByNodeId(plant, "ns=1;s=Plant.Line1");
    ASSERT_FALSE(line1.isEmpty());
    const QJsonObject temp = childByNodeId(line1, "ns=1;s=Plant.Line1.Temp");
    ASSERT_FALSE(temp.isEmpty());
    EXPECT_DOUBLE_EQ(temp.value("value").toDouble(), 36.5);
}

TEST_F(OpcUaServerHandlerTest, UpsertNodesRejectsMissingParent) {
    startServer();

    responder.reset();
    handler.handle("upsert_nodes",
                   QJsonObject{{"nodes", QJsonArray{
                       QJsonObject{
                           {"node_id", "ns=1;s=Plant.LineX.Temp"},
                           {"parent_node_id", "ns=1;s=MissingParent"},
                           {"node_class", "variable"},
                           {"browse_name", "Temp"},
                           {"display_name", "Temp"},
                           {"data_type", "double"},
                           {"access", "read_only"},
                           {"initial_value", 1.0}
                       }
                   }}},
                   responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 2);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("unresolved parent_node_id"));
}

TEST_F(OpcUaServerHandlerTest, WriteValuesRejectsReadOnlyAndSupportsUInt64Strings) {
    startServer();

    responder.reset();
    handler.handle("upsert_nodes", QJsonObject{{"nodes", sampleNodes(true)}}, responder);
    ASSERT_EQ(responder.lastStatus, "done");

    responder.reset();
    handler.handle("write_values",
                   QJsonObject{{"items", QJsonArray{
                       QJsonObject{
                           {"node_id", "ns=1;s=Plant.Line1.Counter"},
                           {"value", "42"}
                       }
                   }}},
                   responder);
    ASSERT_EQ(responder.lastStatus, "done");
    ASSERT_EQ(responder.lastCode, 0);

    responder.reset();
    handler.handle("inspect_node",
                   QJsonObject{{"node_id", "ns=1;s=Plant.Line1.Counter"}},
                   responder);
    ASSERT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastData.value("node").toObject().value("value").toString(), "42");

    responder.reset();
    handler.handle("write_values",
                   QJsonObject{{"items", QJsonArray{
                       QJsonObject{
                           {"node_id", "ns=1;s=Plant.Line1.Temp"},
                           {"value", 50.0}
                       }
                   }}},
                   responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("read_only"));
}

TEST_F(OpcUaServerHandlerTest, DeleteNodesRequiresRecursiveForFolder) {
    startServer();

    responder.reset();
    handler.handle("upsert_nodes", QJsonObject{{"nodes", sampleNodes()}}, responder);
    ASSERT_EQ(responder.lastStatus, "done");

    responder.reset();
    handler.handle("delete_nodes",
                   QJsonObject{
                       {"node_ids", QJsonArray{"ns=1;s=Plant"}},
                       {"recursive", false}
                   },
                   responder);
    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("recursive=true"));

    responder.reset();
    handler.handle("delete_nodes",
                   QJsonObject{
                       {"node_ids", QJsonArray{"ns=1;s=Plant"}},
                       {"recursive", true}
                   },
                   responder);
    EXPECT_EQ(responder.lastStatus, "done");
}

TEST_F(OpcUaServerHandlerTest, ExternalClientWriteEmitsEventAndUpdatesNodeValue) {
    startServer();

    responder.reset();
    handler.handle("upsert_nodes", QJsonObject{{"nodes", sampleNodes()}}, responder);
    ASSERT_EQ(responder.lastStatus, "done");

    UaClientHandle client;
    QString errorMessage;
    ASSERT_TRUE(client.connect("127.0.0.1", port, &errorMessage)) << errorMessage.toStdString();

    double initialValue = 0.0;
    ASSERT_TRUE(readDoubleValue(client.client(), "ns=1;s=Plant.Line1.SetPoint", initialValue));
    EXPECT_DOUBLE_EQ(initialValue, 40.0);

    ASSERT_EQ(writeDoubleValue(client.client(), "ns=1;s=Plant.Line1.SetPoint", 45.5), UA_STATUSCODE_GOOD);
    EXPECT_NE(writeDoubleValue(client.client(), "ns=1;s=Plant.Line1.Temp", 50.0), UA_STATUSCODE_GOOD);

    for (int i = 0; i < 40 && responder.eventCount("node_value_changed") == 0; ++i) {
        QThread::msleep(25);
    }

    const QJsonObject event = responder.latestEvent("node_value_changed");
    ASSERT_FALSE(event.isEmpty());
    EXPECT_EQ(event.value("node_id").toString(), "ns=1;s=Plant.Line1.SetPoint");
    EXPECT_EQ(event.value("source").toString(), "external_write");
    EXPECT_DOUBLE_EQ(event.value("value").toDouble(), 45.5);

    responder.reset();
    handler.handle("inspect_node",
                   QJsonObject{{"node_id", "ns=1;s=Plant.Line1.SetPoint"}},
                   responder);
    ASSERT_EQ(responder.lastStatus, "done");
    EXPECT_DOUBLE_EQ(responder.lastData.value("node").toObject().value("value").toDouble(), 45.5);
}

TEST_F(OpcUaServerHandlerTest, MetadataContainsExpectedServerCommands) {
    const auto& meta = handler.driverMeta();
    EXPECT_EQ(meta.info.id, "stdio.drv.opcua_server");
    EXPECT_EQ(meta.info.profiles, QStringList({"keepalive"}));
    EXPECT_NE(meta.findCommand("run"), nullptr);
    EXPECT_NE(meta.findCommand("start_server"), nullptr);
    EXPECT_NE(meta.findCommand("stop_server"), nullptr);
    EXPECT_NE(meta.findCommand("upsert_nodes"), nullptr);
    EXPECT_NE(meta.findCommand("delete_nodes"), nullptr);
    EXPECT_NE(meta.findCommand("write_values"), nullptr);
    EXPECT_NE(meta.findCommand("inspect_node"), nullptr);
    EXPECT_NE(meta.findCommand("snapshot_nodes"), nullptr);

    const auto* runCommand = meta.findCommand("run");
    ASSERT_NE(runCommand, nullptr);
    ASSERT_FALSE(runCommand->examples.isEmpty());
    const QJsonArray runNodes = runCommand->examples.first().value("params").toObject().value("nodes").toArray();
    ASSERT_EQ(runNodes.size(), 3);
    EXPECT_EQ(runNodes.at(0).toObject().value("node_class").toString(), "folder");
    EXPECT_EQ(runNodes.at(1).toObject().value("node_id").toString(), "ns=1;s=Plant.Temp");
    EXPECT_EQ(runNodes.at(2).toObject().value("node_id").toString(), "ns=1;s=Plant.SetPoint");
}
