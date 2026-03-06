# 里程碑 27：集成测试与完善

## 1. 目标

端到端验证所有 JS Runtime 功能，编写完整的集成测试脚本和示例脚本，确保各模块协同工作正确。

## 2. 技术要点

### 2.1 端到端测试覆盖

- 底层 API（Driver/Task）完整流程
- Proxy 代理调用完整流程
- 多 Driver 并发调用
- 进程调用绑定
- ES Module 跨文件导入
- 错误场景覆盖

### 2.2 测试策略

- 使用 `calculator_driver` 和 `device_simulator_driver` 作为测试 Driver
- 通过 QProcess 启动 `stdiolink_service` 执行测试脚本
- 验证退出码和 stderr 输出内容

## 3. 实现步骤

### 3.1 示例脚本

```
examples/
├── basic_usage.js        # 底层 API 使用示例
├── proxy_usage.js        # Proxy 代理调用示例
├── multi_driver.js       # 多 Driver 协同示例
└── process_exec.js       # 进程调用示例
```

### 3.2 basic_usage.js

> **注意**：示例中 `./calculator_driver` 为 Linux/macOS 路径，Windows 下需改为 `./calculator_driver.exe`。

```js
import { Driver } from "stdiolink";

const d = new Driver();
// Windows: "./calculator_driver.exe"
d.start("./calculator_driver");
const task = d.request("add", { a: 10, b: 20 });
const msg = task.waitNext();
console.log("status:", msg.status);
console.log("result:", msg.data);
d.terminate();
```

### 3.3 proxy_usage.js

```js
import { openDriver } from "stdiolink";

// Windows: "./calculator_driver.exe"
const calc = await openDriver("./calculator_driver");
const r = await calc.add({ a: 5, b: 3 });
console.log("add result:", r);
console.log("meta:", calc.$meta.info.name);
calc.$close();
```

### 3.4 multi_driver.js

```js
import { openDriver } from "stdiolink";

// Windows: "./calculator_driver.exe"
const drvA = await openDriver("./calculator_driver");
const drvB = await openDriver("./calculator_driver");

const [a, b] = await Promise.all([
  drvA.add({ a: 10, b: 20 }),
  drvB.add({ a: 30, b: 40 })
]);

console.log("A:", a, "B:", b);
await Promise.all([drvA.$close(), drvB.$close()]);
```

### 3.5 process_exec.js

```js
import { exec } from "stdiolink";

const r = exec("echo", ["hello from exec"]);
console.log("exitCode:", r.exitCode);
console.log("stdout:", r.stdout);
```

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `examples/basic_usage.js` | 底层 API 使用示例 |
| `examples/proxy_usage.js` | Proxy 代理调用示例 |
| `examples/multi_driver.js` | 多 Driver 协同示例 |
| `examples/process_exec.js` | 进程调用示例 |
| `src/tests/test_js_integration.cpp` | 集成测试 |

## 5. 验收标准

1. `stdiolink_service examples/basic_usage.js` 正常执行，退出码 0
2. `stdiolink_service examples/proxy_usage.js` 正常执行，退出码 0
3. `stdiolink_service examples/multi_driver.js` 正常执行，退出码 0
4. `stdiolink_service examples/process_exec.js` 正常执行，退出码 0
5. Driver 启动失败场景正确报错，退出码 1
6. 命令执行超时场景正确报错
7. 无效参数传递场景正确报错
8. 模块加载失败场景正确报错
9. 多 Driver 并发调用无串扰
10. 所有示例脚本可作为用户文档参考

## 6. 单元测试用例

### 6.1 集成测试框架

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryFile>
#include <QTextStream>

class JsIntegrationTest : public ::testing::Test {
protected:
    QString servicePath() {
        QString path = QCoreApplication::applicationDirPath()
                       + "/stdiolink_service";
#ifdef Q_OS_WIN
        path += ".exe";
#endif
        return path;
    }

    QString driverPath() {
        QString path = QCoreApplication::applicationDirPath()
                       + "/calculator_driver";
#ifdef Q_OS_WIN
        path += ".exe";
#endif
        return path;
    }

    struct RunResult {
        int exitCode;
        QString stdoutStr;
        QString stderrStr;
    };

    RunResult runScript(const QString& scriptPath) {
        QProcess proc;
        proc.start(servicePath(), {scriptPath});
        proc.waitForFinished(30000);
        return {
            proc.exitCode(),
            QString::fromUtf8(proc.readAllStandardOutput()),
            QString::fromUtf8(proc.readAllStandardError())
        };
    }

    QString createScript(const QString& code) {
        auto* f = new QTemporaryFile("XXXXXX.mjs");
        f->setAutoRemove(true);
        f->open();
        QTextStream out(f);
        out << code;
        out.flush();
        tempFiles.append(f);
        return f->fileName();
    }

