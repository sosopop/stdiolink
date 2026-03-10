#pragma once

#include <QMap>
#include <QString>

#include "model/project.h"
#include "scanner/service_scanner.h"

namespace stdiolink_server {

class ProjectManager {
public:
    struct LoadStats {
        int loaded = 0;
        int invalid = 0;
    };

    QMap<QString, Project> loadAll(const QString& projectsDir,
                                   const QMap<QString, ServiceInfo>& services,
                                   LoadStats* stats = nullptr);

    static bool validateProject(Project& project,
                                const QMap<QString, ServiceInfo>& services);

    static bool loadProject(const QString& projectsDir,
                            const QString& id,
                            Project& project,
                            QString& error);

    static bool saveProject(const QString& projectsDir,
                            const Project& project,
                            QString& error);

    static bool removeProject(const QString& projectsDir,
                              const QString& id,
                              QString& error);

    static bool isValidProjectId(const QString& id);

private:
    static Project loadOne(const QString& projectDir,
                           const QString& id);
};

} // namespace stdiolink_server
