#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QThread>

#include <atomic>
#include <thread>

extern "C" {
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/types.h>
#include <open62541/types_generated.h>
}

#include "driver_opcua/handler.h"

namespace {

class JsonResponder : public stdiolink::IResponder {
public:
    QString lastStatus;
    int lastCode = -1;
    QJsonObject lastData;

    void done(int code, const QJsonValue& payload) override {
        lastStatus = "done";
        lastCode = code;
        lastData = payload.toObject();
    }

    void error(int code, const QJsonValue& payload) override {
        lastStatus = "error";
        lastCode = code;
        lastData = payload.toObject();
    }

    void event(int code, const QJsonValue& payload) override {
        Q_UNUSED(code);
        Q_UNUSED(payload);
    }

    void event(const QString& name, int code, const QJsonValue& data) override {
        Q_UNUSED(name);
        Q_UNUSED(code);
        Q_UNUSED(data);
    }
};

quint16 allocateLocalPort() {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return server.serverPort();
}

QJsonObject childByBrowseName(const QJsonObject& node, const QString& browseName) {
    const QJsonArray children = node.value("children").toArray();
    for (const QJsonValue& childValue : children) {
        const QJsonObject child = childValue.toObject();
        if (child.value("browse_name").toString() == browseName) {
            return child;
        }
    }
    return {};
}

const stdiolink::meta::FieldMeta* findParam(const stdiolink::meta::CommandMeta* command,
                                            const QString& name) {
    if (!command) {
        return nullptr;
    }
    for (const auto& param : command->params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

class OpcUaTestServer {
public:
    OpcUaTestServer() = default;

    ~OpcUaTestServer() {
        stop();
    }

    bool start(QString* errorMessage = nullptr) {
        m_port = allocateLocalPort();
        if (m_port == 0) {
            if (errorMessage) {
                *errorMessage = "failed to allocate test port";
            }
            return false;
        }

        m_server = UA_Server_new();
        if (!m_server) {
            if (errorMessage) {
                *errorMessage = "failed to create UA_Server";
            }
            return false;
        }

        UA_ServerConfig* config = UA_Server_getConfig(m_server);
        UA_StatusCode statusCode = UA_ServerConfig_setMinimal(config, m_port, nullptr);
        if (statusCode != UA_STATUSCODE_GOOD) {
            if (errorMessage) {
                *errorMessage = QString("UA_ServerConfig_setMinimal failed: %1")
                                    .arg(UA_StatusCode_name(statusCode));
            }
            return false;
        }

        m_namespaceIndex = UA_Server_addNamespace(m_server, "urn:stdiolink:opcua:test");
        createNodes();

        statusCode = UA_Server_run_startup(m_server);
        if (statusCode != UA_STATUSCODE_GOOD) {
            if (errorMessage) {
                *errorMessage = QString("UA_Server_run_startup failed: %1")
                                    .arg(UA_StatusCode_name(statusCode));
            }
            return false;
        }

        m_running.store(true);
        m_thread = std::thread([this]() {
            while (m_running.load()) {
                UA_Server_run_iterate(m_server, false);
                QThread::msleep(5);
            }
        });
        QThread::msleep(50);
        return true;
    }

    void stop() {
        m_running.store(false);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_server) {
            UA_Server_run_shutdown(m_server);
            UA_Server_delete(m_server);
            m_server = nullptr;
        }
    }

    QString host() const { return "127.0.0.1"; }
    int port() const { return m_port; }
    int namespaceIndex() const { return static_cast<int>(m_namespaceIndex); }

private:
    void createNodes() {
        UA_ObjectAttributes folderAttributes = UA_ObjectAttributes_default;
        folderAttributes.displayName =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Plant"));
        folderAttributes.description =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Plant root"));
        UA_Server_addObjectNode(
            m_server,
            UA_NODEID_STRING(m_namespaceIndex, const_cast<char*>("Plant")),
            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME(m_namespaceIndex, const_cast<char*>("Plant")),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
            folderAttributes,
            nullptr,
            &m_plantNodeId);

        UA_ObjectAttributes line1Attributes = UA_ObjectAttributes_default;
        line1Attributes.displayName =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Line1"));
        line1Attributes.description =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Line 1 folder"));
        UA_Server_addObjectNode(
            m_server,
            UA_NODEID_STRING(m_namespaceIndex, const_cast<char*>("Plant.Line1")),
            m_plantNodeId,
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME(m_namespaceIndex, const_cast<char*>("Line1")),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
            line1Attributes,
            nullptr,
            &m_line1NodeId);

        UA_ObjectAttributes line2Attributes = UA_ObjectAttributes_default;
        line2Attributes.displayName =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Line2"));
        line2Attributes.description =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Line 2 folder"));
        UA_Server_addObjectNode(
            m_server,
            UA_NODEID_STRING(m_namespaceIndex, const_cast<char*>("Plant.Line2")),
            m_plantNodeId,
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME(m_namespaceIndex, const_cast<char*>("Line2")),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
            line2Attributes,
            nullptr,
            &m_line2NodeId);

        UA_Double temperature = 36.5;
        UA_VariableAttributes tempAttributes = UA_VariableAttributes_default;
        UA_Variant_setScalarCopy(&tempAttributes.value, &temperature, &UA_TYPES[UA_TYPES_DOUBLE]);
        tempAttributes.displayName =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Temp"));
        tempAttributes.description =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Process temperature"));
        tempAttributes.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
        tempAttributes.accessLevel = UA_ACCESSLEVELMASK_READ;
        UA_Server_addVariableNode(
            m_server,
            UA_NODEID_STRING(m_namespaceIndex, const_cast<char*>("Plant.Line1.Temp")),
            m_line1NodeId,
            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
            UA_QUALIFIEDNAME(m_namespaceIndex, const_cast<char*>("Temp")),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
            tempAttributes,
            nullptr,
            nullptr);

        UA_String mode = UA_STRING(const_cast<char*>("Auto"));
        UA_VariableAttributes modeAttributes = UA_VariableAttributes_default;
        UA_Variant_setScalarCopy(&modeAttributes.value, &mode, &UA_TYPES[UA_TYPES_STRING]);
        modeAttributes.displayName =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Mode"));
        modeAttributes.description =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Current mode"));
        modeAttributes.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
        modeAttributes.accessLevel = UA_ACCESSLEVELMASK_READ;
        UA_Server_addVariableNode(
            m_server,
            UA_NODEID_STRING(m_namespaceIndex, const_cast<char*>("Plant.Line1.Mode")),
            m_line1NodeId,
            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
            UA_QUALIFIEDNAME(m_namespaceIndex, const_cast<char*>("Mode")),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
            modeAttributes,
            nullptr,
            nullptr);

        UA_Double pressure = 12.3;
        UA_VariableAttributes pressureAttributes = UA_VariableAttributes_default;
        UA_Variant_setScalarCopy(&pressureAttributes.value, &pressure, &UA_TYPES[UA_TYPES_DOUBLE]);
        pressureAttributes.displayName =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Pressure"));
        pressureAttributes.description =
            UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Line pressure"));
        pressureAttributes.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
        pressureAttributes.accessLevel = UA_ACCESSLEVELMASK_READ;
        UA_Server_addVariableNode(
            m_server,
            UA_NODEID_STRING(m_namespaceIndex, const_cast<char*>("Plant.Line2.Pressure")),
            m_line2NodeId,
            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
            UA_QUALIFIEDNAME(m_namespaceIndex, const_cast<char*>("Pressure")),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
            pressureAttributes,
            nullptr,
            nullptr);
    }

    UA_Server* m_server = nullptr;
    UA_UInt16 m_port = 0;
    UA_UInt16 m_namespaceIndex = 0;
    UA_NodeId m_plantNodeId = UA_NODEID_NULL;
    UA_NodeId m_line1NodeId = UA_NODEID_NULL;
    UA_NodeId m_line2NodeId = UA_NODEID_NULL;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

class OpcUaHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int argc = 1;
        static char arg0[] = "test";
        static char* argv[] = {arg0};
        if (!QCoreApplication::instance()) {
            new QCoreApplication(argc, argv);
        }
        QString errorMessage;
        ASSERT_TRUE(server.start(&errorMessage)) << errorMessage.toStdString();
    }

    void TearDown() override {
        server.stop();
    }

    QJsonObject connectionParams() const {
        return QJsonObject{
            {"host", server.host()},
            {"port", server.port()},
            {"timeout_ms", 3000}
        };
    }

    OpcUaHandler handler;
    JsonResponder responder;
    OpcUaTestServer server;
};

} // namespace

