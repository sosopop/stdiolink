#pragma once

#include "icommand_handler.h"
#include "stdiolink/protocol/jsonl_parser.h"

namespace stdiolink {

/**
 * Driver 核心类
 * 处理 stdin/stdout 通信
 */
class DriverCore {
public:
    enum class Profile {
        OneShot,  // 处理一次请求后退出
        KeepAlive // 持续处理请求
    };

    DriverCore() = default;

    void setProfile(Profile p) { m_profile = p; }
    void setHandler(ICommandHandler* h) { m_handler = h; }

    /**
     * 主循环：读取 stdin，处理请求，输出响应
     * @return 退出码
     */
    int run();

private:
    Profile m_profile = Profile::OneShot;
    ICommandHandler* m_handler = nullptr;
    JsonlParser m_parser;

    bool processOneLine(const QByteArray& line);
};

} // namespace stdiolink
