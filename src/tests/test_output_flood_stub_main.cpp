// Stub process that outputs large data without newlines for P0-4 buffer overflow testing.
// Usage:
//   test_output_flood_stub --flood-stdout=<bytes>
//   test_output_flood_stub --flood-stderr=<bytes>
//   test_output_flood_stub --flood-lines=<count>  (each line ~1KB, with newlines)

#include <QCoreApplication>
#include <QTextStream>
#include <cstdio>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const QStringList args = app.arguments();
    for (const QString& arg : args) {
        if (arg.startsWith("--flood-stdout=")) {
            const qint64 bytes = arg.mid(15).toLongLong();
            const QByteArray chunk(1024, 'X');
            qint64 written = 0;
            while (written < bytes) {
                const qint64 toWrite = std::min(static_cast<qint64>(chunk.size()), bytes - written);
                fwrite(chunk.constData(), 1, static_cast<size_t>(toWrite), stdout);
                written += toWrite;
            }
            fflush(stdout);
            return 0;
        }
        if (arg.startsWith("--flood-stderr=")) {
            const qint64 bytes = arg.mid(15).toLongLong();
            const QByteArray chunk(1024, 'E');
            qint64 written = 0;
            while (written < bytes) {
                const qint64 toWrite = std::min(static_cast<qint64>(chunk.size()), bytes - written);
                fwrite(chunk.constData(), 1, static_cast<size_t>(toWrite), stderr);
                written += toWrite;
            }
            fflush(stderr);
            return 0;
        }
        if (arg.startsWith("--flood-lines=")) {
            const int count = arg.mid(14).toInt();
            const QByteArray line(1000, 'L');
            for (int i = 0; i < count; ++i) {
                fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
                fputc('\n', stdout);
            }
            fflush(stdout);
            return 0;
        }
    }

    fprintf(stderr, "Usage: test_output_flood_stub --flood-stdout=<bytes>\n");
    return 1;
}
