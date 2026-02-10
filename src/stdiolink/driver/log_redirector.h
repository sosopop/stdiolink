#pragma once

#include "stdiolink/stdiolink_export.h"

namespace stdiolink {

/**
 * 将 Qt 日志（qDebug/qWarning/qCritical/qFatal）重定向到 stderr
 *
 * 调用此函数后，所有 Qt 日志输出将写入 stderr，
 * 避免干扰 stdout 上的 JSONL 协议通信。
 */
STDIOLINK_API void installStderrLogger();

} // namespace stdiolink
