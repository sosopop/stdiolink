# 里程碑 86：resolveDriver C++ 绑定与 data-root 参数传递

> **前置条件**: M79–M85 已完成（Modbus 驱动与 Service 模板）
> **目标**: 在 stdiolink_service C++ 层新增 `resolveDriver(driverName)` JS 绑定，统一负责驱动路径解析（优先级：dataRoot/drivers/*/ > appDir/ > CWD/），并通过 `--data-root` 参数将 Server 已知的 data_root 路径传递给 JS 运行时

## 1. 目标

### 子系统交付矩阵

| 子系统 | 交付项 |
|--------|--------|
| stdiolink_service (C++) | `--data-root` 参数解析、PathContext.dataRoot 注入、resolveDriver() C++ 实现与 JS 绑定 |
| stdiolink_server (C++) | InstanceManager 启动子进程时传递 `--data-root` 参数 |

- `ServiceArgs::parse()` 支持 `--data-root=<path>` 参数，结果存入 `ParseResult.dataRoot`
- `PathContext` 新增 `dataRoot` 字段，通过 `APP_PATHS.dataRoot` 暴露给 JS 层
- 新增 `resolveDriverPath()` C++ 函数，实现三级优先级驱动路径解析
- 新增 `stdiolink/driver` JS 模块，导出 `resolveDriver(driverName)` 函数
- `InstanceManager::startInstance()` 在启动 stdiolink_service 子进程时传递 `--data-root`
- 失败时错误信息包含 driverName 和已尝试的所有位置
- 单元测试覆盖参数解析、路径注入、三级解析优先级、错误信息格式

## 2. 背景与问题

- 当前 JS 层有三套重复的驱动路径猜测实现（`driver_utils.js` 7 路径、`runtime_utils.js` 4 路径、`process_exec_service` 内联 5 路径），在发布目录结构下全部失败
- Server 通过 `DriverManagerScanner` 已知驱动绝对路径和 `data_root` 位置，但不传递给 JS 服务，导致信息断层
- 路径解析逻辑散布在 JS 层，无法利用 C++ 的 `QDir` 能力进行可靠的文件系统扫描

**范围**:
- `ServiceArgs` 增加 `--data-root` 解析
- `PathContext` 增加 `dataRoot` 字段及 JS 暴露
- 新增 `resolveDriverPath()` C++ 实现
- 新增 `stdiolink/driver` JS 模块绑定
- `InstanceManager` 传递 `--data-root` 参数
- 上述所有改动的单元测试

**非目标**:
- 不修改任何 JS service 代码（M87 负责）
- 不修改目录布局或发布脚本（M88 负责）
- 不删除现有 `shared/lib/driver_utils.js`（M87 负责）
- 不修改 Modbus 驱动 C++ 代码

## 3. 技术要点

### 3.1 --data-root 参数传递链

```
Server (知道 data_root)
  → InstanceManager 传 --data-root=<path> 给 stdiolink_service
    → ServiceArgs::parse() 解析，存入 ParseResult.dataRoot
      → main.cpp 传入 PathContext.dataRoot
        → JS 通过 APP_PATHS.dataRoot 读取
          → resolveDriver() 内部使用 dataRoot 作为首选搜索路径
```

Before（当前）:
```cpp
// instance_manager.cpp L140-141
proc->setArguments({serviceDir, "--config-file=" + tempConfigPath,
                    "--guard=" + guard->guardName()});
```

After（改动后）:
```cpp
proc->setArguments({serviceDir, "--config-file=" + tempConfigPath,
                    "--guard=" + guard->guardName(),
                    "--data-root=" + m_dataRoot});
```

### 3.2 ParseResult 扩展

```cpp
// service_args.h — ParseResult 新增字段
struct ParseResult {
    QString serviceDir;
    QJsonObject rawConfigValues;
    QString configFilePath;
    QString guardName;
    QString dataRoot;          // 新增：--data-root=<path>
    bool dumpSchema = false;
    bool help = false;
    bool version = false;
    QString error;
};
```

解析逻辑：在 `parse()` 中识别 `--data-root=` 前缀，提取路径值。空值或未传递时 `dataRoot` 为空 QString。

### 3.3 PathContext 扩展

```cpp
// js_constants.h — PathContext 新增字段
struct PathContext {
    QString appPath;
    QString appDir;
    QString cwd;
    QString serviceDir;
    QString serviceEntryPath;
    QString serviceEntryDir;
    QString tempDir;
    QString homeDir;
    QString dataRoot;          // 新增
};
```

JS 侧暴露：
```javascript
// APP_PATHS.dataRoot — 字符串或空字符串
import { APP_PATHS } from "stdiolink/constants";
console.log(APP_PATHS.dataRoot); // e.g. "D:/app/data_root" 或 ""
```

### 3.4 resolveDriverPath() C++ 实现

三级优先级解析，不再猜测相对路径：

```cpp
// js_driver_resolve.h
#pragma once
#include <QString>
#include <QStringList>

namespace stdiolink_service {

struct DriverResolveResult {
    QString path;                // 成功时为绝对路径，失败时为空
    QStringList searchedPaths;   // 已尝试的位置列表（用于错误信息）
};

DriverResolveResult resolveDriverPath(const QString &driverName,
                                      const QString &dataRoot,
                                      const QString &appDir);
} // namespace stdiolink_service
```

解析流程：

```
输入: driverName = "stdio.drv.calculator"

1. dataRoot/drivers/*/  （发布布局）
   扫描 dataRoot + "/drivers/" 下所有子目录
   在每个子目录中查找 driverName + 平台后缀
   ↓ 未命中

