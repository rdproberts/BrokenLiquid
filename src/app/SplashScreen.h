#pragma once

#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

class QVariantAnimation;
class QAbstractAnimation;

namespace bl::app {

// Launch splash (PLAN.md 8.1): frameless rounded card with the "broken liquid" droplet art -
// smooth chrome fluid on the left, faceted engineered shards on the right - plus wordmark,
// version, status line and an eased progress bar. Artwork is drawn procedurally for now and
// cached; a rendered image can replace it later without changing the API.
class SplashScreen : public QWidget {
    Q_OBJECT
public:
    SplashScreen();

    void showAnimated();
    // Status line + progress (0..1). Processes events so the splash repaints during startup work.
    void setStatus(const QString& text, double progress);
    // Fades out, then closes and deletes itself.
    void finish();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRectF cardRect() const;
    void buildArt();
    void paintArt(QPainter& p, const QRectF& card);
    void paintShimmer(QPainter& p);

    QPixmap m_art;              // static artwork cache (device-pixel sized)
    QPainterPath m_dropletPath; // union of both droplet halves, for the shimmer clip
    QString m_status;
    double m_progressShown = 0.0;
    double m_shimmer = -1.0;    // <0 => not drawn
    bool m_reduceMotion = false;
    QPointer<QVariantAnimation> m_progressAnim;
    QPointer<QAbstractAnimation> m_shimmerAnim;
};

} // namespace bl::app
