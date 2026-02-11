# 里程碑 41：JS 常量模块与 getConfig 深冻结

> **前置条件**: 里程碑 40 已完成
> **目标**: 新增 `stdiolink/constants` 内置模块（SYSTEM + APP_PATHS），并把 `getConfig()` 从浅冻结升级为深冻结，提升脚本稳定性与可维护性

---

## 1. 目标

- 新增 `stdiolink/constants` 模块，提供只读常量：
  - `SYSTEM.os`、`SYSTEM.arch`
  - `SYSTEM.isWindows/isMac/isLinux`
  - `APP_PATHS.appPath/appDir/cwd/serviceDir/serviceEntryPath/serviceEntryDir/tempDir/homeDir`
- 将 `getConfig()` 返回对象升级为递归深冻结，避免嵌套对象被误修改
- 保持现有 `stdiolink` 主模块导出兼容，不改动现有 API 签名

---

## 2. 设计原则（强约束）

- **简约**: 常量只放稳定、跨脚本高复用内容，不引入动态系统信息抓取 API
- **可靠**: 常量对象和配置对象均只读，防止运行中被脚本篡改
- **稳定**: 不改变现有 `Driver/openDriver/waitAny/exec/getConfig` 用法
- **避免过度设计**: 不做“通用系统信息中心”，只实现本里程碑定义字段

---

## 3. 范围与非目标

### 3.1 范围（M41 内）

- `stdiolink/constants` 内置模块
- `SYSTEM` + `APP_PATHS` 常量对象注入
- `getConfig()` 深冻结实现
- 对应单元测试与 manual 文档更新

### 3.2 非目标（M41 外）

- 不引入 `RUNTIME/PROTOCOL/DRIVER` 额外常量组
- 不提供环境变量遍历、磁盘信息、网络接口信息
- 不改动 `openDriver` 行为

---

## 4. 技术方案

### 4.1 RuntimePathContext（路径上下文）

在 `main.cpp` 中解析完 `ServiceDirectory` 后，构造路径上下文并传入绑定层：

```cpp
// main.cpp 中构造（不新增头文件，直接使用 JsConstantsBinding 的 setPathContext）
JsConstantsBinding::setPathContext(engine.context(), {
    QCoreApplication::applicationFilePath(),  // appPath
    QCoreApplication::applicationDirPath(),   // appDir
    QDir::currentPath(),                      // cwd
    svcDir.rootPath(),                        // serviceDir
    svcDir.entryPath(),                       // serviceEntryPath
    QFileInfo(svcDir.entryPath()).absolutePath(), // serviceEntryDir
    QDir::tempPath(),                         // tempDir
    QDir::homePath()                          // homeDir
});
```

### 4.2 创建 bindings/js_constants.h

```cpp
#pragma once

#include <quickjs.h>
#include <QString>

namespace stdiolink_service {

/// @brief 路径上下文，由 main.cpp 在解析 ServiceDirectory 后注入
struct PathContext {
    QString appPath;
    QString appDir;
    QString cwd;
    QString serviceDir;
    QString serviceEntryPath;
    QString serviceEntryDir;
    QString tempDir;
    QString homeDir;
};

/// @brief stdiolink/constants 内置模块绑定
///
/// 提供 SYSTEM 和 APP_PATHS 两个只读常量对象。
/// 绑定状态按 JSRuntime 维度隔离，与 JsConfigBinding 模式一致。
class JsConstantsBinding {
public:
    /// 绑定到指定 runtime（在 engine 创建后调用）
    static void attachRuntime(JSRuntime* rt);

    /// 解绑（在 engine 销毁前调用）
    static void detachRuntime(JSRuntime* rt);

    /// 注入路径上下文（main.cpp 解析完 ServiceDirectory 后调用）
    static void setPathContext(JSContext* ctx, const PathContext& paths);

    /// 模块初始化回调（注册给 ModuleLoader）
    static JSModuleDef* initModule(JSContext* ctx, const char* name);

    /// 重置状态（测试用）
    static void reset(JSContext* ctx);
};

} // namespace stdiolink_service
```

### 4.3 创建 bindings/js_constants.cpp

