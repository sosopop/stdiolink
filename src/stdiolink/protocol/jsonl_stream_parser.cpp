#include "jsonl_parser.h"

namespace stdiolink {

void JsonlParser::append(const QByteArray& data) {
    m_buffer.append(data);
}

bool JsonlParser::tryReadLine(QByteArray& outLine) {
    int idx = m_buffer.indexOf('\n');
    if (idx < 0) {
        return false;
    }

    outLine = m_buffer.left(idx);
    m_buffer.remove(0, idx + 1);
    return true;
}

void JsonlParser::clear() {
    m_buffer.clear();
}

int JsonlParser::bufferSize() const {
    return m_buffer.size();
}

} // namespace stdiolink
