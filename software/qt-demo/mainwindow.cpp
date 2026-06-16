#include "mainwindow.h"
#include <QPainter>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QStyleOptionSlider>
#include <QTime>
#include <QCoreApplication>
#include <QScrollArea>
#include <QStyle>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_selectedChannelIndex(0)
    , m_activeEqBandIndex(0)
    , m_activeInsertSlotIndex(0)
{
    // 1. Initialize binders vectors
    for (int i = 0; i < 6; ++i) {
        m_bottomEncBinders.append(new ParameterBinder(this));
    }
    for (int i = 0; i < 8; ++i) {
        m_physFaderBinders.append(new ParameterBinder(this));
        m_physMuteBinders.append(new ParameterBinder(this));
        m_physSoloBinders.append(new ParameterBinder(this));
    }

    // 2. Initialize channel model database
    initChannelModel();

    // 3. Setup OSC client to listen to the model parameters
    m_oscClient = new OscClient(m_channels, this);

    // 4. Setup UI components and layout
    setupUI();

    // 5. Populate detail panel binders with the first channel's state
    populateUIFromSelectedChannel();

    // 6. Set window parameters
    resize(1280, 850);
    setWindowTitle("OpenWING Control Console (MVVM Bindings & OSC Demo)");

    // 7. Start clock timer
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer->start(1000);
    updateClock();

    // Apply QSS global stylesheets
    applyStyleSheet();
}

MainWindow::~MainWindow()
{
    qDeleteAll(m_bottomEncBinders);
    qDeleteAll(m_physFaderBinders);
    qDeleteAll(m_physMuteBinders);
    qDeleteAll(m_physSoloBinders);

    for (auto *ch : m_channels) {
        delete ch;
    }
    m_channels.clear();
}

void MainWindow::initChannelModel()
{
    struct ChSetup {
        QString num; QString name; QString colorHex; double gain; double fader;
    };
    
    QList<ChSetup> setups = {
        {"01", "KICK IN",   "#ec4899", 34.0, 0.0}, // Magenta (Drums)
        {"02", "SNARE TOP", "#ec4899", 28.0, -2.0}, // Magenta (Drums)
        {"03", "HI-HAT",    "#ec4899", 16.0, -12.0}, // Magenta (Drums)
        {"04", "BASS DI",   "#f97316", 22.0, 0.0}, // Orange (Bass)
        {"05", "GTR AC",    "#10b981", 24.0, -6.0}, // Green (Acoustic Gtr)
        {"06", "GTR EL",    "#10b981", 18.0, -3.0}, // Green (Electric Gtr)
        {"07", "LEAD VOC",  "#8b5cf6", 36.0, 2.0}, // Purple (Vocal)
        {"08", "BACK VOC",  "#8b5cf6", 34.0, -4.0}  // Purple (Vocal)
    };

    for (const auto &setup : setups) {
        MixerChannel *ch = new MixerChannel(setup.num, setup.name, QColor(setup.colorHex), this);
        
        // Load custom setup configuration values into the parameter models
        ch->parameter("gain")->setValue(setup.gain);
        ch->parameter("level")->setValue(setup.fader);
        
        // Condenser / Vocal mics get phantom power default
        if (setup.num == "01" || setup.num == "07" || setup.num == "08") {
            ch->parameter("phantom")->setValue(1.0);
        }
        
        // Cut low-end on non-bass channels
        if (setup.num != "01" && setup.num != "04") {
            ch->parameter("locut_active")->setValue(1.0);
            ch->parameter("locut_freq")->setValue(setup.num == "02" ? 80.0 : (setup.num == "03" ? 150.0 : 100.0));
        }

        // Bass/Kick EQ setups
        if (setup.num == "01" || setup.num == "04") {
            ch->parameter("eq1_gain")->setValue(3.0); // Boost bass shelf
        }
        ch->parameter("eq2_gain")->setValue(-3.0); // Cut boxiness
        if (setup.num == "01" || setup.num == "07") {
            ch->parameter("eq3_gain")->setValue(4.0); // Add click/articulation
        }
        if (setup.num == "07" || setup.num == "08") {
            ch->parameter("eq4_gain")->setValue(2.5); // Air boost
        }

        // Dynamics configs
        if (setup.num == "01" || setup.num == "02") {
            ch->parameter("gate_active")->setValue(1.0);
        }
        if (setup.num == "04" || setup.num == "07") {
            ch->parameter("comp_active")->setValue(1.0);
        }

        // Set up listener to light up CLR SOLO button if any channel has active solo
        connect(ch->parameter("solo"), &AudioParameter::valueChanged, this, [this]() {
            bool anySolo = false;
            for (auto *c : m_channels) {
                if (c->parameter("solo")->value() > 0.5) {
                    anySolo = true;
                }
            }
            m_clrSoloButton->setEnabled(anySolo);
            if (anySolo) {
                m_clrSoloButton->setStyleSheet("background-color: #fbbf24; color: #000000; font-weight: bold; border-color: #f59e0b;");
            } else {
                m_clrSoloButton->setStyleSheet("");
            }
        });

        m_channels.append(ch);
    }
}

void MainWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // =========================================================================
    //   UPPER PANEL: WING TOUCHSCREEN SIMULATOR
    // =========================================================================
    QFrame *screenBezel = new QFrame(centralWidget);
    screenBezel->setObjectName("ScreenBezel");
    QVBoxLayout *screenLayout = new QVBoxLayout(screenBezel);
    screenLayout->setContentsMargins(8, 8, 8, 8);
    screenLayout->setSpacing(0);

    QFrame *screenContent = new QFrame(screenBezel);
    screenContent->setObjectName("ScreenContent");
    QVBoxLayout *screenContentLayout = new QVBoxLayout(screenContent);
    screenContentLayout->setContentsMargins(0, 0, 0, 0);
    screenContentLayout->setSpacing(0);

    // 1. Screen Top Bar
    QFrame *topBar = new QFrame(screenContent);
    topBar->setObjectName("ScreenTopBar");
    QHBoxLayout *topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(10, 5, 10, 5);
    topBarLayout->setSpacing(10);

    m_homeButton = new QPushButton("HOME", topBar);
    m_homeButton->setObjectName("ScreenHomeBtn");
    connect(m_homeButton, &QPushButton::clicked, this, [this]() { switchScreenPage(0); });

    m_selectedChannelIndicator = new QLabel("01 KICK IN", topBar);
    m_selectedChannelIndicator->setObjectName("ScreenChannelIndicator");

    m_clockLabel = new QLabel("12:00:00", topBar);
    m_clockLabel->setObjectName("ScreenClock");

    m_clrSoloButton = new QPushButton("CLR SOLO", topBar);
    m_clrSoloButton->setObjectName("ScreenClrSoloBtn");
    m_clrSoloButton->setEnabled(false);
    connect(m_clrSoloButton, &QPushButton::clicked, this, &MainWindow::clearAllSolos);

    m_mainLRMeter = new LevelMeter(topBar);
    m_mainLRMeter->setObjectName("ScreenMainMeter");
    m_mainLRMeter->setStereo(true);
    m_mainLRMeter->setSimulated(true);

    topBarLayout->addWidget(m_homeButton);
    topBarLayout->addWidget(m_selectedChannelIndicator);
    topBarLayout->addStretch(1);
    topBarLayout->addWidget(m_clockLabel);
    topBarLayout->addWidget(m_clrSoloButton);
    topBarLayout->addWidget(m_mainLRMeter);

    // 2. Middle Area: Left sidebar menu + Stacked display pages
    QFrame *screenMiddle = new QFrame(screenContent);
    screenMiddle->setObjectName("ScreenMiddle");
    QHBoxLayout *screenMiddleLayout = new QHBoxLayout(screenMiddle);
    screenMiddleLayout->setContentsMargins(0, 0, 0, 0);
    screenMiddleLayout->setSpacing(0);

    QFrame *leftMenu = new QFrame(screenMiddle);
    leftMenu->setObjectName("ScreenLeftMenu");
    QVBoxLayout *leftMenuLayout = new QVBoxLayout(leftMenu);
    leftMenuLayout->setContentsMargins(0, 0, 0, 0);
    leftMenuLayout->setSpacing(4);

    m_menuBtnOverview = new QPushButton("OVERVIEW", leftMenu);
    m_menuBtnInput    = new QPushButton("INPUT", leftMenu);
    m_menuBtnGate     = new QPushButton("GATE", leftMenu);
    m_menuBtnComp     = new QPushButton("COMP", leftMenu);
    m_menuBtnEq       = new QPushButton("EQ", leftMenu);
    m_menuBtnMain     = new QPushButton("MAIN", leftMenu);
    m_menuBtnInserts  = new QPushButton("INSERTS", leftMenu);

    m_menuButtons << m_menuBtnOverview << m_menuBtnInput << m_menuBtnGate << m_menuBtnComp << m_menuBtnEq << m_menuBtnMain << m_menuBtnInserts;
    
    for (int i = 0; i < m_menuButtons.size(); ++i) {
        m_menuButtons[i]->setObjectName("LeftMenuBtn");
        m_menuButtons[i]->setCheckable(true);
        connect(m_menuButtons[i], &QPushButton::clicked, this, [this, i]() {
            switchScreenPage(i);
        });
        leftMenuLayout->addWidget(m_menuButtons[i]);
    }
    leftMenuLayout->addStretch(1);

    m_screenStack = new QStackedWidget(screenMiddle);
    m_screenStack->setObjectName("ScreenStack");

    // ----- PAGE 0: OVERVIEW -----
    m_pageOverview = new QWidget(m_screenStack);
    QGridLayout *ovLayout = new QGridLayout(m_pageOverview);
    ovLayout->setContentsMargins(15, 15, 15, 15);
    ovLayout->setSpacing(12);

    QStringList cardTitles = {
        "INPUT & FILTERS", "NOISE GATE", "COMPRESSOR",
        "PARAMETRIC EQ", "MIX & OUTPUT", "CHANNEL INSERTS"
    };
    QVector<int> cardTargets = {1, 2, 3, 4, 5, 6};
    
    for (int i = 0; i < 6; ++i) {
        QFrame *card = new QFrame(m_pageOverview);
        card->setObjectName("OverviewCard");
        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(4);
        
        QLabel *title = new QLabel(cardTitles[i], card);
        title->setObjectName("CardTitle");
        cardLayout->addWidget(title);
        cardLayout->addSpacing(4);

        if (i == 0) { // Input & Filters
            m_ovInputGain = new QLabel(card); m_ovInputGain->setObjectName("CardDesc");
            m_ovInputFilters = new QLabel(card); m_ovInputFilters->setObjectName("CardDesc");
            m_ovInputTilt = new QLabel(card); m_ovInputTilt->setObjectName("CardDesc");
            cardLayout->addWidget(m_ovInputGain);
            cardLayout->addWidget(m_ovInputFilters);
            cardLayout->addWidget(m_ovInputTilt);
        } else if (i == 1) { // Noise Gate
            m_ovGateState = new QLabel(card); m_ovGateState->setObjectName("CardDesc");
            m_ovGateParams = new QLabel(card); m_ovGateParams->setObjectName("CardDesc");
            cardLayout->addWidget(m_ovGateState);
            cardLayout->addWidget(m_ovGateParams);
        } else if (i == 2) { // Compressor
            m_ovCompState = new QLabel(card); m_ovCompState->setObjectName("CardDesc");
            m_ovCompParams = new QLabel(card); m_ovCompParams->setObjectName("CardDesc");
            cardLayout->addWidget(m_ovCompState);
            cardLayout->addWidget(m_ovCompParams);
        } else if (i == 3) { // Parametric EQ
            for (int b = 0; b < 4; ++b) {
                m_ovEqBands[b] = new QLabel(card); m_ovEqBands[b]->setObjectName("CardDesc");
                cardLayout->addWidget(m_ovEqBands[b]);
            }
        } else if (i == 4) { // Mix & Output
            m_ovMixLevel = new QLabel(card); m_ovMixLevel->setObjectName("CardDesc");
            m_ovMixPan = new QLabel(card); m_ovMixPan->setObjectName("CardDesc");
            m_ovMixMuteSolo = new QLabel(card); m_ovMixMuteSolo->setObjectName("CardDesc");
            cardLayout->addWidget(m_ovMixLevel);
            cardLayout->addWidget(m_ovMixPan);
            cardLayout->addWidget(m_ovMixMuteSolo);
        } else if (i == 5) { // Channel Inserts
            m_ovInsertsCount = new QLabel(card); m_ovInsertsCount->setObjectName("CardDesc");
            cardLayout->addWidget(m_ovInsertsCount);
            for (int s = 0; s < 4; ++s) {
                m_ovInsertSlots[s] = new QLabel(card); m_ovInsertSlots[s]->setObjectName("CardDesc");
                cardLayout->addWidget(m_ovInsertSlots[s]);
            }
        }

        cardLayout->addStretch(1);

        int target = cardTargets[i];
        QPushButton *actionBtn = new QPushButton("OPEN PANEL", card);
        actionBtn->setObjectName("CardOpenBtn");
        connect(actionBtn, &QPushButton::clicked, this, [this, target]() {
            switchScreenPage(target);
        });
        cardLayout->addWidget(actionBtn);

        ovLayout->addWidget(card, i / 3, i % 3);
    }
    
    // ----- PAGE 1: INPUT DETAIL -----
    m_pageInput = new QWidget(m_screenStack);
    QHBoxLayout *inpLayout = new QHBoxLayout(m_pageInput);
    inpLayout->setContentsMargins(15, 15, 15, 15);
    inpLayout->setSpacing(15);

    QFrame *preampBox = new QFrame(m_pageInput);
    preampBox->setObjectName("DetailSectionBox");
    QVBoxLayout *preLayout = new QVBoxLayout(preampBox);
    preLayout->setContentsMargins(10, 10, 10, 10);
    preLayout->addWidget(new QLabel("PREAMP", preampBox), 0, Qt::AlignCenter);

    m_inpGainKnob = new RotaryKnob("GAIN", 0, 120, 0, preampBox);
    m_inpTrimKnob = new RotaryKnob("TRIM", -24, 24, 0, preampBox);

    QHBoxLayout *preBtns = new QHBoxLayout();
    m_inpPhantomBtn = new QPushButton("+48V", preampBox);
    m_inpPhantomBtn->setCheckable(true);
    m_inpPhantomBtn->setObjectName("RedLedBtn");

    m_inpPhaseBtn = new QPushButton("ø", preampBox);
    m_inpPhaseBtn->setCheckable(true);
    m_inpPhaseBtn->setObjectName("OrangeLedBtn");

    preBtns->addWidget(m_inpPhantomBtn);
    preBtns->addWidget(m_inpPhaseBtn);

    preLayout->addWidget(m_inpGainKnob, 1);
    preLayout->addWidget(m_inpTrimKnob, 1);
    preLayout->addLayout(preBtns);

    QFrame *filterBox = new QFrame(m_pageInput);
    filterBox->setObjectName("DetailSectionBox");
    QVBoxLayout *filtLayout = new QVBoxLayout(filterBox);
    filtLayout->setContentsMargins(10, 10, 10, 10);
    filtLayout->addWidget(new QLabel("LO / HI CUT FILTER", filterBox), 0, Qt::AlignCenter);

    m_inpLoCutBtn = new QPushButton("LO CUT ON", filterBox);
    m_inpLoCutBtn->setCheckable(true);
    m_inpLoCutBtn->setObjectName("TealLedBtn");

    m_inpLoCutFreqKnob = new RotaryKnob("LO FREQ", 20, 400, 80, filterBox);

    m_inpHiCutBtn = new QPushButton("HI CUT ON", filterBox);
    m_inpHiCutBtn->setCheckable(true);
    m_inpHiCutBtn->setObjectName("TealLedBtn");

    m_inpHiCutFreqKnob = new RotaryKnob("HI FREQ", 2000, 20000, 12000, filterBox);

    filtLayout->addWidget(m_inpLoCutBtn);
    filtLayout->addWidget(m_inpLoCutFreqKnob, 1);
    filtLayout->addWidget(m_inpHiCutBtn);
    filtLayout->addWidget(m_inpHiCutFreqKnob, 1);

    QFrame *tiltBox = new QFrame(m_pageInput);
    tiltBox->setObjectName("DetailSectionBox");
    QVBoxLayout *tiltLayout = new QVBoxLayout(tiltBox);
    tiltLayout->setContentsMargins(10, 10, 10, 10);
    tiltLayout->addWidget(new QLabel("TILT EQ", tiltBox), 0, Qt::AlignCenter);

    m_inpTiltKnob = new RotaryKnob("TILT", -60, 60, 0, tiltBox);

    tiltLayout->addWidget(m_inpTiltKnob, 1);
    tiltLayout->addStretch(1);

    inpLayout->addWidget(preampBox, 10);
    inpLayout->addWidget(filterBox, 12);
    inpLayout->addWidget(tiltBox, 8);

    // ----- PAGE 3: EQ DETAIL -----
    m_pageEq = new QWidget(m_screenStack);
    QHBoxLayout *eqLayout = new QHBoxLayout(m_pageEq);
    eqLayout->setContentsMargins(15, 15, 15, 15);
    eqLayout->setSpacing(15);

    m_eqGraph = new EqGraphWidget(m_pageEq);
    m_eqGraph->setObjectName("EqGraph");
    connect(m_eqGraph, &EqGraphWidget::bandSelected, this, &MainWindow::onEqBandSelected);
    connect(m_eqGraph, &EqGraphWidget::bandChanged, this, &MainWindow::onEqGraphChanged);

    QFrame *eqParamsBox = new QFrame(m_pageEq);
    eqParamsBox->setObjectName("DetailSectionBox");
    QVBoxLayout *eqParamsLayout = new QVBoxLayout(eqParamsBox);
    eqParamsLayout->setContentsMargins(10, 10, 10, 10);
    eqParamsLayout->setSpacing(8);

    QLabel *eqTitle = new QLabel("PARAMETRIC EQ", eqParamsBox);
    eqTitle->setObjectName("PanelTitleSmall");
    eqParamsLayout->addWidget(eqTitle, 0, Qt::AlignCenter);

    QHBoxLayout *bandSelLayout = new QHBoxLayout();
    QStringList bandNames = {"1", "2", "3", "4"};
    for (int i = 0; i < 4; ++i) {
        m_eqBandButtons[i] = new QPushButton(bandNames[i], eqParamsBox);
        m_eqBandButtons[i]->setObjectName("EqBandSelectBtn");
        m_eqBandButtons[i]->setCheckable(true);
        if (i == 0) m_eqBandButtons[i]->setChecked(true);
        connect(m_eqBandButtons[i], &QPushButton::clicked, this, [this, i]() { onEqBandSelected(i); });
        bandSelLayout->addWidget(m_eqBandButtons[i]);
    }
    eqParamsLayout->addLayout(bandSelLayout);

    m_eqBandActiveBtn = new QPushButton("BAND ON", eqParamsBox);
    m_eqBandActiveBtn->setCheckable(true);
    m_eqBandActiveBtn->setObjectName("YellowLedBtn");

    m_eqGainKnob = new RotaryKnob("GAIN", -150, 150, 0, eqParamsBox);
    m_eqGainKnob->setSmall(true);
    m_eqFreqKnob = new RotaryKnob("FREQ", 20, 20000, 1000, eqParamsBox);
    m_eqFreqKnob->setSmall(true);
    m_eqQKnob = new RotaryKnob("Q", 5, 100, 10, eqParamsBox);
    m_eqQKnob->setSmall(true);

    eqParamsLayout->addWidget(m_eqBandActiveBtn);
    eqParamsLayout->addWidget(m_eqGainKnob, 1);
    eqParamsLayout->addWidget(m_eqFreqKnob, 1);
    eqParamsLayout->addWidget(m_eqQKnob, 1);

    eqLayout->addWidget(m_eqGraph, 3);
    eqLayout->addWidget(eqParamsBox, 1);

    // ----- PAGE 2: GATE DETAIL -----
    m_pageGate = new QWidget(m_screenStack);
    QHBoxLayout *gateLayout = new QHBoxLayout(m_pageGate);
    gateLayout->setContentsMargins(15, 15, 15, 15);
    gateLayout->setSpacing(15);

    QFrame *gateCtrlBox = new QFrame(m_pageGate);
    gateCtrlBox->setObjectName("DetailSectionBox");
    QVBoxLayout *gateCtrlLayout = new QVBoxLayout(gateCtrlBox);
    gateCtrlLayout->setContentsMargins(15, 15, 15, 15);
    gateCtrlLayout->setSpacing(12);
    gateCtrlLayout->addWidget(new QLabel("GATE CONTROLS", gateCtrlBox), 0, Qt::AlignCenter);

    m_gateActiveBtn = new QPushButton("GATE ON", gateCtrlBox);
    m_gateActiveBtn->setCheckable(true);
    m_gateActiveBtn->setObjectName("OrangeLedBtn");

    m_gateThresholdKnob = new RotaryKnob("THRESHOLD", -80, 0, -55, gateCtrlBox);
    m_gateAttackKnob = new RotaryKnob("ATTACK", 1, 5000, 10, gateCtrlBox);
    m_gateHoldKnob = new RotaryKnob("HOLD", 10, 2000, 50, gateCtrlBox);
    m_gateReleaseKnob = new RotaryKnob("RELEASE", 10, 4000, 250, gateCtrlBox);
    m_gateDepthKnob = new RotaryKnob("DEPTH", 0, 60, 40, gateCtrlBox);

    QHBoxLayout *gateKnobsLayout = new QHBoxLayout();
    gateKnobsLayout->addWidget(m_gateThresholdKnob);
    gateKnobsLayout->addWidget(m_gateAttackKnob);
    gateKnobsLayout->addWidget(m_gateHoldKnob);
    gateKnobsLayout->addWidget(m_gateReleaseKnob);
    gateKnobsLayout->addWidget(m_gateDepthKnob);

    gateCtrlLayout->addWidget(m_gateActiveBtn);
    gateCtrlLayout->addLayout(gateKnobsLayout, 1);

    QFrame *gateVisualBox = new QFrame(m_pageGate);
    gateVisualBox->setObjectName("DetailSectionBox");
    QVBoxLayout *gateVisualLayout = new QVBoxLayout(gateVisualBox);
    gateVisualLayout->setContentsMargins(15, 15, 15, 15);
    QLabel *gateVisualTitle = new QLabel("GATE GRAPH & DYNAMICS STATE", gateVisualBox);
    gateVisualLayout->addWidget(gateVisualTitle, 0, Qt::AlignCenter);
    
    QLabel *gateGraphPlaceholder = new QLabel(gateVisualBox);
    gateGraphPlaceholder->setAlignment(Qt::AlignCenter);
    gateGraphPlaceholder->setStyleSheet("color: #71717a; font-size: 12px; background-color: #0c0c10; border: 1px dashed #272737; border-radius: 4px;");
    gateGraphPlaceholder->setText("[ Gate Transfer Curve / Gain Reduction Display ]");
    gateVisualLayout->addWidget(gateGraphPlaceholder, 1);

    gateLayout->addWidget(gateCtrlBox, 2);
    gateLayout->addWidget(gateVisualBox, 1);

    // ----- PAGE 3: COMPRESSOR DETAIL -----
    m_pageComp = new QWidget(m_screenStack);
    QHBoxLayout *compLayout = new QHBoxLayout(m_pageComp);
    compLayout->setContentsMargins(15, 15, 15, 15);
    compLayout->setSpacing(15);

    QFrame *compCtrlBox = new QFrame(m_pageComp);
    compCtrlBox->setObjectName("DetailSectionBox");
    QVBoxLayout *compCtrlLayout = new QVBoxLayout(compCtrlBox);
    compCtrlLayout->setContentsMargins(15, 15, 15, 15);
    compCtrlLayout->setSpacing(12);
    compCtrlLayout->addWidget(new QLabel("COMPRESSOR CONTROLS", compCtrlBox), 0, Qt::AlignCenter);

    m_compActiveBtn = new QPushButton("COMP ON", compCtrlBox);
    m_compActiveBtn->setCheckable(true);
    m_compActiveBtn->setObjectName("BlueLedBtn");

    m_compThresholdKnob = new RotaryKnob("THRESHOLD", -60, 0, -18, compCtrlBox);
    m_compRatioKnob = new RotaryKnob("RATIO", 10, 200, 35, compCtrlBox);
    m_compAttackKnob = new RotaryKnob("ATTACK", 1, 2000, 100, compCtrlBox);
    m_compHoldKnob = new RotaryKnob("HOLD", 0, 1000, 0, compCtrlBox);
    m_compReleaseKnob = new RotaryKnob("RELEASE", 10, 3000, 150, compCtrlBox);

    QHBoxLayout *compKnobsLayout = new QHBoxLayout();
    compKnobsLayout->addWidget(m_compThresholdKnob);
    compKnobsLayout->addWidget(m_compRatioKnob);
    compKnobsLayout->addWidget(m_compAttackKnob);
    compKnobsLayout->addWidget(m_compHoldKnob);
    compKnobsLayout->addWidget(m_compReleaseKnob);

    compCtrlLayout->addWidget(m_compActiveBtn);
    compCtrlLayout->addLayout(compKnobsLayout, 1);

    QFrame *compScBox = new QFrame(m_pageComp);
    compScBox->setObjectName("DetailSectionBox");
    QVBoxLayout *compScLayout = new QVBoxLayout(compScBox);
    compScLayout->setContentsMargins(15, 15, 15, 15);
    compScLayout->setSpacing(12);
    compScLayout->addWidget(new QLabel("SIDECHAIN FILTER", compScBox), 0, Qt::AlignCenter);

    m_compScLoCutKnob = new RotaryKnob("SC LO CUT", 20, 1000, 20, compScBox);
    m_compScHiCutKnob = new RotaryKnob("SC HI CUT", 200, 20000, 20000, compScBox);

    QHBoxLayout *scKnobsLayout = new QHBoxLayout();
    scKnobsLayout->addWidget(m_compScLoCutKnob);
    scKnobsLayout->addWidget(m_compScHiCutKnob);

    compScLayout->addLayout(scKnobsLayout, 1);

    compLayout->addWidget(compCtrlBox, 3);
    compLayout->addWidget(compScBox, 1);

    // ----- PAGE 5: MAIN & LEVEL DETAIL -----
    m_pageMain = new QWidget(m_screenStack);
    QHBoxLayout *mainPageLayout = new QHBoxLayout(m_pageMain);
    mainPageLayout->setContentsMargins(15, 15, 15, 15);
    mainPageLayout->setSpacing(30);

    QFrame *mainControlsBox = new QFrame(m_pageMain);
    mainControlsBox->setObjectName("DetailSectionBox");
    QVBoxLayout *mCtrlLayout = new QVBoxLayout(mainControlsBox);
    mCtrlLayout->setContentsMargins(10, 10, 10, 10);
    mCtrlLayout->addWidget(new QLabel("PAN & MIX OPTIONS", mainControlsBox), 0, Qt::AlignCenter);

    m_screenPanKnob = new RotaryKnob("PAN", -50, 50, 0, mainControlsBox);

    m_screenSoloBtn = new QPushButton("SOLO", mainControlsBox);
    m_screenSoloBtn->setCheckable(true);
    m_screenSoloBtn->setObjectName("YellowLedBtn");

    m_screenMuteBtn = new QPushButton("MUTE", mainControlsBox);
    m_screenMuteBtn->setCheckable(true);
    m_screenMuteBtn->setObjectName("RedLedBtn");

    mCtrlLayout->addWidget(m_screenPanKnob, 1);
    mCtrlLayout->addSpacing(10);
    mCtrlLayout->addWidget(m_screenSoloBtn);
    mCtrlLayout->addWidget(m_screenMuteBtn);

    QFrame *mainLevelBox = new QFrame(m_pageMain);
    mainLevelBox->setObjectName("DetailSectionBox");
    QHBoxLayout *mLevelLayout = new QHBoxLayout(mainLevelBox);
    mLevelLayout->setContentsMargins(15, 15, 15, 15);
    mLevelLayout->setSpacing(15);

    m_screenFader = new QSlider(Qt::Vertical, mainLevelBox);
    m_screenFader->setObjectName("ChannelFaderLarge");

    m_screenMeter = new LevelMeter(mainLevelBox);
    m_screenMeter->setObjectName("ScreenChannelMeter");
    m_screenMeter->setStereo(true);
    m_screenMeter->setSimulated(true);

    mLevelLayout->addWidget(m_screenFader, 1);
    mLevelLayout->addWidget(m_screenMeter, 1);

    mainPageLayout->addWidget(mainControlsBox, 10);
    mainPageLayout->addWidget(mainLevelBox, 15);

    // ----- PAGE 6: INSERTS DETAIL -----
    m_pageInserts = new QWidget(m_screenStack);
    QVBoxLayout *insLayout = new QVBoxLayout(m_pageInserts);
    insLayout->setContentsMargins(15, 15, 15, 15);
    insLayout->setSpacing(12);

    QFrame *insConfigBox = new QFrame(m_pageInserts);
    insConfigBox->setObjectName("DetailSectionBox");
    QHBoxLayout *insConfigLayout = new QHBoxLayout(insConfigBox);
    insConfigLayout->setContentsMargins(12, 8, 12, 8);
    
    QLabel *insConfigLabel = new QLabel("ACTIVE INSERTS FOR THIS CHANNEL:", insConfigBox);
    insConfigLabel->setStyleSheet("color: #ffffff; font-weight: bold; font-size: 12px; border: none; padding-bottom: 0px;");
    
    m_insCountSpin = new QSpinBox(insConfigBox);
    m_insCountSpin->setRange(1, 8);
    m_insCountSpin->setValue(3);
    m_insCountSpin->setObjectName("InsCountSpin");
    m_insCountSpin->setStyleSheet(
        "QSpinBox { background-color: #1a1a24; color: #ffffff; border: 1px solid #a855f7; border-radius: 4px; padding: 4px; font-weight: bold; min-width: 50px; }"
    );

    insConfigLayout->addWidget(insConfigLabel);
    insConfigLayout->addWidget(m_insCountSpin);
    insConfigLayout->addStretch(1);

    m_insTabsContainer = new QFrame(m_pageInserts);
    m_insTabsContainer->setObjectName("InsTabsContainer");
    m_insTabsLayout = new QHBoxLayout(m_insTabsContainer);
    m_insTabsLayout->setContentsMargins(0, 0, 0, 0);
    m_insTabsLayout->setSpacing(4);

    QFrame *insDetailBox = new QFrame(m_pageInserts);
    insDetailBox->setObjectName("DetailSectionBox");
    QVBoxLayout *insDetailLayout = new QVBoxLayout(insDetailBox);
    insDetailLayout->setContentsMargins(15, 15, 15, 15);
    insDetailLayout->setSpacing(15);

    QLabel *insDetailTitle = new QLabel("INSERT EFFECT SLOT CONFIGURATION", insDetailBox);
    insDetailLayout->addWidget(insDetailTitle, 0, Qt::AlignCenter);

    QHBoxLayout *insParamsRow = new QHBoxLayout();
    insParamsRow->setSpacing(20);

    QVBoxLayout *insLeftCol = new QVBoxLayout();
    insLeftCol->setSpacing(15);
    
    QLabel *typeLabel = new QLabel("EFFECT TYPE", insDetailBox);
    typeLabel->setStyleSheet("color: #a1a1aa; font-size: 10px; font-weight: bold; border: none; padding: 0;");
    m_insTypeCombo = new QComboBox(insDetailBox);
    m_insTypeCombo->addItem("None / Bypass");
    m_insTypeCombo->addItem("Parametric EQ");
    m_insTypeCombo->addItem("Compressor");
    m_insTypeCombo->addItem("Stereo Delay");
    m_insTypeCombo->addItem("Plate Reverb");
    m_insTypeCombo->addItem("Chorus");
    m_insTypeCombo->setObjectName("InsTypeCombo");
    m_insTypeCombo->setStyleSheet(
        "QComboBox { background-color: #1e1e28; color: #ffffff; border: 1px solid #2b2b3b; border-radius: 4px; padding: 8px; font-weight: bold; min-width: 150px; }"
        "QComboBox QAbstractItemView { background-color: #181822; color: #ffffff; selection-background-color: #a855f7; selection-color: #ffffff; }"
    );

    m_insBypassBtn = new QPushButton("BYPASS EFFECT", insDetailBox);
    m_insBypassBtn->setCheckable(true);
    m_insBypassBtn->setObjectName("RedLedBtn");

    insLeftCol->addWidget(typeLabel);
    insLeftCol->addWidget(m_insTypeCombo);
    insLeftCol->addSpacing(10);
    insLeftCol->addWidget(m_insBypassBtn);
    insLeftCol->addStretch(1);

    m_insMixKnob = new RotaryKnob("WET/DRY MIX", 0, 100, 100, insDetailBox);
    m_insParam1Knob = new RotaryKnob("PARAM 1", 0, 100, 50, insDetailBox);
    m_insParam2Knob = new RotaryKnob("PARAM 2", 0, 100, 50, insDetailBox);

    insParamsRow->addLayout(insLeftCol, 1);
    insParamsRow->addWidget(m_insMixKnob, 1);
    insParamsRow->addWidget(m_insParam1Knob, 1);
    insParamsRow->addWidget(m_insParam2Knob, 1);

    insDetailLayout->addLayout(insParamsRow, 1);

    insLayout->addWidget(insConfigBox);
    insLayout->addWidget(m_insTabsContainer);
    insLayout->addWidget(insDetailBox, 1);

    m_screenStack->addWidget(m_pageOverview);  // Page 0
    m_screenStack->addWidget(m_pageInput);     // Page 1
    m_screenStack->addWidget(m_pageGate);      // Page 2
    m_screenStack->addWidget(m_pageComp);      // Page 3
    m_screenStack->addWidget(m_pageEq);        // Page 4
    m_screenStack->addWidget(m_pageMain);      // Page 5
    m_screenStack->addWidget(m_pageInserts);   // Page 6

    screenMiddleLayout->addWidget(leftMenu);
    screenMiddleLayout->addWidget(m_screenStack, 1);

    // 3. Screen Bottom Parameter Encoders
    m_bottomEncodersPanel = new QFrame(screenContent);
    m_bottomEncodersPanel->setObjectName("BottomEncodersPanel");
    QHBoxLayout *bottomEncLayout = new QHBoxLayout(m_bottomEncodersPanel);
    bottomEncLayout->setContentsMargins(10, 4, 10, 4);
    bottomEncLayout->setSpacing(15);

    for (int i = 0; i < 6; ++i) {
        QFrame *encoderStrip = new QFrame(m_bottomEncodersPanel);
        encoderStrip->setObjectName("EncoderStrip");
        QVBoxLayout *encStripLayout = new QVBoxLayout(encoderStrip);
        encStripLayout->setContentsMargins(2, 2, 2, 2);
        encStripLayout->setSpacing(2);

        RotaryKnob *knob = new RotaryKnob("", 0, 100, 50, encoderStrip);
        knob->setObjectName("BottomEncoderKnob");
        knob->setSmall(false);
        m_bottomEncoders.append(knob);

        encStripLayout->addWidget(knob, 1);
        bottomEncLayout->addWidget(encoderStrip);
    }

    screenContentLayout->addWidget(topBar);
    screenContentLayout->addWidget(screenMiddle, 1);
    screenContentLayout->addWidget(m_bottomEncodersPanel);

    screenLayout->addWidget(screenContent, 1);
    rootLayout->addWidget(screenBezel, 3);

    // =========================================================================
    //   LOWER PANEL: PHYSICAL FADER DECK SIMULATOR
    // =========================================================================
    QFrame *consoleBezel = new QFrame(centralWidget);
    consoleBezel->setObjectName("ConsoleBezel");
    QVBoxLayout *consoleLayout = new QVBoxLayout(consoleBezel);
    consoleLayout->setContentsMargins(15, 5, 15, 10);
    consoleLayout->setSpacing(5);

    QFrame *consoleDesk = new QFrame(consoleBezel);
    consoleDesk->setObjectName("ConsoleDesk");
    consoleDesk->setVisible(false); // Hide by default

    QPushButton *collapseBtn = new QPushButton("▲ SHOW PHYSICAL CONTROLS", consoleBezel);
    collapseBtn->setObjectName("ConsoleCollapseBtn");
    collapseBtn->setCheckable(true);
    collapseBtn->setChecked(true); // true = collapsed/hidden by default

    connect(collapseBtn, &QPushButton::clicked, this, [consoleDesk, consoleBezel, rootLayout, collapseBtn](bool checked) {
        consoleDesk->setVisible(!checked);
        collapseBtn->setText(checked ? "▲ SHOW PHYSICAL CONTROLS" : "▼ HIDE PHYSICAL CONTROLS");
        rootLayout->setStretchFactor(consoleBezel, checked ? 0 : 2);
    });

    consoleLayout->addWidget(collapseBtn, 0, Qt::AlignCenter);
    consoleLayout->addWidget(consoleDesk, 1);

    QHBoxLayout *deskLayout = new QHBoxLayout(consoleDesk);
    deskLayout->setContentsMargins(10, 12, 10, 12);
    deskLayout->setSpacing(8);

    for (int i = 0; i < 8; ++i) {
        QFrame *physStrip = new QFrame(consoleDesk);
        physStrip->setObjectName("PhysicalChannelStrip");
        QVBoxLayout *stripLayout = new QVBoxLayout(physStrip);
        stripLayout->setContentsMargins(4, 4, 4, 4);
        stripLayout->setSpacing(6);

        ScribbleStrip *scribble = new ScribbleStrip(physStrip);
        scribble->setObjectName("PhysicalScribbleStrip");
        scribble->setChannelInfo(m_channels[i]->number(), m_channels[i]->name(), m_channels[i]->color());
        m_physScribbles.append(scribble);

        QPushButton *selectBtn = new QPushButton("SELECT", physStrip);
        selectBtn->setObjectName("ChannelSelectBtn");
        connect(selectBtn, &QPushButton::clicked, this, [this, i]() {
            selectChannel(i);
        });

        QPushButton *soloBtn = new QPushButton("SOLO", physStrip);
        soloBtn->setCheckable(true);
        soloBtn->setObjectName("PhysicalSoloBtn");
        m_physSoloBtns.append(soloBtn);

        QPushButton *muteBtn = new QPushButton("MUTE", physStrip);
        muteBtn->setCheckable(true);
        muteBtn->setObjectName("PhysicalMuteBtn");
        m_physMuteBtns.append(muteBtn);

        QHBoxLayout *faderMeterLayout = new QHBoxLayout();
        faderMeterLayout->setContentsMargins(0, 0, 0, 0);
        faderMeterLayout->setSpacing(4);

        QSlider *fader = new QSlider(Qt::Vertical, physStrip);
        fader->setObjectName("PhysicalFaderSlider");
        m_physFaders.append(fader);

        LevelMeter *meter = new LevelMeter(physStrip);
        meter->setObjectName("PhysicalLevelMeter");
        meter->setStereo(false);
        meter->setSimulated(true);
        m_physMeters.append(meter);

        faderMeterLayout->addWidget(fader, 1);
        faderMeterLayout->addWidget(meter, 0);

        QLabel *dbLabel = new QLabel("0.0 dB", physStrip);
        dbLabel->setAlignment(Qt::AlignCenter);
        dbLabel->setObjectName("PhysicalDbLabel");
        m_physDbLabels.append(dbLabel);

        // --- Declarative MVVM parameter bindings for the physical strip ---
        m_physFaderBinders[i]->bind(fader, m_channels[i]->parameter("level"));
        m_physMuteBinders[i]->bind(muteBtn, m_channels[i]->parameter("mute"));
        m_physSoloBinders[i]->bind(soloBtn, m_channels[i]->parameter("solo"));

        // Bind the scribble strip visual states to the model mute/solo parameters
        connect(m_channels[i]->parameter("mute"), &AudioParameter::valueChanged, this, [scribble](double val) {
            scribble->setMuted(val > 0.5);
        });
        connect(m_channels[i]->parameter("solo"), &AudioParameter::valueChanged, this, [scribble](double val) {
            scribble->setSoloed(val > 0.5);
        });

        // Bind the dB labels directly to the level parameter values
        connect(m_channels[i]->parameter("level"), &AudioParameter::valueChanged, this, [dbLabel](double val) {
            if (val <= -79.9) dbLabel->setText("-inf dB");
            else dbLabel->setText(QString("%1 dB").arg(val, 0, 'f', 1));
        });

        // Trigger initial setup for the labels
        double val = m_channels[i]->parameter("level")->value();
        if (val <= -79.9) dbLabel->setText("-inf dB");
        else dbLabel->setText(QString("%1 dB").arg(val, 0, 'f', 1));

        stripLayout->addWidget(scribble);
        stripLayout->addWidget(selectBtn);
        stripLayout->addWidget(soloBtn);
        stripLayout->addWidget(muteBtn);
        stripLayout->addLayout(faderMeterLayout, 1);
        stripLayout->addWidget(dbLabel);

        deskLayout->addWidget(physStrip);
    }

    rootLayout->addWidget(consoleBezel, 0); // Start with stretch factor 0 (collapsed)
}

