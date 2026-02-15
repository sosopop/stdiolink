// Guard integration test helper subprocess.
// Parses --guard=<name>, starts ProcessGuardClient, then sleeps.
// When guard server closes, ProcessGuardClient calls forceFastExit(1).

#include <QCoreApplication>
#include <QThread>
#include "stdiolink/guard/process_guard_client.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    auto guard = stdiolink::ProcessGuardClient::startFromArgs(app.arguments());
    if (!guard) {
        // No --guard argument: exit with code 99 to signal "no guard"
        return 99;
    }

    // Sleep indefinitely — guard disconnect will forceFastExit(1)
    QThread::sleep(60);
    return 0;
}
