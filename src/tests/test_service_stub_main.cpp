#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdlib>
#include <csignal>

namespace {

QJsonObject readConfig(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

void writeMarker(const QString& path, const QJsonObject& config) {
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    file.write(QJsonDocument(config).toJson(QJsonDocument::Compact));
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    QString configFile;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith("--config-file=")) {
            configFile = arg.mid(14);
        }
    }

    if (configFile.isEmpty()) {
        return 2;
    }

    const QJsonObject config = readConfig(configFile);
    if (config.isEmpty()) {
        return 3;
    }

    const QJsonObject test = config.value("_test").toObject();

#ifdef Q_OS_UNIX
    if (test.value("ignoreTerminate").toBool(false)) {
        std::signal(SIGTERM, SIG_IGN);
    }
#endif

    writeMarker(test.value("markerFile").toString(), config);

    const int exitCode = test.value("exitCode").toInt(0);
    const int sleepMs = test.value("sleepMs").toInt(0);
    const bool crash = test.value("crash").toBool(false);

    if (sleepMs > 0) {
        QTimer::singleShot(sleepMs, &app, [&]() {
            if (crash) {
                std::abort();
            }
            app.exit(exitCode);
        });
        return app.exec();
    }

    if (crash) {
        std::abort();
    }

    return exitCode;
}
