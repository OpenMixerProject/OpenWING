#include "rotaryknob.h"
#include <QPainter>
#include <QMouseEvent>
#include <QStyleOption>
#include <cmath>

RotaryKnob::RotaryKnob(QWidget *parent)
    : QAbstractSlider(parent)
    , m_themeColor(QColor("#00d1b2")) // Neon Teal default
    , m_label("")
    , m_unit("")
    , m_scaleFactor(1.0)
    , m_offset(0.0)
    , m_defaultValue(0.0)
    , m_dragging(false)
    , m_isSmall(false)
{
    setRange(0, 100);
    setValue(0);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMaximumSize(72, 90);
}

RotaryKnob::RotaryKnob(const QString &label, int min, int max, int defVal, QWidget *parent)
    : QAbstractSlider(parent)
    , m_themeColor(QColor("#00d1b2"))
    , m_label(label)
    , m_unit("")
    , m_scaleFactor(1.0)
    , m_offset(0.0)
    , m_defaultValue(defVal)
    , m_dragging(false)
    , m_isSmall(false)
{
    setRange(min, max);
    setValue(defVal);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMaximumSize(72, 90);
}

void RotaryKnob::setThemeColor(const QColor &color)
{
    m_themeColor = color;
    update();
}

void RotaryKnob::setLabel(const QString &label)
{
    m_label = label;
    update();
}

void RotaryKnob::setUnit(const QString &unit)
{
    m_unit = unit;
    update();
}

void RotaryKnob::setScaleFactor(double factor)
{
    m_scaleFactor = factor;
    update();
}

void RotaryKnob::setOffset(double offset)
{
    m_offset = offset;
    update();
}

void RotaryKnob::setDefaultValue(double val)
{
    m_defaultValue = val;
}

void RotaryKnob::setSmall(bool small)
{
    m_isSmall = small;
    if (m_isSmall) {
        setMaximumSize(56, 74);
    } else {
        setMaximumSize(72, 90);
    }
    updateGeometry();
    update();
}

double RotaryKnob::realValue() const
{
    return (value() * m_scaleFactor) + m_offset;
}

void RotaryKnob::setRealValue(double val)
{
    setValue(qRound((val - m_offset) / m_scaleFactor));
}

QString RotaryKnob::formattedValue() const
{
    if (minimum() == 0 && maximum() == 1) {
        return value() > 0.5 ? "ON" : "OFF";
    }
    double rVal = realValue();
    if (m_unit == "dB") {
        if (value() == minimum() && minimum() < 0) {
            return "-inf";
        }
        return QString("%1%2%3").arg(rVal > 0 ? "+" : "").arg(rVal, 0, 'f', 1).arg(m_unit);
    } else if (m_unit == "Hz") {
        if (rVal >= 1000.0) {
            return QString("%1 k%2").arg(rVal / 1000.0, 0, 'f', 2).arg(m_unit);
        }
        return QString("%1 %2").arg(qRound(rVal)).arg(m_unit);
    } else if (m_unit == "%") {
        return QString("%1%2").arg(qRound(rVal)).arg(m_unit);
    } else if (m_unit == ":1") {
        if (rVal >= 100.0) {
            return QString::fromUtf8("∞:1");
        }
        return QString("%1:1").arg(rVal, 0, 'f', 1);
    }
    return QString("%1 %2").arg(rVal, 0, 'f', 1).arg(m_unit).trimmed();
}

QSize RotaryKnob::sizeHint() const
{
    return m_isSmall ? QSize(56, 74) : QSize(72, 90);
}

QSize RotaryKnob::minimumSizeHint() const
{
    return sizeHint();
}