void MainWindow::selectChannel(int index)
{
    if (index < 0 || index >= m_channels.size() || index == m_selectedChannelIndex) return;

    m_selectedChannelIndex = index;
    populateUIFromSelectedChannel();
}

void MainWindow::switchScreenPage(int pageIndex)
{
    m_screenStack->setCurrentIndex(pageIndex);

    // Highlight left menu buttons
    for (int i = 0; i < m_menuButtons.size(); ++i) {
        m_menuButtons[i]->setChecked(i == pageIndex);
    }

    // If switching to Inserts page, update layout and select first slot
    if (pageIndex == 6) {
        updateInsertSlotsLayout();
    }

    // Refresh active bottom parameter encoders mapping
    updateBottomEncodersLayout();
}

void MainWindow::updateOverviewLabels()
{
    MixerChannel *ch = m_channels[m_selectedChannelIndex];

    // 1. Input & Filters Overview
    double gain = ch->parameter("gain")->value();
    double trim = ch->parameter("trim")->value();
    bool phantom = ch->parameter("phantom")->value() > 0.5;
    bool phase = ch->parameter("phase")->value() > 0.5;
    m_ovInputGain->setText(QString("Gain: +%1 dB | Trim: %2%3 dB\n48V: %4 | Phase: %5")
        .arg(gain, 0, 'f', 1)
        .arg(trim >= 0.0 ? "+" : "")
        .arg(trim, 0, 'f', 1)
        .arg(phantom ? "ON" : "OFF")
        .arg(phase ? "ON" : "OFF"));

    bool loCut = ch->parameter("locut_active")->value() > 0.5;
    bool hiCut = ch->parameter("hicut_active")->value() > 0.5;
    m_ovInputFilters->setText(QString("LoCut: %1 | HiCut: %2")
        .arg(loCut ? QString("%1 Hz").arg(qRound(ch->parameter("locut_freq")->value())) : "OFF")
        .arg(hiCut ? QString("%1 kHz").arg(ch->parameter("hicut_freq")->value() / 1000.0, 0, 'f', 1) : "OFF"));

    double tilt = ch->parameter("tilt")->value();
    m_ovInputTilt->setText(QString("Tilt EQ: %1%2 dB")
        .arg(tilt >= 0.0 ? "+" : "")
        .arg(tilt, 0, 'f', 1));

    // 2. Noise Gate Overview
    bool gateActive = ch->parameter("gate_active")->value() > 0.5;
    m_ovGateState->setText(QString("State: %1").arg(gateActive ? "<span style='color:#f97316; font-weight:bold;'>ON</span>" : "OFF"));
    m_ovGateParams->setText(QString("Thr: %1 dB | Depth: %2 dB\nAtt: %3 ms | Hold: %4 ms | Rel: %5 ms")
        .arg(ch->parameter("gate_threshold")->value(), 0, 'f', 1)
        .arg(ch->parameter("gate_depth")->value(), 0, 'f', 1)
        .arg(ch->parameter("gate_attack")->value(), 0, 'f', 1)
        .arg(ch->parameter("gate_hold")->value(), 0, 'f', 0)
        .arg(ch->parameter("gate_release")->value(), 0, 'f', 0));

    // 3. Compressor Overview
    bool compActive = ch->parameter("comp_active")->value() > 0.5;
    m_ovCompState->setText(QString("State: %1").arg(compActive ? "<span style='color:#3b82f6; font-weight:bold;'>ON</span>" : "OFF"));
    double scLocut = ch->parameter("comp_sc_locut")->value();
    double scHicut = ch->parameter("comp_sc_hicut")->value();
    QString scHicutStr = scHicut >= 1000.0 ? QString("%1 kHz").arg(scHicut / 1000.0, 0, 'f', 1) : QString("%1 Hz").arg(qRound(scHicut));
    m_ovCompParams->setText(QString("Thr: %1 dB | Ratio: %2:1 | Hold: %3 ms\nAtt: %4 ms | Rel: %5 ms\nSC Filter: %6 Hz - %7")
        .arg(ch->parameter("comp_threshold")->value(), 0, 'f', 1)
        .arg(ch->parameter("comp_ratio")->value(), 0, 'f', 1)
        .arg(ch->parameter("comp_hold")->value(), 0, 'f', 0)
        .arg(ch->parameter("comp_attack")->value(), 0, 'f', 1)
        .arg(ch->parameter("comp_release")->value(), 0, 'f', 0)
        .arg(qRound(scLocut))
        .arg(scHicutStr));

    // 4. Parametric EQ Overview
    for (int b = 0; b < 4; ++b) {
        QString prefix = QString("eq%1_").arg(b + 1);
        bool eqOn = ch->parameter(prefix + "active")->value() > 0.5;
        double f = ch->parameter(prefix + "freq")->value();
        double g = ch->parameter(prefix + "gain")->value();
        double q = ch->parameter(prefix + "q")->value();
        
        QString fStr = f >= 1000.0 ? QString("%1k").arg(f / 1000.0, 0, 'f', 1) : QString::number(qRound(f));
        m_ovEqBands[b]->setText(QString("Band %1 (%2): %3 Hz | %4%5 dB | Q: %6")
            .arg(b + 1)
            .arg(eqOn ? "ON" : "OFF")
            .arg(fStr)
            .arg(g >= 0.0 ? "+" : "")
            .arg(g, 0, 'f', 1)
            .arg(q, 0, 'f', 1));
    }

    // 5. Mix & Output Overview
    double lvl = ch->parameter("level")->value();
    QString lvlStr = lvl <= -79.9 ? "-inf dB" : QString("%1 dB").arg(lvl, 0, 'f', 1);
    m_ovMixLevel->setText(QString("Fader Level: %1").arg(lvlStr));

    double pan = ch->parameter("pan")->value();
    QString panStr = pan == 0.0 ? "C" : (pan < 0.0 ? QString("L%1").arg(qAbs(qRound(pan))) : QString("R%1").arg(qRound(pan)));
    m_ovMixPan->setText(QString("Pan position: %1").arg(panStr));

    bool mute = ch->parameter("mute")->value() > 0.5;
    bool solo = ch->parameter("solo")->value() > 0.5;
    m_ovMixMuteSolo->setText(QString("Mute: %1 | Solo: %2")
        .arg(mute ? "<span style='color:#ef4444; font-weight:bold;'>MUTED</span>" : "OFF")
        .arg(solo ? "<span style='color:#fbbf24; font-weight:bold;'>SOLOED</span>" : "OFF"));

    // 6. Channel Inserts Overview
    int activeSlots = ch->numInsertPages();
    m_ovInsertsCount->setText(QString("Rack Slots Active: %1").arg(activeSlots));
    for (int s = 0; s < 4; ++s) {
        if (s < activeSlots) {
            QString prefix = QString("insert%1_").arg(s + 1);
            int type = qRound(ch->parameter(prefix + "type")->value());
            bool bypass = ch->parameter(prefix + "bypass")->value() > 0.5;
            
            QString typeStr = "None";
            if (type == 1) typeStr = "EQ";
            else if (type == 2) typeStr = "Comp";
            else if (type == 3) typeStr = "Delay";
            else if (type == 4) typeStr = "Reverb";
            else if (type == 5) typeStr = "Chorus";

            if (type == 0) {
                m_ovInsertSlots[s]->setText(QString("Slot %1: Empty").arg(s + 1));
            } else {
                double mix = ch->parameter(prefix + "mix")->value();
                double p1 = ch->parameter(prefix + "param1")->value();
                double p2 = ch->parameter(prefix + "param2")->value();
                QString p1Name = ch->parameter(prefix + "param1")->name();
                QString p2Name = ch->parameter(prefix + "param2")->name();
                QString p1Unit = ch->parameter(prefix + "param1")->unit();
                QString p2Unit = ch->parameter(prefix + "param2")->unit();

                m_ovInsertSlots[s]->setText(QString("Slot %1: %2 (%3) | Mix: %4%\n  %5: %6%7 | %8: %9%10")
                    .arg(s + 1)
                    .arg(typeStr)
                    .arg(bypass ? "BYP" : "IN")
                    .arg(qRound(mix))
                    .arg(p1Name)
                    .arg(p1, 0, 'f', 1)
                    .arg(p1Unit)
                    .arg(p2Name)
                    .arg(p2, 0, 'f', 1)
                    .arg(p2Unit));
            }
        } else {
            m_ovInsertSlots[s]->setText(QString("Slot %1: Locked").arg(s + 1));
        }
    }
}

