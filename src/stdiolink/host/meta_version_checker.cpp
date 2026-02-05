#include "meta_version_checker.h"
#include <QRegularExpression>

namespace stdiolink {

bool MetaVersionChecker::isCompatible(const QString& hostVersion,
                                      const QString& driverVersion) {
    int hostMajor = 0, hostMinor = 0;
    int driverMajor = 0, driverMinor = 0;

    if (!parseVersion(hostVersion, hostMajor, hostMinor)) {
        return false;
    }
    if (!parseVersion(driverVersion, driverMajor, driverMinor)) {
        return false;
    }

    // 主版本号必须相同
    if (hostMajor != driverMajor) {
        return false;
    }

    // Host 版本必须 >= Driver 版本
    return hostMinor >= driverMinor;
}

QStringList MetaVersionChecker::getSupportedVersions() {
    return {"1.0", "1.1"};
}

QString MetaVersionChecker::getCurrentVersion() {
    return "1.0";
}

bool MetaVersionChecker::parseVersion(const QString& version, int& major, int& minor) {
    static QRegularExpression re(R"(^(\d+)\.(\d+)$)");
    auto match = re.match(version);

    if (!match.hasMatch()) {
        return false;
    }

    major = match.captured(1).toInt();
    minor = match.captured(2).toInt();
    return true;
}

} // namespace stdiolink
