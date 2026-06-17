#ifndef ROTARYKNOB_H
#define ROTARYKNOB_H

#include <QAbstractSlider>
#include <QColor>
#include <QString>
#include <QPoint>

class RotaryKnob : public QAbstractSlider {
    Q_OBJECT
    Q_PROPERTY(QColor themeColor READ themeColor WRITE setThemeColor)
    Q_PROPERTY(QString label READ label WRITE setLabel)
    Q_PROPERTY(QString unit READ unit WRITE setUnit)
    Q_PROPERTY(double scaleFactor READ scaleFactor WRITE setScaleFactor)
    Q_PROPERTY(double offset READ offset WRITE setOffset)
    Q_PROPERTY(double defaultValue READ defaultValue WRITE setDefaultValue)
    Q_PROPERTY(bool small READ isSmall WRITE setSmall)

public:
    explicit RotaryKnob(QWidget *parent = nullptr);
    explicit RotaryKnob(const QString &label, int min = 0, int max = 100, int defVal = 0, QWidget *parent = nullptr);

    QColor themeColor() const { return m_themeColor; }
    void setThemeColor(const QColor &color);

    QString label() const { return m_label; }
    void setLabel(const QString &label);

    QString unit() const { return m_unit; }
    void setUnit(const QString &unit);

    double scaleFactor() const { return m_scaleFactor; }
    void setScaleFactor(double factor);

    double offset() const { return m_offset; }
    void setOffset(double offset);

    double defaultValue() const { return m_defaultValue; }
    void setDefaultValue(double val);

    bool isSmall() const { return m_isSmall; }
    void setSmall(bool small);

    // Helper to get float/real value
    double realValue() const;
    void setRealValue(double val);

    QString formattedValue() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QColor m_themeColor;
    QString m_label;
    QString m_unit;
    double m_scaleFactor;
    double m_offset;
    double m_defaultValue;

    QPoint m_lastMousePos;
    bool m_dragging;
    bool m_isSmall;
};

#endif // ROTARYKNOB_H
