#include "mainwindow.h"
#include <QPainter>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QStyleOptionSlider>

TouchDrawWidget::TouchDrawWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StaticContents);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Continuous repaint loop at ~60 FPS to measure rendering speed
    QTimer *renderTimer = new QTimer(this);
    connect(renderTimer, &QTimer::timeout, this, [this]() { update(); });
    renderTimer->start(16); // ~60 FPS
}

int TouchDrawWidget::getAndResetFrameCount()
{
    int count = m_frameCount;
    m_frameCount = 0;
    return count;
}

void TouchDrawWidget::clear()
{
    m_paths.clear();
    m_currentPath.clear();
    m_drawing = false;
    update();
}

void TouchDrawWidget::paintEvent(QPaintEvent *event)
{
    m_frameCount++;
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw background
    painter.fillRect(rect(), QColor("#101012"));

    // Draw grid lines
    painter.setPen(QPen(QColor("#1f1f24"), 1, Qt::DashLine));
    int gridSpacing = 40;
    for (int x = gridSpacing; x < width(); x += gridSpacing) {
        painter.drawLine(x, 0, x, height());
    }
    for (int y = gridSpacing; y < height(); y += gridSpacing) {
        painter.drawLine(0, y, width(), y);
    }

    // Draw paths
    QPen pen(QColor("#00d1b2"), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);

    for (const auto &path : m_paths) {
        for (int i = 1; i < path.size(); ++i) {
            painter.drawLine(path[i - 1], path[i]);
        }
    }

    if (m_drawing && m_currentPath.size() > 1) {
        for (int i = 1; i < m_currentPath.size(); ++i) {
            painter.drawLine(m_currentPath[i - 1], m_currentPath[i]);
        }
    }
}

void TouchDrawWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_drawing = true;
        m_currentPath.clear();
        m_currentPath.append(event->pos());
        update();
    }
}

void TouchDrawWidget::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && m_drawing) {
        m_currentPath.append(event->pos());
        update();
    }
}

void TouchDrawWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_drawing) {
        m_drawing = false;
        m_currentPath.append(event->pos());
        m_paths.append(m_currentPath);
        m_currentPath.clear();
        update();
    }
}


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
    applyStyleSheet();
    resize(1280, 800);

    // Timer to calculate and update FPS display every second
    QTimer *fpsTimer = new QTimer(this);
    connect(fpsTimer, &QTimer::timeout, this, &MainWindow::updateFps);
    fpsTimer->start(1000);
    m_fpsElapsedTimer.start();
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(15);

    // --- LEFT SIDE: Mixing Console Fader Deck ---
    QFrame *faderDeck = new QFrame(centralWidget);
    faderDeck->setObjectName("FaderDeck");
    faderDeck->setFrameShape(QFrame::StyledPanel);

    QHBoxLayout *faderLayout = new QHBoxLayout(faderDeck);
    faderLayout->setContentsMargins(10, 15, 10, 15);
    faderLayout->setSpacing(12);

    for (int i = 0; i < 8; ++i) {
        QFrame *channelStrip = new QFrame(faderDeck);
        channelStrip->setObjectName("ChannelStrip");

        QVBoxLayout *stripLayout = new QVBoxLayout(channelStrip);
        stripLayout->setContentsMargins(5, 5, 5, 5);
        stripLayout->setSpacing(8);

        // Fader name
        QLabel *chLabel = new QLabel(QString("CH %1").arg(i + 1, 2, 10, QChar('0')), channelStrip);
        chLabel->setAlignment(Qt::AlignCenter);
        chLabel->setObjectName("ChannelLabel");

        // Fader slider
        QSlider *fader = new QSlider(Qt::Vertical, channelStrip);
        fader->setObjectName("ChannelFader");
        fader->setRange(0, 100);
        fader->setValue(75); // Default position (~0dB)
        m_faders.append(fader);

        // dB Label
        QLabel *dbLabel = new QLabel("0.0 dB", channelStrip);
        dbLabel->setAlignment(Qt::AlignCenter);
        dbLabel->setObjectName("DbLabel");
        m_faderLabels.append(dbLabel);

        // Bind update slot
        connect(fader, &QSlider::valueChanged, this, [this, i](int val) {
            this->handleFaderChanged(val, i);
        });

        stripLayout->addWidget(chLabel);
        stripLayout->addWidget(fader, 1);
        stripLayout->addWidget(dbLabel);

        faderLayout->addWidget(channelStrip);

        // Initial setup for labels
        handleFaderChanged(fader->value(), i);
    }

    // --- RIGHT SIDE: Diagnostic Panel ---
    QFrame *diagPanel = new QFrame(centralWidget);
    diagPanel->setObjectName("DiagPanel");
    diagPanel->setFrameShape(QFrame::StyledPanel);

    QVBoxLayout *diagLayout = new QVBoxLayout(diagPanel);
    diagLayout->setContentsMargins(15, 15, 15, 15);
    diagLayout->setSpacing(12);

    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *titleLabel = new QLabel("OpenWING Touch Diagnostics", diagPanel);
    titleLabel->setObjectName("PanelTitle");
    titleLabel->setAlignment(Qt::AlignLeft);

    m_fpsLabel = new QLabel("FPS: --", diagPanel);
    m_fpsLabel->setObjectName("FpsCounter");
    m_fpsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(m_fpsLabel);

    QLabel *descLabel = new QLabel("Draw on the canvas below to test the touchscreen calibration and alignment.", diagPanel);
    descLabel->setObjectName("PanelDesc");
    descLabel->setWordWrap(true);

    m_drawWidget = new TouchDrawWidget(diagPanel);
    m_drawWidget->setObjectName("TouchCanvas");

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_clearButton = new QPushButton("Clear Drawing", diagPanel);
    m_clearButton->setObjectName("ClearBtn");
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::handleClearDraw);

    QPushButton *quitButton = new QPushButton("Exit Demo", diagPanel);
    quitButton->setObjectName("QuitBtn");
    connect(quitButton, &QPushButton::clicked, this, &QWidget::close);

    buttonLayout->addWidget(m_clearButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(quitButton);

    diagLayout->addLayout(headerLayout);
    diagLayout->addWidget(descLabel);
    diagLayout->addWidget(m_drawWidget, 1);
    diagLayout->addLayout(buttonLayout);

    mainLayout->addWidget(faderDeck, 3);
    mainLayout->addWidget(diagPanel, 4);
}

