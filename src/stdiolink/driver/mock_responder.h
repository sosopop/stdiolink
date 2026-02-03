#pragma once

#include "stdiolink/driver/iresponder.h"
#include <vector>
#include <QJsonValue>

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
    };

    std::vector<Response> responses;

    void event(int code, const QJsonValue& payload) override {
        responses.push_back({"event", code, payload});
    }

    void done(int code, const QJsonValue& payload) override {
        responses.push_back({"done", code, payload});
    }

    void error(int code, const QJsonValue& payload) override {
        responses.push_back({"error", code, payload});
    }

    void clear() { responses.clear(); }
};

} // namespace stdiolink
