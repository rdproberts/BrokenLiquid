#include "app/SplashScreen.h"

#include "bl/Version.h"

#include <QApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPauseAnimation>
#include <QPropertyAnimation>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QScreen>
#include <QSequentialAnimationGroup>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

namespace bl::app {
namespace {

constexpr int kCardW = 760;
constexpr int kCardH = 440;
constexpr int kMargin = 28; // room for the soft shadow
constexpr double kRadius = 14.0;

const QColor kAccent(0x22, 0xC3, 0xE6);
const QColor kInk(0x0A, 0x14, 0x24);
const QColor kInk2(0x13, 0x2A, 0x45);
const QColor kMuted(0x9F, 0xB3, 0xC8);

bool systemReducesMotion() {
#ifdef _WIN32
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0)) {
        return animations == FALSE;
    }
#endif
    return false;
}

QColor mix(const QColor& a, const QColor& b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
                            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
                            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t),
                            static_cast<float>(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

QPainterPath dropletPath(const QPointF& c) {
    // Teardrop: pointed tip at the top, round body below.
    const double r = 104.0;
    const QPointF tip(c.x(), c.y() - 150.0);
    const QPointF body(c.x(), c.y() + 28.0);
    const QPointF right(body.x() + r, body.y());
    const QPointF left(body.x() - r, body.y());
    QPainterPath path;
    path.moveTo(tip);
    path.cubicTo(tip + QPointF(22, 48), right + QPointF(0, -78), right);
    path.arcTo(QRectF(body.x() - r, body.y() - r, 2 * r, 2 * r), 0.0, -180.0);
    path.cubicTo(left + QPointF(0, -78), tip + QPointF(-22, 48), tip);
    path.closeSubpath();
    return path;
}

QPolygonF crackLine(const QPointF& c) {
    return QPolygonF({c + QPointF(1, -152), c + QPointF(-7, -104), c + QPointF(9, -62), c + QPointF(-5, -18),
                      c + QPointF(11, 26), c + QPointF(-8, 74), c + QPointF(6, 116), c + QPointF(-2, 140)});
}

void strokeGlow(QPainter& p, const QPainterPath& path, const QColor& color, double width) {
    QColor glow = color;
    for (int i = 4; i >= 1; --i) {
        glow.setAlphaF(0.07f);
        p.setPen(QPen(glow, width + i * 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }
    p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
}

} // namespace

SplashScreen::SplashScreen() : QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(kCardW + 2 * kMargin, kCardH + 2 * kMargin);
    m_reduceMotion = systemReducesMotion();
    buildArt();
}

QRectF SplashScreen::cardRect() const { return QRectF(kMargin, kMargin, kCardW, kCardH); }

void SplashScreen::showAnimated() {
    if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos()) ? QGuiApplication::screenAt(QCursor::pos())
                                                                    : QGuiApplication::primaryScreen()) {
        const QRect g = screen->availableGeometry();
        move(g.center() - rect().center());
    }

    if (m_reduceMotion) {
        show();
        return;
    }
    setWindowOpacity(0.0);
    show();
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(250);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);

    // Highlight sweep across the droplet every ~2.5 s.
    auto* sweep = new QVariantAnimation(this);
    sweep->setDuration(1500);
    sweep->setStartValue(0.0);
    sweep->setEndValue(1.0);
    sweep->setEasingCurve(QEasingCurve::InOutSine);
    connect(sweep, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_shimmer = v.toDouble();
        update();
    });
    auto* group = new QSequentialAnimationGroup(this);
    group->addPause(350);
    group->addAnimation(sweep);
    group->addPause(1000);
    group->setLoopCount(-1);
    group->start();
    m_shimmerAnim = group;
}

