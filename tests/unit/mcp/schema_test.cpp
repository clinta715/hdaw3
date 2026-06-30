#include <gtest/gtest.h>
#include <QJsonArray>
#include "mcp/McpSchema.h"
using namespace mcp;

TEST(Schema, AcceptsMatchingObject) {
    QJsonObject s{{"type", "object"},
                  {"required", QJsonArray{"x"}},
                  {"properties", QJsonObject{{"x", QJsonObject{{"type", "integer"}}}}}};
    EXPECT_FALSE(validateSchema(QJsonObject{{"x", 1}}, s));
}

TEST(Schema, RejectsMissingRequired) {
    QJsonObject s{{"type", "object"}, {"required", QJsonArray{"x"}}};
    auto err = validateSchema(QJsonObject{}, s);
    ASSERT_TRUE(err);
    EXPECT_EQ(err->path, "x");
}

TEST(Schema, RejectsWrongType) {
    auto err = validateSchema(QJsonValue{"not int"},
                              QJsonObject{{"type", "integer"}});
    ASSERT_TRUE(err);
    EXPECT_EQ(err->message, "expected integer");
}

TEST(Schema, RejectsOutOfRange) {
    QJsonObject s{{"type", "integer"}, {"minimum", 0}, {"maximum", 127}};
    auto highErr = validateSchema(200, s);
    ASSERT_TRUE(highErr);
    EXPECT_TRUE(highErr->message.contains("above maximum"));
    auto err = validateSchema(-1, s);
    ASSERT_TRUE(err);
    EXPECT_TRUE(err->message.contains("below minimum"));
}

TEST(Schema, RejectsBadEnum) {
    auto err = validateSchema(QString("rewind"),
                              QJsonObject{{"enum", QJsonArray{"play", "stop"}}});
    ASSERT_TRUE(err);
}

TEST(Schema, ValidatesNestedArray) {
    QJsonObject s{{"type", "array"},
                  {"items", QJsonObject{{"type", "integer"},
                                        {"minimum", 0},
                                        {"maximum", 127}}}};
    EXPECT_FALSE(validateSchema(QJsonArray{60, 64, 67}, s));
    auto err = validateSchema(QJsonArray{60, 200}, s);
    ASSERT_TRUE(err);
    EXPECT_EQ(err->path, "[1]");
}

TEST(Schema, RejectsUnknownPropertyWhenAdditionalFalse) {
    QJsonObject s{{"type", "object"},
                  {"additionalProperties", false},
                  {"properties", QJsonObject{{"a", QJsonObject{{"type", "integer"}}}}}};
    auto err = validateSchema(QJsonObject{{"a", 1}, {"b", 2}}, s);
    ASSERT_TRUE(err);
    EXPECT_EQ(err->path, "b");
}
