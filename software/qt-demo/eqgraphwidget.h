#ifndef EQGRAPHWIDGET_H
#define EQGRAPHWIDGET_H

#include <QWidget>
#include <QColor>
#include <QPointF>
#include <QVector>

struct EqBand {
    double frequency; // 20.0 to 20000.0 Hz
    double gain;      // -15.0 to +15.0 dB
    double q;         // 0.5 to 10.0
    bool active;
    QColor color;
    int type;         // 0: Low Shelf, 1: Peaking, 2: Peaking, 3: High Shelf
};

class EqGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit EqGraphWidget(QWidget *parent = nullptr);

    void setBand(int index, double frequency, double gain, double q, bool active = true);
    void setBandType(int index, int type);
    EqBand band(int index) const { return m_bands[index]; }
    
    int activeBandIndex() const { return m_activeBandIndex; }
    void setActiveBandIndex(int index);

signals:
    void bandChanged(int index, double frequency, double gain, double q);
    void bandSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    double freqToX(double freq) const;
    double xToFreq(double x) const;
    double gainToY(double gain) const;
    double yToGain(double y) const;
    
    double getResponseAt(double freq) const;
    int findHoveredBand(const QPoint &pos) const;

    QVector<EqBand> m_bands;
    int m_activeBandIndex;
    int m_hoveredBandIndex;
    bool m_dragging;
    
    // Analyzer simulation data
    QVector<double> m_analyzerBins;
    int m_timerId;
};

#endif // EQGRAPHWIDGET_H
