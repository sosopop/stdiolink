#ifndef DOC_VIEWER_H
#define DOC_VIEWER_H

#include <QWidget>
#include <QTextBrowser>
#include <stdiolink/protocol/meta_types.h>

class DocViewer : public QWidget
{
    Q_OBJECT

public:
    explicit DocViewer(QWidget *parent = nullptr);

    void setCommand(const stdiolink::meta::CommandMeta *cmd);
    void clear();

private:
    QString generateMarkdown(const stdiolink::meta::CommandMeta *cmd);
    QString formatFieldMarkdown(const stdiolink::meta::FieldMeta &field, int indent = 0);

    QTextBrowser *m_browser;
};

#endif // DOC_VIEWER_H
