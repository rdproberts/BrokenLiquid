#include "ui/Theme.h"

#include "ui/Icons.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

namespace bl::ui {
namespace {
ThemeKind g_current = ThemeKind::Light;
}

ThemeColors themeColors(ThemeKind kind) {
    ThemeColors c;
    c.accent = QColor(0x22, 0xC3, 0xE6);
    c.selection = QColor(0x1E, 0x6F, 0xD9);
    c.warning = QColor(0xFF, 0x7A, 0x3D);
    if (kind == ThemeKind::Light) {
        c.text = QColor(0x1B, 0x26, 0x33);
        c.textMuted = QColor(0x5B, 0x6B, 0x7F);
        c.viewportTop = QColor(0xF4, 0xF7, 0xFA);
        c.viewportBottom = QColor(0xB9, 0xC6, 0xD3);
    } else {
        c.text = QColor(0xE6, 0xEE, 0xF5);
        c.textMuted = QColor(0x9F, 0xB3, 0xC8);
        c.viewportTop = QColor(0x3A, 0x43, 0x4F);
        c.viewportBottom = QColor(0x15, 0x19, 0x1F);
    }
    return c;
}

void applyTheme(ThemeKind kind) {
    g_current = kind;
    setIconDarkMode(kind == ThemeKind::Dark);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const ThemeColors c = themeColors(kind);
    QPalette p;
    if (kind == ThemeKind::Light) {
        p.setColor(QPalette::Window, QColor(0xEE, 0xF1, 0xF4));
        p.setColor(QPalette::Base, QColor(0xFB, 0xFC, 0xFD));
        p.setColor(QPalette::AlternateBase, QColor(0xF1, 0xF4, 0xF7));
        p.setColor(QPalette::Button, QColor(0xF3, 0xF5, 0xF8));
        p.setColor(QPalette::Mid, QColor(0xC9, 0xD3, 0xDE));
        p.setColor(QPalette::ToolTipBase, QColor(0xFF, 0xFF, 0xFF));
    } else {
        p.setColor(QPalette::Window, QColor(0x23, 0x28, 0x30));
        p.setColor(QPalette::Base, QColor(0x1B, 0x1F, 0x25));
        p.setColor(QPalette::AlternateBase, QColor(0x26, 0x2C, 0x34));
        p.setColor(QPalette::Button, QColor(0x2B, 0x31, 0x3A));
        p.setColor(QPalette::Mid, QColor(0x3E, 0x47, 0x53));
        p.setColor(QPalette::ToolTipBase, QColor(0x2B, 0x31, 0x3A));
    }
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.textMuted);
    p.setColor(QPalette::Highlight, c.selection);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, c.selection);
    p.setColor(QPalette::Disabled, QPalette::Text, c.textMuted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.textMuted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.textMuted);
    QApplication::setPalette(p);

    QFile qss(kind == ThemeKind::Light ? QStringLiteral(":/themes/light.qss") : QStringLiteral(":/themes/dark.qss"));
    if (qss.open(QIODevice::ReadOnly)) {
        qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
    }
}

ThemeKind currentTheme() { return g_current; }

} // namespace bl::ui
