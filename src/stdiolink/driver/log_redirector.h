#pragma once

#include "stdiolink/stdiolink_export.h"
#include <QString>

namespace stdiolink {

/**
 * 将 Qt 日志（qDebug/qWarning/qCritical/qFatal）重定向到 stderr
 *
 * 调用此函数后，所有 Qt 日志输出将写入 stderr，
 * 避免干扰 stdout 上的 JSONL 协议通信。
 */
STDIOLINK_API void installStderrLogger();

/**
 * 将 Qt 日志重定向到指定文件
 *
 * @param filePath 日志文件路径
 * @return 是否成功打开文件
 */
STDIOLINK_API bool installFileLogger(const QString& filePath);

} // namespace stdiolink
