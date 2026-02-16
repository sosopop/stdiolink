#include "process_env_utils.h"

#include <QDir>

namespace stdiolink_server {

void prependDirToPath(const QString& dir, QProcessEnvironment& env) {
    const QString pathValue = env.value("PATH");
    if (!pathValue.isEmpty()) {
        env.insert("PATH", dir + QDir::listSeparator() + pathValue);
    } else {
        env.insert("PATH", dir);
    }
}

} // namespace stdiolink_server