TEST(OpcUaHandlerHelperTest, NormalizeNodeIdRejectsInvalidString) {
    QString normalized;
    QString errorMessage;
    EXPECT_FALSE(OpcUaHandler::normalizeNodeId("not-a-node-id", normalized, &errorMessage));
    EXPECT_TRUE(errorMessage.contains("Invalid node_id"));
}

TEST(OpcUaHandlerHelperTest, ResolveConnectionOptionsUsesDefaults) {
    OpcUaConnectionOptions options;
    QString errorMessage;
    ASSERT_TRUE(OpcUaHandler::resolveConnectionOptions(
        QJsonObject{{"host", "127.0.0.1"}}, options, &errorMessage));
    EXPECT_TRUE(errorMessage.isEmpty());
    EXPECT_EQ(options.host, "127.0.0.1");
    EXPECT_EQ(options.port, 4840);
    EXPECT_EQ(options.timeoutMs, 3000);
}

TEST_F(OpcUaHandlerTest, StatusReturnsReady) {
    handler.handle("status", QJsonObject{}, responder);
    EXPECT_EQ(responder.lastStatus, "done");
    EXPECT_EQ(responder.lastCode, 0);
    EXPECT_EQ(responder.lastData.value("status").toString(), "ready");
}

TEST_F(OpcUaHandlerTest, InspectNodeRejectsInvalidNodeId) {
    QJsonObject params = connectionParams();
    params["node_id"] = "bad-node-id";

    handler.handle("inspect_node", params, responder);

    EXPECT_EQ(responder.lastStatus, "error");
    EXPECT_EQ(responder.lastCode, 3);
    EXPECT_TRUE(responder.lastData.value("message").toString().contains("Invalid node_id"));
}

