#include <gtest/gtest.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstring>
#include <quickjs.h>
#include "bindings/js_stdiolink_module.h"
#include "bindings/js_task_scheduler.h"
#include "engine/console_bridge.h"
#include "engine/js_engine.h"
#include "utils/js_convert.h"

namespace {

QString writeScript(const QTemporaryDir& dir, const QString& relativePath, const QString& content) {
    const QString fullPath = dir.path() + "/" + relativePath;
    QFileInfo fi(fullPath);
    fi.absoluteDir().mkpath(".");
    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream out(&file);
    out << content;
    out.flush();
    return fullPath;
}

int readGlobalInt(JSContext* ctx, const char* key) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue val = JS_GetPropertyStr(ctx, global, key);
    int32_t out = 0;
    JS_ToInt32(ctx, &out, val);
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, global);
    return out;
}

JSMemoryUsage computeMemory(JSRuntime* rt) {
    JS_RunGC(rt);
    JSMemoryUsage usage;
    memset(&usage, 0, sizeof(usage));
    JS_ComputeMemoryUsage(rt, &usage);
    return usage;
}

} // namespace

// ---------------------------------------------------------------------------
// Fixture A: basic engine + console bridge
// ---------------------------------------------------------------------------
class JsStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        ASSERT_NE(m_engine->runtime(), nullptr);
        ConsoleBridge::install(m_engine->context());
    }

    int evalAndDrain(const QString& scriptPath) {
        const int ret = m_engine->evalFile(scriptPath);
        while (m_engine->hasPendingJobs()) {
            m_engine->executePendingJobs();
        }
        return ret;
    }

    JSMemoryUsage snapshotMemory() { return computeMemory(m_engine->runtime()); }

    std::unique_ptr<JsEngine> m_engine;
    QTemporaryDir m_tmpDir;
};

// ---------------------------------------------------------------------------
// Fixture B: engine + scheduler + stdiolink module
// ---------------------------------------------------------------------------
class JsStressSchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        m_engine = std::make_unique<JsEngine>();
        ASSERT_NE(m_engine->context(), nullptr);
        m_scheduler = std::make_unique<JsTaskScheduler>(m_engine->context());
        ConsoleBridge::install(m_engine->context());
        m_engine->registerModule("stdiolink", jsInitStdiolinkModule);
        JsTaskScheduler::installGlobal(m_engine->context(), m_scheduler.get());
    }

    int runScript(const QString& path) {
        int ret = m_engine->evalFile(path);
        while (m_scheduler->hasPending() || m_engine->hasPendingJobs()) {
            if (m_scheduler->hasPending()) {
                m_scheduler->poll(50);
            }
            while (m_engine->hasPendingJobs()) {
                m_engine->executePendingJobs();
            }
        }
        if (ret == 0 && m_engine->hadJobError()) {
            ret = 1;
        }
        return ret;
    }

    JSMemoryUsage snapshotMemory() { return computeMemory(m_engine->runtime()); }

    std::unique_ptr<JsEngine> m_engine;
    std::unique_ptr<JsTaskScheduler> m_scheduler;
    QTemporaryDir m_tmpDir;
};

// ===========================================================================
// Category 1: Memory Leak Detection
// ===========================================================================

TEST(JsStressMemoryTest, RepeatedEngineCreateDestroy) {
    int64_t baselineMalloc = 0;
    {
        JsEngine engine;
        ASSERT_NE(engine.runtime(), nullptr);
        JSMemoryUsage usage = computeMemory(engine.runtime());
        baselineMalloc = usage.malloc_size;
    }

    for (int i = 0; i < 100; ++i) {
        auto engine = std::make_unique<JsEngine>();
        ASSERT_NE(engine->runtime(), nullptr);
    }

    // The last fresh engine should have memory comparable to baseline
    {
        JsEngine engine;
        ASSERT_NE(engine.runtime(), nullptr);
        JSMemoryUsage usage = computeMemory(engine.runtime());
        EXPECT_LE(usage.malloc_size, baselineMalloc * 2)
            << "Engine memory after 100 create/destroy cycles should stay bounded";
    }
}

TEST_F(JsStressTest, RepeatedScriptEvaluation) {
    // Warmup
    const QString warmup = writeScript(m_tmpDir, "warmup.js", "globalThis._w = 1;\n");
    ASSERT_EQ(evalAndDrain(warmup), 0);

    const JSMemoryUsage before = snapshotMemory();

    constexpr int N = 50;
    for (int i = 0; i < N; ++i) {
        const QString path =
            writeScript(m_tmpDir, QString("iter_%1.js").arg(i),
                        "globalThis._tmp = { a: 1, b: 'hello', c: [1,2,3] };\n");
        ASSERT_FALSE(path.isEmpty());
        ASSERT_EQ(evalAndDrain(path), 0);
    }

    const JSMemoryUsage after = snapshotMemory();

    // Module cache grows per eval, but per-iteration overhead should be bounded
    const int64_t objGrowthPerIter = (after.obj_count - before.obj_count) / N;
    EXPECT_LE(objGrowthPerIter, 5) << "obj_count growth per eval should be bounded by module overhead";
    const int64_t strGrowthPerIter = (after.str_count - before.str_count) / N;
    EXPECT_LE(strGrowthPerIter, 5) << "str_count growth per eval should be bounded";
}

