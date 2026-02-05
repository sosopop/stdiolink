#ifndef EMOJI_ICON_H
#define EMOJI_ICON_H

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFont>

class EmojiIcon {
public:
    static QIcon get(const QString &emoji, int size = 24) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        
        // 设置一个支持 Emoji 的字体，Segoe UI Emoji 在 Windows 上效果最好
#ifdef Q_OS_WIN
        QFont font("Segoe UI Emoji");
#else
        QFont font("Apple Color Emoji");
#endif
        font.setPixelSize(size * 0.8); // 稍微缩小一点防止溢出
        painter.setFont(font);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, emoji);
        return QIcon(pixmap);
    }
};

#endif // EMOJI_ICON_H