```cpp
#include "js_constants.h"

#include <QHash>
#include <QSysInfo>
#include <quickjs.h>

namespace stdiolink_service {

namespace {

struct ConstantsState {
    PathContext paths;
};

QHash<quintptr, ConstantsState> s_states;

quintptr runtimeKey(JSContext* ctx) {
    return reinterpret_cast<quintptr>(JS_GetRuntime(ctx));
}

ConstantsState& stateFor(JSContext* ctx) {
    return s_states[runtimeKey(ctx)];
}

/// 递归深冻结（对象和数组均冻结，供 constants 和 config 共用）
JSValue deepFreeze(JSContext* ctx, JSValue obj) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue objectCtor = JS_GetPropertyStr(ctx, global, "Object");
    JSValue freezeFn = JS_GetPropertyStr(ctx, objectCtor, "freeze");
    JSValue keysFn = JS_GetPropertyStr(ctx, objectCtor, "keys");

    // 先递归冻结子属性
    JSValue keys = JS_Call(ctx, keysFn, objectCtor, 1, &obj);
    if (!JS_IsException(keys)) {
        JSValue lenVal = JS_GetPropertyStr(ctx, keys, "length");
        uint32_t len = 0;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        for (uint32_t i = 0; i < len; ++i) {
            JSValue key = JS_GetPropertyUint32(ctx, keys, i);
            const char* keyStr = JS_ToCString(ctx, key);
            if (keyStr) {
                JSValue child = JS_GetPropertyStr(ctx, obj, keyStr);
                if (JS_IsObject(child)) {
                    deepFreeze(ctx, child);
                }
                JS_FreeValue(ctx, child);
                JS_FreeCString(ctx, keyStr);
            }
            JS_FreeValue(ctx, key);
        }
    }
    JS_FreeValue(ctx, keys);

    // 冻结自身
    JSValue result = JS_Call(ctx, freezeFn, objectCtor, 1, &obj);
    JS_FreeValue(ctx, result);

    JS_FreeValue(ctx, keysFn);
    JS_FreeValue(ctx, freezeFn);
    JS_FreeValue(ctx, objectCtor);
    JS_FreeValue(ctx, global);
    return obj;
}

JSValue buildSystemObject(JSContext* ctx) {
    JSValue sys = JS_NewObject(ctx);

#if defined(Q_OS_WIN)
    JS_SetPropertyStr(ctx, sys, "os", JS_NewString(ctx, "windows"));
    JS_SetPropertyStr(ctx, sys, "isWindows", JS_TRUE);
    JS_SetPropertyStr(ctx, sys, "isMac", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isLinux", JS_FALSE);
#elif defined(Q_OS_MACOS)
    JS_SetPropertyStr(ctx, sys, "os", JS_NewString(ctx, "macos"));
    JS_SetPropertyStr(ctx, sys, "isWindows", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isMac", JS_TRUE);
    JS_SetPropertyStr(ctx, sys, "isLinux", JS_FALSE);
#elif defined(Q_OS_LINUX)
    JS_SetPropertyStr(ctx, sys, "os", JS_NewString(ctx, "linux"));
    JS_SetPropertyStr(ctx, sys, "isWindows", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isMac", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isLinux", JS_TRUE);
#else
    JS_SetPropertyStr(ctx, sys, "os", JS_NewString(ctx, "unknown"));
    JS_SetPropertyStr(ctx, sys, "isWindows", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isMac", JS_FALSE);
    JS_SetPropertyStr(ctx, sys, "isLinux", JS_FALSE);
#endif

    QByteArray arch = QSysInfo::currentCpuArchitecture().toUtf8();
    JS_SetPropertyStr(ctx, sys, "arch", JS_NewString(ctx, arch.constData()));

    return deepFreeze(ctx, sys);
}

JSValue buildAppPathsObject(JSContext* ctx) {
    auto& state = stateFor(ctx);
    JSValue paths = JS_NewObject(ctx);

    auto setStr = [&](const char* key, const QString& val) {
        JS_SetPropertyStr(ctx, paths, key,
                          JS_NewString(ctx, val.toUtf8().constData()));
    };

    setStr("appPath", state.paths.appPath);
    setStr("appDir", state.paths.appDir);
    setStr("cwd", state.paths.cwd);
    setStr("serviceDir", state.paths.serviceDir);
    setStr("serviceEntryPath", state.paths.serviceEntryPath);
    setStr("serviceEntryDir", state.paths.serviceEntryDir);
    setStr("tempDir", state.paths.tempDir);
    setStr("homeDir", state.paths.homeDir);

    return deepFreeze(ctx, paths);
}

int constantsModuleInit(JSContext* ctx, JSModuleDef* module) {
    JSValue system = buildSystemObject(ctx);
    JSValue appPaths = buildAppPathsObject(ctx);

    if (JS_SetModuleExport(ctx, module, "SYSTEM", system) < 0) {
        JS_FreeValue(ctx, appPaths);
        return -1;
    }
    return JS_SetModuleExport(ctx, module, "APP_PATHS", appPaths);
}

} // namespace

void JsConstantsBinding::attachRuntime(JSRuntime* rt) {
    if (!rt) return;
    const quintptr key = reinterpret_cast<quintptr>(rt);
    if (!s_states.contains(key)) {
        s_states.insert(key, ConstantsState{});
    }
}

void JsConstantsBinding::detachRuntime(JSRuntime* rt) {
    if (!rt) return;
    s_states.remove(reinterpret_cast<quintptr>(rt));
}

void JsConstantsBinding::setPathContext(JSContext* ctx,
                                         const PathContext& paths) {
    stateFor(ctx).paths = paths;
}

JSModuleDef* JsConstantsBinding::initModule(JSContext* ctx,
                                             const char* name) {
    JSModuleDef* module = JS_NewCModule(ctx, name, constantsModuleInit);
    if (!module) return nullptr;
    JS_AddModuleExport(ctx, module, "SYSTEM");
    JS_AddModuleExport(ctx, module, "APP_PATHS");
    return module;
}

void JsConstantsBinding::reset(JSContext* ctx) {
    s_states[runtimeKey(ctx)] = ConstantsState{};
}

} // namespace stdiolink_service
```

