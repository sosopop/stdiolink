#include <gtest/gtest.h>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include "config/service_manifest.h"

using namespace stdiolink_service;

// --- 正常解析 ---

TEST(ServiceManifest, ParseMinimalValid) {
    QJsonObject obj{
        {"manifestVersion", "1"},
        {"id", "com.example.test"},
        {"name", "Test Service"},
        {"version", "1.0.0"}
    };
    QString err;
    auto m = ServiceManifest::fromJson(obj, err);
    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
    EXPECT_EQ(m.id, "com.example.test");
    EXPECT_EQ(m.name, "Test Service");
    EXPECT_EQ(m.version, "1.0.0");
    EXPECT_TRUE(m.description.isEmpty());
    EXPECT_TRUE(m.author.isEmpty());
}

TEST(ServiceManifest, ParseWithOptionalFields) {
    QJsonObject obj{
        {"manifestVersion", "1"},
        {"id", "com.example.full"},
        {"name", "Full Service"},
        {"version", "2.0.0"},
        {"description", "A full example"},
        {"author", "Test Author"}
    };
    QString err;
    auto m = ServiceManifest::fromJson(obj, err);
    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
    EXPECT_EQ(m.description, "A full example");
    EXPECT_EQ(m.author, "Test Author");
}

// --- 必填字段缺失 ---

TEST(ServiceManifest, MissingManifestVersion) {
    QJsonObject obj{{"id", "x"}, {"name", "x"}, {"version", "1.0"}};
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceManifest, MissingId) {
    QJsonObject obj{{"manifestVersion", "1"}, {"name", "x"}, {"version", "1.0"}};
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceManifest, MissingName) {
    QJsonObject obj{{"manifestVersion", "1"}, {"id", "x"}, {"version", "1.0"}};
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceManifest, MissingVersion) {
    QJsonObject obj{{"manifestVersion", "1"}, {"id", "x"}, {"name", "x"}};
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

// --- 版本号校验 ---

TEST(ServiceManifest, InvalidManifestVersion) {
    QJsonObject obj{
        {"manifestVersion", "2"},
        {"id", "x"}, {"name", "x"}, {"version", "1.0"}
    };
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

// --- 未知字段拒绝 ---

TEST(ServiceManifest, RejectUnknownField) {
    QJsonObject obj{
        {"manifestVersion", "1"},
        {"id", "x"}, {"name", "x"}, {"version", "1.0"},
        {"entry", "custom.js"}
    };
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceManifest, RejectArbitraryUnknownField) {
    QJsonObject obj{
        {"manifestVersion", "1"},
        {"id", "x"}, {"name", "x"}, {"version", "1.0"},
        {"foo", "bar"}
    };
    QString err;
    ServiceManifest::fromJson(obj, err);
    EXPECT_FALSE(err.isEmpty());
}

// --- 文件加载 ---

TEST(ServiceManifest, LoadFromValidFile) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QString path = tmp.path() + "/manifest.json";
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(R"({"manifestVersion":"1","id":"com.test","name":"Test","version":"1.0"})");
    f.close();

    QString err;
    auto m = ServiceManifest::loadFromFile(path, err);
    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
    EXPECT_EQ(m.id, "com.test");
}

TEST(ServiceManifest, LoadFromNonexistentFile) {
    QString err;
    ServiceManifest::loadFromFile("nonexistent.json", err);
    EXPECT_FALSE(err.isEmpty());
}

TEST(ServiceManifest, LoadFromMalformedJson) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    QString path = tmp.path() + "/bad.json";
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("{not valid json}");
    f.close();

    QString err;
    ServiceManifest::loadFromFile(path, err);
    EXPECT_FALSE(err.isEmpty());
}
