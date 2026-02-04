#include "jsonl_parser.h"

namespace stdiolink {

void JsonlParser::append(const QByteArray& data) {
    buffer.append(data);
}

bool JsonlParser::tryReadLine(QByteArray& outLine) {
    int idx = buffer.indexOf('\n');
    if (idx < 0) {
        return false;
    }

    outLine = buffer.left(idx);
    buffer.remove(0, idx + 1);
    return true;
}

void JsonlParser::clear() {
    buffer.clear();
}

int JsonlParser::bufferSize() const {
    return buffer.size();
}

} // namespace stdiolink