TEST_F(OpcUaHandlerTest, InspectNodeReturnsValueDetailsForVariable) {
    QJsonObject params = connectionParams();
    params["node_id"] = QString("ns=%1;s=Plant.Line1.Temp").arg(server.namespaceIndex());

    handler.handle("inspect_node", params, responder);

    ASSERT_EQ(responder.lastStatus, "done");
    ASSERT_EQ(responder.lastCode, 0);
    const QJsonObject node = responder.lastData.value("node").toObject();
    EXPECT_EQ(node.value("display_name").toString(), "Temp");
    EXPECT_EQ(node.value("description").toString(), "Process temperature");
    EXPECT_EQ(node.value("data_type_name").toString(), "Double");
    EXPECT_DOUBLE_EQ(node.value("value").toDouble(), 36.5);
    EXPECT_EQ(node.value("value_text").toString(), "36.5");
    EXPECT_TRUE(node.value("children").toArray().isEmpty());
}

TEST_F(OpcUaHandlerTest, InspectNodeReturnsOneLevelChildrenWhenRecurseDisabled) {
    QJsonObject params = connectionParams();
    params["node_id"] = QString("ns=%1;s=Plant").arg(server.namespaceIndex());
    params["recurse"] = false;

    handler.handle("inspect_node", params, responder);

    ASSERT_EQ(responder.lastStatus, "done");
    const QJsonObject node = responder.lastData.value("node").toObject();
    const QJsonArray children = node.value("children").toArray();
    ASSERT_EQ(children.size(), 2);
    const QJsonObject line1 =
        childByBrowseName(node, QString("%1:Line1").arg(server.namespaceIndex()));
    ASSERT_FALSE(line1.isEmpty());
    EXPECT_TRUE(line1.value("children").toArray().isEmpty());
}

