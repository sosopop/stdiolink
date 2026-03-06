#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include "stdiolink/console/console_args.h"
#include "stdiolink/console/json_cli_codec.h"

using namespace stdiolink;

namespace {

QString findRepoRoot() {
    const QDir binDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        binDir.absoluteFilePath("../../.."),
        binDir.absoluteFilePath("../.."),
        QDir::currentPath(),
    };
    for (const QString& candidate : candidates) {
        if (QFile::exists(QDir(candidate).filePath("src/tests/data/cli_render_cases.json"))) {
            return QDir(candidate).absolutePath();
        }
    }
    return QString();
}

QJsonArray loadFixtureCases() {
    const QString repoRoot = findRepoRoot();
    EXPECT_FALSE(repoRoot.isEmpty());
    QFile file(QDir(repoRoot).filePath("src/tests/data/cli_render_cases.json"));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    return QJsonDocument::fromJson(file.readAll()).array();
}

} // namespace

TEST(JsonCliCodec, T01_ParsePathObjectSegments) {
    CliPath path;
    QString error;
    ASSERT_TRUE(JsonCliCodec::parsePath("a.b", path, &error)) << error.toStdString();
    ASSERT_EQ(path.size(), 2);
    EXPECT_EQ(path[0].kind, CliPathSegment::Kind::Key);
    EXPECT_EQ(path[0].key, "a");
    EXPECT_EQ(path[1].kind, CliPathSegment::Kind::Key);
    EXPECT_EQ(path[1].key, "b");
}

TEST(JsonCliCodec, T02_ParsePathExplicitIndex) {
    CliPath path;
    QString error;
    ASSERT_TRUE(JsonCliCodec::parsePath("units[0].id", path, &error)) << error.toStdString();
    ASSERT_EQ(path.size(), 3);
    EXPECT_EQ(path[0].kind, CliPathSegment::Kind::Key);
    EXPECT_EQ(path[0].key, "units");
    EXPECT_EQ(path[1].kind, CliPathSegment::Kind::Index);
    EXPECT_EQ(path[1].index, 0);
    EXPECT_EQ(path[2].kind, CliPathSegment::Kind::Key);
    EXPECT_EQ(path[2].key, "id");
}

TEST(JsonCliCodec, T03_ParsePathAppendAndAppendWritesPreserveOrder) {
    CliPath path;
    QString error;
    ASSERT_TRUE(JsonCliCodec::parsePath("tags[]", path, &error)) << error.toStdString();
    ASSERT_EQ(path.size(), 2);
    EXPECT_EQ(path[1].kind, CliPathSegment::Kind::Append);

    QJsonObject out;
    ASSERT_TRUE(JsonCliCodec::parseArgs({{"tags[]", "\"alpha\""}, {"tags[]", "\"beta\""}},
                                        CliParseOptions{CliValueMode::Friendly},
                                        out,
                                        &error))
        << error.toStdString();
    const QJsonArray tags = out.value("tags").toArray();
    ASSERT_EQ(tags.size(), 2);
    EXPECT_EQ(tags[0].toString(), "alpha");
    EXPECT_EQ(tags[1].toString(), "beta");
}

TEST(JsonCliCodec, T04_ParsePathQuotedKeyAndEscapedQuotes) {
    CliPath path;
    QString error;
    ASSERT_TRUE(JsonCliCodec::parsePath("labels[\"app.kubernetes.io/name\"]", path, &error)) << error.toStdString();
    ASSERT_EQ(path.size(), 2);
    EXPECT_EQ(path[0].kind, CliPathSegment::Kind::Key);
    EXPECT_EQ(path[0].key, "labels");
    EXPECT_EQ(path[1].kind, CliPathSegment::Kind::Key);
    EXPECT_EQ(path[1].key, "app.kubernetes.io/name");

    ASSERT_TRUE(JsonCliCodec::parsePath("labels[\"key\\\"with\\\"quotes\"]", path, &error)) << error.toStdString();
    ASSERT_EQ(path.size(), 2);
    EXPECT_EQ(path[1].kind, CliPathSegment::Kind::Key);
    EXPECT_EQ(path[1].key, "key\"with\"quotes");
}

TEST(JsonCliCodec, T05_ParsePathRejectsInvalidSyntax) {
    CliPath path;
    QString error;

    EXPECT_FALSE(JsonCliCodec::parsePath("", path, &error));
    EXPECT_TRUE(error.contains("empty path"));

    error.clear();
    EXPECT_FALSE(JsonCliCodec::parsePath("a.b.", path, &error));
    EXPECT_TRUE(error.contains("trailing dot"));

    error.clear();
    EXPECT_FALSE(JsonCliCodec::parsePath("labels[\"unterminated\"", path, &error));
    EXPECT_TRUE(error.contains("unterminated"));

    error.clear();
    EXPECT_FALSE(JsonCliCodec::parsePath("units[999999999999].id", path, &error));
    EXPECT_TRUE(error.contains("invalid array index"));
}

TEST(JsonCliCodec, T06_ParseArgsRepeatedScalarOverwrites) {
    QJsonObject out;
    QString error;
    ASSERT_TRUE(JsonCliCodec::parseArgs({{"mode", "\"slow\""}, {"mode", "\"fast\""}},
                                        CliParseOptions{CliValueMode::Friendly},
                                        out,
                                        &error))
        << error.toStdString();
    EXPECT_EQ(out.value("mode").toString(), "fast");
}

