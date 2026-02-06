#pragma once

#include "stdiolink/stdiolink_export.h"

#include <QJsonObject>
#include <QString>
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

/**
 * 文档生成器
 */
class STDIOLINK_API DocGenerator {
public:
    /**
     * 生成 Markdown 文档
     */
    static QString toMarkdown(const meta::DriverMeta& meta);

    /**
     * 生成 OpenAPI 3.0 JSON
     */
    static QJsonObject toOpenAPI(const meta::DriverMeta& meta);

    /**
     * 生成 HTML 文档
     */
    static QString toHtml(const meta::DriverMeta& meta);

    /**
     * 生成 TypeScript 声明文件
     */
    static QString toTypeScript(const meta::DriverMeta& meta);

private:
    // Markdown helpers
    static QString formatFieldMarkdown(const meta::FieldMeta& field, int indent = 0);
    static QString formatConstraintsMarkdown(const meta::Constraints& c);

    // OpenAPI helpers
    static QString commandToPath(const QString& cmdName);
    static QJsonObject fieldToSchema(const meta::FieldMeta& field);
    static QString fieldTypeToOpenAPIType(meta::FieldType type);

    // HTML helpers
    static QString generateHtmlStyle();
    static QString generateHtmlScript();

    // TypeScript helpers
    static QString fieldTypeToTs(meta::FieldType type);
    static QString fieldToTsType(const meta::FieldMeta& field, int indent = 0);
    static QString generateTsInterface(const QString& name,
                                       const QVector<meta::FieldMeta>& fields,
                                       int indent = 0);
};

} // namespace stdiolink
