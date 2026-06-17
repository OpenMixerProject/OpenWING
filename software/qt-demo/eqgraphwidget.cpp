#include "eqgraphwidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <cmath>
#include <cstdlib>

EqGraphWidget::EqGraphWidget(QWidget *parent)
    : QWidget(parent)
    , m_activeBandIndex(0)
    , m_hoveredBandIndex(-1)
    , m_dragging(false)
    , m_analyzerBins(40, 0.0)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    // Initialize 6 EQ Bands
    // Band 0: L - Low Shelf
    m_bands.append({80.0, 0.0, 1.0, true, QColor("#f43f5e"), 0});
    // Band 1: 1 - Peaking
    m_bands.append({250.0, 0.0, 1.5, true, QColor("#fbbf24"), 1});
    // Band 2: 2 - Peaking
    m_bands.append({600.0, 0.0, 1.5, true, QColor("#10b981"), 1});
    // Band 3: 3 - Peaking
    m_bands.append({2000.0, 0.0, 2.0, true, QColor("#06b6d4"), 1});
    // Band 4: 4 - Peaking
    m_bands.append({6000.0, 0.0, 1.5, true, QColor("#3b82f6"), 1});
    // Band 5: H - High Shelf
    m_bands.append({12000.0, 0.0, 1.0, true, QColor("#a855f7"), 3});

    // Start analyzer simulation timer (~30 FPS)
    m_timerId = startTimer(33);
}

void EqGraphWidget::setBand(int index, double frequency, double gain, double q, bool active)
{
    if (index >= 0 && index < m_bands.size()) {
        m_bands[index].frequency = qBound(20.0, frequency, 20000.0);
        m_bands[index].gain = qBound(-15.0, gain, 15.0);
        m_bands[index].q = qBound(0.5, q, 10.0);
        m_bands[index].active = active;
        update();
    }
}

void EqGraphWidget::setBandType(int index, int type)
{
    if (index >= 0 && index < m_bands.size()) {
        m_bands[index].type = type;
        update();
    }
}

void EqGraphWidget::setActiveBandIndex(int index)
{
    if (index >= 0 && index < m_bands.size() && m_activeBandIndex != index) {
        m_activeBandIndex = index;
        update();
        emit bandSelected(index);
    }
}

double EqGraphWidget::freqToX(double freq) const
{
    double logMin = std::log10(20.0);
    double logMax = std::log10(20000.0);
    double pct = (std::log10(freq) - logMin) / (logMax - logMin);
    return rect().x() + pct * rect().width();
}

double EqGraphWidget::xToFreq(double x) const
{
    double logMin = std::log10(20.0);
    double logMax = std::log10(20000.0);
    double pct = (x - rect().x()) / rect().width();
    pct = qBound(0.0, pct, 1.0);
    return std::pow(10.0, logMin + pct * (logMax - logMin));
}

double EqGraphWidget::gainToY(double gain) const
{
    // Linear gain mapping: +15dB is at top, -15dB at bottom
    double pct = (gain + 15.0) / 30.0; // 0.0 to 1.0
    return rect().y() + (1.0 - pct) * rect().height();
}

double EqGraphWidget::yToGain(double y) const
{
    double pct = 1.0 - (y - rect().y()) / rect().height();
    pct = qBound(0.0, pct, 1.0);
    return -15.0 + pct * 30.0;
}

double EqGraphWidget::getResponseAt(double freq) const
{
    double totalDb = 0.0;
    for (const auto &band : m_bands) {
        if (!band.active) continue;

        double f = freq;
        double f0 = band.frequency;
        double G = band.gain;
        double Q = band.q;

        if (band.type == 4) {
            // Low Cut / HPF (24 dB/octave Butterworth approximation)
            totalDb += -10.0 * std::log10(1.0 + std::pow(f0 / f, 8.0));
        } else if (band.type == 5) {
            // High Cut / LPF (24 dB/octave Butterworth approximation)
            totalDb += -10.0 * std::log10(1.0 + std::pow(f / f0, 8.0));
        } else if (band.type == 0) {
            // Low Shelf
            totalDb += G / (1.0 + std::pow(f / f0, 2.0));
        } else if (band.type == 3) {
            // High Shelf
            totalDb += G / (1.0 + std::pow(f0 / f, 2.0));
        } else {
            // Peaking / Parametric
            // Bandwidth response peak approximation
            double denom = 1.0 + std::pow(Q * (f / f0 - f0 / f), 2.0);
            totalDb += G / denom;
        }
    }
    return totalDb;
}