void MainWindow::updateClock()
{
    m_clockLabel->setText(QTime::currentTime().toString("hh:mm:ss"));
}

void MainWindow::clearAllSolos()
{
    for (int i = 0; i < m_channels.size(); ++i) {
        m_channels[i]->parameter("solo")->setValue(0.0);
    }
}

// =========================================================================
//   TOUCHSCREEN UI UPDATER (MVVM Declarative Bindings Populate)
// =========================================================================
void MainWindow::populateUIFromSelectedChannel()
{
    MixerChannel *ch = m_channels[m_selectedChannelIndex];

    // 1. Top bar indicator color updates dynamically to match current channel category
    m_selectedChannelIndicator->setText(QString("%1 %2").arg(ch->number()).arg(ch->name()));
    m_selectedChannelIndicator->setStyleSheet(QString(
        "background-color: %1;"
        "color: #ffffff;"
        "font-weight: bold;"
        "font-size: 14px;"
        "border-radius: 4px;"
        "padding: 4px 12px;"
    ).arg(ch->color().name()));

    // 2. Bind Preamp/Input widgets
    m_bindGain.bind(m_inpGainKnob, ch->parameter("gain"));
    m_bindTrim.bind(m_inpTrimKnob, ch->parameter("trim"));
    m_bindPhantom.bind(m_inpPhantomBtn, ch->parameter("phantom"));
    m_bindPhase.bind(m_inpPhaseBtn, ch->parameter("phase"));
    
    m_bindLoCutActive.bind(m_inpLoCutBtn, ch->parameter("locut_active"));
    m_bindLoCutFreq.bind(m_inpLoCutFreqKnob, ch->parameter("locut_freq"));

    m_bindHiCutActive.bind(m_inpHiCutBtn, ch->parameter("hicut_active"));
    m_bindHiCutFreq.bind(m_inpHiCutFreqKnob, ch->parameter("hicut_freq"));

    m_bindTilt.bind(m_inpTiltKnob, ch->parameter("tilt"));

    // Interconnect enabling/disabling states directly to active parameters
    m_inpLoCutFreqKnob->setEnabled(ch->parameter("locut_active")->value() > 0.5);
    connect(ch->parameter("locut_active"), &AudioParameter::valueChanged, m_inpLoCutFreqKnob, [this](double val) {
        m_inpLoCutFreqKnob->setEnabled(val > 0.5);
    });

    m_inpHiCutFreqKnob->setEnabled(ch->parameter("hicut_active")->value() > 0.5);
    connect(ch->parameter("hicut_active"), &AudioParameter::valueChanged, m_inpHiCutFreqKnob, [this](double val) {
        m_inpHiCutFreqKnob->setEnabled(val > 0.5);
    });

    // 3. Bind EQ parameters & update EQ graph
    for (int i = 0; i < 4; ++i) {
        m_eqGraph->setBand(i, 
            ch->parameter(QString("eq%1_freq").arg(i + 1))->value(),
            ch->parameter(QString("eq%1_gain").arg(i + 1))->value(),
            ch->parameter(QString("eq%1_q").arg(i + 1))->value(),
            ch->parameter(QString("eq%1_active").arg(i + 1))->value() > 0.5
        );
    }
    
    m_activeEqBandIndex = m_eqGraph->activeBandIndex();
    for (int i = 0; i < 4; ++i) {
        m_eqBandButtons[i]->setChecked(i == m_activeEqBandIndex);
    }

    QString eqPrefix = QString("eq%1_").arg(m_activeEqBandIndex + 1);
    m_bindEqActive.bind(m_eqBandActiveBtn, ch->parameter(eqPrefix + "active"));
    m_bindEqFreq.bind(m_eqFreqKnob, ch->parameter(eqPrefix + "freq"));
    m_bindEqGain.bind(m_eqGainKnob, ch->parameter(eqPrefix + "gain"));
    m_bindEqQ.bind(m_eqQKnob, ch->parameter(eqPrefix + "q"));

    // Update EQ knobs coloring
    QColor bandColor = m_eqGraph->band(m_activeEqBandIndex).color;
    m_eqGainKnob->setThemeColor(bandColor);
    m_eqFreqKnob->setThemeColor(bandColor);
    m_eqQKnob->setThemeColor(bandColor);

    // 4. Bind Dynamics widgets
    m_bindGateActive.bind(m_gateActiveBtn, ch->parameter("gate_active"));
    m_bindGateThreshold.bind(m_gateThresholdKnob, ch->parameter("gate_threshold"));
    m_bindGateAttack.bind(m_gateAttackKnob, ch->parameter("gate_attack"));
    m_bindGateHold.bind(m_gateHoldKnob, ch->parameter("gate_hold"));
    m_bindGateRelease.bind(m_gateReleaseKnob, ch->parameter("gate_release"));
    m_bindGateDepth.bind(m_gateDepthKnob, ch->parameter("gate_depth"));

    m_bindCompActive.bind(m_compActiveBtn, ch->parameter("comp_active"));
    m_bindCompThreshold.bind(m_compThresholdKnob, ch->parameter("comp_threshold"));
    m_bindCompRatio.bind(m_compRatioKnob, ch->parameter("comp_ratio"));
    m_bindCompAttack.bind(m_compAttackKnob, ch->parameter("comp_attack"));
    m_bindCompHold.bind(m_compHoldKnob, ch->parameter("comp_hold"));
    m_bindCompRelease.bind(m_compReleaseKnob, ch->parameter("comp_release"));
    m_bindCompScLoCut.bind(m_compScLoCutKnob, ch->parameter("comp_sc_locut"));
    m_bindCompScHiCut.bind(m_compScHiCutKnob, ch->parameter("comp_sc_hicut"));

    // Enable/disable logic
    bool gateActive = ch->parameter("gate_active")->value() > 0.5;
    m_gateThresholdKnob->setEnabled(gateActive);
    m_gateAttackKnob->setEnabled(gateActive);
    m_gateHoldKnob->setEnabled(gateActive);
    m_gateReleaseKnob->setEnabled(gateActive);
    m_gateDepthKnob->setEnabled(gateActive);

    bool compActive = ch->parameter("comp_active")->value() > 0.5;
    m_compThresholdKnob->setEnabled(compActive);
    m_compRatioKnob->setEnabled(compActive);
    m_compAttackKnob->setEnabled(compActive);
    m_compHoldKnob->setEnabled(compActive);
    m_compReleaseKnob->setEnabled(compActive);
    m_compScLoCutKnob->setEnabled(compActive);
    m_compScHiCutKnob->setEnabled(compActive);

    connect(ch->parameter("gate_active"), &AudioParameter::valueChanged, this, [this](double val) {
        bool active = val > 0.5;
        m_gateThresholdKnob->setEnabled(active);
        m_gateAttackKnob->setEnabled(active);
        m_gateHoldKnob->setEnabled(active);
        m_gateReleaseKnob->setEnabled(active);
        m_gateDepthKnob->setEnabled(active);
    });
    connect(ch->parameter("comp_active"), &AudioParameter::valueChanged, this, [this](double val) {
        bool active = val > 0.5;
        m_compThresholdKnob->setEnabled(active);
        m_compRatioKnob->setEnabled(active);
        m_compAttackKnob->setEnabled(active);
        m_compHoldKnob->setEnabled(active);
        m_compReleaseKnob->setEnabled(active);
        m_compScLoCutKnob->setEnabled(active);
        m_compScHiCutKnob->setEnabled(active);
    });

    // 5. Bind Main mixing output widgets
    m_bindScreenFader.bind(m_screenFader, ch->parameter("level"));
    m_bindScreenPan.bind(m_screenPanKnob, ch->parameter("pan"));
    m_bindScreenMute.bind(m_screenMuteBtn, ch->parameter("mute"));
    m_bindScreenSolo.bind(m_screenSoloBtn, ch->parameter("solo"));

    // 6. Highlight selected scribble strip
    for (int i = 0; i < m_physScribbles.size(); ++i) {
        m_physScribbles[i]->setSelected(i == m_selectedChannelIndex);
    }

    // Set active inserts spinbox count
    m_insCountSpin->blockSignals(true);
    m_insCountSpin->setValue(ch->numInsertPages());
    m_insCountSpin->blockSignals(false);

    // Disconnect old overview connections
    for (const auto &conn : m_overviewConnections) {
        disconnect(conn);
    }
    m_overviewConnections.clear();

    // Connect all parameter value changes to update the overview dashboard
    for (AudioParameter *param : ch->parameters().values()) {
        m_overviewConnections << connect(param, &AudioParameter::valueChanged, this, &MainWindow::updateOverviewLabels);
    }

    // Trigger initial overview text update
    updateOverviewLabels();

    // If currently viewing the Inserts screen, refresh the slots layout
    if (m_screenStack->currentIndex() == 6) {
        updateInsertSlotsLayout();
    }

    // Refresh bottom parameter encoders mapping
    updateBottomEncodersLayout();
}

