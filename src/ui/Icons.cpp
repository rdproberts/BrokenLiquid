#include "ui/Icons.h"

#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QSvgRenderer>

namespace bl::ui {
namespace {

bool g_dark = false;
QHash<QString, QIcon> g_cache;

QString abbreviation(const QString& label) {
    const QStringList words = label.split(QRegularExpression(QStringLiteral("[\\s/_.-]+")), Qt::SkipEmptyParts);
    QString abbr;
    for (const QString& w : words) {
        if (w.front().isLetterOrNumber()) {
            abbr += w.front().toUpper();
        }
        if (abbr.size() == 2) {
            break;
        }
    }
    if (abbr.isEmpty()) {
        abbr = QStringLiteral("?");
    }
    return abbr;
}

QPixmap placeholder(const QString& text, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(1.5, 1.5, size - 3.0, size - 3.0);
    QPainterPath path;
    path.addRoundedRect(r, size * 0.22, size * 0.22);
    p.fillPath(path, g_dark ? QColor(0x22, 0xC3, 0xE6, 60) : QColor(0x22, 0xC3, 0xE6, 45));
    p.setPen(QPen(g_dark ? QColor(0x9F, 0xB3, 0xC8) : QColor(0x2B, 0x45, 0x63), size / 20.0));
    p.drawPath(path);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(static_cast<int>(size * (text.size() > 1 ? 0.40 : 0.5)));
    p.setFont(f);
    p.setPen(g_dark ? QColor(0xE6, 0xEE, 0xF5) : QColor(0x1B, 0x26, 0x33));
    p.drawText(r, Qt::AlignCenter, text);
    return pm;
}

QPixmap renderSvg(const QString& file, int size) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray svg = f.readAll();
    if (g_dark) {
        // Icons use #1F3A5A for strokes; lighten for the dark theme.
        svg.replace("#1F3A5A", "#D5E0EA");
    }
    QSvgRenderer renderer(svg);
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    renderer.render(&p);
    return pm;
}

} // namespace

void setIconDarkMode(bool dark) {
    if (g_dark != dark) {
        g_dark = dark;
        g_cache.clear();
    }
}

QIcon icon(const QString& name, const QString& label) {
    const QString key = name + QLatin1Char('|') + label;
    if (auto it = g_cache.constFind(key); it != g_cache.constEnd()) {
        return it.value();
    }
    QIcon result;
    const QString file = QStringLiteral(":/icons/%1.svg").arg(name);
    const bool hasSvg = QFile::exists(file);
    for (int size : {16, 20, 24, 32, 48, 64}) {
        result.addPixmap(hasSvg ? renderSvg(file, size) : placeholder(abbreviation(label.isEmpty() ? name : label), size));
    }
    g_cache.insert(key, result);
    return result;
}

} // namespace bl::ui
