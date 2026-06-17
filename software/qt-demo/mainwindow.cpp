#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_channelstrip.h"
#include "ui_encoderstrip.h"
#include "compcurvewidget.h"
#include "envelopegraphwidget.h"

#include <QFrame>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QTime>
#include <QFile>
#include <QTextStream>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
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

    // 4. Setup UI components and layout (Designer form + dynamic content)
    setupUI();

    // 5. Populate detail panel binders with the first channel's state
    populateUIFromSelectedChannel();

    // 6. Start clock timer
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

    delete ui;
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
            ch->parameter("eq6_gain")->setValue(2.5); // Air boost
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
            ui->ScreenClrSoloBtn->setEnabled(anySolo);
            if (anySolo) {
                ui->ScreenClrSoloBtn->setStyleSheet("background-color: #fbbf24; color: #000000; font-weight: bold; border-color: #f59e0b;");
            } else {
                ui->ScreenClrSoloBtn->setStyleSheet("");
            }
        });

        m_channels.append(ch);
    }
}

void MainWindow::setupUI()
{
    // Instantiate the Designer-generated static shell + all 7 stacked pages
    ui->setupUi(this);

    // --- Wire up the static widgets from the form ---

    // Left menu tabs (collected in display order)
    m_menuButtons << ui->menuBtnOverview << ui->menuBtnInput << ui->menuBtnGate
                  << ui->menuBtnComp << ui->menuBtnEq << ui->menuBtnMain << ui->menuBtnInserts;
    for (int i = 0; i < m_menuButtons.size(); ++i) {
        connect(m_menuButtons[i], &QPushButton::clicked, this, [this, i]() {
            switchScreenPage(i);
        });
    }

    // Top bar buttons
    connect(ui->ScreenHomeBtn, &QPushButton::clicked, this, [this]() { switchScreenPage(0); });
    connect(ui->ScreenClrSoloBtn, &QPushButton::clicked, this, &MainWindow::clearAllSolos);

    // Overview "OPEN PANEL" buttons -> their detail pages
    connect(ui->ovOpenInput,   &QPushButton::clicked, this, [this]() { switchScreenPage(1); });
    connect(ui->ovOpenGate,    &QPushButton::clicked, this, [this]() { switchScreenPage(2); });
    connect(ui->ovOpenComp,    &QPushButton::clicked, this, [this]() { switchScreenPage(3); });
    connect(ui->ovOpenEq,      &QPushButton::clicked, this, [this]() { switchScreenPage(4); });
    connect(ui->ovOpenMix,     &QPushButton::clicked, this, [this]() { switchScreenPage(5); });
    connect(ui->ovOpenInserts, &QPushButton::clicked, this, [this]() { switchScreenPage(6); });

    // EQ graph & band selection
    connect(ui->EqGraph, &EqGraphWidget::bandSelected, this, &MainWindow::onEqBandSelected);
    connect(ui->EqGraph, &EqGraphWidget::bandChanged, this, &MainWindow::onEqGraphChanged);

    QPushButton *eqBandBtns[6] = { ui->eqBandBtnL, ui->eqBandBtn1, ui->eqBandBtn2, ui->eqBandBtn3, ui->eqBandBtn4, ui->eqBandBtnH };
    for (int i = 0; i < 6; ++i) {
        connect(eqBandBtns[i], &QPushButton::clicked, this, [this, i]() { onEqBandSelected(i); });
    }



    // Dragging the transfer curve / envelope edits the selected channel's params
    connect(ui->compCurve, &CompCurveWidget::thresholdChanged, this, [this](double db) {
        m_channels[m_selectedChannelIndex]->parameter("comp_threshold")->setValue(db);
    });
    connect(ui->compCurve, &CompCurveWidget::ratioChanged, this, [this](double r) {
        m_channels[m_selectedChannelIndex]->parameter("comp_ratio")->setValue(r);
    });
    connect(ui->compEnvelope, &EnvelopeGraphWidget::attackChanged, this, [this](double ms) {
        m_channels[m_selectedChannelIndex]->parameter("comp_attack")->setValue(ms);
    });
    connect(ui->compEnvelope, &EnvelopeGraphWidget::releaseChanged, this, [this](double ms) {
        m_channels[m_selectedChannelIndex]->parameter("comp_release")->setValue(ms);
    });

    connect(ui->gateCurve, &GateCurveWidget::thresholdChanged, this, [this](double db) {
        m_channels[m_selectedChannelIndex]->parameter("gate_threshold")->setValue(db);
    });
    connect(ui->gateCurve, &GateCurveWidget::ratioChanged, this, [this](double r) {
        m_channels[m_selectedChannelIndex]->parameter("gate_ratio")->setValue(r);
    });
    connect(ui->gateEnvelope, &EnvelopeGraphWidget::attackChanged, this, [this](double ms) {
        m_channels[m_selectedChannelIndex]->parameter("gate_attack")->setValue(ms);
    });
    connect(ui->gateEnvelope, &EnvelopeGraphWidget::releaseChanged, this, [this](double ms) {
        m_channels[m_selectedChannelIndex]->parameter("gate_release")->setValue(ms);
    });

    // --- Bottom parameter encoders (instantiated from encoderstrip.ui) ---
    for (int i = 0; i < 6; ++i) {
        QFrame *encoderStrip = new QFrame(ui->BottomEncodersPanel);
        Ui::EncoderStrip es;
        es.setupUi(encoderStrip);
        m_bottomEncoders.append(es.BottomEncoderKnob);
        ui->bottomEncLayout->addWidget(encoderStrip);
    }

    // --- Physical console channel strips (instantiated from channelstrip.ui) ---
    for (int i = 0; i < 8; ++i) {
        QFrame *physStrip = new QFrame(ui->ConsoleDesk);
        Ui::ChannelStrip cs;
        cs.setupUi(physStrip);

        ScribbleStrip *scribble = cs.PhysicalScribbleStrip;
        scribble->setChannelInfo(m_channels[i]->number(), m_channels[i]->name(), m_channels[i]->color());
        m_physScribbles.append(scribble);

        connect(cs.ChannelSelectBtn, &QPushButton::clicked, this, [this, i]() {
            selectChannel(i);
        });

        QPushButton *soloBtn = cs.PhysicalSoloBtn;
        m_physSoloBtns.append(soloBtn);
        QPushButton *muteBtn = cs.PhysicalMuteBtn;
        m_physMuteBtns.append(muteBtn);

        QSlider *fader = cs.PhysicalFaderSlider;
        m_physFaders.append(fader);
        LevelMeter *meter = cs.PhysicalLevelMeter;
        m_physMeters.append(meter);

        QLabel *dbLabel = cs.PhysicalDbLabel;
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

        ui->deskLayout->addWidget(physStrip);
    }

    // --- Physical console collapse/expand toggle ---
    connect(ui->ConsoleCollapseBtn, &QPushButton::clicked, this, [this](bool checked) {
        ui->ConsoleDesk->setVisible(!checked);
        ui->ConsoleCollapseBtn->setText(checked ? "▲ SHOW PHYSICAL CONTROLS" : "▼ HIDE PHYSICAL CONTROLS");
        if (auto *rl = qobject_cast<QVBoxLayout*>(centralWidget()->layout())) {
            rl->setStretchFactor(ui->ConsoleBezel, checked ? 0 : 2);
        }
    });
    // Start collapsed (form sets ConsoleDesk hidden + button checked)
    if (auto *rl = qobject_cast<QVBoxLayout*>(centralWidget()->layout())) {
        rl->setStretchFactor(ui->ConsoleBezel, 0);
    }
}

