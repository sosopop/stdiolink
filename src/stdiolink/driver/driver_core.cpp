#include "driver_core.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <atomic>
#include "help_generator.h"
#include "log_redirector.h"
#include "meta_command_handler.h"
#include "meta_exporter.h"
#include "stdiolink/doc/doc_generator.h"
#include "stdio_responder.h"
#include "stdiolink/console/console_args.h"
#include "stdiolink/console/json_cli_codec.h"
#include "stdiolink/console/console_responder.h"
#include "stdiolink/protocol/jsonl_serializer.h"
#include "stdiolink/protocol/meta_validator.h"
#include "stdiolink/platform/platform_utils.h"

namespace stdiolink {

namespace {

// Console 模式专用响应器：仅在收到 done/error 时请求退出事件循环。
class TerminalAwareConsoleResponder final : public ConsoleResponder {
public:
    explicit TerminalAwareConsoleResponder(QCoreApplication* app) : m_app(app) {}

    void done(int code, const QJsonValue& payload) override {
        ConsoleResponder::done(code, payload);
        requestQuitOnce();
    }

    void error(int code, const QJsonValue& payload) override {
        ConsoleResponder::error(code, payload);
        requestQuitOnce();
    }

private:
    void requestQuitOnce() {
        if (m_quitScheduled.exchange(true, std::memory_order_relaxed)) {
            return;
        }
        if (m_app == nullptr) {
            return;
        }
        if (QThread::currentThread() == m_app->thread()) {
            m_app->quit();
            return;
        }
        QMetaObject::invokeMethod(m_app, &QCoreApplication::quit, Qt::QueuedConnection);
    }

    QCoreApplication* m_app = nullptr;
    std::atomic_bool m_quitScheduled{false};
};

QString canonicalLiteral(const QJsonValue& value) {
    QJsonArray wrapper;
    wrapper.append(value);
    QString json = QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));
    if (json.size() >= 2 && json.front() == '[' && json.back() == ']') {
        return json.mid(1, json.size() - 2);
    }
    return json;
}

bool parseJsonLiteral(const QString& text, QJsonValue& out) {
    QJsonParseError error{};
    const QByteArray wrapped = "[" + text.toUtf8() + "]";
    const QJsonDocument doc = QJsonDocument::fromJson(wrapped, &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return false;
    }
    const QJsonArray arr = doc.array();
    if (arr.size() != 1) {
        return false;
    }
    out = arr.at(0);
    return true;
}

