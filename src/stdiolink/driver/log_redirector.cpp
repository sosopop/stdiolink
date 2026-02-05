#include "log_redirector.h"
#include <QFile>
#include <QtGlobal>

namespace stdiolink {

static QFile* g_logFile = nullptr;

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
        errFile.open(stderr, QIODevice::WriteOnly);
    }

    errFile.write(formatPrefix(type));
    errFile.write(msg.toUtf8());
    errFile.write("\n");
    errFile.flush();

    if (type == QtFatalMsg) {
        abort();
    }
}

static void fileMessageHandler(QtMsgType type,
                                const QMessageLogContext& context,
                                const QString& msg) {
    Q_UNUSED(context)

    if (g_logFile == nullptr || !g_logFile->isOpen()) {
        return;
    }

    g_logFile->write(formatPrefix(type));
    g_logFile->write(msg.toUtf8());
    g_logFile->write("\n");
    g_logFile->flush();

    if (type == QtFatalMsg) {
        abort();
    }
}

void installStderrLogger() {
    qInstallMessageHandler(stderrMessageHandler);
}

bool installFileLogger(const QString& filePath) {
    if (g_logFile != nullptr) {
        g_logFile->close();
        delete g_logFile;
    }

    g_logFile = new QFile(filePath);
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        delete g_logFile;
        g_logFile = nullptr;
        return false;
    }

    qInstallMessageHandler(fileMessageHandler);
    return true;
}

} // namespace stdiolink