TEST_F(OpcUaHandlerTest, InspectNodeRecursesIntoDescendantsWhenRequested) {
    QJsonObject params = connectionParams();
    params["node_id"] = QString("ns=%1;s=Plant").arg(server.namespaceIndex());
    params["recurse"] = true;

    handler.handle("inspect_node", params, responder);

    ASSERT_EQ(responder.lastStatus, "done");
    const QJsonObject node = responder.lastData.value("node").toObject();
    const QJsonObject line1 =
        childByBrowseName(node, QString("%1:Line1").arg(server.namespaceIndex()));
    ASSERT_FALSE(line1.isEmpty());
    const QJsonObject temp =
        childByBrowseName(line1, QString("%1:Temp").arg(server.namespaceIndex()));
    const QJsonObject mode =
        childByBrowseName(line1, QString("%1:Mode").arg(server.namespaceIndex()));
    EXPECT_DOUBLE_EQ(temp.value("value").toDouble(), 36.5);
    EXPECT_EQ(mode.value("value").toString(), "Auto");
}

TEST_F(OpcUaHandlerTest, SnapshotNodesReturnsBusinessTreeWithoutServerNode) {
    handler.handle("snapshot_nodes", connectionParams(), responder);

    ASSERT_EQ(responder.lastStatus, "done");
    ASSERT_EQ(responder.lastCode, 0);
    const QJsonObject root = responder.lastData.value("node").toObject();
    EXPECT_EQ(root.value("browse_name").toString(), "0:Objects");

    const QJsonArray children = root.value("children").toArray();
    ASSERT_EQ(children.size(), 1);
    const QJsonObject plant = children.first().toObject();
    EXPECT_EQ(plant.value("browse_name").toString(),
              QString("%1:Plant").arg(server.namespaceIndex()));
    EXPECT_TRUE(childByBrowseName(root, "0:Server").isEmpty());

    const QJsonObject line2 =
        childByBrowseName(plant, QString("%1:Line2").arg(server.namespaceIndex()));
    ASSERT_FALSE(line2.isEmpty());
    const QJsonObject pressure =
        childByBrowseName(line2, QString("%1:Pressure").arg(server.namespaceIndex()));
    EXPECT_DOUBLE_EQ(pressure.value("value").toDouble(), 12.3);
}

TEST_F(OpcUaHandlerTest, MetadataContainsExpectedCommandsAndDefaults) {
    const auto& meta = handler.driverMeta();
    EXPECT_EQ(meta.info.id, "stdio.drv.opcua");

    const auto* inspectNode = meta.findCommand("inspect_node");
    const auto* snapshotNodes = meta.findCommand("snapshot_nodes");
    ASSERT_NE(inspectNode, nullptr);
    ASSERT_NE(snapshotNodes, nullptr);

    const auto* host = findParam(inspectNode, "host");
    const auto* port = findParam(inspectNode, "port");
    const auto* nodeId = findParam(inspectNode, "node_id");
    const auto* recurse = findParam(inspectNode, "recurse");
    const auto* timeoutMs = findParam(snapshotNodes, "timeout_ms");
    ASSERT_NE(host, nullptr);
    ASSERT_NE(port, nullptr);
    ASSERT_NE(nodeId, nullptr);
    ASSERT_NE(recurse, nullptr);
    ASSERT_NE(timeoutMs, nullptr);
    EXPECT_TRUE(host->required);
    EXPECT_EQ(port->defaultValue.toInt(), 4840);
    EXPECT_TRUE(nodeId->required);
    EXPECT_FALSE(recurse->defaultValue.toBool());
    EXPECT_EQ(timeoutMs->defaultValue.toInt(), 3000);
}

TEST_F(OpcUaHandlerTest, AllCommandsHaveGeneratedExamples) {
    const auto& meta = handler.driverMeta();
    for (const auto& command : meta.commands) {
        bool hasStdio = false;
        bool hasConsole = false;
        for (const auto& example : command.examples) {
            const QString mode = example.value("mode").toString();
            if (mode == "stdio") {
                hasStdio = true;
            }
            if (mode == "console") {
                hasConsole = true;
            }
        }
        EXPECT_TRUE(hasStdio) << command.name.toStdString();
        EXPECT_TRUE(hasConsole) << command.name.toStdString();
    }
}
