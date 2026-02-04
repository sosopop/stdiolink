#pragma once

#include <QByteArray>

namespace stdiolink {

/**
 * JSONL 流式解析器
 * 处理字节流中的行分割，支持半行缓冲
 */
class JsonlParser {
public:
    JsonlParser() = default;

    /**
     * 追加数据到缓冲区
     */
    void append(const QByteArray& data);

    /**
     * 尝试读取一行（不含 \n）
     * @param outLine 输出行内容
     * @return 成功读取返回 true，无完整行返回 false
     */
    bool tryReadLine(QByteArray& outLine);

    /**
     * 清空缓冲区
     */
    void clear();

    /**
     * 获取缓冲区大小
     */
    int bufferSize() const;

private:
    QByteArray m_buffer;
};

} // namespace stdiolink
