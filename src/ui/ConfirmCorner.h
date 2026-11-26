#pragma once

#include <QWidget>

class QToolButton;

namespace bl::ui {

// OK / Cancel buttons in the top-right corner of the graphics area while a command is running
// (PLAN.md 7.1 "Confirm Corner"). The D key moves them next to the pointer.
class ConfirmCorner : public QWidget {
    Q_OBJECT
public:
    explicit ConfirmCorner(QWidget* viewport);

    void showFor(bool confirm); // confirm=false: only the close (X) button
    void moveToCursor();

signals:
    void accepted();
    void cancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void placeInCorner();

    QWidget* m_viewport;
    QToolButton* m_ok;
    QToolButton* m_cancel;
    bool m_pinnedToCursor = false;
};

} // namespace bl::ui