void MainWindow::updateInsertSlotsLayout()
{
    MixerChannel *ch = m_channels[m_selectedChannelIndex];
    int count = ch->numInsertPages();
    
    // Safety check for active slot
    if (m_activeInsertSlotIndex >= count) {
        m_activeInsertSlotIndex = 0;
    }

    // Clear old slot buttons from layout
    QLayoutItem *child;
    while ((child = m_insTabsLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }
    m_insTabButtons.clear();

    for (int i = 0; i < count; ++i) {
        QPushButton *btn = new QPushButton(QString("SLOT %1").arg(i + 1), m_pageInserts);
        btn->setCheckable(true);
        btn->setObjectName("InsSlotTabBtn");
        
        // Custom styling for active tab button
        btn->setStyleSheet(
            "QPushButton { background-color: #1e1e28; color: #a1a1aa; border: 1px solid #2b2b3b; padding: 8px 16px; font-weight: bold; border-radius: 4px; }"
            "QPushButton:hover { background-color: #2a2a38; color: #ffffff; }"
            "QPushButton:checked { background-color: #a855f7; color: #ffffff; border-color: #8b5cf6; }"
        );
        
        if (i == m_activeInsertSlotIndex) {
            btn->setChecked(true);
        }
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            selectInsertSlot(i);
        });
        m_insTabsLayout->addWidget(btn);
        m_insTabButtons.append(btn);
    }
    m_insTabsLayout->addStretch(1);

    // Update bindings for the active slot
    populateInsertSlotUI();
}

