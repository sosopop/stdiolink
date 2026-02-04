# 里程碑 14：完整文档生成器

## 1. 目标

实现元数据到多种文档格式的转换，实现"代码即文档"。

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

## 5. CLI 集成

```bash
./driver.exe --export-doc=markdown > README.md
./driver.exe --export-doc=openapi > api.json
./driver.exe --export-doc=html > doc.html
```

## 6. 依赖关系

- **前置**: M13 (CLI Help与元数据导出)
- **后续**: 无直接后续
