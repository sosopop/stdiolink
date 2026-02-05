/**
 * Demo Host - 综合 Host 演示程序
 *
 * 功能演示:
 * 1. Driver 启动和管理
 * 2. Task 和 waitAnyNext
 * 3. 元数据查询
 * 4. UI 表单生成
 * 5. DriverRegistry
 * 6. ConfigInjector
 * 7. MetaVersionChecker
 */

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

#include "stdiolink/host/config_injector.h"
#include "stdiolink/host/driver.h"
#include "stdiolink/host/driver_registry.h"
#include "stdiolink/host/form_generator.h"
#include "stdiolink/host/meta_version_checker.h"
#include "stdiolink/host/wait_any.h"

using namespace stdiolink;

static QTextStream& out()
{
    static QFile file;
    static QTextStream stream;
    if (!file.isOpen()) {
        file.open(stdout, QIODevice::WriteOnly);
        stream.setDevice(&file);
    }
    return stream;
}

static void printJson(const QJsonObject& obj)
{
    out() << QJsonDocument(obj).toJson(QJsonDocument::Compact) << "\n";
    out().flush();
}

static void demoBasicUsage(const QString& path)
{
    out() << "\n=== 1. Basic Usage ===\n";
    Driver d;
    if (!d.start(path + "/calculator_driver.exe")) {
        out() << "Failed to start calculator_driver\n";
        return;
    }

    Task t = d.request("add", QJsonObject{{"a", 10}, {"b", 20}});
    Message msg;
    if (t.waitNext(msg, 5000)) {
        out() << "add(10,20) = ";
        printJson(msg.payload.toObject());
    }

    d.terminate();
}

static void demoEventStream(const QString& path)
{
    out() << "\n=== 2. Event Stream ===\n";
    Driver d;
    d.start(path + "/calculator_driver.exe");

    QJsonArray ops;
    ops.append(QJsonObject{{"type", "add"}, {"a", 1}, {"b", 2}});
    ops.append(QJsonObject{{"type", "mul"}, {"a", 3}, {"b", 4}});
    ops.append(QJsonObject{{"type", "sub"}, {"a", 10}, {"b", 5}});

    Task t = d.request("batch", QJsonObject{{"operations", ops}});
    Message msg;
    while (t.waitNext(msg, 5000)) {
        out() << "  " << msg.status << ": ";
        printJson(msg.payload.toObject());
        if (msg.status == "done") break;
    }
    d.terminate();
}

static void demoMultiDriver(const QString& path)
{
    out() << "\n=== 3. Multi-Driver (waitAnyNext) ===\n";
    Driver d1, d2;
    d1.start(path + "/calculator_driver.exe");
    d2.start(path + "/device_simulator_driver.exe");

    QVector<Task> tasks;
    tasks << d1.request("statistics", QJsonObject{
        {"numbers", QJsonArray{1, 2, 3, 4, 5}}});
    tasks << d2.request("scan", QJsonObject{{"count", 3}});

    AnyItem item;
    while (waitAnyNext(tasks, item, 5000)) {
        out() << "  Task" << item.taskIndex << " " << item.msg.status << ": ";
        printJson(item.msg.payload.toObject());
    }
    d1.terminate();
    d2.terminate();
}

static void demoMetaQuery(const QString& path)
{
    out() << "\n=== 4. Meta Query ===\n";
    Driver d;
    d.start(path + "/calculator_driver.exe");

    const auto* meta = d.queryMeta(5000);
    if (meta) {
        out() << "  Driver: " << meta->info.name << "\n";
        out() << "  Version: " << meta->info.version << "\n";
        out() << "  Commands:\n";
        for (const auto& cmd : meta->commands) {
            out() << "    - " << cmd.name << ": " << cmd.description << "\n";
        }
        out().flush();
    }
    d.terminate();
}

static void demoFormGenerator(const QString& path)
{
    out() << "\n=== 5. Form Generator ===\n";
    Driver d;
    d.start(path + "/device_simulator_driver.exe");

    const auto* meta = d.queryMeta(5000);
    if (meta) {
        const auto* cmd = meta->findCommand("connect");
        if (cmd) {
            auto form = UiGenerator::generateCommandForm(*cmd);
            out() << "  Form for 'connect': ";
            printJson(UiGenerator::toJson(form));
        }
    }
    d.terminate();
}

static void demoConfigInjector()
{
    out() << "\n=== 6. Config Injector ===\n";
    QJsonObject config{{"timeout", 3000}, {"debug", true}};
    meta::ConfigApply apply;
    apply.method = "startupArgs";

    auto args = ConfigInjector::toArgs(config, apply);
    out() << "  Args: " << args.join(" ") << "\n";
    out().flush();
}

static void demoVersionChecker()
{
    out() << "\n=== 7. Version Checker ===\n";
    out() << "  Current: " << MetaVersionChecker::getCurrentVersion() << "\n";
    out() << "  Supported: " << MetaVersionChecker::getSupportedVersions().join(", ") << "\n";
    out() << "  1.0 compatible: " << (MetaVersionChecker::isCompatible("1.0", "1.0") ? "true" : "false") << "\n";
    out().flush();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QString path = app.applicationDirPath();

    out() << "=== stdiolink Demo Host ===\n";
    out().flush();

    demoBasicUsage(path);
    demoEventStream(path);
    demoMultiDriver(path);
    demoMetaQuery(path);
    demoFormGenerator(path);
    demoConfigInjector();
    demoVersionChecker();

    out() << "\n=== Demo Completed ===\n";
    out().flush();
    return 0;
}
