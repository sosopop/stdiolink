#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace stdiolink_server {

/// Prepend @p dir to the PATH variable in @p env using the platform list separator.
void prependDirToPath(const QString& dir, QProcessEnvironment& env);

} // namespace stdiolink_server
