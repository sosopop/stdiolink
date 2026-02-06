/// @file js_engine.h
/// @brief QuickJS 引擎封装，管理 JS Runtime 和 Context 的完整生命周期

#pragma once

#include <QString>

struct JSContext;
struct JSModuleDef;
struct JSRuntime;

/// @brief QuickJS 引擎核心类
///
/// 封装 QuickJS 的 JSRuntime 和 JSContext，提供模块注册、脚本执行、
/// 异步任务调度等功能。每个 JsEngine 实例拥有独立的运行时环境。
/// 不可拷贝，确保运行时资源的唯一所有权。
class JsEngine {
public:
    /// @brief 构造函数，创建 QuickJS Runtime 和 Context
    JsEngine();

    /// @brief 析构函数，释放 Context 和 Runtime 资源
    ~JsEngine();

    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    /// @brief 注册 ES 模块到引擎
    /// @param name 模块名称（import 时使用的标识符）
    /// @param init 模块初始化函数指针
    void registerModule(const QString& name, JSModuleDef* (*init)(JSContext*, const char*));

    /// @brief 加载并执行指定的 JS 文件
    /// @param filePath JS 文件的路径
    /// @return 0 表示成功，非 0 表示失败
    int evalFile(const QString& filePath);

    /// @brief 执行所有待处理的异步任务（Promise 回调等）
    /// @return 如果还有剩余待处理任务返回 true
    bool executePendingJobs();

    /// @brief 检查是否有待处理的异步任务
    /// @return 有待处理任务返回 true
    bool hasPendingJobs() const;

    /// @brief 检查异步任务执行过程中是否发生过错误
    /// @return 发生过错误返回 true
    bool hadJobError() const { return m_jobError; }

    /// @brief 获取 QuickJS 上下文
    /// @return 当前引擎的 JSContext 指针
    JSContext* context() const { return m_ctx; }

    /// @brief 获取 QuickJS 运行时
    /// @return 当前引擎的 JSRuntime 指针
    JSRuntime* runtime() const { return m_rt; }

private:
    /// @brief 打印 JS 异常信息到 stderr
    /// @param ctx 发生异常的 JSContext
    void printException(JSContext* ctx) const;

    JSRuntime* m_rt = nullptr;   ///< QuickJS 运行时实例
    JSContext* m_ctx = nullptr;  ///< QuickJS 上下文实例
    bool m_jobError = false;     ///< 异步任务是否出错的标记
};