### 4.4 改造 js_config.cpp 深冻结

将现有 `freezeObject()` 替换为递归 `deepFreeze()`，复用 `js_constants.cpp` 中的实现。
为避免代码重复，将 `deepFreeze` 提取为共享工具函数：

```cpp
// utils/js_freeze.h
#pragma once
#include <quickjs.h>

namespace stdiolink_service {

/// @brief 递归深冻结 JS 对象（含嵌套对象和数组）
/// @param ctx QuickJS 上下文
/// @param obj 待冻结的对象（就地冻结，返回同一引用）
/// @return 冻结后的对象（与输入为同一引用）
JSValue deepFreezeObject(JSContext* ctx, JSValue obj);

} // namespace stdiolink_service
```

`js_config.cpp` 变更：

```cpp
// 变更前
#include "utils/js_convert.h"
// 变更后
#include "utils/js_convert.h"
#include "utils/js_freeze.h"

// jsGetConfig 中变更：
// 变更前：state.cachedConfigJs = freezeObject(ctx, configJs);
// 变更后：state.cachedConfigJs = deepFreezeObject(ctx, configJs);
```

### 4.5 main.cpp 集成

```cpp
// main.cpp — 新增部分

#include "bindings/js_constants.h"

// 在 JsConfigBinding::attachRuntime 之后：
JsConstantsBinding::attachRuntime(engine.runtime());
JsConstantsBinding::setPathContext(engine.context(), {
    QCoreApplication::applicationFilePath(),
    QCoreApplication::applicationDirPath(),
    QDir::currentPath(),
    svcDir.rootPath(),
    svcDir.entryPath(),
    QFileInfo(svcDir.entryPath()).absolutePath(),
    QDir::tempPath(),
    QDir::homePath()
});

// 模块注册（在 engine.registerModule("stdiolink", ...) 之后）：
engine.registerModule("stdiolink/constants",
                      JsConstantsBinding::initModule);
```

### 4.6 js_engine.cpp 析构清理

`JsConstantsBinding::detachRuntime(oldRt)` 统一放在 `JsEngine::~JsEngine()` 中执行，避免 runtime 状态泄漏。

```cpp
// JsEngine::~JsEngine()
stdiolink_service::JsConstantsBinding::detachRuntime(oldRt);
```

### 4.7 JS 使用示例

```js
import { SYSTEM, APP_PATHS } from "stdiolink/constants";

console.log(SYSTEM.os);        // "windows" | "macos" | "linux"
console.log(SYSTEM.arch);      // "x86_64" | "arm64" | ...
console.log(SYSTEM.isWindows); // true | false
console.log(SYSTEM.isMac);     // true | false
console.log(SYSTEM.isLinux);   // true | false

console.log(APP_PATHS.appPath);          // 可执行文件完整路径
console.log(APP_PATHS.serviceDir);       // 服务目录
console.log(APP_PATHS.serviceEntryPath); // 入口脚本路径
console.log(APP_PATHS.tempDir);          // 系统临时目录
```

---

## 5. 实现步骤

1. 在 `main.cpp` 构造 `RuntimePathContext`（仅包含 M41 所需路径）
2. 新增 `js_constants` 绑定并注册内置模块 `stdiolink/constants`
3. 将 `RuntimePathContext` 注入 `js_constants` 运行时状态
4. 改造 `js_config.cpp` 的冻结逻辑为深冻结
5. 在 `JsEngine::~JsEngine()` 接入 `JsConstantsBinding::detachRuntime(oldRt)`
6. 增补 manual 文档
7. 编写并通过单元测试

