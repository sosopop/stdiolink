# 里程碑 14：完整文档生成器

## 1. 目标

实现元数据到多种文档格式的转换，实现"代码即文档"。
负责 **DocGenerator 的具体生成逻辑**，由 M13 负责 CLI 入口与参数解析。

## 2. 对应需求

- **需求1**: 完整自文档导出能力 (Full Doc-Gen)

## 3. 支持格式

| 格式 | 用途 |
|------|------|
| Markdown | README、Wiki |
| OpenAPI JSON | API 文档工具 |
| HTML | 独立文档页面 |

## 4. 技术设计

```cpp
class DocGenerator {
public:
    static QString toMarkdown(const meta::DriverMeta& meta);
    static QJsonObject toOpenAPI(const meta::DriverMeta& meta);
    static QString toHtml(const meta::DriverMeta& meta);
};
```

OpenAPI 路径映射规则（补充）：
- 命令名 `scan` → `/scan`
- 命令名包含点号 `mesh.union` → `/mesh/union`

## 5. CLI 集成

```bash
./driver.exe --export-doc=markdown > README.md
./driver.exe --export-doc=openapi > api.json
./driver.exe --export-doc=html > doc.html
./driver.exe --export-doc=markdown=README.md
```

默认输出规则：
- 未指定文件路径时输出到 stdout
- 指定路径时覆盖写入
- Markdown 默认文件名建议 `driver.md`
- OpenAPI 默认文件名建议 `openapi.json`
- HTML 默认文件名建议 `doc.html`

错误处理：
- 不支持的格式返回非 0
- 写文件失败返回非 0 并输出错误信息到 stderr

## 6. 单元测试用例

### 6.1 测试文件：tests/test_doc_generator.cpp

```cpp
#include <gtest/gtest.h>
#include "stdiolink/doc/doc_generator.h"  // 建议新增目录: src/stdiolink/doc/
#include "stdiolink/protocol/meta_types.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class DocGeneratorTest : public ::testing::Test {
protected:
    DriverMeta createTestMeta() {
        DriverMeta meta;
        meta.info.id = "test.driver";
        meta.info.name = "Test Driver";
        meta.info.version = "1.0.0";
        meta.info.description = "A test driver";

        CommandMeta cmd;
        cmd.name = "scan";
        cmd.title = "Scan";
        cmd.description = "Execute scan operation";
        meta.commands.append(cmd);

        return meta;
    }
};

// 测试 Markdown 生成
TEST_F(DocGeneratorTest, ToMarkdown) {
    auto meta = createTestMeta();
    QString md = DocGenerator::toMarkdown(meta);

    EXPECT_TRUE(md.contains("# Test Driver"));
    EXPECT_TRUE(md.contains("## Commands"));
    EXPECT_TRUE(md.contains("### scan"));
}

// 测试 Markdown 包含参数说明
TEST_F(DocGeneratorTest, MarkdownContainsParams) {
    auto meta = createTestMeta();
    FieldMeta field;
    field.name = "fps";
    field.type = FieldType::Int;
    field.description = "Frame rate";
    meta.commands[0].params.append(field);

    QString md = DocGenerator::toMarkdown(meta);
    EXPECT_TRUE(md.contains("fps"));
    EXPECT_TRUE(md.contains("Frame rate"));
}
```

### 6.2 OpenAPI 生成测试

```cpp
// 测试 OpenAPI JSON 生成
TEST_F(DocGeneratorTest, ToOpenAPI) {
    auto meta = createTestMeta();
    QJsonObject api = DocGenerator::toOpenAPI(meta);

    EXPECT_TRUE(api.contains("openapi"));
    EXPECT_TRUE(api.contains("info"));
    EXPECT_TRUE(api.contains("paths"));
}

// 测试 OpenAPI 版本信息
TEST_F(DocGeneratorTest, OpenAPIInfo) {
    auto meta = createTestMeta();
    QJsonObject api = DocGenerator::toOpenAPI(meta);
    QJsonObject info = api["info"].toObject();

    EXPECT_EQ(info["title"].toString(), "Test Driver");
    EXPECT_EQ(info["version"].toString(), "1.0.0");
}

// 测试 OpenAPI 路径生成
TEST_F(DocGeneratorTest, OpenAPIPaths) {
    auto meta = createTestMeta();
    QJsonObject api = DocGenerator::toOpenAPI(meta);
    QJsonObject paths = api["paths"].toObject();

    EXPECT_TRUE(paths.contains("/scan"));
}
```

### 6.3 HTML 生成测试

```cpp
// 测试 HTML 生成
TEST_F(DocGeneratorTest, ToHtml) {
    auto meta = createTestMeta();
    QString html = DocGenerator::toHtml(meta);

    EXPECT_TRUE(html.contains("<html"));
    EXPECT_TRUE(html.contains("Test Driver"));
    EXPECT_TRUE(html.contains("</html>"));
}

// 测试 HTML 包含样式
TEST_F(DocGeneratorTest, HtmlContainsStyle) {
    auto meta = createTestMeta();
    QString html = DocGenerator::toHtml(meta);

    EXPECT_TRUE(html.contains("<style>") || html.contains("stylesheet"));
}
```

## 7. 依赖关系

- **前置**: M13 (CLI Help与元数据导出)
- **后续**: 无直接后续
