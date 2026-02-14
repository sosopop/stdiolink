/**
 * KV Store Driver - KeepAlive + 有状态会话演示
 *
 * 功能演示:
 * 1. KeepAlive 生命周期（跨请求保持状态）
 * 2. QHash 内存键值存储
 * 3. TTL 过期检查
 * 4. list 命令事件流（逐条推送）
 */

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

struct KvEntry {
    QString value;
    qint64 createdMs;
    qint64 ttlMs; // 0 = never expire
};

class KvStoreHandler : public IMetaCommandHandler {
public:
    KvStoreHandler()
    {
        m_uptime.start();
        buildMeta();
    }

    const DriverMeta& driverMeta() const override { return m_meta; }

    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override
    {
        QJsonObject params = data.toObject();
        purgeExpired();

        if (cmd == "set") {
            handleSet(params, resp);
        } else if (cmd == "get") {
            handleGet(params, resp);
        } else if (cmd == "delete") {
            handleDelete(params, resp);
        } else if (cmd == "list") {
            handleList(params, resp);
        } else if (cmd == "stats") {
            handleStats(resp);
        } else if (cmd == "clear") {
            handleClear(resp);
        } else {
            resp.error(404, QJsonObject{{"message", "unknown command: " + cmd}});
        }
    }

private:
    void buildMeta();
    void handleSet(const QJsonObject& params, IResponder& resp);
    void handleGet(const QJsonObject& params, IResponder& resp);
    void handleDelete(const QJsonObject& params, IResponder& resp);
    void handleList(const QJsonObject& params, IResponder& resp);
    void handleStats(IResponder& resp);
    void handleClear(IResponder& resp);
    void purgeExpired();

    DriverMeta m_meta;
    QHash<QString, KvEntry> m_store;
    QElapsedTimer m_uptime;
};

void KvStoreHandler::buildMeta()
{
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("demo.kv_store", "KV Store Driver", "1.0.0",
              "内存键值存储，演示 KeepAlive 有状态会话与 TTL 过期")
        .vendor("stdiolink-demo")
        .command(CommandBuilder("set")
            .description("设置键值对")
            .param(FieldBuilder("key", FieldType::String)
                .required()
                .description("键名"))
            .param(FieldBuilder("value", FieldType::String)
                .required()
                .description("值"))
            .param(FieldBuilder("ttlMs", FieldType::Int)
                .description("过期时间（毫秒），0 表示永不过期")
                .defaultValue(0)
                .range(0, 3600000))
            .returns(FieldType::Object, "{key, created}"))
        .command(CommandBuilder("get")
            .description("获取键值")
            .param(FieldBuilder("key", FieldType::String)
                .required()
                .description("键名"))
            .returns(FieldType::Object, "{key, value, found}"))
        .command(CommandBuilder("delete")
            .description("删除键")
            .param(FieldBuilder("key", FieldType::String)
                .required()
                .description("键名"))
            .returns(FieldType::Object, "{key, deleted}"))
        .command(CommandBuilder("list")
            .description("列出匹配前缀的键，逐条发送 entry 事件")
            .param(FieldBuilder("prefix", FieldType::String)
                .description("键名前缀过滤")
                .defaultValue(""))
            .event("entry", "键值条目 {key, value}")
            .returns(FieldType::Object, "{count}"))
        .command(CommandBuilder("stats")
            .description("返回存储统计信息")
            .returns(FieldType::Object, "{totalKeys, oldestAgeMs}"))
        .command(CommandBuilder("clear")
            .description("清空所有键值")
            .returns(FieldType::Object, "{cleared}"))
        .build();
}

void KvStoreHandler::handleSet(const QJsonObject& params, IResponder& resp)
{
    QString key = params["key"].toString();
    QString value = params["value"].toString();
    qint64 ttlMs = static_cast<qint64>(params["ttlMs"].toDouble(0));

    m_store[key] = KvEntry{value, m_uptime.elapsed(), ttlMs};
    resp.done(0, QJsonObject{
        {"key", key},
        {"created", true}
    });
}

void KvStoreHandler::handleGet(const QJsonObject& params, IResponder& resp)
{
    QString key = params["key"].toString();
    auto it = m_store.find(key);
    if (it != m_store.end()) {
        resp.done(0, QJsonObject{
            {"key", key},
            {"value", it->value},
            {"found", true}
        });
    } else {
        resp.done(0, QJsonObject{
            {"key", key},
            {"value", QJsonValue::Null},
            {"found", false}
        });
    }
}

void KvStoreHandler::handleDelete(const QJsonObject& params, IResponder& resp)
{
    QString key = params["key"].toString();
    bool existed = m_store.remove(key) > 0;
    resp.done(0, QJsonObject{
        {"key", key},
        {"deleted", existed}
    });
}

void KvStoreHandler::handleList(const QJsonObject& params, IResponder& resp)
{
    QString prefix = params["prefix"].toString();
    int count = 0;

    for (auto it = m_store.constBegin(); it != m_store.constEnd(); ++it) {
        if (prefix.isEmpty() || it.key().startsWith(prefix)) {
            resp.event("entry", 0, QJsonObject{
                {"key", it.key()},
                {"value", it->value}
            });
            ++count;
        }
    }

    resp.done(0, QJsonObject{{"count", count}});
}

void KvStoreHandler::handleStats(IResponder& resp)
{
    qint64 now = m_uptime.elapsed();
    qint64 oldestAge = 0;

    for (auto it = m_store.constBegin(); it != m_store.constEnd(); ++it) {
        qint64 age = now - it->createdMs;
        if (age > oldestAge)
            oldestAge = age;
    }

    resp.done(0, QJsonObject{
        {"totalKeys", m_store.size()},
        {"oldestAgeMs", oldestAge}
    });
}

void KvStoreHandler::handleClear(IResponder& resp)
{
    int count = m_store.size();
    m_store.clear();
    resp.done(0, QJsonObject{{"cleared", count}});
}

void KvStoreHandler::purgeExpired()
{
    qint64 now = m_uptime.elapsed();
    QList<QString> expired;

    for (auto it = m_store.constBegin(); it != m_store.constEnd(); ++it) {
        if (it->ttlMs > 0 && (now - it->createdMs) > it->ttlMs) {
            expired.append(it.key());
        }
    }

    for (const QString& key : expired) {
        m_store.remove(key);
    }
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    KvStoreHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
