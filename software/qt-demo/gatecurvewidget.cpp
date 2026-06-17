#include "gatecurvewidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>
#include <cstdlib>

static const double kMinDb = -80.0;
static const double kMaxDb = 0.0;
static const int    kMeterW = 14;   // width reserved for the GR meter strip
static const int    kAxisL = 30;    // width reserved for dB labels
static const int    kAxisB = 18;    // height reserved for input labels

GateCurveWidget::GateCurveWidget(QWidget *parent)
    : QWidget(parent)
    , m_threshold(-55.0)
    , m_depth(40.0)
    , m_active(true)
    , m_draggingKnee(false)
    , m_draggingSlider(false)
    , m_inputLevel(-45.0)
    , m_grSmooth(0.0)
    , m_theme(QColor("#f97316"))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    m_timerId = startTimer(33); // ~30 FPS meter animation
}

void GateCurveWidget::setThemeColor(const QColor &c) { m_theme = c; update(); }

void GateCurveWidget::setThreshold(double db)
{
    db = qBound(kMinDb, db, kMaxDb);
    if (qAbs(db - m_threshold) > 0.001) { m_threshold = db; update(); }
}

void GateCurveWidget::setDepth(double db)
{
    db = qBound(0.0, db, 60.0);
    if (qAbs(db - m_depth) > 0.001) { m_depth = db; update(); }
}

void GateCurveWidget::setActive(bool active)
{
    if (active != m_active) { m_active = active; update(); }
}

QRect GateCurveWidget::plotRect() const
{
    // Reserve space for left labels + meter, and right space for the slider handle overlap
    return QRect(kAxisL + kMeterW, 4, width() - kAxisL - kMeterW - 14, height() - kAxisB - 4);
}

double GateCurveWidget::dbToX(double db) const
{
    const QRect r = plotRect();
    double pct = (db - kMinDb) / (kMaxDb - kMinDb);
    return r.left() + pct * r.width();
}

double GateCurveWidget::dbToY(double db) const
{
    const QRect r = plotRect();
    double pct = (db - kMinDb) / (kMaxDb - kMinDb);
    return r.bottom() - pct * r.height();
}

double GateCurveWidget::xToDb(double x) const
{
    const QRect r = plotRect();
    double pct = (x - r.left()) / r.width();
    return kMinDb + qBound(0.0, pct, 1.0) * (kMaxDb - kMinDb);
}

double GateCurveWidget::yToDb(double y) const
{
    const QRect r = plotRect();
    double pct = (r.bottom() - y) / r.height();
    return kMinDb + qBound(0.0, pct, 1.0) * (kMaxDb - kMinDb);
}

double GateCurveWidget::transfer(double inDb) const
{
    if (inDb >= m_threshold) {
        return inDb;
    } else {
        // Drop steeply (e.g. 5:1 expansion ratio below threshold)
        double out = m_threshold + (inDb - m_threshold) * 5.0;
        // Clamp maximum attenuation to m_depth
        double maxAttenuation = m_depth;
        if (inDb - out > maxAttenuation) {
            out = inDb - maxAttenuation;
        }
        return out;
    }
}

void GateCurveWidget::timerEvent(QTimerEvent *event)
{
    if (event->timerId() != m_timerId) { QWidget::timerEvent(event); return; }

    // Animate a probe input level so the GR/Gate meter has life.
    double r = (std::rand() % 1000) / 1000.0;
    double target = -45.0 + 40.0 * std::pow(r, 0.5); // biased toward louder
    m_inputLevel = m_inputLevel * 0.85 + target * 0.15;

    double grTarget = m_active ? qMax(0.0, m_inputLevel - transfer(m_inputLevel)) : 0.0;
    m_grSmooth = m_grSmooth * 0.7 + grTarget * 0.3;
    update();
}

void GateCurveWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect plot = plotRect();

    // Background
    p.fillRect(rect(), QColor("#101015"));
    p.fillRect(plot, QColor("#141419"));

    QFont gridFont("Inter", 8);
    p.setFont(gridFont);

    // dB grid lines + labels (every 10 dB)
    for (int db = 0; db >= -80; db -= 10) {
        double y = dbToY(db);
        double x = dbToX(db);
        p.setPen(QPen(db == 0 ? QColor("#3f3f4e") : QColor("#23232c"), 1,
                      db == 0 ? Qt::SolidLine : Qt::DashLine));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));

        p.setPen(QColor("#71717a"));
        p.drawText(QRectF(0, y - 7, kAxisL - 4, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(db));
        if (db < 0)
            p.drawText(QRectF(x - 14, plot.bottom() + 2, 28, kAxisB - 2),
                       Qt::AlignCenter, QString::number(db));
    }

    // Unity reference line (input == output)
    p.setPen(QPen(QColor("#2c2c3a"), 1, Qt::DashLine));
    p.drawLine(QPointF(dbToX(kMinDb), dbToY(kMinDb)), QPointF(dbToX(kMaxDb), dbToY(kMaxDb)));

    QColor theme = m_active ? m_theme : QColor("#52525b");

    // Transfer curve path
    QPainterPath curve;
    bool started = false;
    for (int px = 0; px <= plot.width(); ++px) {
        double inDb = xToDb(plot.left() + px);
        double outDb = qBound(kMinDb, transfer(inDb), kMaxDb);
        double y = dbToY(outDb);
        if (!started) { curve.moveTo(plot.left() + px, y); started = true; }
        else          { curve.lineTo(plot.left() + px, y); }
    }

    // Glow fill below the curve
    QPainterPath fill = curve;
    fill.lineTo(plot.right(), plot.bottom());
    fill.lineTo(plot.left(), plot.bottom());
    fill.closeSubpath();
    QLinearGradient grad(0, plot.top(), 0, plot.bottom());
    QColor c0 = theme; c0.setAlpha(70);
    QColor c1 = theme; c1.setAlpha(0);
    grad.setColorAt(0, c0);
    grad.setColorAt(1, c1);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.setClipRect(plot);
    p.drawPath(fill);
    p.setClipping(false);

    // Curve stroke
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(theme, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(curve);

    // Draw horizontal threshold reference line
    double ty = dbToY(m_threshold);
    p.setPen(QPen(QColor("#4b5563"), 1, Qt::SolidLine));
    p.drawLine(QPointF(plot.left(), ty), QPointF(plot.right(), ty));

    // Threshold knee handle
    double hx = dbToX(m_threshold);
    double hy = dbToY(m_threshold);
    p.setPen(QPen(theme, 2.5));
    p.setBrush(QColor("#111115"));
    p.drawEllipse(QPointF(hx, hy), 8, 8);
    p.setPen(Qt::NoPen);
    p.setBrush(theme);
    p.drawEllipse(QPointF(hx, hy), 3.5, 3.5);

    // Draw slider handle on the right edge with a burger icon ≡
    double sx = plot.right();
    double sy = ty;
    p.setPen(QPen(QColor("#4b5563"), 1));
    p.setBrush(QColor("#ffffff"));
    p.drawEllipse(QPointF(sx, sy), 10, 10);

    // Draw ≡ inside the white circle
    p.setPen(QPen(QColor("#111115"), 1.5, Qt::SolidLine));
    p.drawLine(QPointF(sx - 4, sy - 3), QPointF(sx + 4, sy - 3));
    p.drawLine(QPointF(sx - 4, sy),     QPointF(sx + 4, sy));
    p.drawLine(QPointF(sx - 4, sy + 3), QPointF(sx + 4, sy + 3));

    // ---- Gain reduction meter (left strip) ----
    QRect meter(kAxisL, plot.top(), kMeterW - 4, plot.height());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#0c0c10"));
    p.drawRect(meter);

    double grRange = 40.0; // full-scale GR in dB for gate
    double grPct = qBound(0.0, m_grSmooth / grRange, 1.0);
    int segs = 22;
    int litFromTop = qRound(grPct * segs);
    double segH = meter.height() / (double)segs;
    for (int i = 0; i < segs; ++i) {
        QRectF seg(meter.left() + 1, meter.top() + i * segH + 1,
                   meter.width() - 2, segH - 1.5);
        if (i < litFromTop) {
            double t = i / (double)segs;
            QColor on = (t > 0.75) ? QColor("#ef4444")
                      : (t > 0.5)  ? QColor("#fbbf24")
                                   : theme;
            p.setBrush(on);
        } else {
            p.setBrush(QColor("#1c1c26"));
        }
        p.drawRect(seg);
    }
    // "GR" caption under the meter
    p.setPen(QColor("#52525b"));
    p.setFont(QFont("Inter", 7, QFont::Bold));
    p.drawText(QRectF(meter.left() - 6, meter.bottom() + 2, meter.width() + 12, kAxisB - 2),
               Qt::AlignCenter, "GR");
}

void GateCurveWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_active) {
        double ty = dbToY(m_threshold);
        // Check knee handle
        double hx = dbToX(m_threshold);
        double hy = dbToY(m_threshold);
        if (std::hypot(event->pos().x() - hx, event->pos().y() - hy) <= 18.0) {
            m_draggingKnee = true;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        // Check slider handle
        double sx = plotRect().right();
        double sy = ty;
        if (std::hypot(event->pos().x() - sx, event->pos().y() - sy) <= 18.0) {
            m_draggingSlider = true;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void GateCurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_draggingKnee || m_draggingSlider) {
        double newThr = yToDb(event->pos().y());
        setThreshold(newThr);
        emit thresholdChanged(m_threshold);
        event->accept();
        return;
    }

    // Hover cursor feedback
    if (m_active) {
        double ty = dbToY(m_threshold);
        double hx = dbToX(m_threshold);
        double hy = dbToY(m_threshold);
        double sx = plotRect().right();
        double sy = ty;
        if (std::hypot(event->pos().x() - hx, event->pos().y() - hy) <= 18.0 ||
            std::hypot(event->pos().x() - sx, event->pos().y() - sy) <= 18.0) {
            setCursor(Qt::OpenHandCursor);
        } else {
            unsetCursor();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void GateCurveWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_draggingKnee || m_draggingSlider) {
        m_draggingKnee = false;
        m_draggingSlider = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}
