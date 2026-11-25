#pragma once

#include <QStringList>
#include <QWidget>

namespace bl::ui {

class CommandRegistry;

// View HUD (PLAN.md 3.2): translucent toolbar floating at the top centre of the graphics area.
class ViewHud : public QWidget {
    Q_OBJECT
public:
    ViewHud(CommandRegistry& registry, QWidget* viewport);

    void addCommand(const QString& id);
    void addFlyout(const QString& iconName, const QString& tooltip, const QStringList& ids);
    void addSeparator();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void reposition();

    CommandRegistry& m_registry;
    QWidget* m_viewport;
};

} // namespace bl::ui
