#pragma once

#include "icommand_handler.h"
#include "stdiolink/protocol/jsonl_parser.h"
#include "stdiolink/stdiolink_export.h"

namespace stdiolink {

class IMetaCommandHandler;
class ConsoleArgs;

/**
 * Driver 核心类
 * 处理 stdin/stdout 通信，支持 Stdio 和 Console 双模式
 */
class STDIOLINK_API DriverCore {
public:
    enum class Profile {
        OneShot,  // 处理一次请求后退出
        KeepAlive // 持续处理请求
    };

    enum class RunMode {
        Auto,     // 自动检测
        Stdio,    // Stdio 模式
        Console   // Console 模式
    };

    DriverCore() = default;

    void setProfile(Profile p) { m_profile = p; }
    void setHandler(ICommandHandler* h) { m_handler = h; }

    /**
     * 设置支持元数据的处理器
     * 设置后将自动响应 meta.* 命令
     */
    void setMetaHandler(IMetaCommandHandler* h);

    /**
     * 带命令行参数的运行入口（推荐）
     * 自动检测运行模式
     */
    int run(int argc, char* argv[]);

    /**
     * 纯 Stdio 模式入口（保持兼容）
     */
    int run();

private:
    Profile m_profile = Profile::OneShot;
    ICommandHandler* m_handler = nullptr;
    IMetaCommandHandler* m_metaHandler = nullptr;
    JsonlParser m_parser;

    int runStdioMode();
    int runConsoleMode(const ConsoleArgs& args);
    RunMode detectMode(const ConsoleArgs& args);
    void printHelp();
    void printVersion();
    int printCommandHelp(const QString& cmdName);
    int handleExportMeta(const ConsoleArgs& args);
    int handleExportDoc(const ConsoleArgs& args);

    bool processOneLine(const QByteArray& line);
    bool handleMetaCommand(const QString& cmd, const QJsonValue& data,
                           IResponder& responder);
};

} // namespace stdiolink
