#ifndef OSCCLIENT_H
#define OSCCLIENT_H

#include <QObject>
#include <QDebug>
#include <QMap>
#include <QMapIterator>
#include "mixerchannel.h"

class OscClient : public QObject {
    Q_OBJECT
public:
    explicit OscClient(const QList<MixerChannel*> &channels, QObject *parent = nullptr)
        : QObject(parent)
    {
        qDebug() << "[OSC CLIENT] Dummy client initialized. Listening to model updates for" << channels.size() << "channels...";
        
        for (int chIdx = 0; chIdx < channels.size(); ++chIdx) {
            MixerChannel *channel = channels[chIdx];
            QString chPath = QString("/ch/%1").arg(channel->number());
            
            // Connect to every parameter in this channel
            QMap<QString, AudioParameter*> params = channel->parameters();
            QMapIterator<QString, AudioParameter*> it(params);
            while (it.hasNext()) {
                it.next();
                QString paramKey = it.key();
                AudioParameter *param = it.value();
                
                // Construct OSC address, e.g. /ch/01/eq/1/freq or /ch/01/input/gain
                QString oscAddress = chPath;
                if (paramKey.startsWith("eq")) {
                    // eq1_freq -> /eq/1/freq
                    QStringList parts = paramKey.split('_');
                    QString bandNum = parts[0].mid(2); // get "1" from "eq1"
                    QString paramName = parts[1];
                    oscAddress += QString("/eq/%1/%2").arg(bandNum).arg(paramName);
                } else if (paramKey == "gain" || paramKey == "trim" || paramKey == "phantom" || paramKey == "phase" || paramKey.startsWith("locut") || paramKey.startsWith("hicut") || paramKey == "tilt") {
                    oscAddress += QString("/input/%1").arg(paramKey);
                } else if (paramKey.startsWith("gate") || paramKey.startsWith("comp")) {
                    QStringList parts = paramKey.split('_');
                    QString section = parts[0];
                    QStringList subparts = parts;
                    subparts.removeFirst();
                    QString paramName = subparts.join('_');
                    oscAddress += QString("/dyn/%1/%2").arg(section).arg(paramName);
                } else if (paramKey.startsWith("insert")) {
                    QStringList parts = paramKey.split('_');
                    QString slotNum = parts[0].mid(6); // get "1" from "insert1"
                    QString paramName = parts[1];
                    oscAddress += QString("/insert/%1/%2").arg(slotNum).arg(paramName);
                } else {
                    oscAddress += QString("/mix/%1").arg(paramKey);
                }
                
                // Bind to valueChanged
                connect(param, &AudioParameter::valueChanged, this, [oscAddress](double val) {
                    qDebug() << QString("[OSC CLIENT] SEND -> Address: %1 | Type: f | Value: %2")
                                .arg(oscAddress).arg(val);
                });
            }
        }
    }
};

#endif // OSCCLIENT_H
