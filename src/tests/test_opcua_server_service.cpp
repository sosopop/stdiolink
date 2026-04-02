#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QThread>

extern "C" {
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
}

#include "opcua_common.h"
#include "stdiolink/platform/platform_utils.h"

namespace {

QString findRepoRootPath() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo::exists(dir.absoluteFilePath("src/data_root/services"))
            && QFileInfo::exists(dir.absoluteFilePath("doc/knowledge/README.md"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

QString runtimeDataRootPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        QDir(appDir).absoluteFilePath("../data_root"),
        QDir(appDir).absoluteFilePath("data_root"),
        QDir(appDir).absoluteFilePath("../runtime_debug/data_root"),
        QDir(appDir).absoluteFilePath("../runtime_release/data_root"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate + "/services")
            && QFileInfo::exists(candidate + "/drivers")) {
            return candidate;
        }
    }
    return {};
}

QString serviceExecutablePath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        stdiolink::PlatformUtils::executablePath(appDir, "stdiolink_service"),
        stdiolink::PlatformUtils::executablePath(
            QDir(appDir).absoluteFilePath("../runtime_release/bin"), "stdiolink_service"),
        stdiolink::PlatformUtils::executablePath(
            QDir(appDir).absoluteFilePath("../runtime_debug/bin"), "stdiolink_service"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.front();
}

QString opcUaServerServiceDirPath() {
    const QString runtimeDataRoot = runtimeDataRootPath();
    if (!runtimeDataRoot.isEmpty()) {
        const QString runtimeServiceDir =
            QDir(runtimeDataRoot).absoluteFilePath("services/opcua_server_service");
        if (QFileInfo::exists(runtimeServiceDir + "/index.js")) {
            return runtimeServiceDir;
        }
    }

    const QString repoRoot = findRepoRootPath();
    if (repoRoot.isEmpty()) {
        return {};
    }
    return QDir(repoRoot).absoluteFilePath("src/data_root/services/opcua_server_service");
}

QProcessEnvironment childEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList extraDirs{
        appDir,
        QDir(appDir).absoluteFilePath("../runtime_release/bin"),
        QDir(appDir).absoluteFilePath("../runtime_debug/bin"),
    };
    QStringList existingDirs;
    for (const QString& dir : extraDirs) {
        if (QFileInfo::exists(dir)) {
            existingDirs.append(dir);
        }
    }
    const QString oldPath = env.value("PATH");
    const QString extraPath = existingDirs.join(QDir::listSeparator());
    env.insert("PATH", oldPath.isEmpty() ? extraPath : (extraPath + QDir::listSeparator() + oldPath));
    return env;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(20);
    }
    return predicate();
}

quint16 allocateLocalPort() {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return server.serverPort();
}

bool writeJsonFile(const QString& path, const QJsonObject& object, QString* errorMessage = nullptr) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QString("failed to open %1 for writing").arg(path);
        }
        return false;
    }
    const QByteArray content = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size()) {
        if (errorMessage) {
            *errorMessage = QString("failed to write %1").arg(path);
        }
        return false;
    }
    return true;
}

class UaClientHandle {
public:
    ~UaClientHandle() {
        if (m_client) {
            UA_Client_disconnect(m_client);
            UA_Client_delete(m_client);
        }
    }

    bool connect(const QString& endpoint, QString* errorMessage = nullptr) {
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

bool readStringValue(UA_Client* client, const QString& nodeIdText, QString& value) {
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
        || variant.type != &UA_TYPES[UA_TYPES_STRING]) {
        UA_Variant_clear(&variant);
        return false;
    }
    value = opcua_common::uaStringToQString(*static_cast<UA_String*>(variant.data));
    UA_Variant_clear(&variant);
    return true;
}

QJsonObject sampleConfig(quint16 port) {
    return QJsonObject{
        {"bind_host", "127.0.0.1"},
        {"listen_port", static_cast<int>(port)},
        {"endpoint_path", ""},
        {"server_name", "stdiolink OPC UA Server"},
        {"application_uri", "urn:stdiolink:opcua:server"},
        {"namespace_uri", "urn:stdiolink:opcua:nodes"},
        {"event_mode", "write"},
        {"nodes", QJsonArray{
            QJsonObject{
                {"node_id", "ns=1;s=Plant"},
                {"parent_node_id", "i=85"},
                {"node_class", "folder"},
                {"browse_name", "Plant"},
                {"display_name", "Plant"},
                {"description", "Plant root"}
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
                {"node_id", "ns=1;s=Plant.Line1.Mode"},
                {"parent_node_id", "ns=1;s=Plant.Line1"},
                {"node_class", "variable"},
                {"browse_name", "Mode"},
                {"display_name", "Mode"},
                {"description", "Operating mode"},
                {"data_type", "string"},
                {"access", "read_write"},
                {"initial_value", "Auto"}
            }
        }}
    };
}

QString readProcessOutput(QProcess& process) {
    return QString::fromUtf8(process.readAllStandardOutput())
        + "\n[stderr]\n"
        + QString::fromUtf8(process.readAllStandardError());
}

void stopProcess(QProcess& process) {
    if (process.state() == QProcess::NotRunning) {
        return;
    }
    process.terminate();
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(3000);
    }
}

} // namespace

class OpcUaServerServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_serviceExe = serviceExecutablePath();
        ASSERT_TRUE(QFileInfo::exists(m_serviceExe))
            << "stdiolink_service not found: " << qPrintable(m_serviceExe);

        m_dataRoot = runtimeDataRootPath();
        ASSERT_FALSE(m_dataRoot.isEmpty()) << "runtime data_root not found";
        ASSERT_TRUE(QFileInfo::exists(m_dataRoot + "/drivers/stdio.drv.opcua_server"))
            << "runtime driver dir not found under data_root: " << qPrintable(m_dataRoot);

        m_serviceDir = opcUaServerServiceDirPath();
        ASSERT_FALSE(m_serviceDir.isEmpty()) << "opcua_server_service dir not found";
        ASSERT_TRUE(QFileInfo::exists(m_serviceDir + "/index.js"))
            << "index.js not found in " << qPrintable(m_serviceDir);
    }

    void startService(QProcess& process, const QString& configFilePath) const {
        process.setProcessEnvironment(childEnv());
        process.start(m_serviceExe,
                      QStringList{
                          m_serviceDir,
                          "--data-root=" + m_dataRoot,
                          "--config-file=" + configFilePath,
                      });
    }

    QString m_serviceExe;
    QString m_serviceDir;
    QString m_dataRoot;
};

TEST_F(OpcUaServerServiceTest, StartsAndServesConfiguredNodes) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const quint16 port = allocateLocalPort();
    ASSERT_GT(port, 0);

    const QString configPath = tempDir.filePath("config.json");
    QString writeError;
    ASSERT_TRUE(writeJsonFile(configPath, sampleConfig(port), &writeError))
        << writeError.toStdString();

    QProcess process;
    startService(process, configPath);
    ASSERT_TRUE(process.waitForStarted(5000))
        << "failed to start stdiolink_service\n" << qPrintable(readProcessOutput(process));

    const QString endpoint = QString("opc.tcp://127.0.0.1:%1").arg(port);
    const bool ready = waitUntil([&]() {
        if (process.state() == QProcess::NotRunning) {
            return false;
        }

        UaClientHandle client;
        QString errorMessage;
        if (!client.connect(endpoint, &errorMessage)) {
            return false;
        }

        double setPoint = 0.0;
        QString mode;
        return readDoubleValue(client.client(), "ns=1;s=Plant.Line1.SetPoint", setPoint)
            && setPoint == 40.0
            && readStringValue(client.client(), "ns=1;s=Plant.Line1.Mode", mode)
            && mode == "Auto";
    }, 10000);

    stopProcess(process);

    ASSERT_TRUE(ready)
        << "service did not expose configured nodes in time\n"
        << qPrintable(readProcessOutput(process));
}

TEST_F(OpcUaServerServiceTest, InvalidNodeDefinitionFailsFast) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const quint16 port = allocateLocalPort();
    ASSERT_GT(port, 0);

    QJsonObject config = sampleConfig(port);
    QJsonArray nodes = config.value("nodes").toArray();
    nodes[0] = QJsonObject{
        {"node_id", "ns=1;s=Plant"},
        {"parent_node_id", "i=85"},
        {"node_class", "bogus"},
        {"browse_name", "Plant"},
        {"display_name", "Plant"}
    };
    config["nodes"] = nodes;

    const QString configPath = tempDir.filePath("config.json");
    QString writeError;
    ASSERT_TRUE(writeJsonFile(configPath, config, &writeError))
        << writeError.toStdString();

    QProcess process;
    startService(process, configPath);
    ASSERT_TRUE(process.waitForStarted(5000))
        << "failed to start stdiolink_service\n" << qPrintable(readProcessOutput(process));
    ASSERT_TRUE(process.waitForFinished(10000))
        << "service should exit on invalid node definition";

    const QString output = readProcessOutput(process);
    EXPECT_NE(process.exitCode(), 0) << qPrintable(output);
    EXPECT_TRUE(output.contains("invalid enum value")
                || output.contains("node_class")
                || output.contains("bogus"))
        << qPrintable(output);
}
