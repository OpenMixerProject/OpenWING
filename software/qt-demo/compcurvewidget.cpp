#include "compcurvewidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>
#include <cstdlib>

static const double kMinDb = -60.0;
static const double kMaxDb = 0.0;
static const int    kMeterW = 14;   // width reserved for the GR meter strip
static const int    kAxisL = 30;    // width reserved for dB labels
static const int    kAxisB = 18;    // height reserved for input labels

CompCurveWidget::CompCurveWidget(QWidget *parent)
    : QWidget(parent)
    , m_threshold(-18.0)
    , m_ratio(3.5)
    , m_knee(6.0)
    , m_makeup(0.0)
    , m_active(true)
    , m_dragging(false)
    , m_inputLevel(-24.0)
    , m_grSmooth(0.0)
    , m_theme(QColor("#3b82f6"))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    m_timerId = startTimer(33); // ~30 FPS meter animation
}

void CompCurveWidget::setThemeColor(const QColor &c) { m_theme = c; update(); }

void CompCurveWidget::setThreshold(double db)
{
    db = qBound(kMinDb, db, kMaxDb);
    if (qAbs(db - m_threshold) > 0.001) { m_threshold = db; update(); }
}

void CompCurveWidget::setRatio(double ratio)
{
    ratio = qBound(1.0, ratio, 100.0);
    if (qAbs(ratio - m_ratio) > 0.001) { m_ratio = ratio; update(); }
}

void CompCurveWidget::setKnee(double db)
{
    db = qBound(0.0, db, 24.0);
    if (qAbs(db - m_knee) > 0.001) { m_knee = db; update(); }
}

void CompCurveWidget::setMakeup(double db)
{
    if (qAbs(db - m_makeup) > 0.001) { m_makeup = db; update(); }
}

void CompCurveWidget::setActive(bool active)
{
    if (active != m_active) { m_active = active; update(); }
}

QRect CompCurveWidget::plotRect() const
{
    return QRect(kAxisL + kMeterW, 4, width() - kAxisL - kMeterW - 8, height() - kAxisB - 4);
}

double CompCurveWidget::dbToX(double db) const
{
    const QRect r = plotRect();
    double pct = (db - kMinDb) / (kMaxDb - kMinDb);
    return r.left() + pct * r.width();
}

double CompCurveWidget::dbToY(double db) const
{
    const QRect r = plotRect();
    double pct = (db - kMinDb) / (kMaxDb - kMinDb);
    return r.bottom() - pct * r.height();
}

double CompCurveWidget::xToDb(double x) const
{
    const QRect r = plotRect();
    double pct = (x - r.left()) / r.width();
    return kMinDb + qBound(0.0, pct, 1.0) * (kMaxDb - kMinDb);
}

double CompCurveWidget::yToDb(double y) const
{
    const QRect r = plotRect();
    double pct = (r.bottom() - y) / r.height();
    return kMinDb + qBound(0.0, pct, 1.0) * (kMaxDb - kMinDb);
}

double CompCurveWidget::transfer(double inDb) const
{
    double out;
    double over = inDb - m_threshold;
    double knee = qMax(0.0001, m_knee);

    if (2.0 * over < -knee) {
        out = inDb;
    } else if (2.0 * over > knee) {
        out = m_threshold + over / m_ratio;
    } else {
        // Quadratic soft-knee interpolation
        double x = over + knee / 2.0;
        out = inDb + (1.0 / m_ratio - 1.0) * (x * x) / (2.0 * knee);
    }
    return out + m_makeup;
}

void CompCurveWidget::timerEvent(QTimerEvent *event)
{
    if (event->timerId() != m_timerId) { QWidget::timerEvent(event); return; }

    // Animate a probe input level so the gain-reduction meter has life.
    double r = (std::rand() % 1000) / 1000.0;
    double target = -30.0 + 28.0 * std::pow(r, 0.5); // biased toward louder
    m_inputLevel = m_inputLevel * 0.85 + target * 0.15;

    double grTarget = m_active ? qMax(0.0, m_inputLevel - (transfer(m_inputLevel) - m_makeup)) : 0.0;
    m_grSmooth = m_grSmooth * 0.7 + grTarget * 0.3;
    update();
}

void CompCurveWidget::paintEvent(QPaintEvent *)
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
    for (int db = 0; db >= -60; db -= 10) {
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

    // Threshold knee handle
    double hx = dbToX(m_threshold);
    double hy = dbToY(qBound(kMinDb, transfer(m_threshold), kMaxDb));
    p.setPen(QPen(theme, 2.5));
    p.setBrush(QColor("#111115"));
    p.drawEllipse(QPointF(hx, hy), 8, 8);
    p.setPen(Qt::NoPen);
    p.setBrush(theme);
    p.drawEllipse(QPointF(hx, hy), 3.5, 3.5);

    // Ratio readout near the handle
    p.setPen(QColor("#e5e7eb"));
    p.setFont(QFont("Inter", 8, QFont::Bold));
    QString ratioStr = (m_ratio >= 100.0) ? QString::fromUtf8("∞:1") : QString("%1:1").arg(m_ratio, 0, 'f', 1);
    p.drawText(QRectF(hx + 12, hy - 20, 70, 14), Qt::AlignLeft | Qt::AlignVCenter, ratioStr);

    // ---- Gain reduction meter (left strip) ----
    QRect meter(kAxisL, plot.top(), kMeterW - 4, plot.height());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#0c0c10"));
    p.drawRect(meter);

    double grRange = 24.0; // full-scale GR in dB
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

void CompCurveWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_active) {
        double hx = dbToX(m_threshold);
        double hy = dbToY(qBound(kMinDb, transfer(m_threshold), kMaxDb));
        if (std::hypot(event->pos().x() - hx, event->pos().y() - hy) <= 18.0) {
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void CompCurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        double newThr = xToDb(event->pos().x());

        // Vertical position relative to the unity line drives the ratio:
        // dragging the handle down (more output reduction) raises the ratio.
        double yUnity = dbToY(newThr);
        double yPos = event->pos().y();
        double drop = (yPos - yUnity); // pixels below unity line
        double pxRange = plotRect().height() * 0.5;
        double t = qBound(0.0, drop / qMax(1.0, pxRange), 1.0);
        double newRatio = 1.0 + t * 99.0; // 1:1 .. 100:1

        setThreshold(newThr);
        setRatio(newRatio);
        emit thresholdChanged(m_threshold);
        emit ratioChanged(m_ratio);
        event->accept();
        return;
    }

    // Hover cursor feedback
    if (m_active) {
        double hx = dbToX(m_threshold);
        double hy = dbToY(qBound(kMinDb, transfer(m_threshold), kMaxDb));
        if (std::hypot(event->pos().x() - hx, event->pos().y() - hy) <= 18.0)
            setCursor(Qt::OpenHandCursor);
        else
            unsetCursor();
    }
    QWidget::mouseMoveEvent(event);
}

void CompCurveWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}