TEST_F(JsStressTest, ObjectCreationAndGcCycles) {
    // Warmup
    const QString warmup = writeScript(m_tmpDir, "gc_warmup.js", "globalThis._w = 1;\n");
    ASSERT_EQ(evalAndDrain(warmup), 0);

    const JSMemoryUsage before = snapshotMemory();

    const QString script = writeScript(m_tmpDir, "gc_stress.js",
                                       "let arr = [];\n"
                                       "for (let i = 0; i < 10000; i++) {\n"
                                       "    arr.push({ x: i, y: String(i), z: [i, i+1] });\n"
                                       "}\n"
                                       "arr = null;\n"
                                       "globalThis._gcDone = 1;\n");
    ASSERT_FALSE(script.isEmpty());
    ASSERT_EQ(evalAndDrain(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_gcDone"), 1);

    const JSMemoryUsage after = snapshotMemory();

    // After nullifying the array and GC, objects should be reclaimed.
    // Allow small delta for module cache entry of gc_stress.js itself.
    EXPECT_LE(after.obj_count - before.obj_count, 10)
        << "Objects should be reclaimed after nullifying references and GC";
    EXPECT_LE(after.array_count - before.array_count, 2)
        << "Arrays should be reclaimed after GC";
}

TEST_F(JsStressTest, PromiseCreationAndResolutionCycles) {
    const QString warmup = writeScript(m_tmpDir, "promise_warmup.js",
                                       "Promise.resolve(1).then(v => { globalThis._pw = v; });\n");
    ASSERT_EQ(evalAndDrain(warmup), 0);

    const JSMemoryUsage before = snapshotMemory();

    const QString script = writeScript(m_tmpDir, "promise_stress.js",
                                       "globalThis._promiseDone = 0;\n"
                                       "let p = Promise.resolve(0);\n"
                                       "for (let i = 0; i < 1000; i++) {\n"
                                       "    p = p.then(v => v + 1);\n"
                                       "}\n"
                                       "p.then(v => { globalThis._promiseDone = v; });\n");
    ASSERT_FALSE(script.isEmpty());
    ASSERT_EQ(evalAndDrain(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_promiseDone"), 1000);

    const JSMemoryUsage after = snapshotMemory();

    // All 1000 promises should be resolved and GC'd
    EXPECT_LE(after.obj_count - before.obj_count, 15)
        << "Resolved promises should be reclaimed by GC";
}

TEST_F(JsStressTest, ConsoleBridgeRepeatedCalls) {
    const QString warmup = writeScript(m_tmpDir, "console_warmup.js", "console.log('warmup');\n");
    ASSERT_EQ(evalAndDrain(warmup), 0);

    const JSMemoryUsage before = snapshotMemory();

    // Suppress the 5000 lines of qDebug output during this test
    QLoggingCategory::setFilterRules("default.debug=false");

    const QString script = writeScript(m_tmpDir, "console_stress.js",
                                       "for (let i = 0; i < 5000; i++) {\n"
                                       "    console.log('iter', i, { x: i });\n"
                                       "}\n"
                                       "globalThis._consoleDone = 1;\n");
    ASSERT_FALSE(script.isEmpty());
    ASSERT_EQ(evalAndDrain(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_consoleDone"), 1);

    QLoggingCategory::setFilterRules("");

    const JSMemoryUsage after = snapshotMemory();

    EXPECT_LE(after.obj_count - before.obj_count, 15)
        << "console.log should not leak JSValue references";
}

TEST_F(JsStressTest, JsConvertRoundTripNoLeak) {
    const QJsonObject testObj{{"name", "stress"},
                              {"count", 42},
                              {"active", true},
                              {"tags", QJsonArray{"a", "b", "c"}},
                              {"nested", QJsonObject{{"x", 1.5}, {"y", "hello"}}}};

    // Warmup
    for (int i = 0; i < 10; ++i) {
        JSValue js = qjsonObjectToJsValue(m_engine->context(), testObj);
        JS_FreeValue(m_engine->context(), js);
    }

    const JSMemoryUsage before = snapshotMemory();

    for (int i = 0; i < 1000; ++i) {
        JSValue js = qjsonObjectToJsValue(m_engine->context(), testObj);
        QJsonObject back = jsValueToQJsonObject(m_engine->context(), js);
        JS_FreeValue(m_engine->context(), js);
        Q_UNUSED(back);
    }

    const JSMemoryUsage after = snapshotMemory();

    EXPECT_LE(after.obj_count - before.obj_count, 2)
        << "qjson<->JSValue round-trip should not leak objects";
    EXPECT_LE(after.malloc_size, before.malloc_size + 4096)
        << "malloc_size should not grow significantly after round-trips";
}

TEST_F(JsStressTest, RepeatedModuleImportNoLeak) {
    // Create a shared library module
    const QString libPath =
        writeScript(m_tmpDir, "lib/counter.js", "let n = 0;\nexport function inc() { return ++n; }\n");
    ASSERT_FALSE(libPath.isEmpty());

    // Warmup: first import initializes the module
    const QString warmup =
        writeScript(m_tmpDir, "mod_warmup.js", "import { inc } from './lib/counter.js';\ninc();\n");
    ASSERT_EQ(evalAndDrain(warmup), 0);

    const JSMemoryUsage before = snapshotMemory();

    constexpr int N = 20;
    for (int i = 0; i < N; ++i) {
        const QString path = writeScript(
            m_tmpDir, QString("mod_iter_%1.js").arg(i),
            "import { inc } from './lib/counter.js';\nglobalThis._modVal = inc();\n");
        ASSERT_FALSE(path.isEmpty());
        ASSERT_EQ(evalAndDrain(path), 0);
    }

    const JSMemoryUsage after = snapshotMemory();

    // The shared module should be loaded once; only importer modules add overhead
    const int64_t objGrowthPerIter = (after.obj_count - before.obj_count) / N;
    EXPECT_LE(objGrowthPerIter, 5) << "Importing the same module should not duplicate state";
    // Counter should have been incremented N+1 times (warmup + N)
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_modVal"), N + 1);
}

// ===========================================================================
// Category 2: Stress Tests
// ===========================================================================

TEST_F(JsStressTest, LargeObjectCreation) {
    const QString script = writeScript(m_tmpDir, "large_obj.js",
                                       "const big = {};\n"
                                       "for (let i = 0; i < 10000; i++) {\n"
                                       "    big['key_' + i] = i;\n"
                                       "}\n"
                                       "globalThis._bigSize = Object.keys(big).length;\n");
    ASSERT_FALSE(script.isEmpty());
    ASSERT_EQ(evalAndDrain(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_bigSize"), 10000);
}

TEST_F(JsStressTest, LargeArrayCreation) {
    const QString script = writeScript(m_tmpDir, "large_arr.js",
                                       "const arr = new Array(100000);\n"
                                       "for (let i = 0; i < arr.length; i++) arr[i] = i;\n"
                                       "globalThis._arrLen = arr.length;\n");
    ASSERT_FALSE(script.isEmpty());
    ASSERT_EQ(evalAndDrain(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_arrLen"), 100000);
}

TEST_F(JsStressTest, DeepRecursionHitsStackLimit) {
    // Use a reduced stack limit so QuickJS catches the overflow
    // before the C stack is exhausted.
    JS_SetMaxStackSize(m_engine->runtime(), 256ull * 1024ull);

    const QString script = writeScript(m_tmpDir, "deep_recurse.js",
                                       "function recurse(n) { return recurse(n + 1); }\n"
                                       "try {\n"
                                       "    recurse(0);\n"
                                       "    globalThis._overflow = 0;\n"
                                       "} catch (e) {\n"
                                       "    globalThis._overflow = 1;\n"
                                       "}\n");
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(evalAndDrain(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_overflow"), 1);
}

TEST_F(JsStressTest, MemoryLimitEnforcement) {
    const QString script = writeScript(m_tmpDir, "oom.js",
                                       "try {\n"
                                       "    const arrays = [];\n"
                                       "    for (let i = 0; i < 100000; i++) {\n"
                                       "        arrays.push(new Array(10000).fill(i));\n"
                                       "    }\n"
                                       "    globalThis._oom = 0;\n"
                                       "} catch (e) {\n"
                                       "    globalThis._oom = 1;\n"
                                       "}\n");
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(evalAndDrain(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_oom"), 1);
}

TEST(JsStressStandaloneTest, RapidEngineCreateDestroy) {
    for (int i = 0; i < 500; ++i) {
        auto engine = std::make_unique<JsEngine>();
        ASSERT_NE(engine->runtime(), nullptr);
    }

    // Verify the last engine is fully functional
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    auto engine = std::make_unique<JsEngine>();
    ASSERT_NE(engine->context(), nullptr);

    const QString path = writeScript(tmpDir, "final.js", "globalThis._rapid = 42;\n");
    ASSERT_FALSE(path.isEmpty());
    EXPECT_EQ(engine->evalFile(path), 0);
    EXPECT_EQ(readGlobalInt(engine->context(), "_rapid"), 42);
}

TEST_F(JsStressTest, ManyConcurrentPromiseChains) {
    const QString script = writeScript(
        m_tmpDir, "many_promises.js",
        "const chains = [];\n"
        "for (let c = 0; c < 100; c++) {\n"
        "    let p = Promise.resolve(0);\n"
        "    for (let i = 0; i < 50; i++) {\n"
        "        p = p.then(v => v + 1);\n"
        "    }\n"
        "    chains.push(p);\n"
        "}\n"
        "Promise.all(chains).then(results => {\n"
        "    let sum = 0;\n"
        "    for (const v of results) sum += v;\n"
        "    globalThis._chainResult = sum;\n"
        "});\n");
    ASSERT_FALSE(script.isEmpty());
    ASSERT_EQ(evalAndDrain(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_chainResult"), 5000);
}

TEST_F(JsStressTest, LargeStringHandling) {
    const QString script = writeScript(m_tmpDir, "large_str.js",
                                       "let s = 'x';\n"
                                       "for (let i = 0; i < 20; i++) {\n"
                                       "    s = s + s;\n"
                                       "}\n"
                                       "globalThis._strLen = s.length;\n");
    ASSERT_FALSE(script.isEmpty());
    ASSERT_EQ(evalAndDrain(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_strLen"), 1048576);
}

// ===========================================================================
// Category 3: Edge Cases
// ===========================================================================

TEST_F(JsStressSchedulerTest, EngineDestructionWithPendingJobs) {
    const QString script =
        writeScript(m_tmpDir, "pending.js",
                    "Promise.resolve(1).then(v => v + 1).then(v => v + 1);\n");
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(m_engine->evalFile(script), 0);
    EXPECT_TRUE(m_engine->hasPendingJobs());
    // Destroy without draining -- should not crash
    m_scheduler.reset();
    m_engine.reset();
}

TEST_F(JsStressSchedulerTest, EngineDestructionWithUnresolvedPromises) {
    const QString script =
        writeScript(m_tmpDir, "unresolved.js",
                    "globalThis._p = new Promise(() => {});\n"
                    "globalThis._ok = 1;\n");
    ASSERT_FALSE(script.isEmpty());
    EXPECT_EQ(m_engine->evalFile(script), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_ok"), 1);
    // Destroy with unresolved promise -- should not crash
    m_scheduler.reset();
    m_engine.reset();
}

TEST_F(JsStressTest, EvalAfterSyntaxErrorRecovery) {
    QLoggingCategory::setFilterRules("default.critical=false");

    const QString bad = writeScript(m_tmpDir, "bad.js", "let = ;\n");
    ASSERT_FALSE(bad.isEmpty());
    EXPECT_EQ(evalAndDrain(bad), 1);

    QLoggingCategory::setFilterRules("");

    const QString good = writeScript(m_tmpDir, "good.js", "globalThis._recovered = 42;\n");
    ASSERT_FALSE(good.isEmpty());
    EXPECT_EQ(evalAndDrain(good), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_recovered"), 42);
}

TEST_F(JsStressTest, MultipleSequentialEvalsAccumulateState) {
    const QString init = writeScript(m_tmpDir, "init.js", "globalThis._counter = 0;\n");
    ASSERT_EQ(evalAndDrain(init), 0);

    for (int i = 0; i < 10; ++i) {
        const QString script =
            writeScript(m_tmpDir, QString("inc_%1.js").arg(i), "globalThis._counter += 1;\n");
        ASSERT_FALSE(script.isEmpty());
        ASSERT_EQ(evalAndDrain(script), 0);
    }

    EXPECT_EQ(readGlobalInt(m_engine->context(), "_counter"), 10);
}

TEST_F(JsStressTest, EvalAfterRuntimeErrorRecovery) {
    const QString bad = writeScript(m_tmpDir, "runtime_err.js",
                                    "try { undeclaredVar.foo(); } catch(e) {}\n"
                                    "globalThis._rtErr = 1;\n");
    ASSERT_FALSE(bad.isEmpty());
    EXPECT_EQ(evalAndDrain(bad), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_rtErr"), 1);

    const QString good = writeScript(m_tmpDir, "after_rt_err.js", "globalThis._rtOk = 99;\n");
    ASSERT_FALSE(good.isEmpty());
    EXPECT_EQ(evalAndDrain(good), 0);
    EXPECT_EQ(readGlobalInt(m_engine->context(), "_rtOk"), 99);
}
