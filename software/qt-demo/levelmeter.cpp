#include "levelmeter.h"
#include <QPainter>
#include <QPaintEvent>
#include <cstdlib>
#include <cmath>

LevelMeter::LevelMeter(QWidget *parent)
    : QWidget(parent)
    , m_stereo(false)
    , m_simulated(true)
    , m_leftDb(-60.0)
    , m_rightDb(-60.0)
    , m_leftPeakDb(-60.0)
    , m_rightPeakDb(-60.0)
    , m_leftPeakHold(0)
    , m_rightPeakHold(0)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_timerId = startTimer(33); // ~30 FPS
}

void LevelMeter::setStereo(bool stereo)
{
    if (m_stereo != stereo) {
        m_stereo = stereo;
        updateGeometry();
        update();
    }
}

void LevelMeter::setLevels(double leftDb, double rightDb)
{
    m_leftDb = qBound(-60.0, leftDb, 6.0);
    if (m_stereo) {
        m_rightDb = qBound(-60.0, rightDb == -99.0 ? leftDb : rightDb, 6.0);
    } else {
        m_rightDb = -60.0;
    }
    m_simulated = false; // Disable auto-simulation if driven externally
    update();
}

void LevelMeter::setSimulated(bool enabled)
{
    m_simulated = enabled;
}

QSize LevelMeter::sizeHint() const
{
    return QSize(m_stereo ? 45 : 28, 120);
}

QSize LevelMeter::minimumSizeHint() const
{
    return QSize(m_stereo ? 30 : 18, 50);
}

void LevelMeter::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_timerId) {
        // 1. Simulate levels if enabled
        if (m_simulated) {
            // Random walk simulation for a lively mixer interface
            double r = (std::rand() % 100) / 100.0;
            
            // Randomly jump or decay
            double targetLeft = m_leftDb;
            if (r > 0.85) {
                targetLeft = -30.0 + (std::rand() % 35); // -30dB to +5dB
            } else {
                targetLeft -= (r * 3.5); // decay
            }
            targetLeft = qBound(-60.0, targetLeft, 2.0);
            m_leftDb = m_leftDb * 0.7 + targetLeft * 0.3; // smooth

            if (m_stereo) {
                double r2 = (std::rand() % 100) / 100.0;
                double targetRight = m_rightDb;
                if (r2 > 0.85) {
                    targetRight = -30.0 + (std::rand() % 35);
                } else {
                    targetRight -= (r2 * 3.5);
                }
                targetRight = qBound(-60.0, targetRight, 2.0);
                m_rightDb = m_rightDb * 0.7 + targetRight * 0.3;
            }
        }

        // 2. Peak hold dynamics
        // Left Channel
        if (m_leftDb > m_leftPeakDb) {
            m_leftPeakDb = m_leftDb;
            m_leftPeakHold = 30; // hold for ~1 second
        } else {
            if (m_leftPeakHold > 0) {
                m_leftPeakHold--;
            } else {
                m_leftPeakDb -= 0.6; // slow decay
                if (m_leftPeakDb < -60.0) m_leftPeakDb = -60.0;
            }
        }

        // Right Channel
        if (m_stereo) {
            if (m_rightDb > m_rightPeakDb) {
                m_rightPeakDb = m_rightDb;
                m_rightPeakHold = 30;
            } else {
                if (m_rightPeakHold > 0) {
                    m_rightPeakHold--;
                } else {
                    m_rightPeakDb -= 0.6;
                    if (m_rightPeakDb < -60.0) m_rightPeakDb = -60.0;
                }
            }
        }

        update();
    } else {
        QWidget::timerEvent(event);
    }
}

