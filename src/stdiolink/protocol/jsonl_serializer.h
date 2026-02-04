#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include "jsonl_types.h"

namespace stdiolink {

/**
 * 序列化请求为 JSONL 格式
 * @param cmd 命令名
 * @param data 数据（可选）
 * @return JSONL 行（以 \n 结尾）
 */
QByteArray serializeRequest(const QString& cmd, const QJsonValue& data = QJsonValue());

/**
 * 序列化响应为 JSONL 格式（header + payload 两行）
 * @param status 状态："event" | "done" | "error"
 * @param code 错误码
 * @param payload 数据载荷
 * @return JSONL 两行（各以 \n 结尾）
 */
QByteArray serializeResponse(const QString& status, int code, const QJsonValue& payload);

/**
 * 解析请求
 * @param line JSON 行（不含 \n）
 * @param out 输出请求结构
 * @return 解析成功返回 true
 */
bool parseRequest(const QByteArray& line, Request& out);

/**
 * 解析响应头
 * @param line JSON 行（不含 \n）
 * @param out 输出头结构
 * @return 解析成功返回 true
 */
bool parseHeader(const QByteArray& line, FrameHeader& out);

/**
 * 解析 payload（任意 JSON 值）
 * @param line JSON 行（不含 \n）
 * @return 解析后的 JSON 值
 */
QJsonValue parsePayload(const QByteArray& line);

} // namespace stdiolink
