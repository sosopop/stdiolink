#include <gtest/gtest.h>

#include "stdiolink_server/runtime/server_runtime_support.h"

using namespace stdiolink_server;

TEST(ServerRuntimeSupportTest, ConsoleUrlUsesLoopbackAndPort) {
    EXPECT_EQ(buildServerConsoleUrl(6200), "http://127.0.0.1:6200");
    EXPECT_EQ(buildServerConsoleUrl(8088), "http://127.0.0.1:8088");
}

TEST(ServerRuntimeSupportTest, SingleInstanceKeyIsStableForSamePath) {
    const QString key1 = buildServerSingleInstanceKey("./tmp/server-data");
    const QString key2 = buildServerSingleInstanceKey("./tmp/../tmp/server-data");

    EXPECT_EQ(key1, key2);
    EXPECT_TRUE(key1.startsWith("stdiolink_server_"));
}

TEST(ServerRuntimeSupportTest, SingleInstanceGuardRejectsSecondAcquire) {
    ServerSingleInstanceGuard guard1("./tmp/server-single-instance-test");
    QString error1;
    ASSERT_TRUE(guard1.tryAcquire(&error1)) << qPrintable(error1);

    ServerSingleInstanceGuard guard2("./tmp/server-single-instance-test");
    QString error2;
    EXPECT_FALSE(guard2.tryAcquire(&error2));
    EXPECT_FALSE(error2.isEmpty());
}