const meta::FieldMeta* findFieldByName(const QVector<meta::FieldMeta>& fields, const QString& name) {
    for (const auto& field : fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

const meta::FieldMeta* resolveFieldMetaByPath(const meta::CommandMeta* cmdMeta,
                                              const CliPath& path,
                                              QString* error) {
    if (cmdMeta == nullptr || path.isEmpty() || path.first().kind != CliPathSegment::Kind::Key) {
        return nullptr;
    }

    const meta::FieldMeta* field = findFieldByName(cmdMeta->params, path.first().key);
    if (field == nullptr) {
        return nullptr;
    }

    for (int i = 1; i < path.size(); ++i) {
        const CliPathSegment& segment = path.at(i);
        if (field->type == meta::FieldType::Array) {
            if (segment.kind != CliPathSegment::Kind::Index &&
                segment.kind != CliPathSegment::Kind::Append) {
                if (error != nullptr) {
                    *error = QString("path does not match array field: %1").arg(field->name);
                }
                return nullptr;
            }
            field = field->items.get();
            if (field == nullptr) {
                return nullptr;
            }
            continue;
        }

        if (field->type == meta::FieldType::Object) {
            if (segment.kind != CliPathSegment::Kind::Key) {
                if (error != nullptr) {
                    *error = QString("path does not match object field: %1").arg(field->name);
                }
                return nullptr;
            }
            field = findFieldByName(field->fields, segment.key);
            if (field == nullptr) {
                return nullptr;
            }
            continue;
        }

        if (error != nullptr) {
            *error = QString("path exceeds scalar field: %1").arg(field->name);
        }
        return nullptr;
    }

    return field;
}

QJsonValue decodeStringOrKeepRaw(const QString& raw) {
    QJsonValue parsed;
    if (parseJsonLiteral(raw, parsed) && parsed.isString()) {
        return parsed;
    }
    return QJsonValue(raw);
}

bool parseBoolValue(const QString& raw, QJsonValue& out, QString* error) {
    if (raw == "true") {
        out = true;
        return true;
    }
    if (raw == "false") {
        out = false;
        return true;
    }
    if (error != nullptr) {
        *error = "expected boolean literal";
    }
    return false;
}

bool parseIntValue(const QString& raw, QJsonValue& out, QString* error) {
    bool ok = false;
    const int value = raw.toInt(&ok);
    if (!ok) {
        if (error != nullptr) {
            *error = "expected integer literal";
        }
        return false;
    }
    out = value;
    return true;
}

bool parseSafeInt64Value(const QString& raw, QJsonValue& out, QString* error) {
    bool ok = false;
    const qlonglong value = raw.toLongLong(&ok);
    if (!ok) {
        if (error != nullptr) {
            *error = "expected integer literal";
        }
        return false;
    }
    static constexpr qlonglong kMinSafe = -9007199254740992LL;
    static constexpr qlonglong kMaxSafe = 9007199254740992LL;
    if (value < kMinSafe || value > kMaxSafe) {
        if (error != nullptr) {
            *error = "integer out of safe range";
        }
        return false;
    }
    out = static_cast<double>(value);
    return true;
}

bool parseDoubleValue(const QString& raw, QJsonValue& out, QString* error) {
    bool ok = false;
    const double value = raw.toDouble(&ok);
    if (!ok) {
        if (error != nullptr) {
            *error = "expected number literal";
        }
        return false;
    }
    out = value;
    return true;
}

bool parseExpectedContainerValue(const QString& raw,
                                 QJsonValue::Type expectedType,
                                 QJsonValue& out,
                                 QString* error) {
    QJsonValue parsed;
    if (!parseJsonLiteral(raw, parsed) || parsed.type() != expectedType) {
        if (error != nullptr) {
            *error = (expectedType == QJsonValue::Array) ? "expected array literal"
                                                         : "expected object literal";
        }
        return false;
    }
    out = parsed;
    return true;
}

bool resolveFieldValue(const QString& raw,
                       const meta::FieldMeta* fieldMeta,
                       QJsonValue& out,
                       QString* error) {
    if (fieldMeta == nullptr) {
        out = inferType(raw);
        return true;
    }

    switch (fieldMeta->type) {
    case meta::FieldType::String:
    case meta::FieldType::Enum:
        out = decodeStringOrKeepRaw(raw);
        return true;
    case meta::FieldType::Bool:
        return parseBoolValue(raw, out, error);
    case meta::FieldType::Int:
        return parseIntValue(raw, out, error);
    case meta::FieldType::Int64:
        return parseSafeInt64Value(raw, out, error);
    case meta::FieldType::Double:
        return parseDoubleValue(raw, out, error);
    case meta::FieldType::Object:
        return parseExpectedContainerValue(raw, QJsonValue::Object, out, error);
    case meta::FieldType::Array:
        return parseExpectedContainerValue(raw, QJsonValue::Array, out, error);
    case meta::FieldType::Any:
        out = inferType(raw);
        return true;
    }
    return false;
}

bool buildConsoleData(const QList<RawCliArg>& rawArgs,
                      const meta::CommandMeta* cmdMeta,
                      QJsonObject& out,
                      QString* error) {
    QList<RawCliArg> typedArgs;
    typedArgs.reserve(rawArgs.size());

    for (const auto& rawArg : rawArgs) {
        CliPath path;
        QString pathError;
        if (!JsonCliCodec::parsePath(rawArg.path, path, &pathError)) {
            if (error != nullptr) {
                *error = pathError;
            }
            return false;
        }

        QString metaError;
        const meta::FieldMeta* fieldMeta = resolveFieldMetaByPath(cmdMeta, path, &metaError);
        if (!metaError.isEmpty()) {
            if (error != nullptr) {
                *error = metaError;
            }
            return false;
        }

        QJsonValue value;
        QString valueError;
        if (!resolveFieldValue(rawArg.rawValue, fieldMeta, value, &valueError)) {
            if (error != nullptr) {
                *error = QString("%1: %2").arg(rawArg.path, valueError);
            }
            return false;
        }
        // 先按 meta 将原始文本转换成目标 QJsonValue，再回写为 Canonical 字面量，
        // 统一交给 JsonCliCodec::parseArgs() 复用正式的路径聚合和冲突检测逻辑。
        typedArgs.append(RawCliArg{rawArg.path, canonicalLiteral(value)});
    }

    return JsonCliCodec::parseArgs(typedArgs,
                                   CliParseOptions{CliValueMode::Canonical},
                                   out,
                                   error);
}

} // namespace

int DriverCore::run() {
    PlatformUtils::initConsoleEncoding();
    return runStdioMode();
}

int DriverCore::runStdioMode() {
    if (!m_handler) {
        return 1;
    }

    auto* app = QCoreApplication::instance();
    if (app == nullptr) {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write("DriverCore: QCoreApplication not created\n");
        err.flush();
        return 1;
    }

    // 重置本次运行的状态，避免重复调用 run 时残留旧状态。
    m_stdioStopRequested.store(false, std::memory_order_relaxed);
    m_stdioAcceptLines.store(true, std::memory_order_relaxed);
    m_stdioQuitScheduled = false;

    m_stdinReaderThread = QThread::create([this, app]() {
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly)) {
            QMetaObject::invokeMethod(
                app,
                [this]() {
                    m_stdioStopRequested.store(true, std::memory_order_relaxed);
                    scheduleStdioQuit();
                },
                Qt::QueuedConnection);
            return;
        }

        QTextStream in(&input);
        while (!m_stdioStopRequested.load(std::memory_order_relaxed) && !in.atEnd()) {
            const QString line = in.readLine();
            if (line.isNull()) {
                break;
            }

            const QByteArray data = line.toUtf8();
            QMetaObject::invokeMethod(
                app,
                [this, data]() { handleStdioLineOnMainThread(data); },
                Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(
            app,
            [this]() {
                m_stdioStopRequested.store(true, std::memory_order_relaxed);
                scheduleStdioQuit();
            },
            Qt::QueuedConnection);
    });

    if (m_stdinReaderThread == nullptr) {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write("DriverCore: failed to create stdin reader thread\n");
        err.flush();
        return 1;
    }
    m_stdinReaderThread->start();

    const bool oldQuitLock = QCoreApplication::isQuitLockEnabled();
    QCoreApplication::setQuitLockEnabled(false);
    const int exitCode = app->exec();
    QCoreApplication::setQuitLockEnabled(oldQuitLock);

    m_stdioStopRequested.store(true, std::memory_order_relaxed);

    if (m_stdinReaderThread != nullptr) {
        // KeepAlive 允许更长的协作退出窗口；OneShot 保持较短窗口，避免影响快速失败链路时延。
        const unsigned long kReaderJoinTimeoutMs =
            (m_profile == Profile::KeepAlive) ? 2000UL : 200UL;
        constexpr unsigned long kReaderTerminateWaitMs = 200;
        if (!m_stdinReaderThread->wait(kReaderJoinTimeoutMs)) {
            qWarning("DriverCore: stdin reader thread did not exit promptly, forcing terminate");
            m_stdinReaderThread->terminate();
            m_stdinReaderThread->wait(kReaderTerminateWaitMs);
        }
        delete m_stdinReaderThread;
        m_stdinReaderThread = nullptr;
    }

    return exitCode;
}

void DriverCore::handleStdioLineOnMainThread(const QByteArray& line) {
    if (!m_stdioAcceptLines.load(std::memory_order_relaxed)) {
        return;
    }
    if (line.trimmed().isEmpty()) {
        return;
    }

    (void)processOneLine(line);

    if (m_profile == Profile::OneShot) {
        m_stdioAcceptLines.store(false, std::memory_order_relaxed);
        m_stdioStopRequested.store(true, std::memory_order_relaxed);
        scheduleStdioQuit();
    }
}

void DriverCore::scheduleStdioQuit() {
    if (m_stdioQuitScheduled) {
        return;
    }
    m_stdioQuitScheduled = true;

    auto* app = QCoreApplication::instance();
    if (app == nullptr) {
        return;
    }
    if (QThread::currentThread() == app->thread()) {
        app->quit();
        return;
    }
    QMetaObject::invokeMethod(app, &QCoreApplication::quit, Qt::QueuedConnection);
}

int DriverCore::run(int argc, char* argv[]) {
    PlatformUtils::initConsoleEncoding();

    ConsoleArgs args;
    if (!args.parse(argc, argv)) {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write(args.errorMessage.toUtf8());
        err.write("\n");
        err.flush();
        return 1;
    }

    // 初始化日志（全部输出到 stderr）
    installStderrLogger();

    // 无参数且 stdin 为交互终端时，输出帮助
    if (argc == 1 && ConsoleArgs::isInteractiveStdin()) {
        printHelp();
        return 0;
    }

    // 处理 --help（优先级最高）
    if (args.showHelp) {
        // 如果同时指定了 --cmd，显示命令详情帮助
        if (!args.cmd.isEmpty()) {
            return printCommandHelp(args.cmd);
        }
        printHelp();
        return 0;
    }

    // 处理 --version
    if (args.showVersion) {
        printVersion();
        return 0;
    }

    // 处理 --export-meta
    if (args.exportMeta) {
        return handleExportMeta(args);
    }

    // 处理 --export-doc
    if (!args.exportDocFormat.isEmpty()) {
        return handleExportDoc(args);
    }

    // 命令行 profile 参数覆盖默认值
    if (!args.profile.isEmpty()) {
        if (args.profile == "keepalive") {
            m_profile = Profile::KeepAlive;
        } else if (args.profile == "oneshot") {
            m_profile = Profile::OneShot;
        }
    }

    // 检测运行模式
    RunMode mode = detectMode(args);

    if (mode == RunMode::Console) {
        return runConsoleMode(args);
    }
    return runStdioMode();
}

void DriverCore::setMetaHandler(IMetaCommandHandler* h) {
    m_metaHandler = h;
    m_handler = h;  // 同时设置为普通处理器
}

bool DriverCore::processOneLine(const QByteArray& line) {
    // 跳过空行
    if (line.trimmed().isEmpty()) {
        return true;
    }

    // 解析请求
    Request req;
    if (!parseRequest(line, req)) {
        StdioResponder responder;
        responder.error(1000, QJsonObject{{"message", "invalid request format"},
                                          {"raw", QString::fromUtf8(line)}});
        return false;
    }

    // 处理命令
    StdioResponder responder;

    // 优先处理 meta 命令
    if (handleMetaCommand(req.cmd, req.data, responder)) {
        return true;
    }

    // 自动参数验证
    if (m_metaHandler && m_metaHandler->autoValidateParams()) {
        const auto* cmdMeta = m_metaHandler->driverMeta().findCommand(req.cmd);
        if (cmdMeta) {
            // 填充默认值
            QJsonObject filledData = meta::DefaultFiller::fillDefaults(
                req.data.toObject(), *cmdMeta);

            // 验证参数
            auto result = meta::MetaValidator::validateParams(filledData, *cmdMeta);
            if (!result.valid) {
                responder.error(400, QJsonObject{
                    {"name", "ValidationFailed"},
                    {"message", result.toString()}
                });
                return false;
            }

            // 使用填充后的数据调用处理器
            m_handler->handle(req.cmd, filledData, responder);
            return true;
        }
    }

    m_handler->handle(req.cmd, req.data, responder);
    return true;
}

bool DriverCore::handleMetaCommand(const QString& cmd, const QJsonValue& data,
                                   IResponder& responder) {
    Q_UNUSED(data)

    if (!cmd.startsWith("meta.")) {
        return false;
    }

    if (m_metaHandler == nullptr) {
        responder.error(501, QJsonObject{
            {"name", "MetaNotSupported"},
            {"message", "This driver does not support meta commands"}
        });
        return true;
    }

    if (cmd == "meta.describe") {
        QJsonObject metaJson = m_metaHandler->driverMeta().toJson();
        responder.done(0, metaJson);
        return true;
    }

    // 未知的 meta 命令
    responder.error(404, QJsonObject{
        {"name", "CommandNotFound"},
        {"message", QString("Unknown meta command: %1").arg(cmd)}
    });
    return true;
}

DriverCore::RunMode DriverCore::detectMode(const ConsoleArgs& args) {
    // 显式指定
    if (args.mode == "stdio") return RunMode::Stdio;
    if (args.mode == "console") return RunMode::Console;

    // 自动检测
    if (!args.cmd.isEmpty()) return RunMode::Console;
    if (args.showHelp || args.showVersion) return RunMode::Console;

    return RunMode::Stdio;
}

int DriverCore::runConsoleMode(const ConsoleArgs& args) {
    if (!m_handler) {
        return 1;
    }

    if (args.cmd.isEmpty()) {
        printHelp();
        return 1;
    }

    auto* app = QCoreApplication::instance();
    if (app == nullptr) {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write("DriverCore: QCoreApplication not created\n");
        err.flush();
        return 1;
    }

    TerminalAwareConsoleResponder responder(app);

    // 通过事件循环投递命令执行，确保异步场景与 runStdioMode 一致。
    QTimer::singleShot(0, app, [this, args, &responder]() {
        QJsonObject consoleData;
        const meta::CommandMeta* cmdMeta = nullptr;
        if (m_metaHandler) {
            cmdMeta = m_metaHandler->driverMeta().findCommand(args.cmd);
        }

        if (!args.rawDataArgs.isEmpty()) {
            QString buildError;
            if (!buildConsoleData(args.rawDataArgs, cmdMeta, consoleData, &buildError)) {
                responder.error(400, QJsonObject{
                    {"name", "CliParseFailed"},
                    {"message", buildError}
                });
                return;
            }
        }

        // 处理 meta 命令
        if (handleMetaCommand(args.cmd, consoleData, responder)) {
            return;
        }

        // 自动参数验证
        QJsonValue data = consoleData;
        if (m_metaHandler && m_metaHandler->autoValidateParams()) {
            if (cmdMeta) {
                QJsonObject filledData = meta::DefaultFiller::fillDefaults(
                    consoleData, *cmdMeta);
                auto result = meta::MetaValidator::validateParams(filledData, *cmdMeta);
                if (!result.valid) {
                    responder.error(400, QJsonObject{
                        {"name", "ValidationFailed"},
                        {"message", result.toString()}
                    });
                    return;
                }
                data = filledData;
            }
        }

        m_handler->handle(args.cmd, data, responder);
    });

    const bool oldQuitLock = QCoreApplication::isQuitLockEnabled();
    QCoreApplication::setQuitLockEnabled(false);
    const int loopExitCode = app->exec();
    QCoreApplication::setQuitLockEnabled(oldQuitLock);

    if (responder.hasResult()) {
        return responder.exitCode();
    }
    return loopExitCode;
}

void DriverCore::printHelp() {
    QFile err;
    (void)err.open(stderr, QIODevice::WriteOnly);
    QTextStream ts(&err);

    if (m_metaHandler) {
        ts << HelpGenerator::generateHelp(m_metaHandler->driverMeta());
    } else {
        ts << "stdiolink driver\n\n";
        ts << "Usage:\n";
        ts << "  <program> [options]\n";
        ts << "  <program> --cmd=<command> [params...]\n\n";
        ts << HelpGenerator::generateSystemOptions();
    }

    ts.flush();
}

void DriverCore::printVersion() {
    QFile err;
    (void)err.open(stderr, QIODevice::WriteOnly);
    QTextStream ts(&err);

    if (m_metaHandler) {
        const auto& meta = m_metaHandler->driverMeta();
        ts << meta.info.name << " v" << meta.info.version << "\n";
        if (!meta.info.vendor.isEmpty()) {
            ts << meta.info.vendor << "\n";
        }
    } else {
        ts << "stdiolink driver\n";
    }

    ts.flush();
}

int DriverCore::printCommandHelp(const QString& cmdName) {
    QFile err;
    (void)err.open(stderr, QIODevice::WriteOnly);
    QTextStream ts(&err);

    if (!m_metaHandler) {
        ts << "No metadata available\n";
        ts.flush();
        return 1;
    }

    const auto* cmdMeta = m_metaHandler->driverMeta().findCommand(cmdName);
    if (!cmdMeta) {
        ts << "Unknown command: " << cmdName << "\n";
        ts.flush();
        return 1;
    }

    ts << HelpGenerator::generateCommandHelp(*cmdMeta);
    ts.flush();
    return 0;
}

int DriverCore::handleExportMeta(const ConsoleArgs& args) {
    if (!m_metaHandler) {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write("No metadata available\n");
        err.flush();
        return 1;
    }

    const auto& meta = m_metaHandler->driverMeta();

    if (args.exportMetaPath.isEmpty()) {
        // 输出到 stdout
        QFile out;
        (void)out.open(stdout, QIODevice::WriteOnly);
        out.write(MetaExporter::exportJson(meta, true));
        out.flush();
        return 0;
    }

    // 输出到文件
    if (!MetaExporter::exportToFile(meta, args.exportMetaPath)) {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write("Failed to write file: ");
        err.write(args.exportMetaPath.toUtf8());
        err.write("\n");
        err.flush();
        return 1;
    }
    return 0;
}

int DriverCore::handleExportDoc(const ConsoleArgs& args) {
    if (m_metaHandler == nullptr) {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write("Error: No meta handler registered\n");
        err.flush();
        return 1;
    }

    const meta::DriverMeta& meta = m_metaHandler->driverMeta();
    QString format = args.exportDocFormat.toLower();
    QByteArray output;

    if (format == "markdown" || format == "md") {
        output = DocGenerator::toMarkdown(meta).toUtf8();
    } else if (format == "openapi" || format == "swagger") {
        QJsonDocument doc(DocGenerator::toOpenAPI(meta));
        output = doc.toJson(QJsonDocument::Indented);
    } else if (format == "html") {
        output = DocGenerator::toHtml(meta).toUtf8();
    } else if (format == "ts" || format == "typescript" || format == "dts") {
        output = DocGenerator::toTypeScript(meta).toUtf8();
    } else {
        QFile err;
        (void)err.open(stderr, QIODevice::WriteOnly);
        err.write("Error: Unknown format '");
        err.write(format.toUtf8());
        err.write("'. Supported: markdown, openapi, html, ts\n");
        err.flush();
        return 1;
    }

    // 输出到文件或 stdout
    if (!args.exportDocPath.isEmpty()) {
        QFile file(args.exportDocPath);
        if (!file.open(QIODevice::WriteOnly)) {
            QFile err;
            (void)err.open(stderr, QIODevice::WriteOnly);
            err.write("Error: Cannot write to ");
            err.write(args.exportDocPath.toUtf8());
            err.write("\n");
            err.flush();
            return 1;
        }
        file.write(output);
        file.close();
    } else {
        QFile out;
        (void)out.open(stdout, QIODevice::WriteOnly);
        out.write(output);
        out.flush();
    }

    return 0;
}

} // namespace stdiolink