void MainWindow::selectChannel(int index)
{
    if (index < 0 || index >= m_channels.size() || index == m_selectedChannelIndex) return;

    m_selectedChannelIndex = index;
    populateUIFromSelectedChannel();
}

void MainWindow::switchScreenPage(int pageIndex)
{
    ui->ScreenStack->setCurrentIndex(pageIndex);

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

    QLabel *ovEqBands[4] = { ui->m_ovEqBand0, ui->m_ovEqBand1, ui->m_ovEqBand2, ui->m_ovEqBand3 };
    QLabel *ovInsertSlots[4] = { ui->m_ovInsertSlot0, ui->m_ovInsertSlot1, ui->m_ovInsertSlot2, ui->m_ovInsertSlot3 };

    // 1. Input & Filters Overview
    double gain = ch->parameter("gain")->value();
    double trim = ch->parameter("trim")->value();
    bool phantom = ch->parameter("phantom")->value() > 0.5;
    bool phase = ch->parameter("phase")->value() > 0.5;
    ui->m_ovInputGain->setText(QString("Gain: +%1 dB | Trim: %2%3 dB\n48V: %4 | Phase: %5")
        .arg(gain, 0, 'f', 1)
        .arg(trim >= 0.0 ? "+" : "")
        .arg(trim, 0, 'f', 1)
        .arg(phantom ? "ON" : "OFF")
        .arg(phase ? "ON" : "OFF"));

    bool loCut = ch->parameter("locut_active")->value() > 0.5;
    bool hiCut = ch->parameter("hicut_active")->value() > 0.5;
    ui->m_ovInputFilters->setText(QString("LoCut: %1 | HiCut: %2")
        .arg(loCut ? QString("%1 Hz").arg(qRound(ch->parameter("locut_freq")->value())) : "OFF")
        .arg(hiCut ? QString("%1 kHz").arg(ch->parameter("hicut_freq")->value() / 1000.0, 0, 'f', 1) : "OFF"));

    double tilt = ch->parameter("tilt")->value();
    ui->m_ovInputTilt->setText(QString("Tilt EQ: %1%2 dB")
        .arg(tilt >= 0.0 ? "+" : "")
        .arg(tilt, 0, 'f', 1));

    // 2. Noise Gate Overview
    bool gateActive = ch->parameter("gate_active")->value() > 0.5;
    ui->m_ovGateState->setText(QString("State: %1").arg(gateActive ? "<span style='color:#f97316; font-weight:bold;'>ON</span>" : "OFF"));
    ui->m_ovGateParams->setText(QString("Thr: %1 dB | Depth: %2 dB\nAtt: %3 ms | Hold: %4 ms | Rel: %5 ms")
        .arg(ch->parameter("gate_threshold")->value(), 0, 'f', 1)
        .arg(ch->parameter("gate_depth")->value(), 0, 'f', 1)
        .arg(ch->parameter("gate_attack")->value(), 0, 'f', 1)
        .arg(ch->parameter("gate_hold")->value(), 0, 'f', 0)
        .arg(ch->parameter("gate_release")->value(), 0, 'f', 0));

    // 3. Compressor Overview
    bool compActive = ch->parameter("comp_active")->value() > 0.5;
    ui->m_ovCompState->setText(QString("State: %1").arg(compActive ? "<span style='color:#3b82f6; font-weight:bold;'>ON</span>" : "OFF"));
    double scLocut = ch->parameter("comp_sc_locut")->value();
    double scHicut = ch->parameter("comp_sc_hicut")->value();
    QString scHicutStr = scHicut >= 1000.0 ? QString("%1 kHz").arg(scHicut / 1000.0, 0, 'f', 1) : QString("%1 Hz").arg(qRound(scHicut));
    ui->m_ovCompParams->setText(QString("Thr: %1 dB | Ratio: %2:1 | Hold: %3 ms\nAtt: %4 ms | Rel: %5 ms\nSC Filter: %6 Hz - %7")
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
        ovEqBands[b]->setText(QString("Band %1 (%2): %3 Hz | %4%5 dB | Q: %6")
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
    ui->m_ovMixLevel->setText(QString("Fader Level: %1").arg(lvlStr));

    double pan = ch->parameter("pan")->value();
    QString panStr = pan == 0.0 ? "C" : (pan < 0.0 ? QString("L%1").arg(qAbs(qRound(pan))) : QString("R%1").arg(qRound(pan)));
    ui->m_ovMixPan->setText(QString("Pan position: %1").arg(panStr));

    bool mute = ch->parameter("mute")->value() > 0.5;
    bool solo = ch->parameter("solo")->value() > 0.5;
    ui->m_ovMixMuteSolo->setText(QString("Mute: %1 | Solo: %2")
        .arg(mute ? "<span style='color:#ef4444; font-weight:bold;'>MUTED</span>" : "OFF")
        .arg(solo ? "<span style='color:#fbbf24; font-weight:bold;'>SOLOED</span>" : "OFF"));

    // 6. Channel Inserts Overview
    int activeSlots = ch->numInsertPages();
    ui->m_ovInsertsCount->setText(QString("Rack Slots Active: %1").arg(activeSlots));
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
                ovInsertSlots[s]->setText(QString("Slot %1: Empty").arg(s + 1));
            } else {
                double mix = ch->parameter(prefix + "mix")->value();
                double p1 = ch->parameter(prefix + "param1")->value();
                double p2 = ch->parameter(prefix + "param2")->value();
                QString p1Name = ch->parameter(prefix + "param1")->name();
                QString p2Name = ch->parameter(prefix + "param2")->name();
                QString p1Unit = ch->parameter(prefix + "param1")->unit();
                QString p2Unit = ch->parameter(prefix + "param2")->unit();

                ovInsertSlots[s]->setText(QString("Slot %1: %2 (%3) | Mix: %4%\n  %5: %6%7 | %8: %9%10")
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
            ovInsertSlots[s]->setText(QString("Slot %1: Locked").arg(s + 1));
        }
    }
}

void MainWindow::updateClock()
{
    ui->ScreenClock->setText(QTime::currentTime().toString("hh:mm:ss"));
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
    ui->ScreenChannelIndicator->setText(QString("%1 %2").arg(ch->number()).arg(ch->name()));
    ui->ScreenChannelIndicator->setStyleSheet(QString(
        "background-color: %1;"
        "color: #ffffff;"
        "font-weight: bold;"
        "font-size: 14px;"
        "border-radius: 4px;"
        "padding: 4px 12px;"
    ).arg(ch->color().name()));

    // 2. Bind Preamp/Input widgets
    m_bindGain.bind(ui->inpGainKnob, ch->parameter("gain"));
    m_bindTrim.bind(ui->inpTrimKnob, ch->parameter("trim"));
    m_bindPhantom.bind(ui->inpPhantomBtn, ch->parameter("phantom"));
    m_bindPhase.bind(ui->inpPhaseBtn, ch->parameter("phase"));

    m_bindLoCutActive.bind(ui->inpLoCutBtn, ch->parameter("locut_active"));
    m_bindLoCutFreq.bind(ui->inpLoCutFreqKnob, ch->parameter("locut_freq"));

    m_bindHiCutActive.bind(ui->inpHiCutBtn, ch->parameter("hicut_active"));
    m_bindHiCutFreq.bind(ui->inpHiCutFreqKnob, ch->parameter("hicut_freq"));

    m_bindTilt.bind(ui->inpTiltKnob, ch->parameter("tilt"));

    // Interconnect enabling/disabling states directly to active parameters
    ui->inpLoCutFreqKnob->setEnabled(ch->parameter("locut_active")->value() > 0.5);
    connect(ch->parameter("locut_active"), &AudioParameter::valueChanged, ui->inpLoCutFreqKnob, [this](double val) {
        ui->inpLoCutFreqKnob->setEnabled(val > 0.5);
    });

    ui->inpHiCutFreqKnob->setEnabled(ch->parameter("hicut_active")->value() > 0.5);
    connect(ch->parameter("hicut_active"), &AudioParameter::valueChanged, ui->inpHiCutFreqKnob, [this](double val) {
        ui->inpHiCutFreqKnob->setEnabled(val > 0.5);
    });

    // 3. Bind EQ parameters & update EQ graph
    for (int i = 0; i < 6; ++i) {
        ui->EqGraph->setBand(i,
            ch->parameter(QString("eq%1_freq").arg(i + 1))->value(),
            ch->parameter(QString("eq%1_gain").arg(i + 1))->value(),
            ch->parameter(QString("eq%1_q").arg(i + 1))->value(),
            ch->parameter(QString("eq%1_active").arg(i + 1))->value() > 0.5
        );
    }

    for (const auto &c : m_eqConnections) disconnect(c);
    m_eqConnections.clear();

    for (int i = 0; i < 6; ++i) {
        QString prefix = QString("eq%1_").arg(i + 1);
        AudioParameter *freqParam = ch->parameter(prefix + "freq");
        AudioParameter *gainParam = ch->parameter(prefix + "gain");
        AudioParameter *qParam = ch->parameter(prefix + "q");
        AudioParameter *activeParam = ch->parameter(prefix + "active");

        auto updateGraphBand = [this, i, freqParam, gainParam, qParam, activeParam]() {
            ui->EqGraph->setBand(i,
                freqParam->value(),
                gainParam->value(),
                qParam->value(),
                activeParam->value() > 0.5
            );
        };

        m_eqConnections << connect(freqParam, &AudioParameter::valueChanged, this, updateGraphBand);
        m_eqConnections << connect(gainParam, &AudioParameter::valueChanged, this, updateGraphBand);
        m_eqConnections << connect(qParam, &AudioParameter::valueChanged, this, updateGraphBand);
        m_eqConnections << connect(activeParam, &AudioParameter::valueChanged, this, updateGraphBand);
    }

    m_activeEqBandIndex = ui->EqGraph->activeBandIndex();
    QPushButton *eqBandBtns[6] = { ui->eqBandBtnL, ui->eqBandBtn1, ui->eqBandBtn2, ui->eqBandBtn3, ui->eqBandBtn4, ui->eqBandBtnH };
    for (int i = 0; i < 6; ++i) {
        eqBandBtns[i]->setChecked(i == m_activeEqBandIndex);
    }

    QString eqPrefix = QString("eq%1_").arg(m_activeEqBandIndex + 1);
    m_bindEqActive.bind(ui->eqBandActiveBtn, ch->parameter(eqPrefix + "active"));
    m_bindEqFreq.bind(ui->eqFreqKnob, ch->parameter(eqPrefix + "freq"));
    m_bindEqGain.bind(ui->eqGainKnob, ch->parameter(eqPrefix + "gain"));
    m_bindEqQ.bind(ui->eqQKnob, ch->parameter(eqPrefix + "q"));

    // Update EQ knobs coloring
    QColor bandColor = ui->EqGraph->band(m_activeEqBandIndex).color;
    ui->eqGainKnob->setThemeColor(bandColor);
    ui->eqFreqKnob->setThemeColor(bandColor);
    ui->eqQKnob->setThemeColor(bandColor);

    // 4. Bind Dynamics widgets - Noise Gate
    m_bindGateActive.bind(ui->gateActiveBtn, ch->parameter("gate_active"));
    m_bindGateModel.bind(ui->gateModelCombo, ch->parameter("gate_model"));
    m_bindGateKeySource.bind(ui->gateKeySourceCombo, ch->parameter("gate_key_source"));
    m_bindGateAccent.bind(ui->gateAccentKnob, ch->parameter("gate_accent"));
    m_bindGateKeySolo.bind(ui->gateKeySoloBtn, ch->parameter("gate_key_solo"));
    m_bindGateKeyFilter.bind(ui->gateKeyFilterCombo, ch->parameter("gate_key_filter"));
    m_bindGateAttack.bind(ui->gateAttackKnob, ch->parameter("gate_attack"));
    m_bindGateHold.bind(ui->gateHoldKnob, ch->parameter("gate_hold"));
    m_bindGateRelease.bind(ui->gateReleaseKnob, ch->parameter("gate_release"));

    // Tear down the previous channel's gate-page live connections
    for (const auto &c : m_gateConnections) disconnect(c);
    m_gateConnections.clear();

    bool gateActive = ch->parameter("gate_active")->value() > 0.5;

    // ON/OFF button label reflects state
    ui->gateActiveBtn->setText(gateActive ? "ON" : "OFF");
    m_gateConnections << connect(ch->parameter("gate_active"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->gateActiveBtn->setText(v > 0.5 ? "ON" : "OFF"); });

    // Gate Curve widget reflects threshold / ratio / depth / active
    ui->gateCurve->setThreshold(ch->parameter("gate_threshold")->value());
    ui->gateCurve->setRatio(ch->parameter("gate_ratio")->value());
    ui->gateCurve->setDepth(ch->parameter("gate_depth")->value());
    ui->gateCurve->setActive(gateActive);
    m_gateConnections << connect(ch->parameter("gate_threshold"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->gateCurve->setThreshold(v); });
    m_gateConnections << connect(ch->parameter("gate_ratio"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->gateCurve->setRatio(v); });
    m_gateConnections << connect(ch->parameter("gate_depth"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->gateCurve->setDepth(v); });

    // Envelope widget reflects attack / release / hold / active
    ui->gateEnvelope->setAttack(ch->parameter("gate_attack")->value());
    ui->gateEnvelope->setRelease(ch->parameter("gate_release")->value());
    ui->gateEnvelope->setHold(ch->parameter("gate_hold")->value());
    ui->gateEnvelope->setActive(gateActive);
    m_gateConnections << connect(ch->parameter("gate_attack"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->gateEnvelope->setAttack(v); });
    m_gateConnections << connect(ch->parameter("gate_release"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->gateEnvelope->setRelease(v); });
    m_gateConnections << connect(ch->parameter("gate_hold"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->gateEnvelope->setHold(v); });

    m_gateConnections << connect(ch->parameter("gate_active"), &AudioParameter::valueChanged, this,
        [this](double v) { bool a = v > 0.5; ui->gateCurve->setActive(a); ui->gateEnvelope->setActive(a); });

    // Enable/disable based on gate_active
    ui->gateModelCombo->setEnabled(gateActive);
    ui->gateKeySourceCombo->setEnabled(gateActive);
    ui->gateAccentKnob->setEnabled(gateActive);
    ui->gateKeySoloBtn->setEnabled(gateActive);
    ui->gateKeyFilterCombo->setEnabled(gateActive);
    ui->gateAttackKnob->setEnabled(gateActive);
    ui->gateHoldKnob->setEnabled(gateActive);
    ui->gateReleaseKnob->setEnabled(gateActive);

    m_gateConnections << connect(ch->parameter("gate_active"), &AudioParameter::valueChanged, this, [this](double val) {
        bool active = val > 0.5;
        ui->gateModelCombo->setEnabled(active);
        ui->gateKeySourceCombo->setEnabled(active);
        ui->gateAccentKnob->setEnabled(active);
        ui->gateKeySoloBtn->setEnabled(active);
        ui->gateKeyFilterCombo->setEnabled(active);
        ui->gateAttackKnob->setEnabled(active);
        ui->gateHoldKnob->setEnabled(active);
        ui->gateReleaseKnob->setEnabled(active);
    });

    // Tear down the previous channel's compressor-page live connections
    for (const auto &c : m_compConnections) disconnect(c);
    m_compConnections.clear();

    m_bindCompActive.bind(ui->compActiveBtn, ch->parameter("comp_active"));
    m_bindCompMix.bind(ui->compMixKnob, ch->parameter("comp_mix"));
    m_bindCompGain.bind(ui->compGainKnob, ch->parameter("comp_gain"));
    m_bindCompKeySource.bind(ui->compKeySourceCombo, ch->parameter("comp_key_source"));
    m_bindCompAttack.bind(ui->compAttackKnob, ch->parameter("comp_attack"));
    m_bindCompRelease.bind(ui->compReleaseKnob, ch->parameter("comp_release"));
    m_bindCompHold.bind(ui->compHoldKnob, ch->parameter("comp_hold"));

    bool compActive = ch->parameter("comp_active")->value() > 0.5;

    // ON/OFF button label reflects state
    ui->compActiveBtn->setText(compActive ? "ON" : "OFF");
    m_compConnections << connect(ch->parameter("comp_active"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->compActiveBtn->setText(v > 0.5 ? "ON" : "OFF"); });

    // Transfer-curve widget reflects threshold / ratio / knee / makeup / active
    ui->compCurve->setThreshold(ch->parameter("comp_threshold")->value());
    ui->compCurve->setRatio(ch->parameter("comp_ratio")->value());
    ui->compCurve->setKnee(ch->parameter("comp_knee")->value());
    ui->compCurve->setMakeup(ch->parameter("comp_gain")->value());
    ui->compCurve->setActive(compActive);
    m_compConnections << connect(ch->parameter("comp_threshold"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->compCurve->setThreshold(v); });
    m_compConnections << connect(ch->parameter("comp_ratio"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->compCurve->setRatio(v); });
    m_compConnections << connect(ch->parameter("comp_knee"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->compCurve->setKnee(v); });
    m_compConnections << connect(ch->parameter("comp_gain"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->compCurve->setMakeup(v); });

    // Envelope widget reflects attack / release / hold / active
    ui->compEnvelope->setAttack(ch->parameter("comp_attack")->value());
    ui->compEnvelope->setRelease(ch->parameter("comp_release")->value());
    ui->compEnvelope->setHold(ch->parameter("comp_hold")->value());
    ui->compEnvelope->setActive(compActive);
    m_compConnections << connect(ch->parameter("comp_attack"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->compEnvelope->setAttack(v); });
    m_compConnections << connect(ch->parameter("comp_release"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->compEnvelope->setRelease(v); });
    m_compConnections << connect(ch->parameter("comp_hold"), &AudioParameter::valueChanged, this,
        [this](double v) { ui->compEnvelope->setHold(v); });

    m_compConnections << connect(ch->parameter("comp_active"), &AudioParameter::valueChanged, this,
        [this](double v) { bool a = v > 0.5; ui->compCurve->setActive(a); ui->compEnvelope->setActive(a); });





    // 5. Bind Main mixing output widgets
    m_bindScreenFader.bind(ui->ChannelFaderLarge, ch->parameter("level"));
    m_bindScreenPan.bind(ui->screenPanKnob, ch->parameter("pan"));
    m_bindScreenMute.bind(ui->screenMuteBtn, ch->parameter("mute"));
    m_bindScreenSolo.bind(ui->screenSoloBtn, ch->parameter("solo"));

    // 6. Highlight selected scribble strip
    for (int i = 0; i < m_physScribbles.size(); ++i) {
        m_physScribbles[i]->setSelected(i == m_selectedChannelIndex);
    }

    // Set active inserts spinbox count
    ui->InsCountSpin->blockSignals(true);
    ui->InsCountSpin->setValue(ch->numInsertPages());
    ui->InsCountSpin->blockSignals(false);

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
    if (ui->ScreenStack->currentIndex() == 6) {
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
    while ((child = ui->insTabsLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }
    m_insTabButtons.clear();

    for (int i = 0; i < count; ++i) {
        QPushButton *btn = new QPushButton(QString("SLOT %1").arg(i + 1), ui->pageInserts);
        btn->setCheckable(true);
        btn->setObjectName("InsSlotTabBtn");

        if (i == m_activeInsertSlotIndex) {
            btn->setChecked(true);
        }
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            selectInsertSlot(i);
        });
        ui->insTabsLayout->addWidget(btn);
        m_insTabButtons.append(btn);
    }
    ui->insTabsLayout->addStretch(1);

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
    m_bindInsType.bind(ui->InsTypeCombo, pType);

    // Bind bypass and mix
    m_bindInsBypass.bind(ui->insBypassBtn, pBypass);
    m_bindInsMix.bind(ui->insMixKnob, pMix);

    // Bind param 1 and 2
    m_bindInsParam1.bind(ui->insParam1Knob, p1);
    m_bindInsParam2.bind(ui->insParam2Knob, p2);

    // Enable/disable mix and parameters if bypass is active
    bool isBypassed = (pBypass->value() > 0.5);
    ui->insMixKnob->setEnabled(!isBypassed);
    ui->insParam1Knob->setEnabled(!isBypassed);
    ui->insParam2Knob->setEnabled(!isBypassed);

    // Connect bypass changes to disable/enable state
    m_insertUIConnections << connect(pBypass, &AudioParameter::valueChanged, this, [this](double val) {
        bool bypass = (val > 0.5);
        ui->insMixKnob->setEnabled(!bypass);
        ui->insParam1Knob->setEnabled(!bypass);
        ui->insParam2Knob->setEnabled(!bypass);
    });

    // When type changes, reconfigure parameters metadata and rebind
    m_insertUIConnections << connect(pType, &AudioParameter::valueChanged, this, [this, ch](double val) {
        QString pref = QString("insert%1_").arg(m_activeInsertSlotIndex + 1);
        AudioParameter *p1_new = ch->parameter(pref + "param1");
        AudioParameter *p2_new = ch->parameter(pref + "param2");

        ch->configureInsertSlotParameters(m_activeInsertSlotIndex, qRound(val));

        m_bindInsParam1.bind(ui->insParam1Knob, p1_new);
        m_bindInsParam2.bind(ui->insParam2Knob, p2_new);

        updateBottomEncodersLayout();
    });
}

void MainWindow::updateBottomEncodersLayout()
{
    int pageIdx = ui->ScreenStack->currentIndex();
    MixerChannel *ch = m_channels[m_selectedChannelIndex];

    QVector<AudioParameter*> activeParams(6, nullptr);

    if (pageIdx == 0) { // Overview
        activeParams[0] = ch->parameter("gain");
        activeParams[1] = ch->parameter("locut_freq");
        activeParams[2] = ch->parameter("eq1_freq");
        activeParams[3] = ch->parameter("eq6_freq");
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
        activeParams[0] = ch->parameter("gate_ratio");
        activeParams[1] = ch->parameter("gate_threshold");
        activeParams[2] = ch->parameter("gate_attack");
        activeParams[3] = ch->parameter("gate_hold");
        activeParams[4] = ch->parameter("gate_release");
        activeParams[5] = ch->parameter("gate_depth");
    } else if (pageIdx == 3) { // Compressor
        activeParams[0] = ch->parameter("comp_threshold");
        activeParams[1] = ch->parameter("comp_ratio");
        activeParams[2] = ch->parameter("comp_attack");
        activeParams[3] = ch->parameter("comp_hold");
        activeParams[4] = ch->parameter("comp_release");
        activeParams[5] = ch->parameter("comp_gain");
    } else if (pageIdx == 4) { // EQ
        QString eqPrefix = QString("eq%1_").arg(m_activeEqBandIndex + 1);
        activeParams[0] = ch->parameter(eqPrefix + "freq");
        activeParams[1] = ch->parameter(eqPrefix + "gain");
        activeParams[2] = ch->parameter(eqPrefix + "q");
        activeParams[3] = ch->parameter(eqPrefix + "active");
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
    if (ui->EqGraph->activeBandIndex() != bandIdx) {
        ui->EqGraph->setActiveBandIndex(bandIdx);
        return;
    }

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
    ui->EqGraph->setBand(bandIdx, freq, gain, q, ch->parameter(eqPrefix + "active")->value() > 0.5);
}

// =========================================================================
//   STYLESHEET / UI DESIGN SYSTEM
// =========================================================================
void MainWindow::applyStyleSheet()
{
    QFile styleFile(":/styles.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream stream(&styleFile);
        setStyleSheet(stream.readAll());
        styleFile.close();
    }
}
