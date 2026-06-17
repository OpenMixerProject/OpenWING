#include "envelopegraphwidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>

EnvelopeGraphWidget::EnvelopeGraphWidget(QWidget *parent)
    : QWidget(parent)
    , m_attack(10.0)
    , m_release(150.0)
    , m_hold(0.0)
    , m_active(true)
    , m_theme(QColor("#3b82f6"))
    , m_drag(0)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void EnvelopeGraphWidget::setThemeColor(const QColor &c) { m_theme = c; update(); }

void EnvelopeGraphWidget::setAttack(double ms)
{
    ms = qBound(kAttMin, ms, kAttMax);
    if (qAbs(ms - m_attack) > 0.0001) { m_attack = ms; update(); }
}

void EnvelopeGraphWidget::setRelease(double ms)
{
    ms = qBound(kRelMin, ms, kRelMax);
    if (qAbs(ms - m_release) > 0.0001) { m_release = ms; update(); }
}

void EnvelopeGraphWidget::setHold(double ms)
{
    ms = qBound(kHoldMin, ms, kHoldMax);
    if (qAbs(ms - m_hold) > 0.0001) { m_hold = ms; update(); }
}

void EnvelopeGraphWidget::setActive(bool active)
{
    if (active != m_active) { m_active = active; update(); }
}

QRect EnvelopeGraphWidget::plotRect() const
{
    return rect().adjusted(16, 14, -16, -14);
}

double EnvelopeGraphWidget::attackNorm() const
{
    double l = (std::log10(m_attack) - std::log10(kAttMin)) /
               (std::log10(kAttMax) - std::log10(kAttMin));
    return qBound(0.0, l, 1.0);
}

double EnvelopeGraphWidget::releaseNorm() const
{
    double l = (std::log10(m_release) - std::log10(kRelMin)) /
               (std::log10(kRelMax) - std::log10(kRelMin));
    return qBound(0.0, l, 1.0);
}

QPointF EnvelopeGraphWidget::apex() const
{
    const QRect r = plotRect();
    return QPointF(r.center().x(), r.top());
}

QPointF EnvelopeGraphWidget::leftHandle() const
{
    const QRect r = plotRect();
    double cx = r.center().x();
    double holdWidth = (m_hold / kHoldMax) * (r.width() * 0.3);
    double leftStart = cx - holdWidth / 2.0;
    double x = leftStart - attackNorm() * (leftStart - r.left());
    return QPointF(x, r.bottom());
}

QPointF EnvelopeGraphWidget::rightHandle() const
{
    const QRect r = plotRect();
    double cx = r.center().x();
    double holdWidth = (m_hold / kHoldMax) * (r.width() * 0.3);
    double rightStart = cx + holdWidth / 2.0;
    double x = rightStart + releaseNorm() * (r.right() - rightStart);
    return QPointF(x, r.bottom());
}

void EnvelopeGraphWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = plotRect();
    p.fillRect(rect(), QColor("#141419"));

    // Subtle baseline + centre trigger guides
    p.setPen(QPen(QColor("#23232c"), 1, Qt::DashLine));
    p.drawLine(r.left(), r.bottom(), r.right(), r.bottom());
    double cx = r.center().x();
    p.drawLine(QPointF(cx, r.top()), QPointF(cx, r.bottom()));

    QColor theme = m_active ? m_theme : QColor("#52525b");

    double holdWidth = (m_hold / kHoldMax) * (r.width() * 0.3);
    QPointF attackEnd(cx - holdWidth / 2.0, r.top());
    QPointF releaseStart(cx + holdWidth / 2.0, r.top());

    QPointF lh = leftHandle();
    QPointF rh = rightHandle();

    // Filled wedge
    QPainterPath wedge;
    wedge.moveTo(lh);
    wedge.lineTo(attackEnd);
    wedge.lineTo(releaseStart);
    wedge.lineTo(rh);
    wedge.lineTo(rh.x(), r.bottom());
    wedge.lineTo(lh.x(), r.bottom());
    wedge.closeSubpath();

    QColor fillTop = theme; fillTop.setAlpha(110);
    QColor fillBot = theme; fillBot.setAlpha(20);
    QLinearGradient grad(0, r.top(), 0, r.bottom());
    grad.setColorAt(0, fillTop);
    grad.setColorAt(1, fillBot);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawPath(wedge);

    // Slopes and hold line
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(theme, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(lh, attackEnd);
    p.drawLine(attackEnd, releaseStart);
    p.drawLine(releaseStart, rh);

    // Handles
    auto drawHandle = [&](const QPointF &pt) {
        p.setPen(QPen(theme, 2.0));
        p.setBrush(QColor("#111115"));
        p.drawEllipse(pt, 7, 7);
    };
    drawHandle(attackEnd);
    drawHandle(releaseStart);
    drawHandle(lh);
    drawHandle(rh);

    // Labels
    p.setFont(QFont("Inter", 8, QFont::Bold));
    p.setPen(QColor("#9ca3af"));
    QString attStr = m_attack >= 100.0 ? QString("%1 ms").arg(m_attack, 0, 'f', 0)
                                       : QString("%1 ms").arg(m_attack, 0, 'f', 1);
    QString relStr = QString("%1 ms").arg(m_release, 0, 'f', 0);
    QString holdStr = QString("%1 ms").arg(m_hold, 0, 'f', 0);

    p.drawText(QRectF(r.left(), r.bottom() - 18, cx - r.left() - 35, 16),
               Qt::AlignLeft | Qt::AlignVCenter, "ATT " + attStr);
    p.drawText(QRectF(cx - 30, r.bottom() - 18, 60, 16),
               Qt::AlignCenter, "HLD " + holdStr);
    p.drawText(QRectF(cx + 35, r.bottom() - 18, r.right() - cx - 35, 16),
               Qt::AlignRight | Qt::AlignVCenter, "REL " + relStr);
}

void EnvelopeGraphWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_active) {
        if (std::hypot(event->pos().x() - leftHandle().x(),
                       event->pos().y() - leftHandle().y()) <= 18.0) {
            m_drag = 1; setCursor(Qt::SizeHorCursor); event->accept(); return;
        }
        if (std::hypot(event->pos().x() - rightHandle().x(),
                       event->pos().y() - rightHandle().y()) <= 18.0) {
            m_drag = 2; setCursor(Qt::SizeHorCursor); event->accept(); return;
        }
    }
    QWidget::mousePressEvent(event);
}

void EnvelopeGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QRect r = plotRect();
    double cx = r.center().x();

    if (m_drag == 1) {
        double holdWidth = (m_hold / kHoldMax) * (r.width() * 0.3);
        double leftStart = cx - holdWidth / 2.0;
        double norm = qBound(0.0, (leftStart - event->pos().x()) / qMax(1.0, leftStart - r.left()), 1.0);
        double ms = std::pow(10.0, std::log10(kAttMin) +
                    norm * (std::log10(kAttMax) - std::log10(kAttMin)));
        setAttack(ms);
        emit attackChanged(m_attack);
        event->accept();
        return;
    }
    if (m_drag == 2) {
        double holdWidth = (m_hold / kHoldMax) * (r.width() * 0.3);
        double rightStart = cx + holdWidth / 2.0;
        double norm = qBound(0.0, (event->pos().x() - rightStart) / qMax(1.0, r.right() - rightStart), 1.0);
        double ms = std::pow(10.0, std::log10(kRelMin) +
                    norm * (std::log10(kRelMax) - std::log10(kRelMin)));
        setRelease(ms);
        emit releaseChanged(m_release);
        event->accept();
        return;
    }

    if (m_active) {
        bool nearHandle =
            std::hypot(event->pos().x() - leftHandle().x(), event->pos().y() - leftHandle().y()) <= 18.0 ||
            std::hypot(event->pos().x() - rightHandle().x(), event->pos().y() - rightHandle().y()) <= 18.0;
        if (nearHandle) setCursor(Qt::SizeHorCursor);
        else unsetCursor();
    }
    QWidget::mouseMoveEvent(event);
}

void EnvelopeGraphWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_drag) {
        m_drag = 0;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}
