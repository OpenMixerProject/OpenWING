#ifndef MIXERCHANNEL_H
#define MIXERCHANNEL_H

#include <QObject>
#include <QString>
#include <QColor>
#include <QMap>
#include <QVector>
#include "audioparameter.h"

class MixerChannel : public QObject {
    Q_OBJECT
public:
    MixerChannel(const QString &number, const QString &name, const QColor &color, QObject *parent = nullptr)
        : QObject(parent)
        , m_number(number)
        , m_name(name)
        , m_color(color)
    {
        // 1. Input/Filter parameters
        m_parameters["gain"] = new AudioParameter("Gain", 0.0, 60.0, 0.0, "dB", 0.5, QColor("#06b6d4"), this);
        m_parameters["trim"] = new AudioParameter("Trim", -12.0, 12.0, 0.0, "dB", 0.5, QColor("#06b6d4"), this);
        m_parameters["phantom"] = new AudioParameter("48V", 0.0, 1.0, 0.0, "", 1.0, QColor("#ef4444"), this);
        m_parameters["phase"] = new AudioParameter("Phase", 0.0, 1.0, 0.0, "", 1.0, QColor("#ea580c"), this);
        m_parameters["locut_active"] = new AudioParameter("LoCut On", 0.0, 1.0, 0.0, "", 1.0, QColor("#00d1b2"), this);
        m_parameters["locut_freq"] = new AudioParameter("Lo Freq", 20.0, 400.0, 80.0, "Hz", 1.0, QColor("#00d1b2"), this);
        m_parameters["hicut_active"] = new AudioParameter("HiCut On", 0.0, 1.0, 0.0, "", 1.0, QColor("#00d1b2"), this);
        m_parameters["hicut_freq"] = new AudioParameter("Hi Freq", 2000.0, 20000.0, 12000.0, "Hz", 1.0, QColor("#00d1b2"), this);
        m_parameters["tilt"] = new AudioParameter("Tilt", -6.0, 6.0, 0.0, "dB", 0.1, QColor("#10b981"), this);

        // 2. EQ parameters (6 bands: L, 1, 2, 3, 4, H)
        QVector<QColor> eqColors = {
            QColor("#f43f5e"), // L (Red)
            QColor("#fbbf24"), // 1 (Amber)
            QColor("#10b981"), // 2 (Green)
            QColor("#06b6d4"), // 3 (Cyan)
            QColor("#3b82f6"), // 4 (Blue)
            QColor("#a855f7")  // H (Purple)
        };
        QVector<double> eqFreqs = {80.0, 250.0, 600.0, 2000.0, 6000.0, 12000.0};
        m_parameters["eq_active"] = new AudioParameter("EQ On", 0.0, 1.0, 1.0, "", 1.0, QColor("#fbbf24"), this);
        m_parameters["eq_band_solo"] = new AudioParameter("Band Solo", 0.0, 1.0, 0.0, "", 1.0, QColor("#fbbf24"), this);
        m_parameters["eq1_type"] = new AudioParameter("EQ1 Type", 0.0, 2.0, 1.0, "", 1.0, eqColors[0], this);
        m_parameters["eq6_type"] = new AudioParameter("EQ6 Type", 0.0, 2.0, 1.0, "", 1.0, eqColors[5], this);
        for (int i = 0; i < 6; ++i) {
            QString prefix = QString("eq%1_").arg(i + 1);
            m_parameters[prefix + "active"] = new AudioParameter(QString("EQ%1 On").arg(i + 1), 0.0, 1.0, 1.0, "", 1.0, eqColors[i], this);
            m_parameters[prefix + "freq"] = new AudioParameter(QString("EQ%1 Freq").arg(i + 1), 20.0, 20000.0, eqFreqs[i], "Hz", 1.0, eqColors[i], this);
            m_parameters[prefix + "gain"] = new AudioParameter(QString("EQ%1 Gain").arg(i + 1), -15.0, 15.0, 0.0, "dB", 0.1, eqColors[i], this);
            m_parameters[prefix + "q"] = new AudioParameter(QString("EQ%1 Q").arg(i + 1), 0.5, 10.0, 1.0, "", 0.1, eqColors[i], this);
        }

        // 3. Dynamics parameters
        // Noise Gate
        m_parameters["gate_active"] = new AudioParameter("Gate On", 0.0, 1.0, 0.0, "", 1.0, QColor("#f97316"), this);
        m_parameters["gate_threshold"] = new AudioParameter("Gate Thr", -80.0, 0.0, -55.0, "dB", 1.0, QColor("#f97316"), this);
        m_parameters["gate_attack"] = new AudioParameter("Gate Att", 0.1, 500.0, 1.0, "ms", 0.1, QColor("#f97316"), this);
        m_parameters["gate_hold"] = new AudioParameter("Gate Hold", 10.0, 2000.0, 50.0, "ms", 1.0, QColor("#f97316"), this);
        m_parameters["gate_release"] = new AudioParameter("Gate Rel", 10.0, 4000.0, 250.0, "ms", 1.0, QColor("#f97316"), this);
        m_parameters["gate_depth"] = new AudioParameter("Gate Dpt", 0.0, 60.0, 40.0, "dB", 1.0, QColor("#f97316"), this);
        m_parameters["gate_ratio"] = new AudioParameter("Gate Ratio", 1.0, 100.0, 10.0, ":1", 0.1, QColor("#f97316"), this);
        m_parameters["gate_model"] = new AudioParameter("Gate Model", 0.0, 1.0, 0.0, "", 1.0, QColor("#f97316"), this);
        m_parameters["gate_key_source"] = new AudioParameter("Gate Key Src", 0.0, 3.0, 0.0, "", 1.0, QColor("#f97316"), this);
        m_parameters["gate_accent"] = new AudioParameter("Gate Accent", 0.0, 100.0, 0.0, "%", 1.0, QColor("#f97316"), this);
        m_parameters["gate_key_solo"] = new AudioParameter("Gate Key Solo", 0.0, 1.0, 0.0, "", 1.0, QColor("#f97316"), this);
        m_parameters["gate_key_filter"] = new AudioParameter("Gate Key Fltr", 0.0, 3.0, 0.0, "", 1.0, QColor("#f97316"), this);

        // Compressor
        m_parameters["comp_active"] = new AudioParameter("Comp On", 0.0, 1.0, 0.0, "", 1.0, QColor("#3b82f6"), this);
        m_parameters["comp_threshold"] = new AudioParameter("Threshold", -60.0, 0.0, -18.0, "dB", 1.0, QColor("#3b82f6"), this);
        m_parameters["comp_ratio"] = new AudioParameter("Ratio", 1.0, 100.0, 3.5, ":1", 0.1, QColor("#3b82f6"), this);
        m_parameters["comp_attack"] = new AudioParameter("Attack", 0.1, 200.0, 10.0, "ms", 0.1, QColor("#3b82f6"), this);
        m_parameters["comp_hold"] = new AudioParameter("Hold", 0.0, 1000.0, 0.0, "ms", 1.0, QColor("#3b82f6"), this);
        m_parameters["comp_release"] = new AudioParameter("Release", 10.0, 3000.0, 150.0, "ms", 1.0, QColor("#3b82f6"), this);
        m_parameters["comp_sc_locut"] = new AudioParameter("SC LoCut", 20.0, 1000.0, 20.0, "Hz", 1.0, QColor("#3b82f6"), this);
        m_parameters["comp_sc_hicut"] = new AudioParameter("SC HiCut", 200.0, 20000.0, 20000.0, "Hz", 1.0, QColor("#3b82f6"), this);
        // Compressor header / detector controls
        m_parameters["comp_mix"]        = new AudioParameter("Mix", 0.0, 100.0, 100.0, "%", 1.0, QColor("#3b82f6"), this);
        m_parameters["comp_gain"]       = new AudioParameter("Gain", -12.0, 12.0, 0.0, "dB", 0.1, QColor("#3b82f6"), this);
        m_parameters["comp_knee"]       = new AudioParameter("Knee", 0.0, 24.0, 6.0, "dB", 1.0, QColor("#3b82f6"), this);
        m_parameters["comp_key_source"] = new AudioParameter("Key Src", 0.0, 3.0, 0.0, "", 1.0, QColor("#3b82f6"), this);

        // 4. Mix / Level parameters
        m_parameters["level"] = new AudioParameter("Level", -80.0, 10.0, 0.0, "dB", 0.1, QColor("#ffffff"), this);
        m_parameters["pan"] = new AudioParameter("Pan", -50.0, 50.0, 0.0, "", 1.0, QColor("#10b981"), this);
        m_parameters["mute"] = new AudioParameter("Mute", 0.0, 1.0, 0.0, "", 1.0, QColor("#ef4444"), this);
        m_parameters["solo"] = new AudioParameter("Solo", 0.0, 1.0, 0.0, "", 1.0, QColor("#fbbf24"), this);

        // 5. Inserts parameters (up to 8 slots)
        m_numInsertPages = 3; // default to 3
        for (int i = 0; i < 8; ++i) {
            QString prefix = QString("insert%1_").arg(i + 1);
            m_parameters[prefix + "type"] = new AudioParameter(QString("Ins%1 Type").arg(i + 1), 0.0, 5.0, 0.0, "", 1.0, QColor("#a855f7"), this);
            m_parameters[prefix + "bypass"] = new AudioParameter(QString("Ins%1 Byp").arg(i + 1), 0.0, 1.0, 0.0, "", 1.0, QColor("#a855f7"), this);
            m_parameters[prefix + "mix"] = new AudioParameter(QString("Ins%1 Mix").arg(i + 1), 0.0, 100.0, 100.0, "%", 1.0, QColor("#a855f7"), this);
            m_parameters[prefix + "param1"] = new AudioParameter(QString("Ins%1 P1").arg(i + 1), 0.0, 100.0, 50.0, "", 1.0, QColor("#a855f7"), this);
            m_parameters[prefix + "param2"] = new AudioParameter(QString("Ins%1 P2").arg(i + 1), 0.0, 100.0, 50.0, "", 1.0, QColor("#a855f7"), this);

            // Connect type parameter to auto-configure slot/lambda
            connect(m_parameters[prefix + "type"], &AudioParameter::valueChanged, this, [this, i](double val) {
                configureInsertSlotParameters(i, qRound(val));
            });

            // Initial call to set defaults
            configureInsertSlotParameters(i, 0);
        }
    }