    QList<QTemporaryFile*> tempFiles;
};
```

### 6.2 底层 API 端到端测试

```cpp
TEST_F(JsIntegrationTest, BasicDriverUsage) {
    QString path = createScript(QString(
        "import { Driver } from 'stdiolink';\n"
        "const d = new Driver();\n"
        "d.start('%1');\n"
        "const task = d.request('add', { a: 10, b: 20 });\n"
        "const msg = task.waitNext(5000);\n"
        "if (!msg || msg.status !== 'done')\n"
        "    throw new Error('unexpected status');\n"
        "console.log('result:', JSON.stringify(msg.data));\n"
        "d.terminate();\n"
    ).arg(driverPath()));

    auto r = runScript(path);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("result:"));
}
```

### 6.3 Proxy 代理端到端测试

```cpp
TEST_F(JsIntegrationTest, ProxyDriverUsage) {
    QString path = createScript(QString(
        "import { openDriver } from 'stdiolink';\n"
        "const calc = await openDriver('%1');\n"
        "const r = await calc.add({ a: 5, b: 3 });\n"
        "console.log('proxy result:', JSON.stringify(r));\n"
        "calc.$close();\n"
    ).arg(driverPath()));

    auto r = runScript(path);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("proxy result:"));
}
```

### 6.4 进程调用端到端测试

```cpp
TEST_F(JsIntegrationTest, ProcessExec) {
    QString path = createScript(
#ifdef Q_OS_WIN
        "import { exec } from 'stdiolink';\n"
        "const r = exec('cmd', ['/c', 'echo', 'hello']);\n"
#else
        "import { exec } from 'stdiolink';\n"
        "const r = exec('echo', ['hello']);\n"
#endif
        "console.log('exit:', r.exitCode);\n"
        "console.log('out:', r.stdout);\n"
    );

    auto r = runScript(path);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("exit: 0"));
}
```

### 6.5 错误场景测试

```cpp
TEST_F(JsIntegrationTest, DriverStartFailure) {
    QString path = createScript(
        "import { openDriver } from 'stdiolink';\n"
        "await openDriver('__nonexistent__');\n"
    );
    auto r = runScript(path);
    EXPECT_EQ(r.exitCode, 1);
}
```

### 6.6 模块加载失败测试

```cpp
TEST_F(JsIntegrationTest, ModuleNotFound) {
    QString path = createScript(
        "import { foo } from './nonexistent_module.js';\n"
    );
    auto r = runScript(path);
    EXPECT_EQ(r.exitCode, 1);
}
```

### 6.7 JS 异常退出测试

```cpp
TEST_F(JsIntegrationTest, UncaughtException) {
    QString path = createScript(
        "throw new Error('test uncaught');\n"
    );
    auto r = runScript(path);
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_TRUE(r.stderrStr.contains("test uncaught"));
}
```

### 6.8 console 输出不污染 stdout 测试

```cpp
TEST_F(JsIntegrationTest, StdoutClean) {
    QString path = createScript(
        "console.log('debug info');\n"
        "console.warn('warning info');\n"
    );
    auto r = runScript(path);
    EXPECT_EQ(r.exitCode, 0);
    // stdout 应为空（console 输出到 stderr）
    EXPECT_TRUE(r.stdoutStr.isEmpty());
    EXPECT_TRUE(r.stderrStr.contains("debug info"));
}
```

### 6.9 ES Module 跨文件导入测试

```cpp
TEST_F(JsIntegrationTest, CrossFileImport) {
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    // 创建库文件
    QFile libFile(tmpDir.path() + "/lib.js");
    libFile.open(QIODevice::WriteOnly);
    libFile.write("export function add(a, b) { return a + b; }\n");
    libFile.close();

    // 创建主文件
    QFile mainFile(tmpDir.path() + "/main.js");
    mainFile.open(QIODevice::WriteOnly);
    mainFile.write(
        "import { add } from './lib.js';\n"
        "console.log('sum:', add(3, 4));\n"
    );
    mainFile.close();

    auto r = runScript(tmpDir.path() + "/main.js");
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stderrStr.contains("sum: 7"));
}
```

## 7. 依赖关系

- **前置依赖**：
  - 里程碑 21（JS 引擎脚手架）
  - 里程碑 22（ES Module 加载器）
  - 里程碑 23（Driver/Task 绑定）
  - 里程碑 24（进程调用绑定）
  - 里程碑 25（Proxy 代理调用与并发调度）
  - 里程碑 26（TypeScript 声明文件生成）
- **后续依赖**：
  - 无（本里程碑为 JS Runtime 系列的终点）