void MainWindow::handleFaderChanged(int value, int index)
{
    if (index < 0 || index >= m_faderLabels.size()) return;

    // Convert 0..100 to decibels (-60dB to +10dB)
    // Value 75 = 0dB
    double db = 0.0;
    if (value == 0) {
        m_faderLabels[index]->setText("-inf dB");
        return;
    } else if (value <= 75) {
        // Linear scale from -60dB (at value 1) to 0dB (at value 75)
        db = -60.0 + (value - 1) * (60.0 / 74.0);
    } else {
        // Linear scale from 0dB (at value 75) to +10dB (at value 100)
        db = (value - 75) * (10.0 / 25.0);
    }

    m_faderLabels[index]->setText(QString("%1 dB").arg(db, 0, 'f', 1));
}

void MainWindow::handleClearDraw()
{
    m_drawWidget->clear();
}

void MainWindow::applyStyleSheet()
{
    QString style = R"(
        /* Global Main Window */
        QMainWindow {
            background-color: #121214;
        }

        /* Panels */
        #FaderDeck, #DiagPanel {
            background-color: #1e1e24;
            border: 1px solid #2d2d35;
            border-radius: 8px;
        }

        #ChannelStrip {
            background-color: #161619;
            border: 1px solid #25252b;
            border-radius: 6px;
        }

        /* Typography */
        QLabel {
            color: #d1d5db;
            font-family: "Inter", "Helvetica", Arial, sans-serif;
            font-size: 13px;
        }

        #ChannelLabel {
            font-weight: bold;
            font-size: 14px;
            color: #ffffff;
            padding: 4px 0px;
        }

        #DbLabel {
            color: #00d1b2;
            font-family: monospace;
            font-size: 13px;
            font-weight: bold;
            padding: 4px 0px;
        }

        #PanelTitle {
            font-size: 18px;
            font-weight: bold;
            color: #ffffff;
        }

        #FpsCounter {
            color: #00d1b2; /* neon teal */
            font-family: monospace;
            font-weight: bold;
            font-size: 15px;
            padding-right: 5px;
        }

        #PanelDesc {
            color: #9ca3af;
            font-size: 13px;
        }

        /* Sliders (Faders) */
        QSlider::groove:vertical {
            background: #272730;
            width: 8px;
            border-radius: 4px;
        }

        QSlider::sub-page:vertical {
            background: #272730;
            border-radius: 4px;
        }

        QSlider::add-page:vertical {
            background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                        stop: 0 #00d1b2, stop: 1 #008f7a);
            border-radius: 4px;
        }

        QSlider::handle:vertical {
            background: #ffffff;
            border: 2px solid #00d1b2;
            height: 20px;
            width: 36px;
            margin: 0 -14px;
            border-radius: 5px;
        }

        QSlider::handle:vertical:hover {
            background: #e6fffa;
            border-color: #39ffd0;
        }

        /* Canvas Widget border */
        #TouchCanvas {
            border: 2px solid #2d2d35;
            border-radius: 4px;
        }

        /* Buttons */
        QPushButton {
            background-color: #2a2a35;
            color: #ffffff;
            border: 1px solid #3e3e4f;
            border-radius: 4px;
            padding: 8px 16px;
            font-weight: bold;
            font-size: 13px;
        }

        QPushButton:hover {
            background-color: #3a3a4a;
            border-color: #00d1b2;
        }

        QPushButton:pressed {
            background-color: #1a1a24;
        }

        #QuitBtn {
            background-color: #3b2025;
            border: 1px solid #5a3038;
        }

        #QuitBtn:hover {
            background-color: #552830;
            border-color: #ef4444;
        }
    )";
    setStyleSheet(style);
}

void MainWindow::updateFps()
{
    int frames = m_drawWidget->getAndResetFrameCount();
    qint64 elapsed = m_fpsElapsedTimer.restart();
    if (elapsed > 0) {
        double fps = (frames * 1000.0) / elapsed;
        m_fpsLabel->setText(QString("FPS: %1").arg(qRound(fps)));
    }
}
