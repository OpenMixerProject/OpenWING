#ifndef ENVELOPEGRAPHWIDGET_H
#define ENVELOPEGRAPHWIDGET_H

#include <QWidget>
#include <QColor>

// Stylised attack / release envelope display.
//
// Draws a wedge whose left slope represents the attack time and right slope
// the release time. The two bottom handles can be dragged horizontally to set
// attack (left) and release (right); the apex marks the trigger point.
class EnvelopeGraphWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor themeColor READ themeColor WRITE setThemeColor)

public:
    explicit EnvelopeGraphWidget(QWidget *parent = nullptr);

    QColor themeColor() const { return m_theme; }
    void setThemeColor(const QColor &c);

    void setAttack(double ms);
    void setRelease(double ms);
    void setHold(double ms);
    void setActive(bool active);

signals:
    void attackChanged(double ms);
    void releaseChanged(double ms);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRect plotRect() const;
    double attackNorm() const; // 0..1 (log mapped)
    double releaseNorm() const;
    QPointF leftHandle() const;
    QPointF rightHandle() const;
    QPointF apex() const;

    double m_attack;   // ms
    double m_release;  // ms
    double m_hold;     // ms
    bool   m_active;
    QColor m_theme;

    int m_drag; // 0 none, 1 attack, 2 release

    static constexpr double kAttMin = 0.1;
    static constexpr double kAttMax = 200.0;
    static constexpr double kRelMin = 10.0;
    static constexpr double kRelMax = 3000.0;
    static constexpr double kHoldMin = 0.0;
    static constexpr double kHoldMax = 1000.0;
};

#endif // ENVELOPEGRAPHWIDGET_H