void LevelMeter::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    
    // Draw background
    painter.fillRect(rect(), QColor("#111115"));

    int w = width();
    int h = height();

    if (w >= 30) {
        // Draw with dB scale!
        // For stereo: Left bar, scale in middle, Right bar
        if (m_stereo) {
            int scaleW = 16;
            int chW = (w - scaleW - 4) / 2;
            
            drawMeterChannel(painter, QRect(0, 0, chW, h), m_leftDb, m_leftPeakDb, m_leftPeakHold);
            drawScale(painter, QRect(chW + 1, 0, scaleW, h));
            drawMeterChannel(painter, QRect(chW + scaleW + 3, 0, chW, h), m_rightDb, m_rightPeakDb, m_rightPeakHold);
        } else {
            // For mono: scale on the left, bar on the right
            int scaleW = 16;
            int chW = w - scaleW - 2;
            drawScale(painter, QRect(0, 0, scaleW, h));
            drawMeterChannel(painter, QRect(scaleW + 2, 0, chW, h), m_leftDb, m_leftPeakDb, m_leftPeakHold);
        }
    } else {
        // Narrow draw (no text scale)
        if (m_stereo) {
            int chW = (w - 2) / 2;
            drawMeterChannel(painter, QRect(0, 0, chW, h), m_leftDb, m_leftPeakDb, m_leftPeakHold);
            drawMeterChannel(painter, QRect(chW + 2, 0, chW, h), m_rightDb, m_rightPeakDb, m_rightPeakHold);
        } else {
            drawMeterChannel(painter, QRect(0, 0, w, h), m_leftDb, m_leftPeakDb, m_leftPeakHold);
        }
    }
}

void LevelMeter::drawScale(QPainter &painter, const QRect &rect)
{
    painter.setPen(QColor("#71717a"));
    QFont font = painter.font();
    font.setFamily("Courier New");
    font.setPixelSize(8);
    font.setBold(true);
    painter.setFont(font);

    // List of dB levels to draw
    QVector<double> dbTicks = {4.0, 0.0, -6.0, -12.0, -18.0, -30.0, -45.0, -60.0};
    double minDb = -60.0;
    double maxDb = 4.0;
    double dbRange = maxDb - minDb;

    for (double tickDb : dbTicks) {
        double pct = (tickDb - minDb) / dbRange;
        int y = rect.y() + rect.height() - qRound(pct * rect.height());
        y = qBound(rect.y() + 4, y, rect.y() + rect.height() - 4);

        // Draw a tiny tick line in center
        painter.drawLine(rect.x(), y, rect.x() + 2, y);
        painter.drawLine(rect.x() + rect.width() - 2, y, rect.x() + rect.width(), y);

        // Draw text label centered horizontally in the scale area
        QString lbl = QString::number(qRound(tickDb));
        if (tickDb > 0) lbl = "+" + lbl;
        
        QRectF textRect(rect.x() + 1, y - 5, rect.width() - 2, 10);
        painter.drawText(textRect, Qt::AlignCenter, lbl);
    }
}

void LevelMeter::drawMeterChannel(QPainter &painter, const QRect &rect, double valueDb, double &peakDb, int &peakHoldCounter)
{
    Q_UNUSED(peakHoldCounter);
    
    int numSegments = 20;
    int spacing = 1;
    double segmentH = (double)(rect.height() - (spacing * (numSegments - 1))) / numSegments;

    // Define dB range
    double minDb = -60.0;
    double maxDb = 4.0;
    double dbRange = maxDb - minDb;

    for (int i = 0; i < numSegments; ++i) {
        // Segment Y positions, going from bottom (i=0) to top (i=numSegments-1)
        int segIdx = numSegments - 1 - i;
        double yPos = rect.y() + segIdx * (segmentH + spacing);
        
        // Calculate dB value representing this segment
        double segDb = minDb + ((double)i / (numSegments - 1)) * dbRange;
        bool isActive = (valueDb >= segDb);

        // Segment color: Red for high, Amber for mid, Green for low
        QColor color;
        if (segDb >= -3.0) {
            color = isActive ? QColor("#ef4444") : QColor("#451a1a"); // Red / Dark Red
        } else if (segDb >= -18.0) {
            color = isActive ? QColor("#f59e0b") : QColor("#4a320f"); // Amber / Dark Amber
        } else {
            color = isActive ? QColor("#10b981") : QColor("#113725"); // Green / Dark Green
        }

        painter.fillRect(QRectF(rect.x(), yPos, rect.width(), segmentH), color);
    }

    // Draw peak line
    if (peakDb > minDb) {
        double pct = (peakDb - minDb) / dbRange;
        int peakY = rect.y() + rect.height() - qRound(pct * rect.height());
        peakY = qBound(rect.y(), peakY, rect.y() + rect.height() - 2);

        QColor peakColor = (peakDb >= -3.0) ? QColor("#ef4444") : ((peakDb >= -18.0) ? QColor("#f59e0b") : QColor("#10b981"));
        painter.setPen(QPen(peakColor.lighter(130), 2));
        painter.drawLine(rect.x(), peakY, rect.x() + rect.width(), peakY);
    }
}
