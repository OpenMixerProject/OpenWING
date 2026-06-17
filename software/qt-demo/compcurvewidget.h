#ifndef COMPCURVEWIDGET_H
#define COMPCURVEWIDGET_H

#include <QWidget>
#include <QColor>

// Interactive compression transfer-curve display.
//
// Plots the static input -> output transfer function for a downward
// compressor (input on the X axis, output on the Y axis, both in dB over a
// -60..0 range). A knee handle on the curve can be dragged horizontally to set
// the threshold and vertically to set the ratio. A simulated gain-reduction
// meter is drawn against the left edge.
class CompCurveWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor themeColor READ themeColor WRITE setThemeColor)

public:
    explicit CompCurveWidget(QWidget *parent = nullptr);

    QColor themeColor() const { return m_theme; }
    void setThemeColor(const QColor &c);

    void setThreshold(double db);
    void setRatio(double ratio);
    void setKnee(double db);
    void setMakeup(double db);
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
    double m_ratio;     // :1
    double m_knee;      // dB width
    double m_makeup;    // dB
    bool   m_active;

    bool m_dragging;

    // Simulated meter state
    double m_inputLevel;    // dB, animated probe level
    double m_grSmooth;      // dB of gain reduction (smoothed, >= 0)
    int    m_timerId;
};

#endif // COMPCURVEWIDGET_H
