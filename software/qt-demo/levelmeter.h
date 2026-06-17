#ifndef LEVELMETER_H
#define LEVELMETER_H

#include <QWidget>
#include <QTimer>

class LevelMeter : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool stereo READ isStereo WRITE setStereo)
    Q_PROPERTY(bool simulated READ isSimulated WRITE setSimulated)

public:
    explicit LevelMeter(QWidget *parent = nullptr);
    
    void setStereo(bool stereo);
    bool isStereo() const { return m_stereo; }

    void setLevels(double leftDb, double rightDb = -99.0);
    void setSimulated(bool enabled);
    bool isSimulated() const { return m_simulated; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    void drawMeterChannel(QPainter &painter, const QRect &rect, double valueDb, double &peakDb, int &peakHoldCounter);
    void drawScale(QPainter &painter, const QRect &rect);

    bool m_stereo;
    bool m_simulated;
    
    double m_leftDb;
    double m_rightDb;
    
    double m_leftPeakDb;
    double m_rightPeakDb;
    
    int m_leftPeakHold;
    int m_rightPeakHold;
    
    int m_timerId;
};

#endif // LEVELMETER_H
