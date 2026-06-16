#ifndef PARAMETERBINDER_H
#define PARAMETERBINDER_H

#include <QObject>
#include <QVector>
#include <QMetaObject>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <cmath>
#include "audioparameter.h"
#include "rotaryknob.h"

class ParameterBinder : public QObject {
    Q_OBJECT
public:
    explicit ParameterBinder(QObject *parent = nullptr) 
        : QObject(parent)
        , m_param(nullptr)
        , m_widget(nullptr) 
    {}

    ~ParameterBinder() {
        unbind();
    }

    void bind(RotaryKnob *knob, AudioParameter *param) {
        unbind();
        m_widget = knob;
        m_param = param;
        if (!knob || !param) return;

        // Configure knob from parameter metadata
        // Note: we calculate ranges in slider values based on scaleFactor
        double scale = param->scaleFactor();
        int minSliderVal = qRound(param->min() / scale);
        int maxSliderVal = qRound(param->max() / scale);
        int defaultSliderVal = qRound(param->defaultValue() / scale);

        knob->setRange(minSliderVal, maxSliderVal);
        knob->setScaleFactor(scale);
        knob->setUnit(param->unit());
        knob->setLabel(param->name());
        knob->setThemeColor(param->color());
        knob->setDefaultValue(defaultSliderVal);
        
        // Set initial value
        knob->setRealValue(param->value());

        // Connect param -> knob
        m_connections << connect(param, &AudioParameter::valueChanged, this, [knob](double val) {
            if (qAbs(knob->realValue() - val) > 0.0001) {
                knob->setRealValue(val);
            }
        });

        // Connect knob -> param
        m_connections << connect(knob, &QSlider::valueChanged, this, [param, knob]() {
            double val = knob->realValue();
            if (qAbs(param->value() - val) > 0.0001) {
                param->setValue(val);
            }
        });
    }

    void bind(QSlider *slider, AudioParameter *param) {
        unbind();
        m_widget = slider;
        m_param = param;
        if (!slider || !param) return;

        double scale = param->scaleFactor();
        int minSliderVal = qRound(param->min() / scale);
        int maxSliderVal = qRound(param->max() / scale);

        slider->setRange(minSliderVal, maxSliderVal);
        slider->setValue(qRound(param->value() / scale));

        m_connections << connect(param, &AudioParameter::valueChanged, this, [slider, scale](double val) {
            int sliderVal = qRound(val / scale);
            if (slider->value() != sliderVal) {
                slider->setValue(sliderVal);
            }
        });

        m_connections << connect(slider, &QSlider::valueChanged, this, [param, scale](int val) {
            double realVal = val * scale;
            if (qAbs(param->value() - realVal) > 0.0001) {
                param->setValue(realVal);
            }
        });
    }

    void bind(QPushButton *button, AudioParameter *param) {
        unbind();
        m_widget = button;
        m_param = param;
        if (!button || !param) return;

        button->setChecked(param->value() > 0.5);

        m_connections << connect(param, &AudioParameter::valueChanged, this, [button](double val) {
            bool checked = (val > 0.5);
            if (button->isChecked() != checked) {
                button->setChecked(checked);
            }
        });

        m_connections << connect(button, &QPushButton::toggled, this, [param](bool checked) {
            double val = checked ? 1.0 : 0.0;
            if (qAbs(param->value() - val) > 0.0001) {
                param->setValue(val);
            }
        });
    }

    void bind(QComboBox *comboBox, AudioParameter *param) {
        unbind();
        m_widget = comboBox;
        m_param = param;
        if (!comboBox || !param) return;

        comboBox->setCurrentIndex(qRound(param->value()));

        m_connections << connect(param, &AudioParameter::valueChanged, this, [comboBox](double val) {
            int idx = qRound(val);
            if (comboBox->currentIndex() != idx) {
                comboBox->setCurrentIndex(idx);
            }
        });

        m_connections << connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [param](int idx) {
            if (qAbs(param->value() - idx) > 0.0001) {
                param->setValue(idx);
            }
        });
    }

    void unbind() {
        for (const auto &conn : m_connections) {
            disconnect(conn);
        }
        m_connections.clear();
        m_param = nullptr;
        m_widget = nullptr;
    }

private:
    AudioParameter *m_param;
    QWidget *m_widget;
    QVector<QMetaObject::Connection> m_connections;
};

#endif // PARAMETERBINDER_H
