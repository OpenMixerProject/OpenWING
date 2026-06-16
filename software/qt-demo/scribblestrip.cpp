#include "scribblestrip.h"
#include <QPainter>
#include <QPaintEvent>
#include <QLinearGradient>

ScribbleStrip::ScribbleStrip(QWidget *parent)
    : QWidget(parent)
    , m_number("01")
    , m_label("CH 01")
    , m_color(QColor("#00d1b2"))
    , m_selected(false)
    , m_muted(false)
    , m_soloed(false)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ScribbleStrip::setChannelInfo(const QString &number, const QString &label, const QColor &color)
{
    m_number = number;
    m_label = label;
    m_color = color;
    update();
}

void ScribbleStrip::setSelected(bool selected)
{
    if (m_selected != selected) {
        m_selected = selected;
        update();
    }
}

void ScribbleStrip::setMuted(bool muted)
{
    if (m_muted != muted) {
        m_muted = muted;
        update();
    }
}

void ScribbleStrip::setSoloed(bool soloed)
{
    if (m_soloed != soloed) {
        m_soloed = soloed;
        update();
    }
}

QSize ScribbleStrip::sizeHint() const
{
    return QSize(90, 42);
}

QSize ScribbleStrip::minimumSizeHint() const
{
    return QSize(70, 36);
}

void ScribbleStrip::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();

    // 1. Draw main screen background (dark glassmorphism)
    QLinearGradient bgGrad(0, 0, 0, h);
    bgGrad.setColorAt(0, QColor("#1e1e24"));
    bgGrad.setColorAt(1, QColor("#0d0d10"));
    painter.fillRect(rect(), bgGrad);

    // 2. Draw border
    if (m_selected) {
        painter.setPen(QPen(m_color, 2));
    } else {
        painter.setPen(QPen(QColor("#2d2d35"), 1));
    }
    painter.drawRect(0, 0, w - 1, h - 1);

    // 3. Draw Category Color Bar (at the top)
    painter.fillRect(QRect(1, 1, w - 2, 4), m_color);

    // 4. Draw Channel Number (Top left)
    QFont numFont("Inter", 8, QFont::Bold);
    painter.setFont(numFont);
    painter.setPen(QColor("#71717a"));
    painter.drawText(QRect(6, 6, w - 12, 12), Qt::AlignLeft | Qt::AlignVCenter, m_number);

    // 5. Draw Channel Name/Label (Center)
    QFont labelFont("Inter", 11, QFont::Bold);
    painter.setFont(labelFont);
    painter.setPen(m_color.lighter(130));
    painter.drawText(QRect(6, 15, w - 12, h - 20), Qt::AlignCenter, m_label);

    // 6. Draw Muted / Soloed Small Indicators
    if (m_muted) {
        painter.fillRect(QRect(w - 15, 6, 10, 10), QColor("#ef4444")); // Red block
        painter.setPen(QColor("#ffffff"));
        painter.setFont(QFont("Inter", 7, QFont::Bold));
        painter.drawText(QRect(w - 15, 5, 10, 10), Qt::AlignCenter, "M");
    } else if (m_soloed) {
        painter.fillRect(QRect(w - 15, 6, 10, 10), QColor("#fbbf24")); // Amber block
        painter.setPen(QColor("#000000"));
        painter.setFont(QFont("Inter", 7, QFont::Bold));
        painter.drawText(QRect(w - 15, 5, 10, 10), Qt::AlignCenter, "S");
    }
}