void MainWindow::selectInsertSlot(int slotIdx)
{
    m_activeInsertSlotIndex = slotIdx;
    for (int i = 0; i < m_insTabButtons.size(); ++i) {
        m_insTabButtons[i]->setChecked(i == slotIdx);
    }
    populateInsertSlotUI();
    updateBottomEncodersLayout();
}

void MainWindow::populateInsertSlotUI()
{
    // Disconnect previous connections for this insert slot view
    for (const auto &conn : m_insertUIConnections) {
        disconnect(conn);
    }
    m_insertUIConnections.clear();

    MixerChannel *ch = m_channels[m_selectedChannelIndex];
    QString prefix = QString("insert%1_").arg(m_activeInsertSlotIndex + 1);

    AudioParameter *pType = ch->parameter(prefix + "type");
    AudioParameter *pBypass = ch->parameter(prefix + "bypass");
    AudioParameter *pMix = ch->parameter(prefix + "mix");
    AudioParameter *p1 = ch->parameter(prefix + "param1");
    AudioParameter *p2 = ch->parameter(prefix + "param2");

    // First ensure the parameters have the correct metadata for current type
    ch->configureInsertSlotParameters(m_activeInsertSlotIndex, qRound(pType->value()));

    // Bind the type combo
    m_bindInsType.bind(m_insTypeCombo, pType);

    // Bind bypass and mix
    m_bindInsBypass.bind(m_insBypassBtn, pBypass);
    m_bindInsMix.bind(m_insMixKnob, pMix);

    // Bind param 1 and 2
    m_bindInsParam1.bind(m_insParam1Knob, p1);
    m_bindInsParam2.bind(m_insParam2Knob, p2);

    // Enable/disable mix and parameters if bypass is active
    bool isBypassed = (pBypass->value() > 0.5);
    m_insMixKnob->setEnabled(!isBypassed);
    m_insParam1Knob->setEnabled(!isBypassed);
    m_insParam2Knob->setEnabled(!isBypassed);

    // Connect bypass changes to disable/enable state
    m_insertUIConnections << connect(pBypass, &AudioParameter::valueChanged, this, [this](double val) {
        bool bypass = (val > 0.5);
        m_insMixKnob->setEnabled(!bypass);
        m_insParam1Knob->setEnabled(!bypass);
        m_insParam2Knob->setEnabled(!bypass);
    });

    // When type changes, reconfigure parameters metadata and rebind
    m_insertUIConnections << connect(pType, &AudioParameter::valueChanged, this, [this, ch](double val) {
        QString pref = QString("insert%1_").arg(m_activeInsertSlotIndex + 1);
        AudioParameter *p1_new = ch->parameter(pref + "param1");
        AudioParameter *p2_new = ch->parameter(pref + "param2");
        
        ch->configureInsertSlotParameters(m_activeInsertSlotIndex, qRound(val));
        
        m_bindInsParam1.bind(m_insParam1Knob, p1_new);
        m_bindInsParam2.bind(m_insParam2Knob, p2_new);
        
        updateBottomEncodersLayout();
    });
}

