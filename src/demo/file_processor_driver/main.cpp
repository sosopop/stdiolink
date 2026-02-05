/**
 * File Processor Driver - 文件处理演示
 *
 * 功能演示:
 * 1. 字符串约束、正则验证
 * 2. 数组参数
 * 3. 嵌套对象参数
 * 4. UI 提示（分组、占位符）
 */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class FileProcessorHandler : public IMetaCommandHandler {
public:
    FileProcessorHandler() { buildMeta(); }

    const DriverMeta& driverMeta() const override { return m_meta; }

    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override;

private:
    void buildMeta();
    void handleListFiles(const QJsonObject& params, IResponder& resp);
    void handleReadFile(const QJsonObject& params, IResponder& resp);
    void handleWriteFile(const QJsonObject& params, IResponder& resp);
    void handleSearchFiles(const QJsonObject& params, IResponder& resp);
    void handleFileInfo(const QJsonObject& params, IResponder& resp);

    DriverMeta m_meta;
};

void FileProcessorHandler::handle(const QString& cmd, const QJsonValue& data,
                                   IResponder& resp)
{
    QJsonObject params = data.toObject();
    if (cmd == "list") handleListFiles(params, resp);
    else if (cmd == "read") handleReadFile(params, resp);
    else if (cmd == "write") handleWriteFile(params, resp);
    else if (cmd == "search") handleSearchFiles(params, resp);
    else if (cmd == "info") handleFileInfo(params, resp);
    else resp.error(404, QJsonObject{{"message", "unknown: " + cmd}});
}

void FileProcessorHandler::buildMeta()
{
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("demo.file_processor", "File Processor", "1.0.0",
              "文件处理器，演示字符串约束和嵌套对象")
        .vendor("stdiolink-demo")
        .command(CommandBuilder("list")
            .description("列出目录文件")
            .param(FieldBuilder("path", FieldType::String)
                .required()
                .minLength(1)
                .maxLength(260)
                .placeholder("/path/to/dir")
                .group("路径"))
            .param(FieldBuilder("pattern", FieldType::String)
                .defaultValue("*")
                .description("文件名模式"))
            .param(FieldBuilder("recursive", FieldType::Bool)
                .defaultValue(false)))
        .command(CommandBuilder("read")
            .description("读取文件内容")
            .param(FieldBuilder("path", FieldType::String).required())
            .param(FieldBuilder("encoding", FieldType::Enum)
                .enumValues(QStringList{"utf-8", "gbk", "latin1"})
                .defaultValue("utf-8")))
        .command(CommandBuilder("write")
            .description("写入文件")
            .param(FieldBuilder("path", FieldType::String).required())
            .param(FieldBuilder("content", FieldType::String).required())
            .param(FieldBuilder("append", FieldType::Bool).defaultValue(false)))
        .command(CommandBuilder("search")
            .description("搜索文件内容")
            .param(FieldBuilder("paths", FieldType::Array)
                .required()
                .minItems(1)
                .items(FieldBuilder("p", FieldType::String)))
            .param(FieldBuilder("keyword", FieldType::String).required())
            .event("match", "匹配结果"))
        .command(CommandBuilder("info")
            .description("获取文件信息")
            .param(FieldBuilder("path", FieldType::String).required()))
        .build();
}

void FileProcessorHandler::handleListFiles(const QJsonObject& params,
                                            IResponder& resp)
{
    QString path = params["path"].toString();
    QString pattern = params["pattern"].toString("*");
    QDir dir(path);
    if (!dir.exists()) {
        resp.error(404, QJsonObject{{"message", "dir not found"}});
        return;
    }
    QJsonArray files;
    for (const auto& f : dir.entryList(QStringList{pattern}, QDir::Files)) {
        files.append(f);
    }
    resp.done(0, QJsonObject{{"files", files}, {"count", files.size()}});
}

void FileProcessorHandler::handleReadFile(const QJsonObject& params,
                                           IResponder& resp)
{
    QString path = params["path"].toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        resp.error(404, QJsonObject{{"message", "cannot open file"}});
        return;
    }
    QTextStream in(&file);
    resp.done(0, QJsonObject{{"content", in.readAll()}});
}

void FileProcessorHandler::handleWriteFile(const QJsonObject& params,
                                            IResponder& resp)
{
    QString path = params["path"].toString();
    QString content = params["content"].toString();
    bool append = params["append"].toBool(false);
    QFile file(path);
    auto mode = append ? QIODevice::Append : QIODevice::WriteOnly;
    if (!file.open(mode | QIODevice::Text)) {
        resp.error(500, QJsonObject{{"message", "cannot write"}});
        return;
    }
    QTextStream out(&file);
    out << content;
    resp.done(0, QJsonObject{{"written", content.size()}});
}

void FileProcessorHandler::handleSearchFiles(const QJsonObject& params,
                                              IResponder& resp)
{
    QJsonArray paths = params["paths"].toArray();
    QString keyword = params["keyword"].toString();
    int matches = 0;
    for (const auto& p : paths) {
        QFile file(p.toString());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&file);
        int line = 0;
        while (!in.atEnd()) {
            ++line;
            QString text = in.readLine();
            if (text.contains(keyword)) {
                resp.event("match", 0, QJsonObject{
                    {"file", p.toString()}, {"line", line}
                });
                ++matches;
            }
        }
    }
    resp.done(0, QJsonObject{{"matches", matches}});
}

void FileProcessorHandler::handleFileInfo(const QJsonObject& params,
                                           IResponder& resp)
{
    QString path = params["path"].toString();
    QFileInfo info(path);
    if (!info.exists()) {
        resp.error(404, QJsonObject{{"message", "not found"}});
        return;
    }
    resp.done(0, QJsonObject{
        {"name", info.fileName()},
        {"size", info.size()},
        {"isDir", info.isDir()},
        {"modified", info.lastModified().toString(Qt::ISODate)}
    });
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    FileProcessorHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