void SplashScreen::setStatus(const QString& text, double progress) {
    m_status = text;
    progress = std::clamp(progress, 0.0, 1.0);
    if (m_reduceMotion) {
        m_progressShown = progress;
    } else {
        if (m_progressAnim) {
            m_progressAnim->stop();
        }
        auto* anim = new QVariantAnimation(this);
        anim->setDuration(220);
        anim->setStartValue(m_progressShown);
        anim->setEndValue(progress);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_progressShown = v.toDouble();
            update();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
        m_progressAnim = anim;
    }
    update();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SplashScreen::finish() {
    if (m_shimmerAnim) {
        m_shimmerAnim->stop();
    }
    if (m_reduceMotion) {
        close();
        return;
    }
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(200);
    fade->setStartValue(windowOpacity());
    fade->setEndValue(0.0);
    connect(fade, &QPropertyAnimation::finished, this, &QWidget::close);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

// --- Artwork -----------------------------------------------------------------------------------------

void SplashScreen::buildArt() {
    const qreal dpr = qApp->devicePixelRatio();
    m_art = QPixmap(QSize(width(), height()) * dpr);
    m_art.setDevicePixelRatio(dpr);
    m_art.fill(Qt::transparent);
    QPainter p(&m_art);
    p.setRenderHint(QPainter::Antialiasing);
    paintArt(p, cardRect());
}

void SplashScreen::paintArt(QPainter& p, const QRectF& card) {
    // Soft shadow.
    p.setPen(Qt::NoPen);
    for (int i = 14; i >= 1; --i) {
        QPainterPath s;
        s.addRoundedRect(card.adjusted(-i, -i + 6, i, i + 6), kRadius + i, kRadius + i);
        p.setBrush(QColor(0, 0, 0, 7));
        p.drawPath(s);
    }

    QPainterPath cardPath;
    cardPath.addRoundedRect(card, kRadius, kRadius);
    p.save();
    p.setClipPath(cardPath);

    // Background: deep navy diagonal gradient.
    QLinearGradient bg(card.topLeft(), card.bottomRight());
    bg.setColorAt(0.0, kInk);
    bg.setColorAt(1.0, kInk2);
    p.fillRect(card, bg);

    // Faint engineering grid, fading towards the text side.
    for (double x = card.left(); x < card.right(); x += 32.0) {
        const double t = (x - card.left()) / card.width();
        p.setPen(QPen(QColor(255, 255, 255, static_cast<int>(4 + 10 * t)), 1));
        p.drawLine(QPointF(x, card.top()), QPointF(x, card.bottom()));
    }
    for (double y = card.top(); y < card.bottom(); y += 32.0) {
        p.setPen(QPen(QColor(255, 255, 255, 7), 1));
        p.drawLine(QPointF(card.left() + card.width() * 0.35, y), QPointF(card.right(), y));
    }

    const QPointF c(card.left() + 520.0, card.top() + 200.0);

    // Glow behind the droplet.
    QRadialGradient halo(c, 320);
    halo.setColorAt(0.0, QColor(0x22, 0xC3, 0xE6, 70));
    halo.setColorAt(0.5, QColor(0x22, 0xC3, 0xE6, 18));
    halo.setColorAt(1.0, Qt::transparent);
    p.setPen(Qt::NoPen);
    p.setBrush(halo);
    p.drawEllipse(c, 320, 320);

    // Split the droplet along the crack.
    const QPainterPath drop = dropletPath(c);
    const QPolygonF crack = crackLine(c);
    QPolygonF leftPoly = crack;
    leftPoly << c + QPointF(-400, 200) << c + QPointF(-400, -300);
    QPolygonF rightPoly = crack;
    rightPoly << c + QPointF(400, 200) << c + QPointF(400, -300);
    QPainterPath leftClip, rightClip;
    leftClip.addPolygon(leftPoly);
    rightClip.addPolygon(rightPoly);
    const QPainterPath leftHalf = drop.intersected(leftClip);
    const QPointF shardOffset(18, -10);
    const QPainterPath rightHalf = drop.intersected(rightClip).translated(shardOffset);
    m_dropletPath = leftHalf.united(rightHalf);

    // --- Left half: smooth chrome fluid.
    QRadialGradient chrome(c + QPointF(-45, -10), 190, c + QPointF(-60, -40));
    chrome.setColorAt(0.0, QColor(0xF4, 0xFA, 0xFF));
    chrome.setColorAt(0.28, QColor(0xB7, 0xCB, 0xDD));
    chrome.setColorAt(0.62, QColor(0x4A, 0x6A, 0x8C));
    chrome.setColorAt(1.0, QColor(0x10, 0x22, 0x38));
    p.setBrush(chrome);
    p.drawPath(leftHalf);
    p.save();
    p.setClipPath(leftHalf);
    QRadialGradient spec(c + QPointF(-52, -30), 42);
    spec.setColorAt(0.0, QColor(255, 255, 255, 220));
    spec.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(spec);
    p.drawEllipse(c + QPointF(-52, -30), 42, 54);
    p.setPen(QPen(QColor(255, 255, 255, 60), 6, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(c.x() - 92, c.y() - 50, 160, 150), 150 * 16, 60 * 16); // reflection band
    p.restore();

    // --- Right half: faceted, engineered shards (low-poly triangles with cyan edges).
    p.save();
    p.setClipPath(rightHalf);
    const QRectF rb = rightHalf.boundingRect().adjusted(-40, -40, 40, 40);
    QRandomGenerator rng(1729); // deterministic art
    const double step = 30.0;
    const int cols = static_cast<int>(rb.width() / step) + 2;
    const int rows = static_cast<int>(rb.height() / step) + 2;
    std::vector<QPointF> grid(static_cast<size_t>(cols * rows));
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            const double jx = (rng.generateDouble() - 0.5) * 16.0;
            const double jy = (rng.generateDouble() - 0.5) * 16.0;
            grid[static_cast<size_t>(j * cols + i)] =
                QPointF(rb.left() + i * step + (j % 2) * step * 0.5 + jx, rb.top() + j * step + jy);
        }
    }
    const QColor dark(0x1E, 0x38, 0x57);
    const QColor lightC(0xD4, 0xE2, 0xEE);
    QPainterPath edges;
    for (int j = 0; j + 1 < rows; ++j) {
        for (int i = 0; i + 1 < cols; ++i) {
            const QPointF a = grid[static_cast<size_t>(j * cols + i)];
            const QPointF b = grid[static_cast<size_t>(j * cols + i + 1)];
            const QPointF d = grid[static_cast<size_t>((j + 1) * cols + i)];
            const QPointF e = grid[static_cast<size_t>((j + 1) * cols + i + 1)];
            for (const QPolygonF& tri : {QPolygonF({a, b, d}), QPolygonF({b, e, d})}) {
                const QPointF centroid = (tri[0] + tri[1] + tri[2]) / 3.0;
                // Light from the upper left, with per-facet variation.
                const double lx = (centroid.x() - rb.left()) / rb.width();
                const double ly = (centroid.y() - rb.top()) / rb.height();
                const double shade = 0.85 - 0.55 * lx - 0.45 * ly + (rng.generateDouble() - 0.5) * 0.35;
                p.setPen(Qt::NoPen);
                p.setBrush(mix(dark, lightC, shade));
                p.drawPolygon(tri);
                edges.addPolygon(tri);
            }
        }
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x22, 0xC3, 0xE6, 110), 1.0));
    p.drawPath(edges);
    p.restore();

    // Crack edges glow.
    QPainterPath crackPath;
    crackPath.addPolygon(crack);
    p.setBrush(Qt::NoBrush);
    strokeGlow(p, crackPath, QColor(0x5F, 0xE0, 0xF6), 1.6);
    strokeGlow(p, crackPath.translated(shardOffset), QColor(0x5F, 0xE0, 0xF6), 1.6);
    p.setPen(QPen(QColor(0x22, 0xC3, 0xE6, 140), 1.2));
    p.drawPath(rightHalf);

    // Flying shards (right) and fluid droplets (left).
    const struct {
        double x, y, s, rot;
    } shards[] = {{150, -110, 14, 20}, {175, -40, 9, -35}, {160, 60, 11, 60}, {195, 10, 6, 10}, {135, 120, 7, -15}};
    for (const auto& s : shards) {
        QPolygonF tri({QPointF(0, -s.s), QPointF(s.s * 0.9, s.s * 0.6), QPointF(-s.s * 0.7, s.s * 0.5)});
        QTransform t;
        t.translate(c.x() + s.x, c.y() + s.y);
        t.rotate(s.rot);
        const QPolygonF placed = t.map(tri);
        p.setBrush(mix(QColor(0x2A, 0x4A, 0x6E), QColor(0xC9, 0xD8, 0xE6), 0.35 + s.s / 40.0));
        p.setPen(QPen(QColor(0x22, 0xC3, 0xE6, 200), 1.0));
        p.drawPolygon(placed);
    }
    const struct {
        double x, y, r;
    } drops[] = {{-150, 95, 9}, {-178, 40, 5}, {-130, 150, 4}, {-195, 120, 3}};
    for (const auto& d : drops) {
        QRadialGradient g(c + QPointF(d.x - d.r * 0.4, d.y - d.r * 0.4), d.r * 1.6);
        g.setColorAt(0.0, QColor(0xF4, 0xFA, 0xFF));
        g.setColorAt(0.5, QColor(0x7F, 0x9C, 0xB8));
        g.setColorAt(1.0, QColor(0x1A, 0x30, 0x4A));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(c + QPointF(d.x, d.y), d.r, d.r);
    }

    // --- Text.
    const double tx = card.left() + 46.0;
    QFont word(QStringLiteral("Segoe UI"));
    word.setPixelSize(44);
    word.setWeight(QFont::Bold);
    p.setFont(word);
    p.setPen(Qt::white);
    const QString broken = QStringLiteral("Broken");
    p.drawText(QPointF(tx, card.top() + 330.0), broken);
    const double brokenW = QFontMetricsF(word).horizontalAdvance(broken);
    QFont word2 = word;
    word2.setWeight(QFont::Light);
    p.setFont(word2);
    p.setPen(kAccent);
    p.drawText(QPointF(tx + brokenW + 2.0, card.top() + 330.0), QStringLiteral("Liquid"));

    QFont tag(QStringLiteral("Segoe UI"));
    tag.setPixelSize(14);
    tag.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    p.setFont(tag);
    p.setPen(kMuted);
    p.drawText(QPointF(tx + 2.0, card.top() + 358.0), QStringLiteral("Parametric CAD  ·  Free  ·  Local"));

    QFont badge(QStringLiteral("Segoe UI"));
    badge.setPixelSize(10);
    badge.setWeight(QFont::DemiBold);
    badge.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    p.setFont(badge);
    p.setPen(QColor(0x22, 0xC3, 0xE6, 200));
    p.drawText(QPointF(tx + 2.0, card.top() + 44.0), QStringLiteral("PHASE 0 PREVIEW"));

    QFont smallFont(QStringLiteral("Segoe UI"));
    smallFont.setPixelSize(11);
    p.setFont(smallFont);
    p.setPen(QColor(0x6F, 0x86, 0xA0));
    const QString version = QStringLiteral("v%1  ·  build %2").arg(QString::fromLatin1(kVersion), QString::fromLatin1(kBuildId));
    const double vw = QFontMetricsF(smallFont).horizontalAdvance(version);
    p.drawText(QPointF(card.right() - 22.0 - vw, card.bottom() - 18.0), version);

    p.restore(); // card clip

    // Hairline border.
    p.setPen(QPen(QColor(255, 255, 255, 22), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(cardPath);
}

void SplashScreen::paintShimmer(QPainter& p) {
    if (m_shimmer < 0.0 || m_dropletPath.isEmpty()) {
        return;
    }
    const QRectF b = m_dropletPath.boundingRect();
    const double x = b.left() - 120.0 + (b.width() + 240.0) * m_shimmer;
    QLinearGradient band(QPointF(x - 50, b.top()), QPointF(x + 50, b.top() + 40));
    band.setColorAt(0.0, QColor(255, 255, 255, 0));
    band.setColorAt(0.5, QColor(255, 255, 255, 85));
    band.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.save();
    p.setClipPath(m_dropletPath);
    p.fillRect(b, band);
    p.restore();
}

void SplashScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap(0, 0, m_art);
    paintShimmer(p);

    const QRectF card = cardRect();
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, kRadius, kRadius);
    p.setClipPath(cardPath);

    // Status line.
    QFont smallFont(QStringLiteral("Segoe UI"));
    smallFont.setPixelSize(11);
    p.setFont(smallFont);
    p.setPen(kMuted);
    p.drawText(QPointF(card.left() + 48.0, card.bottom() - 18.0), m_status);

    // Progress bar along the bottom edge.
    const QRectF track(card.left(), card.bottom() - 3.0, card.width(), 3.0);
    p.fillRect(track, QColor(255, 255, 255, 18));
    QLinearGradient fill(track.topLeft(), track.topRight());
    fill.setColorAt(0.0, kAccent);
    fill.setColorAt(1.0, QColor(0x8F, 0xE9, 0xF8));
    p.fillRect(QRectF(track.left(), track.top(), track.width() * m_progressShown, track.height()), fill);
}

} // namespace bl::app
