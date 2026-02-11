#include <gtest/gtest.h>

#include "stdiolink_server/config/server_args.h"

using namespace stdiolink_server;

TEST(ServerArgsTest, DefaultValues) {
    const auto args = ServerArgs::parse({"stdiolink_server"});
    EXPECT_EQ(args.dataRoot, ".");
    EXPECT_EQ(args.port, 8080);
    EXPECT_EQ(args.host, "127.0.0.1");
    EXPECT_EQ(args.logLevel, "info");
    EXPECT_TRUE(args.error.isEmpty());
    EXPECT_FALSE(args.hasPort);
    EXPECT_FALSE(args.hasHost);
    EXPECT_FALSE(args.hasLogLevel);
}

TEST(ServerArgsTest, AllOptions) {
    const auto args = ServerArgs::parse({
        "stdiolink_server",
        "--data-root=/tmp/data",
        "--port=9090",
        "--host=0.0.0.0",
        "--log-level=debug"
    });

    EXPECT_EQ(args.dataRoot, "/tmp/data");
    EXPECT_EQ(args.port, 9090);
    EXPECT_EQ(args.host, "0.0.0.0");
    EXPECT_EQ(args.logLevel, "debug");
    EXPECT_TRUE(args.hasPort);
    EXPECT_TRUE(args.hasHost);
    EXPECT_TRUE(args.hasLogLevel);
    EXPECT_TRUE(args.error.isEmpty());
}

TEST(ServerArgsTest, InvalidPort) {
    const auto args = ServerArgs::parse({
        "stdiolink_server",
        "--port=70000"
    });
    EXPECT_FALSE(args.error.isEmpty());
}

TEST(ServerArgsTest, InvalidLogLevel) {
    const auto args = ServerArgs::parse({
        "stdiolink_server",
        "--log-level=trace"
    });
    EXPECT_FALSE(args.error.isEmpty());
}

TEST(ServerArgsTest, UnknownOption) {
    const auto args = ServerArgs::parse({
        "stdiolink_server",
        "--unknown=1"
    });
    EXPECT_FALSE(args.error.isEmpty());
}
