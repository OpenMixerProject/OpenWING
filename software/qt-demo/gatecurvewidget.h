#ifndef GATECURVEWIDGET_H
#define GATECURVEWIDGET_H

#include <QWidget>
#include <QColor>

// Interactive gate transfer-curve display.
//
// Plots the static input -> output transfer function for a noise gate/expander
// (input on the X axis, output on the Y axis, both in dB over a -80..0 range).
// A handle at the knee and a horizontal slider handle on the right can be dragged
// vertically/diagonally to set the threshold. A simulated gain-reduction
// meter is drawn against the left edge.
class GateCurveWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor themeColor READ themeColor WRITE setThemeColor)

public:
    explicit GateCurveWidget(QWidget *parent = nullptr);

    QColor themeColor() const { return m_theme; }
    void setThemeColor(const QColor &c);

    void setThreshold(double db);
    void setRatio(double ratio);
    void setDepth(double db);
    void setActive(bool active);

signals:
    void thresholdChanged(double db);
    void ratioChanged(double ratio);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    QRect plotRect() const;
    double dbToX(double db) const;
    double dbToY(double db) const;
    double xToDb(double x) const;
    double yToDb(double y) const;
    double transfer(double inDb) const; // input dB -> output dB

    QColor m_theme;
    double m_threshold; // dB
    double m_ratio;     // expansion ratio below threshold
    double m_depth;     // dB
    bool   m_active;

    bool m_draggingKnee;
    bool m_draggingSlider;

    // Simulated meter state
    double m_inputLevel;    // dB, animated probe level
    double m_grSmooth;      // dB of gain reduction (smoothed, >= 0)
    int    m_timerId;
};

#endif // GATECURVEWIDGET_H