---

## 6. 文件改动清单

### 6.1 新增文件

- `src/stdiolink_service/bindings/js_constants.h`
- `src/stdiolink_service/bindings/js_constants.cpp`
- `src/stdiolink_service/utils/js_freeze.h`
- `src/stdiolink_service/utils/js_freeze.cpp`
- `src/tests/test_constants_binding.cpp`
- `doc/manual/10-js-service/constants-binding.md`

### 6.2 修改文件

- `src/stdiolink_service/main.cpp`
- `src/stdiolink_service/engine/js_engine.cpp`
- `src/stdiolink_service/CMakeLists.txt`
- `src/stdiolink_service/bindings/js_config.cpp`
- `src/tests/CMakeLists.txt`
- `doc/manual/10-js-service/module-system.md`
- `doc/manual/10-js-service/README.md`

---

## 7. 单元测试计划（全面覆盖）

### 7.1 测试 Fixture

```cpp
// test_constants_binding.cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include "engine/js_engine.h"
#include "engine/module_loader.h"
#include "engine/console_bridge.h"
#include "bindings/js_constants.h"
#include "bindings/js_stdiolink_module.h"

using namespace stdiolink_service;

class JsConstantsTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<JsEngine>();
        ModuleLoader::install(engine->context());
        ConsoleBridge::install(engine->context());
        ModuleLoader::addBuiltin("stdiolink", jsInitStdiolinkModule);

        JsConstantsBinding::attachRuntime(engine->runtime());
        JsConstantsBinding::setPathContext(engine->context(), {
            "/usr/bin/stdiolink_service",   // appPath
            "/usr/bin",                      // appDir
            "/home/user",                    // cwd
            "/srv/demo",                     // serviceDir
            "/srv/demo/index.js",            // serviceEntryPath
            "/srv/demo",                     // serviceEntryDir
            "/tmp",                          // tempDir
            "/home/user"                     // homeDir
        });
        ModuleLoader::addBuiltin("stdiolink/constants",
                                 JsConstantsBinding::initModule);
    }
    void TearDown() override {
        JsConstantsBinding::reset(engine->context());
        engine.reset();
    }

    int runScript(const QString& code) {
        QTemporaryDir dir;
        QString path = dir.path() + "/test.mjs";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        QTextStream out(&f);
        out << code;
        out.flush();
        f.close();
        return engine->evalFile(path);
    }

    int32_t getGlobalInt(const char* name) {
        JSValue g = JS_GetGlobalObject(engine->context());
        JSValue v = JS_GetPropertyStr(engine->context(), g, name);
        int32_t r = 0;
        JS_ToInt32(engine->context(), &r, v);
        JS_FreeValue(engine->context(), v);
        JS_FreeValue(engine->context(), g);
        return r;
    }

    std::unique_ptr<JsEngine> engine;
};
```

### 7.2 模块加载与字段完整性