2. appDir/              （开发布局，build/bin/ 同目录）
   查找 appDir + "/" + driverName + 平台后缀
   ↓ 未命中

3. CWD/                 （当前工作目录回退）
   查找 QDir::currentPath() + "/" + driverName + 平台后缀
   ↓ 未命中

返回空路径 + 已尝试位置列表
```

### 3.5 stdiolink/driver JS 模块

新增 JS 模块 `stdiolink/driver`，导出 `resolveDriver()` 函数：

```javascript
import { resolveDriver } from "stdiolink/driver";

// 成功: 返回绝对路径字符串
const path = resolveDriver("stdio.drv.calculator");

// 失败: 抛出 Error，信息包含 driverName 和已尝试位置
// Error: driver not found: "stdio.drv.calculator"
//   searched:
//     - D:/app/data_root/drivers/*/
//     - D:/app/bin/
//     - D:/app/working_dir/
```

### 3.6 错误处理策略

| 错误场景 | 行为 | 错误信息 |
|----------|------|----------|
| driverName 为空字符串 | JS 层抛出 TypeError | `resolveDriver: driverName must be a non-empty string` |
| driverName 含路径分隔符（`/` 或 `\`） | JS 层抛出 TypeError | `resolveDriver: driverName must not contain path separators` |
| driverName 以 `.exe` 结尾 | JS 层抛出 TypeError | `resolveDriver: driverName must not end with .exe` |
| 三级搜索均未命中 | JS 层抛出 Error (JS_ThrowInternalError) | 包含 driverName + 已尝试位置列表 |
| dataRoot 为空（未传 --data-root） | 跳过第 1 级，继续 2、3 级 | 正常行为，不报错 |
| dataRoot/drivers/ 目录不存在 | 跳过第 1 级，继续 2、3 级 | 正常行为，不报错 |

设计决策：

- `resolveDriver()` 只接受驱动基础名（如 `stdio.drv.calculator`），不接受路径或带平台后缀的名称。若调用方已持有完整路径，应直接传给 `openDriver()` 而非经过 `resolveDriver()`。
- `.exe` 后缀采用严格拒绝策略（而非宽容归一化/自动剥离），理由：(1) 避免 Windows 下拼接为 `name.exe.exe` 的隐蔽 bug；(2) 驱动名是跨平台标识符，不应包含平台特定后缀；(3) 严格拒绝比静默修正更易排查问题。

### 3.7 向后兼容

- `--data-root` 为可选参数，不传时 `dataRoot` 为空，`resolveDriver()` 退化为 appDir + CWD 两级查找
- 现有 JS service 仍可使用旧的 `findDriverPath()`（M87 才迁移）
- `APP_PATHS` 新增 `dataRoot` 字段不影响现有字段读取

## 4. 实现步骤

### 4.1 ServiceArgs 增加 --data-root 解析

- 修改 `src/stdiolink_service/config/service_args.h`：
  - `ParseResult` 新增 `QString dataRoot` 字段：
    ```cpp
    struct ParseResult {
        // ... 现有字段 ...
        QString dataRoot;  // 新增：--data-root=<path>，空表示未指定
    };
    ```

- 修改 `src/stdiolink_service/config/service_args.cpp`：
  - `parse()` 方法中，在现有参数识别链（约 L41-118）中新增 `--data-root=` 分支：
    ```cpp
    } else if (arg.startsWith("--data-root=")) {
        result.dataRoot = arg.mid(QString("--data-root=").length());
    }
    ```
  - 理由：与 `--config-file=`、`--guard=` 等参数保持一致的 `--key=value` 解析模式
  - 验收：T01–T03 单元测试

### 4.2 PathContext 增加 dataRoot 字段

- 修改 `src/stdiolink_service/bindings/js_constants.h`：
  - `PathContext` 新增 `QString dataRoot` 字段：
    ```cpp
    struct PathContext {
        // ... 现有 8 个字段 ...
        QString dataRoot;  // 新增
    };
    ```
  - `JsConstantsBinding` 新增公开 API，供 `JsDriverResolveBinding` 访问 PathContext：
    ```cpp
    /// 获取当前 PathContext（供其他绑定模块读取 dataRoot、appDir 等）
    static const PathContext& getPathContext(JSContext* ctx);
    ```

- 修改 `src/stdiolink_service/bindings/js_constants.cpp`：
  - `buildAppPathsObject()`（约 L58-77）中新增 `dataRoot` 属性：
    ```cpp
    static JSValue buildAppPathsObject(JSContext* ctx, const PathContext& paths) {
        JSValue obj = JS_NewObject(ctx);
        // ... 现有 8 个 setStr 调用 ...
        setStr(ctx, obj, "dataRoot", paths.dataRoot);  // 新增
        return obj;
    }
    ```
  - 新增 `getPathContext()` 公开实现（供 JsDriverResolveBinding 调用）：
    ```cpp
    const PathContext& JsConstantsBinding::getPathContext(JSContext* ctx) {
        return stateFor(ctx).paths;
    }
    ```
  - 理由：使 JS 层可通过 `APP_PATHS.dataRoot` 读取 data_root 路径
  - 验收：T04–T05 单元测试

### 4.3 main.cpp 传递 dataRoot

- 新增 `normalizeDataRoot()` 公共函数到 `src/stdiolink_service/config/service_args.h`：
  ```cpp
  /// 将 dataRoot 规范化为绝对路径；空输入返回空 QString
  inline QString normalizeDataRoot(const QString &raw) {
      return raw.isEmpty() ? QString() : QDir(raw).absolutePath();
  }
  ```

- 修改 `src/stdiolink_service/main.cpp`：
  - `setPathContext()` 调用（约 L194-210）中增加 `parsed.dataRoot`：
    ```cpp
    QString normalizedDataRoot = normalizeDataRoot(parsed.dataRoot);

    JsConstantsBinding::setPathContext(engine.context(), {
        QCoreApplication::applicationFilePath(),      // appPath
        QCoreApplication::applicationDirPath(),       // appDir
        QDir::currentPath(),                          // cwd
        parsed.serviceDir,                            // serviceDir
        svcDir.entryPath(),                           // serviceEntryPath
        QFileInfo(svcDir.entryPath()).absolutePath(), // serviceEntryDir
        QDir::tempPath(),                             // tempDir
        QDir::homePath(),                             // homeDir
        normalizedDataRoot                            // dataRoot — 新增（已规范化）
    });
    ```
  - 理由：将 CLI 解析结果注入 JS 运行时上下文；`parsed.dataRoot` 可能是相对路径（手动调用 stdiolink_service 时），需在 CWD 尚未改变时规范化为绝对路径；规范化逻辑提取为公共函数以便单元测试直接覆盖（T19）
  - 验收：T19 单元测试 + 集成测试（启动 service 后 JS 可读取 APP_PATHS.dataRoot）

### 4.4 InstanceManager 传递 --data-root

- 修改 `src/stdiolink_server/manager/instance_manager.cpp`：
  - `startInstance()`（约 L140-141）的 `setArguments()` 增加 `--data-root`：
    ```cpp
    QStringList args = {serviceDir, "--config-file=" + tempConfigPath,
                        "--guard=" + guard->guardName()};
    if (!m_dataRoot.isEmpty()) {
        args << ("--data-root=" + m_dataRoot);
    }
    proc->setArguments(args);
    ```
  - `m_dataRoot` 来源：`InstanceManager` 构造时从 `ServerManager` 获取，`ServerManager` 从 CLI `--data-root` 或默认路径推导
  - 理由：Server 已知 data_root 绝对路径，传递给子进程避免 JS 层猜测
  - 验收：集成测试（Server 启动 Instance 后，JS 可通过 resolveDriver 找到驱动）

### 4.5 resolveDriverPath() C++ 实现

- 新增 `src/stdiolink_service/bindings/js_driver_resolve.h`：
  ```cpp
  #pragma once
  #include <QString>
  #include <QStringList>

  namespace stdiolink_service {

  struct DriverResolveResult {
      QString path;
      QStringList searchedPaths;
  };

  DriverResolveResult resolveDriverPath(const QString &driverName,
                                        const QString &dataRoot,
                                        const QString &appDir);
  } // namespace stdiolink_service
  ```

- 新增 `src/stdiolink_service/bindings/js_driver_resolve.cpp`：
  ```cpp
  #include "js_driver_resolve.h"
  #include <QDir>
  #include <QFileInfo>

  namespace stdiolink_service {

  static QString execName(const QString &baseName) {
  #ifdef Q_OS_WIN
      return baseName + ".exe";
  #else
      return baseName;
  #endif
  }

  // 校验 driverName 合法性：不允许路径分隔符、不允许已带 .exe 后缀
  static bool isValidDriverName(const QString &name) {
      if (name.contains('/') || name.contains('\\')) return false;
      if (name.endsWith(".exe", Qt::CaseInsensitive)) return false;
      return true;
  }

  // 检查文件存在且可执行（Unix 校验 execute 权限，Windows 仅检查存在）
  static bool isDriverCandidate(const QString &path) {
      QFileInfo fi(path);
      if (!fi.exists()) return false;
  #ifdef Q_OS_WIN
      return true;  // Windows 通过 .exe 后缀判定可执行
  #else
      return fi.isExecutable();
  #endif
  }

  DriverResolveResult resolveDriverPath(const QString &driverName,
                                        const QString &dataRoot,
                                        const QString &appDir)
  {
      DriverResolveResult result;
      if (!isValidDriverName(driverName)) return result;  // 非法名称直接返回空
      const QString name = execName(driverName);

      // 1. dataRoot/drivers/*/
      if (!dataRoot.isEmpty()) {
          QDir driversDir(dataRoot + "/drivers");
          QString searchEntry = driversDir.absolutePath() + "/*/";
          result.searchedPaths << searchEntry;
          if (driversDir.exists()) {
              const auto subs = driversDir.entryList(
                  QDir::Dirs | QDir::NoDotAndDotDot);
              for (const auto &sub : subs) {
                  QString p = driversDir.absoluteFilePath(sub + "/" + name);
                  if (isDriverCandidate(p)) {
                      result.path = p;
                      return result;
                  }
              }
          }
      }

      // 2. appDir/
      if (!appDir.isEmpty()) {
          QString p = QDir(appDir).absoluteFilePath(name);
          result.searchedPaths << p;
          if (isDriverCandidate(p)) {
              result.path = p;
              return result;
          }
      }

      // 3. CWD/
      {
          QString p = QDir::current().absoluteFilePath(name);
          result.searchedPaths << p;
          if (isDriverCandidate(p)) {
              result.path = p;
              return result;
          }
      }

      return result;
  }

  } // namespace stdiolink_service
  ```
  - 理由：纯 C++ 实现，利用 QDir 进行可靠的文件系统扫描；返回 searchedPaths 用于错误诊断
  - 验收：T06–T10 单元测试

### 4.6 stdiolink/driver JS 模块绑定

- 新增 `src/stdiolink_service/bindings/js_driver_resolve_binding.h` / `.cpp`：
  - 注意：现有 `js_driver.h` 已定义 `JsDriverBinding` 类（Driver 实例的 JS 绑定），新类命名为 `JsDriverResolveBinding` 以避免冲突
  - 注册 `stdiolink/driver` 模块，导出 `resolveDriver` 函数：
    ```cpp
    // js_driver_resolve_binding.h
    #pragma once
    #include <quickjs.h>

    class JsDriverResolveBinding {
    public:
        static void attachRuntime(JSRuntime* rt);
        static void detachRuntime(JSRuntime* rt);
        static JSModuleDef* initModule(JSContext* ctx, const char* name);
    ```
  - `initModule()` 中注册 `resolveDriver` 为模块导出函数：
    ```cpp
    // resolveDriver(driverName) JS 入口
    static JSValue js_resolveDriver(JSContext* ctx, JSValueConst this_val,
                                     int argc, JSValueConst* argv)
    {
        if (argc < 1 || !JS_IsString(argv[0]))
            return JS_ThrowTypeError(ctx,
                "resolveDriver: driverName must be a non-empty string");

        const char* cname = JS_ToCString(ctx, argv[0]);
        if (!cname || strlen(cname) == 0) {
            JS_FreeCString(ctx, cname);
            return JS_ThrowTypeError(ctx,
                "resolveDriver: driverName must be a non-empty string");
        }

        // 先转为 QString 再释放 C 字符串，避免 use-after-free
        QString driverName = QString::fromUtf8(cname);
        JS_FreeCString(ctx, cname);

        // JS 层前置校验：提供明确的错误信息（而非 C++ 层静默返回空结果）
        if (driverName.contains('/') || driverName.contains('\\'))
            return JS_ThrowTypeError(ctx,
                "resolveDriver: driverName must not contain path separators");
        if (driverName.endsWith(".exe", Qt::CaseInsensitive))
            return JS_ThrowTypeError(ctx,
                "resolveDriver: driverName must not end with .exe");

        // 通过 JsConstantsBinding 公开 API 获取 PathContext
        auto& paths = JsConstantsBinding::getPathContext(ctx);
        auto result = stdiolink_service::resolveDriverPath(
            driverName, paths.dataRoot, paths.appDir);

        if (result.path.isEmpty()) {
            // 构造包含已尝试位置的错误信息
            QString msg = QString("driver not found: \"%1\"\n  searched:")
                .arg(driverName);
            for (const auto& p : result.searchedPaths)
                msg += "\n    - " + p;
            return JS_ThrowInternalError(ctx, "%s",
                msg.toUtf8().constData());
        }

        return JS_NewString(ctx, result.path.toUtf8().constData());
    }
    ```

- 修改 `src/stdiolink_service/main.cpp`：
  - 在模块注册区（约 L212-219）新增 `stdiolink/driver` 模块：
    ```cpp
    JsDriverResolveBinding::attachRuntime(engine.runtime());
    // ... 现有模块注册 ...
    ```
  - 理由：遵循现有 `stdiolink/constants`、`stdiolink/fs` 等模块的注册模式
  - 验收：T11–T13, T17–T18 单元测试（JS 层调用验证）

## 5. 文件变更清单

### 5.1 新增文件
- `src/stdiolink_service/bindings/js_driver_resolve.h` — resolveDriverPath C++ 声明
- `src/stdiolink_service/bindings/js_driver_resolve.cpp` — resolveDriverPath C++ 实现（三级优先级）
- `src/stdiolink_service/bindings/js_driver_resolve_binding.h` — stdiolink/driver JS 模块绑定声明（类名 JsDriverResolveBinding，避免与现有 JsDriverBinding 冲突）
- `src/stdiolink_service/bindings/js_driver_resolve_binding.cpp` — stdiolink/driver JS 模块绑定实现

### 5.2 修改文件
- `src/stdiolink_service/config/service_args.h` — ParseResult 增加 dataRoot 字段
- `src/stdiolink_service/config/service_args.cpp` — parse() 识别 --data-root=
- `src/stdiolink_service/bindings/js_constants.h` — PathContext 增加 dataRoot 字段 + 新增 getPathContext 公开 API
- `src/stdiolink_service/bindings/js_constants.cpp` — buildAppPathsObject 增加 dataRoot + getPathContext 实现
- `src/stdiolink_service/main.cpp` — setPathContext 传入 dataRoot + 注册 stdiolink/driver 模块
- `src/stdiolink_server/manager/instance_manager.cpp` — setArguments 增加 --data-root
- `src/stdiolink_service/CMakeLists.txt` — 新增源文件编译
- `src/tests/CMakeLists.txt` — 新增 test_driver_resolve.cpp 编译目标

### 5.3 测试文件
- `src/tests/test_service_args.cpp` — 新增 T01–T03（--data-root 解析测试）、T19（normalizeDataRoot 规范化测试）
- `src/tests/test_constants_binding.cpp` — 新增 T04–T05（APP_PATHS.dataRoot 测试）
- `src/tests/test_driver_resolve.cpp` — 新建，T06–T18（resolveDriverPath + JS 绑定 + 边界条件测试）

## 6. 测试与验收

### 6.1 单元测试

- 测试对象: `ServiceArgs::parse()`、`JsConstantsBinding`（APP_PATHS.dataRoot）、`resolveDriverPath()`、`stdiolink/driver` JS 模块
- 用例分层: 正常路径（参数解析、三级命中）、边界值（空 dataRoot、空 appDir）、异常输入（空 driverName、非法参数）、错误传播（未命中时错误信息格式）
- 断言要点: 返回值精确匹配、错误信息包含 driverName 和已尝试位置、JS 异常类型正确
- 桩替身策略: 使用临时目录创建 fake driver 可执行文件（空文件即可），无需 mock
- 测试文件: `src/tests/test_service_args.cpp`、`src/tests/test_constants_binding.cpp`、`src/tests/test_driver_resolve.cpp`

#### 路径矩阵

| 决策点 | 路径 | 用例 ID |
|--------|------|---------|
| `parse()`: 含 `--data-root=/path` | `dataRoot` = `/path` | T01 |
| `parse()`: 无 `--data-root` | `dataRoot` 为空 QString | T02 |
| `parse()`: `--data-root=`（空值） | `dataRoot` 为空 QString | T03 |
| `buildAppPathsObject()`: dataRoot 非空 | JS `APP_PATHS.dataRoot` 返回路径字符串 | T04 |
| `buildAppPathsObject()`: dataRoot 为空 | JS `APP_PATHS.dataRoot` 返回空字符串 | T05 |
| `resolveDriverPath()`: dataRoot/drivers 下命中 | 返回绝对路径 | T06 |
| `resolveDriverPath()`: dataRoot 无匹配，appDir 命中 | 返回 appDir 下路径 | T07 |
| `resolveDriverPath()`: 前两级无匹配，CWD 命中 | 返回 CWD 下路径 | T08 |
| `resolveDriverPath()`: 三级均未命中 | path 为空，searchedPaths 含 3 条 | T09 |
| `resolveDriverPath()`: dataRoot 为空 | 跳过第 1 级，searchedPaths 含 2 条 | T10 |
| JS `resolveDriver()`: 正常命中 | 返回绝对路径字符串 | T11 |
| JS `resolveDriver()`: 未命中 | 抛出 Error，信息含 driverName + 位置列表 | T12 |
| JS `resolveDriver()`: 空字符串参数 | 抛出 TypeError | T13 |
| `resolveDriverPath()`: driverName 含路径分隔符 | path 为空（拒绝） | T14 |
| `resolveDriverPath()`: driverName 带 .exe 后缀 | path 为空（拒绝，避免双重拼接） | T15 |
| `resolveDriverPath()`: Unix 下文件存在但无执行权限 | 跳过该文件（仅 Unix） | T16 |
| JS `resolveDriver()`: driverName 含路径分隔符 | 抛出 TypeError | T17 |
| JS `resolveDriver()`: driverName 带 .exe 后缀 | 抛出 TypeError | T18 |
| `normalizeDataRoot()`: 相对/空/绝对路径 | 相对路径返回绝对路径，空返回空，绝对路径不变 | T19 |

#### 用例详情

**T01 — parse() 解析 --data-root 参数**
- 前置条件: 无
- 输入: `ServiceArgs::parse({"app", "svcDir", "--data-root=/some/path"})`
- 预期: `result.dataRoot == "/some/path"`
- 断言: `EXPECT_EQ(result.dataRoot, "/some/path")`

**T02 — parse() 无 --data-root 参数**
- 前置条件: 无
- 输入: `ServiceArgs::parse({"app", "svcDir", "--guard=test"})`
- 预期: `result.dataRoot` 为空
- 断言: `EXPECT_TRUE(result.dataRoot.isEmpty())`

**T03 — parse() --data-root= 空值**
- 前置条件: 无
- 输入: `ServiceArgs::parse({"app", "svcDir", "--data-root="})`
- 预期: `result.dataRoot` 为空
- 断言: `EXPECT_TRUE(result.dataRoot.isEmpty())`

**T04 — APP_PATHS.dataRoot 非空**
- 前置条件: 创建 JS 引擎，setPathContext 传入 dataRoot="/test/data"
- 输入: JS 执行 `import { APP_PATHS } from "stdiolink/constants"; APP_PATHS.dataRoot`
- 预期: 返回 `"/test/data"`
- 断言: JS 返回值为字符串 `"/test/data"`

**T05 — APP_PATHS.dataRoot 为空**
- 前置条件: 创建 JS 引擎，setPathContext 传入 dataRoot=""
- 输入: JS 执行 `APP_PATHS.dataRoot`
- 预期: 返回空字符串 `""`
- 断言: JS 返回值为空字符串

**T06 — resolveDriverPath 在 dataRoot/drivers 下命中**
- 前置条件: 创建临时目录 `tmpDir/drivers/my_drv/`，在其中创建空文件 `stdio.drv.calc`（Unix）或 `stdio.drv.calc.exe`（Windows）
- 输入: `resolveDriverPath("stdio.drv.calc", tmpDir, "/nonexist")`
- 预期: `result.path` 为 `tmpDir/drivers/my_drv/stdio.drv.calc[.exe]` 的绝对路径
- 断言: `EXPECT_FALSE(result.path.isEmpty())`; `EXPECT_TRUE(QFileInfo::exists(result.path))`

**T07 — resolveDriverPath dataRoot 无匹配，appDir 命中**
- 前置条件: 创建临时目录 `tmpAppDir/`，在其中创建空文件 `stdio.drv.calc[.exe]`；`tmpDataRoot/drivers/` 为空目录
- 输入: `resolveDriverPath("stdio.drv.calc", tmpDataRoot, tmpAppDir)`
- 预期: `result.path` 为 `tmpAppDir/stdio.drv.calc[.exe]` 的绝对路径
- 断言: `EXPECT_TRUE(result.path.endsWith("stdio.drv.calc" + ext))`

**T08 — resolveDriverPath 前两级无匹配，CWD 命中**
- 前置条件: 使用 `QTemporaryDir` 创建临时目录，通过 `QDir::setCurrent()` 临时切换 CWD 到该目录，在其中创建 fake driver 文件；dataRoot 和 appDir 均指向不存在的路径。测试结束后恢复原 CWD
- 输入: `resolveDriverPath("stdio.drv.test", "/nonexist_dr", "/nonexist_app")`
- 预期: `result.path` 为临时 CWD 下的绝对路径
- 断言: `EXPECT_FALSE(result.path.isEmpty())`; path 位于临时目录内

**T09 — resolveDriverPath 三级均未命中**
- 前置条件: dataRoot、appDir、CWD 均无 `stdio.drv.ghost` 文件
- 输入: `resolveDriverPath("stdio.drv.ghost", tmpDataRoot, tmpAppDir)`
- 预期: `result.path` 为空；`result.searchedPaths` 包含 3 条记录
- 断言: `EXPECT_TRUE(result.path.isEmpty())`; `EXPECT_EQ(result.searchedPaths.size(), 3)`

**T10 — resolveDriverPath dataRoot 为空时跳过第 1 级**
- 前置条件: appDir 和 CWD 均无目标文件
- 输入: `resolveDriverPath("stdio.drv.none", "", tmpAppDir)`
- 预期: `result.path` 为空；`result.searchedPaths` 包含 2 条（无 dataRoot 条目）
- 断言: `EXPECT_TRUE(result.path.isEmpty())`; `EXPECT_EQ(result.searchedPaths.size(), 2)`

**T11 — JS resolveDriver() 正常命中**
- 前置条件: 创建 JS 引擎，setPathContext 传入有效 appDir（含 fake driver 文件）
- 输入: JS 执行 `import { resolveDriver } from "stdiolink/driver"; resolveDriver("stdio.drv.calc")`
- 预期: 返回绝对路径字符串
- 断言: JS 返回值为非空字符串，路径存在

**T12 — JS resolveDriver() 未命中抛出 Error**
- 前置条件: 创建 JS 引擎，setPathContext 传入空目录
- 输入: JS 执行 `resolveDriver("stdio.drv.nonexist")`
- 预期: 抛出 Error，信息包含 `"driver not found"` 和 `"stdio.drv.nonexist"`
- 断言: JS 异常类型为 Error；异常信息包含 `"driver not found"` 和 `"searched"`

**T13 — JS resolveDriver() 空字符串参数抛出 TypeError**
- 前置条件: 创建 JS 引擎
- 输入: JS 执行 `resolveDriver("")`
- 预期: 抛出 TypeError
- 断言: JS 异常类型为 TypeError；异常信息包含 `"non-empty string"`

**T14 — resolveDriverPath driverName 含路径分隔符被拒绝**
- 前置条件: 无
- 输入: `resolveDriverPath("../etc/passwd", tmpDataRoot, tmpAppDir)`
- 预期: `result.path` 为空（isValidDriverName 拒绝）
- 断言: `EXPECT_TRUE(result.path.isEmpty())`

**T15 — resolveDriverPath driverName 带 .exe 后缀被拒绝**
- 前置条件: 无
- 输入: `resolveDriverPath("stdio.drv.calc.exe", tmpDataRoot, tmpAppDir)`
- 预期: `result.path` 为空（避免 Windows 下拼接为 `stdio.drv.calc.exe.exe`）
- 断言: `EXPECT_TRUE(result.path.isEmpty())`

**T16 — Unix 下文件存在但无执行权限被跳过**
- 前置条件: 仅 Unix 平台运行。在 tmpAppDir 创建文件 `stdio.drv.noexec`，权限设为 0644（无执行位）
- 输入: `resolveDriverPath("stdio.drv.noexec", "", tmpAppDir)`
- 预期: `result.path` 为空（isDriverCandidate 检查 isExecutable 失败）
- 断言: `EXPECT_TRUE(result.path.isEmpty())`

**T17 — JS resolveDriver() driverName 含路径分隔符抛出 TypeError**
- 前置条件: 创建 JS 引擎
- 输入: JS 执行 `resolveDriver("../etc/passwd")`
- 预期: 抛出 TypeError，信息包含 `"path separators"`
- 断言: JS 异常类型为 TypeError

**T18 — JS resolveDriver() driverName 带 .exe 后缀抛出 TypeError**
- 前置条件: 创建 JS 引擎
- 输入: JS 执行 `resolveDriver("stdio.drv.calc.exe")`
- 预期: 抛出 TypeError，信息包含 `".exe"`
- 断言: JS 异常类型为 TypeError

**T19 — 相对 --data-root 输入规范化为绝对路径**
- 前置条件: 无（直接测试 `normalizeDataRoot()` 公共函数）
- 输入: `normalizeDataRoot("../some/relative")`、`normalizeDataRoot("")`、`normalizeDataRoot("/abs/path")`
- 预期: 相对路径返回绝对路径（不含 `..`）；空字符串返回空；绝对路径原样返回
- 断言: `QDir::isAbsolutePath()` 为 true（非空时）；空输入返回空 QString
- 注: `normalizeDataRoot()` 从 main.cpp 内联逻辑提取为 `service_args.h` 中的公共函数（见 §4.3），main.cpp 调用该函数

#### 测试代码

```cpp
// test_service_args.cpp — 新增用例

// T01 — parse() 解析 --data-root 参数
TEST(ServiceArgsTest, T01_ParseDataRoot) {
    auto r = ServiceArgs::parse({"app", "svcDir", "--data-root=/some/path"});
    EXPECT_TRUE(r.error.isEmpty());
    EXPECT_EQ(r.dataRoot, "/some/path");
}

// T02 — parse() 无 --data-root 参数
TEST(ServiceArgsTest, T02_ParseNoDataRoot) {
    auto r = ServiceArgs::parse({"app", "svcDir", "--guard=test"});
    EXPECT_TRUE(r.error.isEmpty());
    EXPECT_TRUE(r.dataRoot.isEmpty());
}

// T03 — parse() --data-root= 空值
TEST(ServiceArgsTest, T03_ParseDataRootEmpty) {
    auto r = ServiceArgs::parse({"app", "svcDir", "--data-root="});
    EXPECT_TRUE(r.error.isEmpty());
    EXPECT_TRUE(r.dataRoot.isEmpty());
}

// T19 — normalizeDataRoot 规范化测试
TEST(ServiceArgsTest, T19_NormalizeDataRoot) {
    // 空输入返回空
    EXPECT_TRUE(normalizeDataRoot("").isEmpty());

    // 绝对路径原样返回（已规范化）
    QString abs = normalizeDataRoot("/abs/path");
    EXPECT_TRUE(QDir::isAbsolutePath(abs));
    EXPECT_EQ(abs, QDir("/abs/path").absolutePath());

    // 相对路径规范化为绝对路径，不含 ".." 片段
    QString rel = normalizeDataRoot("../some/relative");
    EXPECT_TRUE(QDir::isAbsolutePath(rel));
    EXPECT_FALSE(rel.contains(".."));
}
```

```cpp
// test_driver_resolve.cpp — 新建

class DriverResolveTest : public ::testing::Test {
protected:
    QTemporaryDir m_tmpDir;
    QString ext() {
#ifdef Q_OS_WIN
        return ".exe";
#else
        return "";
#endif
    }
    // 在指定目录创建 fake driver 文件
    void createFakeDriver(const QString& dir, const QString& name) {
        QDir().mkpath(dir);
        QFile f(dir + "/" + name + ext());
        f.open(QIODevice::WriteOnly);
        f.close();
    }
};

// T06 — dataRoot/drivers 下命中
TEST_F(DriverResolveTest, T06_HitInDataRootDrivers) {
    QString dataRoot = m_tmpDir.path() + "/data";
    createFakeDriver(dataRoot + "/drivers/my_drv", "stdio.drv.calc");
    auto r = stdiolink_service::resolveDriverPath("stdio.drv.calc", dataRoot, "/nonexist");
    EXPECT_FALSE(r.path.isEmpty());
    EXPECT_TRUE(QFileInfo::exists(r.path));
    EXPECT_TRUE(r.path.contains("my_drv"));
}

// T07 — dataRoot 无匹配，appDir 命中
TEST_F(DriverResolveTest, T07_FallbackToAppDir) {
    QString dataRoot = m_tmpDir.path() + "/data";
    QDir().mkpath(dataRoot + "/drivers");  // 空 drivers 目录
    QString appDir = m_tmpDir.path() + "/app";
    createFakeDriver(appDir, "stdio.drv.calc");
    auto r = stdiolink_service::resolveDriverPath("stdio.drv.calc", dataRoot, appDir);
    EXPECT_FALSE(r.path.isEmpty());
    EXPECT_TRUE(r.path.contains("app"));
}

// T08 — 前两级无匹配，CWD 命中（使用临时目录避免污染）
TEST_F(DriverResolveTest, T08_FallbackToCwd) {
    // 临时切换 CWD 到隔离目录
    QTemporaryDir cwdDir;
    ASSERT_TRUE(cwdDir.isValid());
    QString origCwd = QDir::currentPath();
    QDir::setCurrent(cwdDir.path());

    createFakeDriver(cwdDir.path(), "stdio.drv.cwdtest");
    auto r = stdiolink_service::resolveDriverPath("stdio.drv.cwdtest", "/nonexist", "/nonexist");
    EXPECT_FALSE(r.path.isEmpty());
    EXPECT_TRUE(r.path.startsWith(cwdDir.path()));

    QDir::setCurrent(origCwd);  // 恢复原 CWD
}

// T09 — 三级均未命中
TEST_F(DriverResolveTest, T09_AllMiss) {
    auto r = stdiolink_service::resolveDriverPath("stdio.drv.ghost",
        m_tmpDir.path(), m_tmpDir.path() + "/app");
    EXPECT_TRUE(r.path.isEmpty());
    EXPECT_EQ(r.searchedPaths.size(), 3);
}

// T10 — dataRoot 为空时跳过第 1 级
TEST_F(DriverResolveTest, T10_EmptyDataRootSkipsLevel1) {
    auto r = stdiolink_service::resolveDriverPath("stdio.drv.none",
        "", m_tmpDir.path() + "/app");
    EXPECT_TRUE(r.path.isEmpty());
    EXPECT_EQ(r.searchedPaths.size(), 2);
}

// T14 — driverName 含路径分隔符被拒绝
TEST_F(DriverResolveTest, T14_PathSeparatorRejected) {
    auto r = stdiolink_service::resolveDriverPath("../etc/passwd",
        m_tmpDir.path(), m_tmpDir.path());
    EXPECT_TRUE(r.path.isEmpty());
}

// T15 — driverName 带 .exe 后缀被拒绝
TEST_F(DriverResolveTest, T15_ExeSuffixRejected) {
    createFakeDriver(m_tmpDir.path() + "/app", "stdio.drv.calc");
    auto r = stdiolink_service::resolveDriverPath("stdio.drv.calc.exe",
        "", m_tmpDir.path() + "/app");
    EXPECT_TRUE(r.path.isEmpty());
}

#ifndef Q_OS_WIN
// T16 — Unix 下文件无执行权限被跳过
TEST_F(DriverResolveTest, T16_NonExecutableSkipped) {
    QString appDir = m_tmpDir.path() + "/app";
    QDir().mkpath(appDir);
    QFile f(appDir + "/stdio.drv.noexec");
    f.open(QIODevice::WriteOnly);
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);  // 0644, 无执行位
    auto r = stdiolink_service::resolveDriverPath("stdio.drv.noexec", "", appDir);
    EXPECT_TRUE(r.path.isEmpty());
}
#endif
```

```cpp
// test_driver_resolve.cpp — JS 层验证（需 JS 引擎 fixture）