void MainWindow::updateBottomEncodersLayout()
{
    int pageIdx = m_screenStack->currentIndex();
    MixerChannel *ch = m_channels[m_selectedChannelIndex];

    QVector<AudioParameter*> activeParams(6, nullptr);

    if (pageIdx == 0) { // Overview
        activeParams[0] = ch->parameter("gain");
        activeParams[1] = ch->parameter("locut_freq");
        activeParams[2] = ch->parameter("eq1_freq");
        activeParams[3] = ch->parameter("eq4_freq");
        activeParams[4] = ch->parameter("comp_threshold");
        activeParams[5] = ch->parameter("pan");
    } else if (pageIdx == 1) { // Input
        activeParams[0] = ch->parameter("gain");
        activeParams[1] = ch->parameter("trim");
        activeParams[2] = ch->parameter("locut_freq");
        activeParams[3] = ch->parameter("hicut_freq");
        activeParams[4] = ch->parameter("tilt");
        activeParams[5] = ch->parameter("pan");
    } else if (pageIdx == 2) { // Gate
        activeParams[0] = ch->parameter("gate_active");
        activeParams[1] = ch->parameter("gate_threshold");
        activeParams[2] = ch->parameter("gate_attack");
        activeParams[3] = ch->parameter("gate_hold");
        activeParams[4] = ch->parameter("gate_release");
        activeParams[5] = ch->parameter("gate_depth");
    } else if (pageIdx == 3) { // Compressor
        activeParams[0] = ch->parameter("comp_active");
        activeParams[1] = ch->parameter("comp_threshold");
        activeParams[2] = ch->parameter("comp_ratio");
        activeParams[3] = ch->parameter("comp_attack");
        activeParams[4] = ch->parameter("comp_release");
        activeParams[5] = ch->parameter("comp_sc_locut");
    } else if (pageIdx == 4) { // EQ
        QString eqPrefix = QString("eq%1_").arg(m_activeEqBandIndex + 1);
        activeParams[0] = ch->parameter(eqPrefix + "freq");
        activeParams[1] = ch->parameter(eqPrefix + "gain");
        activeParams[2] = ch->parameter(eqPrefix + "q");
        activeParams[3] = ch->parameter(eqPrefix + "active");
        activeParams[4] = ch->parameter("pan");
        activeParams[5] = ch->parameter("level");
    } else if (pageIdx == 5) { // Main
        activeParams[0] = ch->parameter("pan");
        activeParams[1] = ch->parameter("mute");
        activeParams[2] = ch->parameter("solo");
        activeParams[5] = ch->parameter("level");
    } else if (pageIdx == 6) { // Inserts
        QString prefix = QString("insert%1_").arg(m_activeInsertSlotIndex + 1);
        activeParams[0] = ch->parameter(prefix + "type");
        activeParams[1] = ch->parameter(prefix + "bypass");
        activeParams[2] = ch->parameter(prefix + "mix");
        activeParams[3] = ch->parameter(prefix + "param1");
        activeParams[4] = ch->parameter(prefix + "param2");
        activeParams[5] = ch->parameter("level");
    }

    for (int i = 0; i < 6; ++i) {
        if (activeParams[i] == nullptr) {
            m_bottomEncoders[i]->setVisible(false);
            m_bottomEncBinders[i]->unbind();
        } else {
            m_bottomEncoders[i]->setVisible(true);
            m_bottomEncBinders[i]->bind(m_bottomEncoders[i], activeParams[i]);
        }
    }
}

// =========================================================================
//   INTERACTIVE COMPONENT CALLBACKS
// =========================================================================
void MainWindow::onEqBandSelected(int bandIdx)
{
    m_activeEqBandIndex = bandIdx;
    m_eqGraph->setActiveBandIndex(bandIdx);
    
    // Bind parameters of the newly selected EQ band to detail controls
    populateUIFromSelectedChannel();
}

void MainWindow::onEqGraphChanged(int bandIdx, double freq, double gain, double q)
{
    MixerChannel *ch = m_channels[m_selectedChannelIndex];
    QString eqPrefix = QString("eq%1_").arg(bandIdx + 1);

    // Directly set values on the parameters (which updates bound GUI components automatically)
    ch->parameter(eqPrefix + "freq")->setValue(freq);
    ch->parameter(eqPrefix + "gain")->setValue(gain);
    ch->parameter(eqPrefix + "q")->setValue(q);

    // Refresh graph visual
    m_eqGraph->setBand(bandIdx, freq, gain, q, ch->parameter(eqPrefix + "active")->value() > 0.5);
}

