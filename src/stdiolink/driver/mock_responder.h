#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <vector>
#include "stdiolink/driver/iresponder.h"

namespace stdiolink {

/**
 * 测试用响应器，记录所有响应
 */
class MockResponder : public IResponder {
public:
    struct Response {
        QString status;
        int code;
        QJsonValue payload;
        QString eventName;  // M15: 事件名称
    };

    std::vector<Response> responses;

    void event(int code, const QJsonValue& payload) override {
        responses.push_back({"event", code, payload, "default"});
    }

    void event(const QString& eventName, int code, const QJsonValue& data) override {
        QJsonObject payload;
        payload["event"] = eventName;
        payload["data"] = data;
        responses.push_back({"event", code, payload, eventName});
    }

    void done(int code, const QJsonValue& payload) override {
        responses.push_back({"done", code, payload, QString()});
    }

    void error(int code, const QJsonValue& payload) override {
        responses.push_back({"error", code, payload, QString()});
    }

    void clear() { responses.clear(); }

    // M15: 辅助方法
    QString lastEventName() const {
        for (auto it = responses.rbegin(); it != responses.rend(); ++it) {
            if (it->status == "event") {
                return it->eventName;
            }
        }
        return QString();
    }

    int lastEventCode() const {
        for (auto it = responses.rbegin(); it != responses.rend(); ++it) {
            if (it->status == "event") {
                return it->code;
            }
        }
        return -1;
    }
};

} // namespace stdiolink