TEST(JsonCliCodec, T07_ParseArgsRejectsScalarObjectConflict) {
    QJsonObject out;
    QString error;
    EXPECT_FALSE(JsonCliCodec::parseArgs({{"a", "1"}, {"a.b", "2"}},
                                         CliParseOptions{CliValueMode::Friendly},
                                         out,
                                         &error));
    EXPECT_TRUE(error.contains("path conflict: scalar vs object"));
}

TEST(JsonCliCodec, T08_ParseArgsRejectsAppendIndexConflict) {
    QJsonObject out;
    QString error;
    EXPECT_FALSE(JsonCliCodec::parseArgs({{"tags[]", "\"a\""}, {"tags[0]", "\"b\""}},
                                         CliParseOptions{CliValueMode::Friendly},
                                         out,
                                         &error));
    EXPECT_TRUE(error.contains("append vs explicit index"));
}

TEST(JsonCliCodec, T14_RenderArgsRoundtripWithDefaultModes) {
    const QJsonObject data{
        {"password", "123456"},
        {"units", QJsonArray{QJsonObject{{"id", 1}, {"size", 10000}}}},
        {"labels", QJsonObject{{"app.kubernetes.io/name", "demo"}}},
    };

    const QStringList args = JsonCliCodec::renderArgs(data, CliRenderOptions{});
    QList<RawCliArg> rawArgs;
    rawArgs.reserve(args.size());
    for (const QString& arg : args) {
        const QString withoutPrefix = arg.mid(2);
        const int eqPos = withoutPrefix.indexOf('=');
        ASSERT_GE(eqPos, 0);
        rawArgs.append(RawCliArg{withoutPrefix.left(eqPos), withoutPrefix.mid(eqPos + 1)});
    }

    QJsonObject parsed;
    QString error;
    ASSERT_TRUE(JsonCliCodec::parseArgs(rawArgs, CliParseOptions{CliValueMode::Friendly}, parsed, &error))
        << error.toStdString();
    EXPECT_EQ(parsed, data);
}

TEST(JsonCliCodec, T16_AppendPathMustBeTerminal) {
    QJsonObject out;
    QString error;
    EXPECT_FALSE(JsonCliCodec::parseArgs({{"users[].name", "\"alice\""}},
                                         CliParseOptions{CliValueMode::Friendly},
                                         out,
                                         &error));
    EXPECT_TRUE(error.contains("append path must be terminal"));
}

TEST(JsonCliCodec, T18_RejectsContainerLiteralChildPathMixing) {
    QJsonObject out;
    QString error;
    EXPECT_FALSE(JsonCliCodec::parseArgs({{"a", "{\"b\":1}"}, {"a.c", "2"}},
                                         CliParseOptions{CliValueMode::Friendly},
                                         out,
                                         &error));
    EXPECT_TRUE(error.contains("container literal vs child path"));

    error.clear();
    EXPECT_FALSE(JsonCliCodec::parseArgs({{"units", "[{\"id\":1}]"}, {"units[0].size", "2"}},
                                         CliParseOptions{CliValueMode::Friendly},
                                         out,
                                         &error));
    EXPECT_TRUE(error.contains("container literal vs child path"));
}

TEST(JsonCliCodec, RenderArgsUsesCanonicalLiteralsAndStablePaths) {
    const QJsonObject data{
        {"password", "123456"},
        {"units", QJsonArray{QJsonObject{{"id", 1}, {"size", 10000}}}},
        {"labels", QJsonObject{{"app.kubernetes.io/name", "demo"}}},
    };

    const QStringList args = JsonCliCodec::renderArgs(data, CliRenderOptions{});
    EXPECT_TRUE(args.contains("--password=\"123456\""));
    EXPECT_TRUE(args.contains("--units[0].id=1"));
    EXPECT_TRUE(args.contains("--units[0].size=10000"));
    EXPECT_TRUE(args.contains("--labels[\"app.kubernetes.io/name\"]=\"demo\""));
}

TEST(JsonCliCodec, FriendlyParseConsumesCanonicalStringLiteral) {
    EXPECT_EQ(inferType("\"hello\"").toString(), "hello");
    EXPECT_TRUE(inferType("true").toBool());
    EXPECT_EQ(inferType("42").toInt(), 42);
}

TEST(JsonCliCodec, RejectsBoundaryInvalidInputs) {
    QJsonObject out;
    QString error;

    EXPECT_FALSE(JsonCliCodec::parseArgs({{"", "\"value\""}},
                                         CliParseOptions{CliValueMode::Friendly},
                                         out,
                                         &error));
    EXPECT_TRUE(error.contains("empty path"));

    error.clear();
    EXPECT_FALSE(JsonCliCodec::parseArgs({{"units[-1].id", "1"}},
                                         CliParseOptions{CliValueMode::Friendly},
                                         out,
                                         &error));
    EXPECT_TRUE(error.contains("invalid array index"));
}

TEST(JsonCliCodec, MatchesSharedFixtureCases) {
    const QJsonArray cases = loadFixtureCases();
    ASSERT_FALSE(cases.isEmpty());
    for (const QJsonValue& itemValue : cases) {
        const QJsonObject item = itemValue.toObject();
        const QStringList expectedArgs = [&item]() {
            QStringList out;
            for (const QJsonValue& arg : item.value("args").toArray()) {
                out.append(arg.toString());
            }
            return out;
        }();
        const QStringList actualArgs =
            JsonCliCodec::renderArgs(item.value("params").toObject(), CliRenderOptions{});
        EXPECT_EQ(actualArgs, expectedArgs) << item.value("name").toString().toStdString();
    }
}
