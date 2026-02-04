#include "driver_core.h"
#include <QFile>
#include <QJsonObject>
#include <QTextStream>
#include "meta_command_handler.h"
#include "stdio_responder.h"
#include "stdiolink/console/console_args.h"
#include "stdiolink/console/console_responder.h"
#include "stdiolink/protocol/jsonl_serializer.h"
#include "stdiolink/protocol/meta_validator.h"

namespace stdiolink {

int DriverCore::run() {
    return runStdioMode();
}

int DriverCore::runStdioMode() {
    if (!m_handler) {
        return 1;
    }

    QFile input;
    input.open(stdin, QIODevice::ReadOnly);
    QTextStream in(&input);

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty())
            continue;

        if (!processOneLine(line.toUtf8())) {
            // 处理失败，继续下一行
        }

        if (m_profile == Profile::OneShot) {
            break;
        }
    }

    return 0;
}

int DriverCore::run(int argc, char* argv[]) {
    ConsoleArgs args;
    if (!args.parse(argc, argv)) {
        QFile err;
        err.open(stderr, QIODevice::WriteOnly);
        err.write(args.errorMessage.toUtf8());
        err.write("\n");
        err.flush();
        return 1;
    }

    // 无参数且 stdin 为交互终端时，输出帮助
    if (argc == 1 && ConsoleArgs::isInteractiveStdin()) {
        printHelp();
        return 0;
    }

    // 处理 --help
    if (args.showHelp) {
        printHelp();
        return 0;
    }

    // 处理 --version
    if (args.showVersion) {
        printVersion();
        return 0;
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

    ConsoleResponder responder;

    // 处理 meta 命令
    if (handleMetaCommand(args.cmd, args.data, responder)) {
        return responder.exitCode();
    }

    // 自动参数验证
    QJsonValue data = args.data;
    if (m_metaHandler && m_metaHandler->autoValidateParams()) {
        const auto* cmdMeta = m_metaHandler->driverMeta().findCommand(args.cmd);
        if (cmdMeta) {
            QJsonObject filledData = meta::DefaultFiller::fillDefaults(
                args.data, *cmdMeta);
            auto result = meta::MetaValidator::validateParams(filledData, *cmdMeta);
            if (!result.valid) {
                responder.error(400, QJsonObject{
                    {"name", "ValidationFailed"},
                    {"message", result.toString()}
                });
                return responder.exitCode();
            }
            data = filledData;
        }
    }

    m_handler->handle(args.cmd, data, responder);
    return responder.exitCode();
}

void DriverCore::printHelp() {
    QFile out;
    out.open(stdout, QIODevice::WriteOnly);
    QTextStream ts(&out);

    if (m_metaHandler) {
        const auto& meta = m_metaHandler->driverMeta();
        ts << meta.info.name << " v" << meta.info.version << "\n";
        if (!meta.info.description.isEmpty()) {
            ts << meta.info.description << "\n";
        }
        ts << "\n";
    }

    ts << "Usage:\n";
    ts << "  <program> [options]\n";
    ts << "  <program> --cmd=<command> [params...]\n\n";

    ts << "Options:\n";
    ts << "  --help              Show help\n";
    ts << "  --version           Show version\n";
    ts << "  --mode=<mode>       Run mode (stdio|console)\n";
    ts << "  --cmd=<command>     Execute command\n\n";

    if (m_metaHandler) {
        const auto& meta = m_metaHandler->driverMeta();
        if (!meta.commands.isEmpty()) {
            ts << "Commands:\n";
            for (const auto& cmd : meta.commands) {
                ts << "  " << cmd.name.leftJustified(18) << cmd.description.left(50) << "\n";
            }
        }
    }

    ts.flush();
}

void DriverCore::printVersion() {
    QFile out;
    out.open(stdout, QIODevice::WriteOnly);
    QTextStream ts(&out);

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

} // namespace stdiolink