```cpp
TEST_F(JsConstantsTest, ImportSucceeds) {
    int ret = runScript(
        "import { SYSTEM, APP_PATHS } from 'stdiolink/constants';\n"
        "globalThis.ok = (typeof SYSTEM === 'object'"
        " && typeof APP_PATHS === 'object') ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsConstantsTest, SystemFieldsComplete) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "const fields = ['os','arch','isWindows','isMac','isLinux'];\n"
        "globalThis.ok = fields.every(f => f in SYSTEM) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsConstantsTest, AppPathsFieldsComplete) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "const fields = ['appPath','appDir','cwd','serviceDir',\n"
        "  'serviceEntryPath','serviceEntryDir','tempDir','homeDir'];\n"
        "globalThis.ok = fields.every(f => f in APP_PATHS) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.3 平台一致性

```cpp
TEST_F(JsConstantsTest, PlatformBoolsMutuallyExclusive) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "const count = [SYSTEM.isWindows, SYSTEM.isMac, SYSTEM.isLinux]\n"
        "  .filter(Boolean).length;\n"
        "globalThis.ok = (count === 1) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsConstantsTest, OsMatchesBoolFlags) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "let ok = false;\n"
        "if (SYSTEM.os === 'windows') ok = SYSTEM.isWindows;\n"
        "else if (SYSTEM.os === 'macos') ok = SYSTEM.isMac;\n"
        "else if (SYSTEM.os === 'linux') ok = SYSTEM.isLinux;\n"
        "globalThis.ok = ok ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsConstantsTest, ArchIsNonEmptyString) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "globalThis.ok = (typeof SYSTEM.arch === 'string'"
        " && SYSTEM.arch.length > 0) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.4 路径值有效性

```cpp
TEST_F(JsConstantsTest, AllPathsNonEmpty) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "const vals = Object.values(APP_PATHS);\n"
        "globalThis.ok = vals.every(v =>"
        " typeof v === 'string' && v.length > 0) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsConstantsTest, AppDirIsParentOfAppPath) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "globalThis.ok = APP_PATHS.appPath.startsWith("
        "APP_PATHS.appDir) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsConstantsTest, ServiceEntryDirIsParentOfServiceEntryPath) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "globalThis.ok = APP_PATHS.serviceEntryPath.startsWith("
        "APP_PATHS.serviceEntryDir) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.5 只读性验证

```cpp
TEST_F(JsConstantsTest, SystemIsFrozen) {
    int ret = runScript(
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "globalThis.ok = Object.isFrozen(SYSTEM) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsConstantsTest, AppPathsIsFrozen) {
    int ret = runScript(
        "import { APP_PATHS } from 'stdiolink/constants';\n"
        "globalThis.ok = Object.isFrozen(APP_PATHS) ? 1 : 0;\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}

TEST_F(JsConstantsTest, SystemWriteThrowsInStrictMode) {
    int ret = runScript(
        "'use strict';\n"
        "import { SYSTEM } from 'stdiolink/constants';\n"
        "try {\n"
        "  SYSTEM.os = 'hacked';\n"
        "  globalThis.ok = 0;\n"
        "} catch (e) {\n"
        "  globalThis.ok = 1;\n"
        "}\n"
    );
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(getGlobalInt("ok"), 1);
}
```

### 7.6 getConfig 深冻结回归

扩展 `test_service_config_js.cpp`：

```cpp
TEST_F(ServiceConfigJsTest, DeepFreezeNestedObject) {
    JsConfigBinding::setMergedConfig(
        engine->context(),
        QJsonObject{{"server", QJsonObject{{"host", "127.0.0.1"},
                                            {"port", 3000}}}});
    int ret = engine->evalScript(R"(
        import { getConfig } from 'stdiolink';
        const cfg = getConfig();
        // 嵌套对象也应被冻结
        if (!Object.isFrozen(cfg.server))
            throw new Error('nested object not frozen');
        try {
            cfg.server.host = 'hacked';
            throw new Error('should not reach');
        } catch (e) {
            if (e.message === 'should not reach') throw e;
        }
    )");
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, DeepFreezeNestedArray) {
    JsConfigBinding::setMergedConfig(
        engine->context(),
        QJsonObject{{"tags", QJsonArray{"a", "b", "c"}}});
    int ret = engine->evalScript(R"(
        import { getConfig } from 'stdiolink';
        const cfg = getConfig();
        if (!Object.isFrozen(cfg.tags))
            throw new Error('nested array not frozen');
        try {
            cfg.tags.push('d');
            throw new Error('should not reach');
        } catch (e) {
            if (e.message === 'should not reach') throw e;
        }
    )");
    EXPECT_EQ(ret, 0);
}

TEST_F(ServiceConfigJsTest, MultipleGetConfigReturnsSameStructure) {
    JsConfigBinding::setMergedConfig(
        engine->context(),
        QJsonObject{{"port", 8080}});
    int ret = engine->evalScript(R"(
        import { getConfig } from 'stdiolink';
        const a = getConfig();
        const b = getConfig();
        if (a.port !== b.port) throw new Error('mismatch');
    )");
    EXPECT_EQ(ret, 0);
}
```

### 7.7 回归测试（必须全量通过）

- `test_service_config_js.cpp`
- `test_driver_task_binding.cpp`
- `test_process_binding.cpp`
- `test_proxy_and_scheduler.cpp`
- `test_js_stress.cpp`

---

## 8. 验收标准（DoD）

- `stdiolink/constants` 可用且字段符合定义
- `getConfig` 深冻结生效（含嵌套层）
- 新增与回归测试全部通过
- 文档更新完成

---

## 9. 风险与控制

- **风险 1**：路径来源不一致导致值漂移
  - 控制：全部从 `main.cpp` 已解析对象注入，不在 JS 层二次推导
- **风险 2**：深冻结带来性能回退
  - 控制：仅在首次生成配置对象时执行，后续复用缓存对象
- **风险 3**：过度扩展常量域
  - 控制：严格限制在 M41 字段，不新增动态查询能力
