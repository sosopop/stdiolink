#pragma once

#include <QString>

struct JSContext;
struct JSModuleDef;
struct JSRuntime;

class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    void registerModule(const QString& name, JSModuleDef* (*init)(JSContext*, const char*));
    int evalFile(const QString& filePath);
    bool executePendingJobs();
    bool hasPendingJobs() const;
    bool hadJobError() const { return m_jobError; }

    JSContext* context() const { return m_ctx; }
    JSRuntime* runtime() const { return m_rt; }

private:
    void printException(JSContext* ctx) const;

    JSRuntime* m_rt = nullptr;
    JSContext* m_ctx = nullptr;
    bool m_jobError = false;
};