int EqGraphWidget::findHoveredBand(const QPoint &pos) const
{
    for (int i = 0; i < m_bands.size(); ++i) {
        double bx = freqToX(m_bands[i].frequency);
        double by = gainToY(m_bands[i].gain);
        double dist = std::hypot(pos.x() - bx, pos.y() - by);
        if (dist <= 12.0) {
            return i;
        }
    }
    return -1;
}

void EqGraphWidget::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_timerId) {
        // Simulate music analyzer frequency response (FFT)
        // Let's create a dynamic curve that peaks in the low-mids and rolls off at the high end
        for (int i = 0; i < m_analyzerBins.size(); ++i) {
            double r = (std::rand() % 100) / 100.0;
            double target = 0.0;

            // Generate music-like behavior (more bass/mids, random peaks)
            if (i < 8) {
                target = 0.2 + 0.6 * std::sin(i * 0.4) * r; // Bass
            } else if (i < 24) {
                target = 0.1 + 0.45 * std::cos((i - 12) * 0.15) * r; // Mids
            } else {
                target = 0.05 + 0.25 * r; // Highs
            }

            // Apply active EQ response to analyzer visual
            double binFreq = xToFreq(rect().x() + ((double)i / m_analyzerBins.size()) * rect().width());
            double eqResponse = getResponseAt(binFreq); // dB response
            double eqMultiplier = std::pow(10.0, eqResponse / 20.0); // convert dB to linear multiplier

            target = qBound(0.05, target * eqMultiplier, 0.95);
            m_analyzerBins[i] = m_analyzerBins[i] * 0.65 + target * 0.35; // smooth transition
        }
        update();
    } else {
        QWidget::timerEvent(event);
    }
}

void EqGraphWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();

    // 1. Draw Background
    painter.fillRect(rect(), QColor("#141419"));

    // 2. Draw Grid Lines and Labels
    painter.setPen(QPen(QColor("#23232c"), 1, Qt::SolidLine));
    
    // Horizontal dB lines (+15, +10, +5, 0, -5, -10, -15)
    QVector<double> dbLines = {15.0, 10.0, 5.0, 0.0, -5.0, -10.0, -15.0};
    QFont gridFont("Inter", 8);
    painter.setFont(gridFont);
    
    for (double db : dbLines) {
        int y = qRound(gainToY(db));
        if (db == 0.0) {
            painter.setPen(QPen(QColor("#3f3f4e"), 1.5, Qt::SolidLine));
        } else {
            painter.setPen(QPen(QColor("#23232c"), 1, Qt::DashLine));
        }
        painter.drawLine(0, y, w, y);
        
        // Label
        painter.setPen(QColor("#71717a"));
        painter.drawText(5, y - 3, QString("%1 dB").arg(db > 0 ? "+" : "") + QString::number(db));
    }

    // Vertical Logarithmic Frequency Lines (20, 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k)
    struct FreqLabel { double freq; QString text; };
    QVector<FreqLabel> freqLines = {
        {20.0, "20"}, {50.0, "50"}, {100.0, "100"}, {200.0, "200"}, {500.0, "500"},
        {1000.0, "1k"}, {2000.0, "2k"}, {5000.0, "5k"}, {10000.0, "10k"}, {20000.0, "20k"}
    };

    for (const auto &fl : freqLines) {
        int x = qRound(freqToX(fl.freq));
        painter.setPen(QPen(QColor("#23232c"), 1, Qt::DashLine));
        painter.drawLine(x, 0, x, h);

        // Label
        painter.setPen(QColor("#71717a"));
        painter.drawText(x + 3, h - 6, fl.text);
    }

    // 3. Draw Simulated FFT Analyzer Spectrum (background)
    painter.setPen(Qt::NoPen);
    QLinearGradient specGrad(0, h, 0, 0);
    specGrad.setColorAt(0, QColor(6, 182, 212, 10));  // Light Teal
    specGrad.setColorAt(0.7, QColor(6, 182, 212, 40));
    specGrad.setColorAt(1, QColor(251, 191, 36, 100)); // Glowing Yellow Top
    painter.setBrush(specGrad);

    int numBins = m_analyzerBins.size();
    double barW = (double)w / numBins;
    for (int i = 0; i < numBins; ++i) {
        double barH = m_analyzerBins[i] * h;
        painter.drawRect(QRectF(i * barW + 1, h - barH, barW - 2, barH));
    }

    // 4. Draw EQ Curve
    QPainterPath eqPath;
    bool pathStarted = false;
    for (int x = 0; x < w; ++x) {
        double freq = xToFreq(x);
        double response = getResponseAt(freq);
        double y = gainToY(response);

        if (!pathStarted) {
            eqPath.moveTo(x, y);
            pathStarted = true;
        } else {
            eqPath.lineTo(x, y);
        }
    }

    // Fill area under the curve (semi-transparent glow)
    QPainterPath fillPath = eqPath;
    fillPath.lineTo(w, gainToY(0.0));
    fillPath.lineTo(0, gainToY(0.0));
    fillPath.closeSubpath();
    
    QLinearGradient curveFillGrad(0, gainToY(15.0), 0, gainToY(-15.0));
    curveFillGrad.setColorAt(0, QColor(251, 191, 36, 40)); // Yellow
    curveFillGrad.setColorAt(0.5, QColor(6, 182, 212, 20)); // Teal
    curveFillGrad.setColorAt(1, QColor(6, 182, 212, 0));
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(curveFillGrad);
    painter.drawPath(fillPath);

    // Draw the main response curve stroke
    QPen curvePen(QColor("#fbbf24"), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); // Golden yellow line
    painter.setPen(curvePen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(eqPath);

    // 4.5 Draw Vertical Grid Lines for each active band
    for (int i = 0; i < m_bands.size(); ++i) {
        if (!m_bands[i].active) continue;
        double bx = freqToX(m_bands[i].frequency);
        bool isActive = (i == m_activeBandIndex);
        QColor lineCol = isActive ? m_bands[i].color : QColor("#3f3f4e");
        lineCol.setAlpha(isActive ? 150 : 60);
        painter.setPen(QPen(lineCol, isActive ? 1.5 : 1.0, Qt::SolidLine));
        painter.drawLine(QPointF(bx, 0), QPointF(bx, h));

        // Draw top label at top of the line
        painter.setPen(QColor("#ffffff"));
        QFont topLabelFont("Inter", 10, QFont::Bold);
        painter.setFont(topLabelFont);
        QString labelStr = (i == 0) ? "L" : (i == 5) ? "H" : QString::number(i);
        painter.drawText(QRectF(bx - 15, 8, 30, 16), Qt::AlignCenter, labelStr);
    }

    // 5. Draw Interactive Band Handles
    for (int i = 0; i < m_bands.size(); ++i) {
        const auto &band = m_bands[i];
        if (!band.active) continue;

        double bx = freqToX(band.frequency);
        double by = gainToY(band.gain);

        // Highlight active band (outer glow)
        if (i == m_activeBandIndex) {
            painter.setPen(QPen(band.color, 3.0));
        } else {
            painter.setPen(QPen(QColor("#71717a"), 2.0));
        }
        painter.setBrush(QColor("#141419"));
        painter.drawEllipse(QPointF(bx, by), 8, 8);

        // Draw inner dot when active
        if (i == m_activeBandIndex) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(band.color);
            painter.drawEllipse(QPointF(bx, by), 3.0, 3.0);
        }
    }
}

void EqGraphWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int idx = findHoveredBand(event->pos());
        if (idx != -1) {
            setActiveBandIndex(idx);
            m_dragging = true;
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void EqGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    // Update hovered band highlight
    int prevHovered = m_hoveredBandIndex;
    m_hoveredBandIndex = findHoveredBand(event->pos());
    if (m_hoveredBandIndex != prevHovered) {
        update();
    }

    if (m_dragging && m_activeBandIndex != -1) {
        double newFreq = xToFreq(event->pos().x());
        double newGain = yToGain(event->pos().y());
        
        int bType = m_bands[m_activeBandIndex].type;
        if (bType == 4 || bType == 5) {
            newGain = 0.0;
        }

        // Snap/bounds
        newFreq = qBound(20.0, newFreq, 20000.0);
        newGain = qBound(-15.0, newGain, 15.0);

        m_bands[m_activeBandIndex].frequency = newFreq;
        m_bands[m_activeBandIndex].gain = newGain;
        
        update();
        emit bandChanged(m_activeBandIndex, newFreq, newGain, m_bands[m_activeBandIndex].q);
        event->accept();
    } else {
        // Change cursor shape when hovering over a handle
        if (m_hoveredBandIndex != -1) {
            setCursor(Qt::PointingHandCursor);
        } else {
            unsetCursor();
        }
        QWidget::mouseMoveEvent(event);
    }
}

void EqGraphWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}