// T17 — JS resolveDriver() driverName 含路径分隔符抛出 TypeError
// 输入: resolveDriver("../etc/passwd")
// 预期: JS 异常类型为 TypeError，信息含 "path separators"

// T18 — JS resolveDriver() driverName 带 .exe 后缀抛出 TypeError
// 输入: resolveDriver("stdio.drv.calc.exe")
// 预期: JS 异常类型为 TypeError，信息含 ".exe"
// 注: T17/T18 与 T11-T13 共用 JS 引擎 fixture，具体实现参照 T13 模式
```

### 6.2 集成测试

- 启动 `stdiolink_server --data-root <path>`，通过 REST API 触发一个使用 `findDriverPath()` 的 Project（如 `manual_driver_pipeline`）
- 验证 Instance 正常启动，JS 层可通过 `APP_PATHS.dataRoot` 读取到正确路径
- 验证 `resolveDriver()` 在 JS 层可正常调用并返回驱动绝对路径（M87 迁移后验证）

### 6.3 验收标准

- [ ] `ServiceArgs::parse()` 正确解析 `--data-root=<path>`（T01）
- [ ] `--data-root` 缺失时 `dataRoot` 为空（T02、T03）
- [ ] `APP_PATHS.dataRoot` 在 JS 层正确暴露（T04、T05）
- [ ] `resolveDriverPath()` 在 dataRoot/drivers 下命中（T06）
- [ ] `resolveDriverPath()` 回退到 appDir（T07）
- [ ] `resolveDriverPath()` 回退到 CWD（T08）
- [ ] 三级均未命中时返回空路径 + 已尝试位置列表（T09）
- [ ] dataRoot 为空时跳过第 1 级（T10）
- [ ] JS `resolveDriver()` 正常命中返回路径（T11）
- [ ] JS `resolveDriver()` 未命中抛出含诊断信息的 Error（T12）
- [ ] JS `resolveDriver()` 空参数抛出 TypeError（T13）
- [ ] driverName 含路径分隔符被拒绝（T14）
- [ ] driverName 带 .exe 后缀被拒绝（T15）
- [ ] Unix 下文件无执行权限被跳过（T16，仅 Unix）
- [ ] JS `resolveDriver()` driverName 含路径分隔符抛出 TypeError（T17）
- [ ] JS `resolveDriver()` driverName 带 .exe 后缀抛出 TypeError（T18）
- [ ] 相对 `--data-root` 输入规范化为绝对路径（T19）
- [ ] `InstanceManager` 启动子进程时传递 `--data-root`（集成测试）
- [ ] 既有测试无回归：`stdiolink_tests` 全部通过

## 7. 风险与控制

- 风险: `resolveDriverPath()` 的 CWD 回退可能在不同启动方式下命中意外文件（如用户 HOME 目录下恰好有同名文件）
  - 控制: CWD 为第三优先级，仅在 dataRoot 和 appDir 均未命中时才使用；生产环境通过 Server 启动时 dataRoot 始终有效
  - 控制: 错误信息包含完整搜索路径列表，便于排查意外命中
  - 测试覆盖: T08（CWD 命中）、T09（全部未命中）

- 风险: Windows 与 Unix 平台后缀差异（`.exe` vs 无后缀）导致跨平台行为不一致
  - 控制: `execName()` 通过 `Q_OS_WIN` 编译期宏统一处理，不依赖运行时判断
  - 测试覆盖: T06–T08（使用 `ext()` helper 自动适配平台）

- 风险: `InstanceManager` 传递的 `--data-root` 路径含空格或特殊字符
  - 控制: 使用 `QProcess::setArguments(QStringList)` 而非拼接命令行字符串，Qt 自动处理引号转义
  - 测试覆盖: 集成测试

- 风险: driverName 含路径分隔符可能导致路径穿越，或已带 `.exe` 后缀导致 Windows 下双重拼接
  - 控制: `isValidDriverName()` 在解析前校验，拒绝含 `/`、`\` 或以 `.exe` 结尾的名称
  - 测试覆盖: T14、T15

- 风险: Unix 下 `resolveDriverPath()` 返回无执行权限的文件，导致后续 spawn 失败
  - 控制: `isDriverCandidate()` 在 Unix 下额外检查 `QFileInfo::isExecutable()`
  - 测试覆盖: T16

## 8. 里程碑完成定义（DoD）

- [ ] `ServiceArgs::parse()` 支持 `--data-root=<path>` 参数
- [ ] `PathContext` 新增 `dataRoot` 字段，`APP_PATHS.dataRoot` 在 JS 层可读
- [ ] `resolveDriverPath()` C++ 实现三级优先级解析，返回绝对路径 + 已尝试位置
- [ ] `stdiolink/driver` JS 模块导出 `resolveDriver()` 函数
- [ ] `InstanceManager` 启动子进程时传递 `--data-root`
- [ ] 新增单元测试全部通过：T01–T19
- [ ] 既有测试无回归：`stdiolink_tests` 全部通过
- [ ] 向后兼容：不传 `--data-root` 时行为与改动前一致
- [ ] driverName 边界条件校验：路径分隔符拒绝、.exe 后缀拒绝、Unix 可执行权限检查
