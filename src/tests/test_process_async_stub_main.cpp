// Test stub process for js_process_async tests.
// Supports modes: echo, stdout, stderr, sleep, exit-code.
#include <QCoreApplication>
#include <QTextStream>
#include <QThread>
#include <QTimer>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments();

    QString mode = "echo";
    int sleepMs = 0;
    int exitCode = 0;
    QString text;

    for (int i = 1; i < args.size(); ++i) {
        if (args[i].startsWith("--mode=")) {
            mode = args[i].mid(7);
        } else if (args[i].startsWith("--sleep-ms=")) {
            sleepMs = args[i].mid(11).toInt();
        } else if (args[i].startsWith("--exit-code=")) {
            exitCode = args[i].mid(12).toInt();
        } else if (args[i].startsWith("--text=")) {
            text = args[i].mid(7);
        }
    }

    if (mode == "echo") {
        // Read stdin and echo to stdout
        QTextStream in(stdin);
        QTextStream out(stdout);
        while (!in.atEnd()) {
            QString line = in.readLine();
            out << line << "\n";
            out.flush();
        }
    } else if (mode == "stdout") {
        QTextStream out(stdout);
        out << (text.isEmpty() ? "hello from stdout" : text) << "\n";
        out.flush();
    } else if (mode == "stderr") {
        QTextStream err(stderr);
        err << (text.isEmpty() ? "hello from stderr" : text) << "\n";
        err.flush();
    } else if (mode == "both") {
        QTextStream out(stdout);
        QTextStream err(stderr);
        out << "stdout-line\n";
        out.flush();
        err << "stderr-line\n";
        err.flush();
    } else if (mode == "sleep") {
        if (sleepMs > 0) {
            QThread::msleep(sleepMs);
        }
    }

    if (sleepMs > 0 && mode != "sleep") {
        QThread::msleep(sleepMs);
    }

    return exitCode;
}
