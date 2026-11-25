#pragma once

#include <QIcon>
#include <QString>

namespace bl::ui {

// Returns the icon ":/icons/<name>.svg". When no SVG exists yet, a generated placeholder (rounded
// tile with an abbreviation of `label`) is returned so every command still has a distinct icon.
QIcon icon(const QString& name, const QString& label = {});

// Tint used for icon strokes; switched by the theme.
void setIconDarkMode(bool dark);

} // namespace bl::ui
