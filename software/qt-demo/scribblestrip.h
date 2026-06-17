#ifndef SCRIBBLESTRIP_H
#define SCRIBBLESTRIP_H

#include <QWidget>
#include <QString>
#include <QColor>

class ScribbleStrip : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected)

public:
    explicit ScribbleStrip(QWidget *parent = nullptr);
    
    void setChannelInfo(const QString &number, const QString &label, const QColor &color);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    
    void setMuted(bool muted);
    void setSoloed(bool soloed);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_number;
    QString m_label;
    QColor m_color;
    bool m_selected;
    bool m_muted;
    bool m_soloed;
};

#endif // SCRIBBLESTRIP_H