// =========================================================================
//   STYLESHEET / UI DESIGN SYSTEM
// =========================================================================
void MainWindow::applyStyleSheet()
{
    QString style = R"(
        /* Global Styles */
        QMainWindow {
            background-color: #0c0c0f;
        }

        /* Top Screen Panel Simulator */
        #ScreenBezel {
            background-color: #1a1a20;
            border-bottom: 4px solid #0f0f13;
        }

        #ScreenContent {
            background-color: #121217;
            border: 2px solid #282834;
            border-radius: 6px;
        }

        #ScreenTopBar {
            background-color: #191922;
            border-bottom: 1px solid #232330;
            max-height: 48px;
        }

        #ScreenHomeBtn {
            background-color: #10b981;
            color: #000000;
            font-weight: bold;
            font-size: 11px;
            border-radius: 3px;
            padding: 4px 10px;
            border: none;
        }

        #ScreenHomeBtn:hover {
            background-color: #34d399;
        }

        #ScreenChannelIndicator {
            color: #ffffff;
            font-weight: bold;
            font-size: 14px;
            border-radius: 4px;
            padding: 4px 12px;
        }

        #ScreenClock {
            color: #f3f4f6;
            font-family: "Courier New", monospace;
            font-size: 14px;
            font-weight: bold;
            padding: 0 5px;
        }

        #ScreenClrSoloBtn {
            background-color: #2d2d3c;
            color: #6b7280;
            font-size: 10px;
            font-weight: bold;
            padding: 5px 8px;
            border: 1px solid #1e1e28;
            border-radius: 3px;
        }

        #ScreenMainMeter {
            max-width: 45px;
            max-height: 38px;
        }

        /* Screen Middle Pages & Left Menu */
        #ScreenLeftMenu {
            background-color: #15151e;
            border-right: 1px solid #232330;
            width: 80px;
            padding: 4px;
        }

        #ScreenLeftMenu QPushButton {
            color: #9ca3af;
            background-color: #1e1e28;
            border: 1px solid #272737;
            border-radius: 4px;
            font-weight: bold;
            font-size: 10px;
            padding: 10px 2px;
            min-width: 72px;
        }

        #ScreenLeftMenu QPushButton:hover {
            background-color: #272737;
            color: #ffffff;
        }

        #ScreenLeftMenu QPushButton:checked {
            color: #ffffff;
        }

        #ScreenLeftMenu QPushButton:nth-of-type(1):checked { background-color: #3b82f6; border-color: #2563eb; } /* Overview */
        #ScreenLeftMenu QPushButton:nth-of-type(2):checked { background-color: #06b6d4; border-color: #0891b2; } /* Input */
        #ScreenLeftMenu QPushButton:nth-of-type(3):checked { background-color: #f97316; border-color: #ea580c; } /* Gate */
        #ScreenLeftMenu QPushButton:nth-of-type(4):checked { background-color: #2563eb; border-color: #1d4ed8; } /* Comp */
        #ScreenLeftMenu QPushButton:nth-of-type(5):checked { background-color: #fbbf24; border-color: #d97706; color: #000; } /* EQ */
        #ScreenLeftMenu QPushButton:nth-of-type(6):checked { background-color: #10b981; border-color: #059669; } /* Main */
        #ScreenLeftMenu QPushButton:nth-of-type(7):checked { background-color: #a855f7; border-color: #8b5cf6; } /* Inserts */

        #ScreenStack {
            background-color: #101015;
        }

        /* Detail Boxes & Cards */
        #OverviewCard {
            background-color: #181822;
            border: 1px solid #272737;
            border-radius: 6px;
        }

        #OverviewCard:hover {
            border-color: #3b82f6;
            background-color: #1d1d2b;
        }

        #CardTitle {
            font-size: 14px;
            font-weight: bold;
            color: #ffffff;
        }

        #CardDesc {
            font-size: 11px;
            color: #71717a;
        }

        #CardOpenBtn {
            background-color: #2d2d3d;
            border: 1px solid #3f3f54;
            color: #d1d5db;
            font-size: 10px;
            font-weight: bold;
            padding: 4px;
            border-radius: 4px;
        }

        #CardOpenBtn:hover {
            background-color: #3b82f6;
            color: #ffffff;
            border-color: #2563eb;
        }

        #DetailSectionBox {
            background-color: #161620;
            border: 1px solid #252535;
            border-radius: 6px;
        }

        #DetailSectionBox QLabel {
            color: #ffffff;
            font-weight: bold;
            font-size: 12px;
            padding-bottom: 5px;
            border-bottom: 1px solid #232332;
        }

        /* LEDs / Option Switches */
        QPushButton#RedLedBtn, QPushButton#OrangeLedBtn, QPushButton#TealLedBtn, QPushButton#YellowLedBtn, QPushButton#BlueLedBtn {
            background-color: #1e1e28;
            color: #9ca3af;
            border: 1px solid #2b2b3b;
            border-radius: 3px;
            padding: 5px 8px;
            font-size: 10px;
            font-weight: bold;
        }

        QPushButton#RedLedBtn:checked { background-color: #7f1d1d; color: #fecaca; border-color: #ef4444; }
        QPushButton#OrangeLedBtn:checked { background-color: #7c2d12; color: #ffedd5; border-color: #ea580c; }
        QPushButton#TealLedBtn:checked { background-color: #134e5e; color: #cffafe; border-color: #06b6d4; }
        QPushButton#YellowLedBtn:checked { background-color: #713f12; color: #fef9c3; border-color: #eab308; }
        QPushButton#BlueLedBtn:checked { background-color: #1e3a8a; color: #dbeafe; border-color: #3b82f6; }

        /* On screen Fader */
        QSlider::groove:vertical#ChannelFaderLarge {
            background: #252532;
            width: 10px;
            border-radius: 5px;
        }

        QSlider::add-page:vertical#ChannelFaderLarge {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #10b981, stop:1 #059669);
            border-radius: 5px;
        }

        QSlider::handle:vertical#ChannelFaderLarge {
            background: #ffffff;
            border: 2px solid #10b981;
            height: 22px;
            width: 32px;
            margin: 0 -11px;
            border-radius: 3px;
        }

        #ScreenChannelMeter {
            min-width: 45px;
            max-width: 60px;
        }

        /* EQ selectors */
        #EqBandSelectBtn {
            background-color: #1e1e28;
            color: #9ca3af;
            border: 1px solid #282839;
            font-weight: bold;
            font-size: 11px;
            padding: 5px;
            border-radius: 3px;
        }

        #EqBandSelectBtn:checked {
            background-color: #fbbf24;
            color: #000;
            border-color: #d97706;
        }

        /* Bottom Encoder Strip */
        #BottomEncodersPanel {
            background-color: #15151d;
            border-top: 1px solid #232330;
            max-height: 105px;
        }

        #EncoderStrip {
            background-color: #121217;
            border: 1px solid #1f1f2c;
            border-radius: 4px;
            padding: 2px;
        }

        #BottomEncoderKnob {
            min-height: 90px;
        }

        /* =========================================================================
           PHYSICAL CONSOLE SIMULATOR
           ========================================================================= */
        #ConsoleBezel {
            background-color: #0f0f14;
            border-top: 3px solid #1f1f28;
        }

        #ConsoleDesk {
            background-color: #141419;
            border: 1.5px solid #2b2b3b;
            border-radius: 8px;
        }

        #PhysicalChannelStrip {
            background-color: #0e0e12;
            border: 1px solid #1e1e28;
            border-radius: 6px;
        }

        #ChannelSelectBtn {
            background-color: #242431;
            color: #d1d5db;
            font-size: 9px;
            font-weight: bold;
            border: 1px solid #313143;
            padding: 4px 0;
            border-radius: 3px;
        }

        #ChannelSelectBtn:hover {
            background-color: #3b82f6;
            color: #ffffff;
            border-color: #2563eb;
        }

        #PhysicalSoloBtn, #PhysicalMuteBtn {
            font-size: 10px;
            font-weight: bold;
            border-radius: 3px;
            padding: 5px 0;
            color: #9ca3af;
            border: 1px solid #1e1e2a;
            background-color: #181822;
        }

        #PhysicalSoloBtn:checked {
            background-color: #713f12;
            color: #fef9c3;
            border-color: #fbbf24;
        }

        #PhysicalMuteBtn:checked {
            background-color: #7f1d1d;
            color: #fecaca;
            border-color: #ef4444;
        }

        /* Physical Faders */
        QSlider::groove:vertical#PhysicalFaderSlider {
            background: #1d1d26;
            width: 8px;
            border-radius: 4px;
        }

        QSlider::handle:vertical#PhysicalFaderSlider {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 #e5e7eb, stop:0.4 #9ca3af,
                                        stop:0.5 #374151, stop:0.6 #9ca3af,
                                        stop:1 #e5e7eb);
            border: 1px solid #1f2937;
            height: 28px;
            width: 22px;
            margin: 0 -7px;
            border-radius: 2px;
        }

        QSlider::handle:vertical#PhysicalFaderSlider:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 #ffffff, stop:0.4 #d1d5db,
                                        stop:0.5 #4b5563, stop:0.6 #d1d5db,
                                        stop:1 #ffffff);
            border-color: #3b82f6;
        }

        #PhysicalLevelMeter {
            max-width: 10px;
        }

        #PhysicalDbLabel {
            color: #10b981;
            font-family: "Courier New", monospace;
            font-size: 10px;
            font-weight: bold;
            padding: 2px 0;
        }

        #ConsoleCollapseBtn {
            background-color: #181822;
            color: #a1a1aa;
            border: 1px solid #2d2d3d;
            border-radius: 4px;
            padding: 4px 15px;
            font-size: 10px;
            font-weight: bold;
        }
        #ConsoleCollapseBtn:hover {
            background-color: #272737;
            color: #ffffff;
            border-color: #3b82f6;
        }
        #ConsoleCollapseBtn:checked {
            background-color: #111116;
            border-color: #10b981;
            color: #10b981;
        }
    )";
    setStyleSheet(style);
}
