#include <gtest/gtest.h>
#include "stdiolink/driver/mock_responder.h"
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

// ============================================
// EventMeta 序列化测试
// ============================================

TEST(TypedEvent, EventMetaSerialization) {
    EventMeta event;
    event.name = "progress";
    event.description = "Progress update";

    FieldMeta field;
    field.name = "percent";
    field.type = FieldType::Int;
    event.fields.append(field);

    QJsonObject json = event.toJson();
    EXPECT_EQ(json["name"].toString(), "progress");
    EXPECT_EQ(json["description"].toString(), "Progress update");
    EXPECT_TRUE(json.contains("fields"));
    EXPECT_EQ(json["fields"].toArray().size(), 1);
}

TEST(TypedEvent, EventMetaDeserialization) {
    QJsonObject json{
        {"name", "progress"},
        {"description", "Progress update"},
        {"fields", QJsonArray{
            QJsonObject{{"name", "percent"}, {"type", "int"}}
        }}
    };

    EventMeta event = EventMeta::fromJson(json);
    EXPECT_EQ(event.name, "progress");
    EXPECT_EQ(event.description, "Progress update");
    EXPECT_EQ(event.fields.size(), 1);
    EXPECT_EQ(event.fields[0].name, "percent");
}

// ============================================
// 命令包含事件定义测试
// ============================================

TEST(TypedEvent, CommandWithEvents) {
    CommandMeta cmd;
    cmd.name = "scan";

    EventMeta event;
    event.name = "progress";
    event.description = "Scan progress";
    cmd.events.append(event);

    QJsonObject json = cmd.toJson();
    EXPECT_TRUE(json.contains("events"));
    EXPECT_EQ(json["events"].toArray().size(), 1);
}

TEST(TypedEvent, CommandEventsRoundTrip) {
    CommandMeta cmd;
    cmd.name = "scan";

    EventMeta event1;
    event1.name = "progress";
    cmd.events.append(event1);

    EventMeta event2;
    event2.name = "found";
    cmd.events.append(event2);

    QJsonObject json = cmd.toJson();
    CommandMeta restored = CommandMeta::fromJson(json);

    EXPECT_EQ(restored.events.size(), 2);
    EXPECT_EQ(restored.events[0].name, "progress");
    EXPECT_EQ(restored.events[1].name, "found");
}

// ============================================
// IResponder 扩展测试
// ============================================

TEST(TypedEvent, ResponderEventWithName) {
    MockResponder resp;
    resp.event("progress", 50, QJsonObject{{"message", "Processing"}});

    EXPECT_EQ(resp.lastEventName(), "progress");
    EXPECT_EQ(resp.lastEventCode(), 50);
    EXPECT_EQ(resp.responses.size(), 1);
}

TEST(TypedEvent, ResponderEventLegacy) {
    MockResponder resp;
    resp.event(50, QJsonObject{{"percent", 50}});

    // 旧版接口应自动标记为 default 事件
    EXPECT_EQ(resp.lastEventName(), "default");
    EXPECT_EQ(resp.lastEventCode(), 50);
}

TEST(TypedEvent, ResponderMultipleEvents) {
    MockResponder resp;
    resp.event("start", 0, QJsonObject{});
    resp.event("progress", 50, QJsonObject{{"percent", 50}});
    resp.event("end", 0, QJsonObject{});

    EXPECT_EQ(resp.responses.size(), 3);
    EXPECT_EQ(resp.responses[0].eventName, "start");
    EXPECT_EQ(resp.responses[1].eventName, "progress");
    EXPECT_EQ(resp.responses[2].eventName, "end");
}

TEST(TypedEvent, EventPayloadStructure) {
    MockResponder resp;
    resp.event("progress", 0, QJsonObject{{"percent", 75}});

    auto payload = resp.responses[0].payload.toObject();
    EXPECT_EQ(payload["event"].toString(), "progress");
    EXPECT_TRUE(payload.contains("data"));
    EXPECT_EQ(payload["data"].toObject()["percent"].toInt(), 75);
}
