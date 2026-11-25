#pragma once

#include <QColor>

namespace bl::ui {

enum class ThemeKind { Light, Dark };

struct ThemeColors {
    QColor accent;          // Liquid Cyan
    QColor selection;       // deep blue
    QColor warning;         // Signal Orange
    QColor text;
    QColor textMuted;
    QColor viewportTop;
    QColor viewportBottom;
};

// Applies Fusion style + palette + the theme's QSS (":/themes/<light|dark>.qss") to the app.
void applyTheme(ThemeKind kind);
ThemeKind currentTheme();
ThemeColors themeColors(ThemeKind kind);

} // namespace bl::ui
