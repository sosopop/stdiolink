#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include "stdiolink/host/driver.h"
#include "stdiolink/host/meta_cache.h"
#include "stdiolink/host/form_generator.h"

using namespace stdiolink;
using namespace stdiolink::meta;

static QFile g_outFile;
static QTextStream g_out;

void initOutput() {
    g_outFile.open(stdout, QIODevice::WriteOnly);
    g_out.setDevice(&g_outFile);
}

void print(const QString& msg) {
    g_out << msg << "\n";
    g_out.flush();
}

void printJson(const QString& label, const QJsonObject& obj) {
    g_out << label << " " << QJsonDocument(obj).toJson(QJsonDocument::Indented) << "\n";
    g_out.flush();
}

void printFormDesc(const FormDesc& form) {
    print("  Title: " + form.title);
    print("  Description: " + form.description);
    print("  Widgets: " + QString::number(form.widgets.size()));
    for (const auto& w : form.widgets) {
        QJsonObject widget = w.toObject();
        print("    - " + widget["name"].toString() +
              " (" + widget["type"].toString() + ")" +
              " widget: " + widget["widget"].toString());
    }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    initOutput();
    QString driverPath = app.applicationDirPath();

    print("=== Meta Host Demo ===\n");

    Driver d;
    if (!d.start(driverPath + "/meta_driver.exe")) {
        print("Failed to start meta_driver");
        return 1;
    }

    // 1. 查询元数据
    print("--- 1. Query Metadata ---");
    const DriverMeta* meta = d.queryMeta(5000);
    if (!meta) {
        print("Failed to query metadata");
        d.terminate();
        return 1;
    }

    print("Driver ID: " + meta->info.id);
    print("Driver Name: " + meta->info.name);
    print("Version: " + meta->info.version);
    print("Description: " + meta->info.description);
    print("Commands: " + QString::number(meta->commands.size()));

    // 2. 使用 MetaCache
    print("\n--- 2. MetaCache Demo ---");
    auto cached = MetaCache::instance().get(meta->info.id);
    print("Cached metadata found: " + QString(cached != nullptr ? "true" : "false"));
    print("Has changed (same hash): " + QString(MetaCache::instance().hasChanged(meta->info.id, "hash1") ? "true" : "false"));

    // 3. 生成 UI 表单
    print("\n--- 3. Generate UI Forms ---");
    for (const auto& cmd : meta->commands) {
        print("\nCommand: " + cmd.name);
        FormDesc form = UiGenerator::generateCommandForm(cmd);
        printFormDesc(form);
    }

    // 4. 生成配置表单
    print("\n--- 4. Config Form ---");
    FormDesc configForm = UiGenerator::generateConfigForm(meta->config);
    printFormDesc(configForm);

    // 5. 调用命令 - 正常参数
    print("\n--- 5. Call Commands ---");
    {
        print("\n[scan] with valid params:");
        Task t = d.request("scan", QJsonObject{{"fps", 30}, {"duration", 2.0}});
        Message msg;
        while (t.waitNext(msg, 5000)) {
            print("  " + msg.status + " " + QString::number(msg.code));
            if (msg.status == "done" || msg.status == "error") break;
        }
    }

    // 6. 调用命令 - 验证失败
    print("\n--- 6. Validation Error Demo ---");
    {
        print("\n[scan] fps out of range (fps=100):");
        Task t = d.request("scan", QJsonObject{{"fps", 100}});
        Message msg;
        if (t.waitNext(msg, 5000)) {
            print("  Status: " + msg.status);
            print("  Code: " + QString::number(msg.code));
            if (msg.payload.isObject()) {
                printJson("  Payload:", msg.payload.toObject());
            }
        }
    }

    {
        print("\n[configure] missing required field:");
        Task t = d.request("configure", QJsonObject{{"mode", "fast"}});
        Message msg;
        if (t.waitNext(msg, 5000)) {
            print("  Status: " + msg.status);
            if (msg.payload.isObject()) {
                printJson("  Payload:", msg.payload.toObject());
            }
        }
    }

    {
        print("\n[configure] invalid enum value:");
        Task t = d.request("configure", QJsonObject{
            {"mode", "invalid"},
            {"name", "test"}
        });
        Message msg;
        if (t.waitNext(msg, 5000)) {
            print("  Status: " + msg.status);
            if (msg.payload.isObject()) {
                printJson("  Payload:", msg.payload.toObject());
            }
        }
    }

    // 7. 调用 process 命令
    print("\n--- 7. Process Command ---");
    {
        Task t = d.request("process", QJsonObject{
            {"tags", QJsonArray{"tag1", "tag2", "tag3"}},
            {"options", QJsonObject{{"verbose", true}, {"level", 2}}}
        });
        Message msg;
        if (t.waitNext(msg, 5000)) {
            print("  Status: " + msg.status);
            if (msg.payload.isObject()) {
                printJson("  Result:", msg.payload.toObject());
            }
        }
    }

    d.terminate();
    print("\nDemo completed.");
    return 0;
}
