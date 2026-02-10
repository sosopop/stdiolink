#include "log_redirector.h"
#include <QFile>
#include <QtGlobal>

namespace stdiolink {

static QByteArray formatPrefix(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return "[DEBUG] ";
    case QtInfoMsg:
        return "[INFO] ";
    case QtWarningMsg:
        return "[WARN] ";
    case QtCriticalMsg:
        return "[ERROR] ";
    case QtFatalMsg:
        return "[FATAL] ";
    }
    return "";
}

static void stderrMessageHandler(QtMsgType type,
                                  const QMessageLogContext& context,
                                  const QString& msg) {
    Q_UNUSED(context)

    static QFile errFile;
    if (!errFile.isOpen()) {
        (void)errFile.open(stderr, QIODevice::WriteOnly);
    }

    errFile.write(formatPrefix(type));
    errFile.write(msg.toUtf8());
    errFile.write("\n");
    errFile.flush();

    if (type == QtFatalMsg) {
        abort();
    }
}

void installStderrLogger() {
    qInstallMessageHandler(stderrMessageHandler);
}

} // namespace stdiolink
