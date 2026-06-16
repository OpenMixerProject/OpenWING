#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QList>
#include <QPoint>
#include <QSlider>
#include <QLabel>
#include <QPushButton>

class TouchDrawWidget : public QWidget {
    Q_OBJECT
public:
    explicit TouchDrawWidget(QWidget *parent = nullptr);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QList<QList<QPoint>> m_paths;
    QList<QPoint> m_currentPath;
    bool m_drawing = false;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void handleFaderChanged(int value, int index);
    void handleClearDraw();

private:
    void setupUI();
    void applyStyleSheet();

    // UI elements
    QList<QSlider*> m_faders;
    QList<QLabel*> m_faderLabels;
    TouchDrawWidget *m_drawWidget;
    QPushButton *m_clearButton;
};

#endif // MAINWINDOW_H
