#ifndef AUDIOPARAMETER_H
#define AUDIOPARAMETER_H

#include <QObject>
#include <QString>
#include <QColor>
#include <QtGlobal>

class AudioParameter : public QObject {
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged)

public:
    AudioParameter(const QString &name, double min, double max, double defaultValue, 
                   const QString &unit, double scaleFactor = 1.0, const QColor &color = QColor("#00d1b2"),
                   QObject *parent = nullptr)
        : QObject(parent)
        , m_name(name)
        , m_min(min)
        , m_max(max)
        , m_value(defaultValue)
        , m_defaultValue(defaultValue)
        , m_unit(unit)
        , m_scaleFactor(scaleFactor)
        , m_color(color)
    {}

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    double min() const { return m_min; }
    void setMin(double min) { m_min = min; }

    double max() const { return m_max; }
    void setMax(double max) { m_max = max; }

    double defaultValue() const { return m_defaultValue; }
    void setDefaultValue(double val) { m_defaultValue = val; }

    QString unit() const { return m_unit; }
    void setUnit(const QString &unit) { m_unit = unit; }

    double scaleFactor() const { return m_scaleFactor; }
    void setScaleFactor(double sf) { m_scaleFactor = sf; }

    QColor color() const { return m_color; }
    void setColor(const QColor &color) { m_color = color; }

    double value() const { return m_value; }
    void setValue(double val) {
        if (val < m_min) val = m_min;
        if (val > m_max) val = m_max;
        
        // Use a threshold for double comparison to avoid floating-point noise
        if (qAbs(m_value - val) > 0.0001) {
            m_value = val;
            emit valueChanged(m_value);
        }
    }

signals:
    void valueChanged(double newValue);

private:
    QString m_name;
    double m_min;
    double m_max;
    double m_value;
    double m_defaultValue;
    QString m_unit;
    double m_scaleFactor;
    QColor m_color;
};

#endif // AUDIOPARAMETER_H
