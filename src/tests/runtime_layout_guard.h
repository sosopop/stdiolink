#pragma once

#include <QString>

namespace stdiolink::tests {

struct RuntimeLayoutCheckResult {
    bool ok = false;
    QString message;
};

RuntimeLayoutCheckResult validateTestRuntimeLayout(const QString& applicationFilePath,
                                                   const QString& applicationDirPath);

}  // namespace stdiolink::tests