void RotaryKnob::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    int width = this->width();
    int height = this->height();
    int side = qMin(width, height - (m_isSmall ? 24 : 28)); // Reserve space for labels at the bottom

    // Draw parameters
    int knobSize = side - (m_isSmall ? 8 : 10);
    if (m_isSmall && knobSize > 38) knobSize = 38;
    if (!m_isSmall && knobSize > 48) knobSize = 48;
    if (knobSize < (m_isSmall ? 12 : 20)) knobSize = (m_isSmall ? 12 : 20);
    QRectF knobRect((width - knobSize) / 2.0, m_isSmall ? 3.0 : 4.0, knobSize, knobSize);

    // 1. Draw outer background track arc
    // Angle: standard 270 degrees total, centered at the bottom.
    // Start angle 225 degrees (bottom left) to -45 degrees (bottom right).
    int startAngle = -135; // Qt coordinates: positive is CCW, so -135 is 225 degrees CCW from 3 o'clock (bottom-left)
    int spanAngle = -270;

    double penWidth = m_isSmall ? 2.5 : 3.5;
    QPen trackPen(QColor("#2d2d38"), penWidth, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(trackPen);
    painter.drawArc(knobRect.adjusted(-2, -2, 2, 2), startAngle * 16, spanAngle * 16);

    // 2. Draw active value arc
    double percent = 0.0;
    if (maximum() > minimum()) {
        percent = (double)(value() - minimum()) / (maximum() - minimum());
    }
    int activeSpan = qRound(percent * -270);

    QPen activePen(m_themeColor, penWidth, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(activePen);
    painter.drawArc(knobRect.adjusted(-2, -2, 2, 2), startAngle * 16, activeSpan * 16);

    // 3. Draw Knob body
    QRadialGradient knobGrad(knobRect.center(), knobRect.width() / 2.0, knobRect.center() + QPointF(-knobRect.width()/8.0, -knobRect.height()/8.0));
    knobGrad.setColorAt(0.0, QColor("#3e3e4a"));
    knobGrad.setColorAt(0.8, QColor("#1e1e24"));
    knobGrad.setColorAt(1.0, QColor("#141418"));
    
    painter.setPen(QPen(QColor("#0e0e12"), m_isSmall ? 1.0 : 1.5));
    painter.setBrush(knobGrad);
    painter.drawEllipse(knobRect);

    // 4. Draw pointer line
    double angleRad = (225.0 - percent * 270.0) * M_PI / 180.0;
    QPointF center = knobRect.center();
    double pointerLength = knobRect.width() / 2.0 - (m_isSmall ? 1.5 : 2);
    QPointF pointerEnd(
        center.x() + pointerLength * std::cos(angleRad),
        center.y() - pointerLength * std::sin(angleRad) // screen y decreases upwards, so subtract sin
    );

    QPen pointerPen(m_themeColor, m_isSmall ? 1.5 : 2.5, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pointerPen);
    painter.drawLine(center + (pointerEnd - center) * 0.4, pointerEnd);

    // 5. Draw inner cap detail
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 40));
    painter.drawEllipse(knobRect.center(), knobRect.width() / 5.0, knobRect.width() / 5.0);

    // 6. Draw Value Text (overlay in center of knob if label is empty, or above the knob)
    painter.setPen(QColor("#ffffff"));
    QFont font = painter.font();
    font.setFamily("Inter");
    font.setPixelSize(m_isSmall ? 9 : 10);
    font.setBold(true);
    painter.setFont(font);

    // Format value
    QString valStr = formattedValue();
    QRectF valRect(0, knobRect.bottom() + 2, width, m_isSmall ? 11 : 12);
    painter.drawText(valRect, Qt::AlignCenter, valStr);

    // 7. Draw Parameter Label (at the bottom)
    if (!m_label.isEmpty()) {
        painter.setPen(QColor("#a1a1aa"));
        font.setBold(false);
        font.setPixelSize(m_isSmall ? 9 : 10);
        painter.setFont(font);
        QRectF lblRect(0, valRect.bottom() + 1, width, m_isSmall ? 11 : 12);
        painter.drawText(lblRect, Qt::AlignCenter, m_label.toUpper());
    }
}

void RotaryKnob::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_lastMousePos = event->pos();
        m_dragging = true;
        setSliderDown(true);
        event->accept();
    }
}

void RotaryKnob::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        int diffY = m_lastMousePos.y() - event->pos().y(); // moving up increases value
        int diffX = event->pos().x() - m_lastMousePos.x(); // moving right increases value
        
        double diff = diffY + diffX / 2.0;
        
        int range = maximum() - minimum();
        double baseStep = (double)range / 1000.0;
        if (baseStep < 1.0) baseStep = 1.0;
        
        double absDiff = std::abs(diff);
        double velocityMult = 1.0 + (absDiff * 0.2);
        
        // Holding Shift, Control, or Alt overrides acceleration for fine control (1 step per pixel)
        if (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier)) {
            baseStep = 1.0;
            velocityMult = 1.0;
        }
        
        int delta = qRound(diff * baseStep * velocityMult);
        
        if (delta != 0) {
            int newVal = value() + delta;
            newVal = qBound(minimum(), newVal, maximum());
            setValue(newVal);
            m_lastMousePos = event->pos();
        }
        event->accept();
    }
}

void RotaryKnob::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setSliderDown(false);
        event->accept();
    }
}

void RotaryKnob::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setValue(qRound(m_defaultValue));
        event->accept();
    }
}
