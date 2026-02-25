#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "stdiolink_server/http/event_bus.h"
#include "stdiolink_server/http/event_log.h"

using namespace stdiolink_server;

class EventLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_logPath = m_tmpDir.path() + "/events.jsonl";
    }

    QTemporaryDir m_tmpDir;
    QString m_logPath;
};

TEST_F(EventLogTest, SingleEventWrittenToFile) {
    EventBus bus;
    EventLog log(m_logPath, &bus);

    bus.publish("instance.started", QJsonObject{{"instanceId", "i1"}, {"projectId", "p1"}});

    // Force flush by destroying the log
    // (spdlog flushes on info level for this logger)

    QFile file(m_logPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    ASSERT_FALSE(content.trimmed().isEmpty());

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(content.trimmed(), &err);
    ASSERT_EQ(err.error, QJsonParseError::NoError);
    ASSERT_TRUE(doc.isObject());

    QJsonObject obj = doc.object();
    EXPECT_EQ(obj.value("type").toString(), "instance.started");
    EXPECT_EQ(obj.value("data").toObject().value("instanceId").toString(), "i1");
    const QString ts = obj.value("ts").toString();
    EXPECT_FALSE(ts.isEmpty());
    const QDateTime parsed = QDateTime::fromString(ts, Qt::ISODateWithMs);
    EXPECT_TRUE(parsed.isValid()) << "ts not ISO 8601: " << ts.toStdString();
}

TEST_F(EventLogTest, MultipleEventsEachLineValidJson) {
    EventBus bus;
    EventLog log(m_logPath, &bus);

    bus.publish("instance.started", QJsonObject{{"instanceId", "i1"}});
    bus.publish("instance.finished", QJsonObject{{"instanceId", "i1"}});
    bus.publish("schedule.triggered", QJsonObject{{"projectId", "p1"}});

    QFile file(m_logPath);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = file.readAll().split('\n');

    int validLines = 0;
    for (const QByteArray& line : lines) {
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line.trimmed(), &err);
        EXPECT_EQ(err.error, QJsonParseError::NoError) << line.constData();
        EXPECT_TRUE(doc.isObject());
        validLines++;
    }
    EXPECT_EQ(validLines, 3);
}

TEST_F(EventLogTest, QueryNoFilterNewestFirst) {
    EventBus bus;
    EventLog log(m_logPath, &bus);

    bus.publish("event.a", QJsonObject{});
    bus.publish("event.b", QJsonObject{});
    bus.publish("event.c", QJsonObject{});

    QJsonArray results = log.query(100);
    ASSERT_EQ(results.size(), 3);
    // Newest first
    EXPECT_EQ(results[0].toObject().value("type").toString(), "event.c");
    EXPECT_EQ(results[1].toObject().value("type").toString(), "event.b");
    EXPECT_EQ(results[2].toObject().value("type").toString(), "event.a");
}

TEST_F(EventLogTest, QueryFilterByTypePrefix) {
    EventBus bus;
    EventLog log(m_logPath, &bus);

    bus.publish("instance.started", QJsonObject{{"instanceId", "i1"}});
    bus.publish("schedule.triggered", QJsonObject{{"projectId", "p1"}});
    bus.publish("instance.finished", QJsonObject{{"instanceId", "i1"}});

    QJsonArray results = log.query(100, "instance");
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].toObject().value("type").toString(), "instance.finished");
    EXPECT_EQ(results[1].toObject().value("type").toString(), "instance.started");
}

TEST_F(EventLogTest, QueryFilterByProjectId) {
    EventBus bus;
    EventLog log(m_logPath, &bus);

    bus.publish("instance.started", QJsonObject{{"instanceId", "i1"}, {"projectId", "p1"}});
    bus.publish("instance.started", QJsonObject{{"instanceId", "i2"}, {"projectId", "p2"}});
    bus.publish("instance.finished", QJsonObject{{"instanceId", "i1"}, {"projectId", "p1"}});

    QJsonArray results = log.query(100, QString(), "p1");
    ASSERT_EQ(results.size(), 2);
    for (int i = 0; i < results.size(); ++i) {
        EXPECT_EQ(results[i].toObject().value("data").toObject()
                      .value("projectId").toString(), "p1");
    }
}

TEST_F(EventLogTest, QueryLimitReturnsAtMostN) {
    EventBus bus;
    EventLog log(m_logPath, &bus);

    for (int i = 0; i < 10; ++i) {
        bus.publish("event.x", QJsonObject{{"i", i}});
    }

    QJsonArray results = log.query(3);
    EXPECT_EQ(results.size(), 3);
    // Should be the 3 newest (i=9, i=8, i=7)
    EXPECT_EQ(results[0].toObject().value("data").toObject().value("i").toInt(), 9);
    EXPECT_EQ(results[1].toObject().value("data").toObject().value("i").toInt(), 8);
    EXPECT_EQ(results[2].toObject().value("data").toObject().value("i").toInt(), 7);
}

TEST_F(EventLogTest, FileRotation) {
    EventBus bus;
    // Small max size to trigger rotation quickly
    const qint64 maxBytes = 512;
    EventLog log(m_logPath, &bus, maxBytes, 2);

    // Write enough events to exceed 512 bytes
    for (int i = 0; i < 20; ++i) {
        bus.publish("instance.started",
                    QJsonObject{{"instanceId", QString("instance_%1").arg(i)},
                                {"projectId", "project_rotation_test"}});
    }

    // spdlog rotation naming: events.1.jsonl
    const QString rotatedPath = m_tmpDir.path() + "/events.1.jsonl";
    EXPECT_TRUE(QFile::exists(m_logPath));
    EXPECT_TRUE(QFile::exists(rotatedPath))
        << "Rotated file should exist at: " << rotatedPath.toStdString();
}
