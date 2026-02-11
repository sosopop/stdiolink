#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include "stdiolink/host/driver.h"
#include "stdiolink/host/wait_any.h"
#include "stdiolink/platform/platform_utils.h"

using namespace stdiolink;

// 测试 Driver 程序路径
static const QString TestDriver =
    PlatformUtils::executablePath(".", "test_driver");

class WaitAnyTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_driverPath = QCoreApplication::applicationDirPath() + "/" + TestDriver;
    }

    QString m_driverPath;
};

// ============================================
// 基础功能测试
// ============================================

TEST_F(WaitAnyTest, SingleTask) {
    Driver d;
    ASSERT_TRUE(d.start(m_driverPath));

    QVector<Task> tasks;
    tasks << d.request("echo", QJsonObject{{"msg", "hello"}});

    AnyItem item;
    bool got = waitAnyNext(tasks, item, 5000);

    EXPECT_TRUE(got);
    EXPECT_EQ(item.taskIndex, 0);
    EXPECT_EQ(item.msg.status, "done");

    d.terminate();
}

TEST_F(WaitAnyTest, EmptyTasks) {
    QVector<Task> tasks;
    AnyItem item;
    EXPECT_FALSE(waitAnyNext(tasks, item, 100));
}

TEST_F(WaitAnyTest, InvalidTasks) {
    QVector<Task> tasks;
    tasks << Task(); // 无效 Task
    tasks << Task();

    AnyItem item;
    EXPECT_FALSE(waitAnyNext(tasks, item, 100));
}

// ============================================
// 多任务测试
// ============================================

TEST_F(WaitAnyTest, MultipleTasks) {
    Driver d1, d2;
    ASSERT_TRUE(d1.start(m_driverPath));
    ASSERT_TRUE(d2.start(m_driverPath));

    QVector<Task> tasks;
    tasks << d1.request("echo", QJsonObject{{"id", 1}});
    tasks << d2.request("echo", QJsonObject{{"id", 2}});

    std::set<int> receivedIndices;
    AnyItem item;

    while (waitAnyNext(tasks, item, 5000)) {
        receivedIndices.insert(item.taskIndex);
    }

    // 应该收到两个任务的响应
    EXPECT_EQ(receivedIndices.size(), 2u);

    d1.terminate();
    d2.terminate();
}

TEST_F(WaitAnyTest, AllDone) {
    Driver d1, d2;
    ASSERT_TRUE(d1.start(m_driverPath));
    ASSERT_TRUE(d2.start(m_driverPath));

    QVector<Task> tasks;
    tasks << d1.request("echo", QJsonObject{});
    tasks << d2.request("echo", QJsonObject{});

    // 消费所有消息
    AnyItem item;
    while (waitAnyNext(tasks, item, 5000)) {
        // 处理消息
    }

    // 再次调用应返回 false
    EXPECT_FALSE(waitAnyNext(tasks, item, 100));

    d1.terminate();
    d2.terminate();
}

// ============================================
// 事件流测试
// ============================================

TEST_F(WaitAnyTest, EventStream) {
    Driver d1, d2;
    ASSERT_TRUE(d1.start(m_driverPath));
    ASSERT_TRUE(d2.start(m_driverPath));

    QVector<Task> tasks;
    tasks << d1.request("progress", QJsonObject{{"steps", 3}});
    tasks << d2.request("progress", QJsonObject{{"steps", 2}});

    std::map<int, int> eventCounts;
    AnyItem item;

    while (waitAnyNext(tasks, item, 5000)) {
        if (item.msg.status == "event") {
            eventCounts[item.taskIndex]++;
        }
    }

    EXPECT_EQ(eventCounts[0], 3);
    EXPECT_EQ(eventCounts[1], 2);

    d1.terminate();
    d2.terminate();
}

TEST_F(WaitAnyTest, DriverExitWithoutTerminalDoesNotHangWaitAny) {
    Driver d;
    ASSERT_TRUE(d.start(m_driverPath));

    QVector<Task> tasks;
    tasks << d.request("exit_now");

    AnyItem item;
    QElapsedTimer timer;
    timer.start();
    const bool got = waitAnyNext(tasks, item, 1000);

    EXPECT_FALSE(got);
    EXPECT_TRUE(tasks[0].isDone());
    EXPECT_EQ(tasks[0].exitCode(), 1001);
    EXPECT_TRUE(tasks[0].errorText().contains("program="));
    EXPECT_LT(timer.elapsed(), 500);
}