    QString number() const { return m_number; }
    QString name() const { return m_name; }
    QColor color() const { return m_color; }

    AudioParameter* parameter(const QString &key) const {
        return m_parameters.value(key, nullptr);
    }
    
    QMap<QString, AudioParameter*> parameters() const {
        return m_parameters;
    }

    int numInsertPages() const { return m_numInsertPages; }
    void setNumInsertPages(int count) {
        if (count < 1) count = 1;
        if (count > 8) count = 8;
        m_numInsertPages = count;
    }

    void configureInsertSlotParameters(int slotIdx, int effectType) {
        QString prefix = QString("insert%1_").arg(slotIdx + 1);
        AudioParameter *p1 = m_parameters.value(prefix + "param1");
        AudioParameter *p2 = m_parameters.value(prefix + "param2");
        if (!p1 || !p2) return;

        if (effectType == 1) { // EQ
            p1->setName("Freq");
            p1->setMin(20.0);
            p1->setMax(20000.0);
            p1->setDefaultValue(1000.0);
            p1->setUnit("Hz");
            p1->setScaleFactor(1.0);

            p2->setName("Gain");
            p2->setMin(-15.0);
            p2->setMax(15.0);
            p2->setDefaultValue(0.0);
            p2->setUnit("dB");
            p2->setScaleFactor(0.1);
        } else if (effectType == 2) { // Compressor
            p1->setName("Threshold");
            p1->setMin(-60.0);
            p1->setMax(0.0);
            p1->setDefaultValue(-18.0);
            p1->setUnit("dB");
            p1->setScaleFactor(1.0);

            p2->setName("Ratio");
            p2->setMin(1.0);
            p2->setMax(20.0);
            p2->setDefaultValue(3.5);
            p2->setUnit(":1");
            p2->setScaleFactor(0.1);
        } else if (effectType == 3) { // Delay
            p1->setName("Time");
            p1->setMin(10.0);
            p1->setMax(1000.0);
            p1->setDefaultValue(250.0);
            p1->setUnit("ms");
            p1->setScaleFactor(1.0);

            p2->setName("Feedback");
            p2->setMin(0.0);
            p2->setMax(99.0);
            p2->setDefaultValue(30.0);
            p2->setUnit("%");
            p2->setScaleFactor(1.0);
        } else if (effectType == 4) { // Reverb
            p1->setName("Decay");
            p1->setMin(1.0);
            p1->setMax(10.0);
            p1->setDefaultValue(2.5);
            p1->setUnit("s");
            p1->setScaleFactor(0.1);

            p2->setName("Damping");
            p2->setMin(0.0);
            p2->setMax(100.0);
            p2->setDefaultValue(20.0);
            p2->setUnit("%");
            p2->setScaleFactor(1.0);
        } else if (effectType == 5) { // Chorus
            p1->setName("Rate");
            p1->setMin(0.1);
            p1->setMax(10.0);
            p1->setDefaultValue(1.5);
            p1->setUnit("Hz");
            p1->setScaleFactor(0.1);

            p2->setName("Depth");
            p2->setMin(0.0);
            p2->setMax(100.0);
            p2->setDefaultValue(40.0);
            p2->setUnit("%");
            p2->setScaleFactor(1.0);
        } else { // None
            p1->setName("Param 1");
            p1->setMin(0.0);
            p1->setMax(100.0);
            p1->setDefaultValue(0.0);
            p1->setUnit("");
            p1->setScaleFactor(1.0);

            p2->setName("Param 2");
            p2->setMin(0.0);
            p2->setMax(100.0);
            p2->setDefaultValue(0.0);
            p2->setUnit("");
            p2->setScaleFactor(1.0);
        }
        
        // Clamp existing values to the new limits
        p1->setValue(p1->value());
        p2->setValue(p2->value());
    }

private:
    QString m_number;
    QString m_name;
    QColor m_color;
    QMap<QString, AudioParameter*> m_parameters;
    int m_numInsertPages;
};

#endif // MIXERCHANNEL_H
