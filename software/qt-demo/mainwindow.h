#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QList>
#include <QPoint>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QTime>
#include <QVector>
#include <QSpinBox>
#include <QComboBox>
#include <QHBoxLayout>

#include "rotaryknob.h"
#include "levelmeter.h"
#include "eqgraphwidget.h"
#include "scribblestrip.h"
#include "audioparameter.h"
#include "mixerchannel.h"
#include "parameterbinder.h"
#include "oscclient.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Navigation / Actions
    void selectChannel(int index);
    void switchScreenPage(int pageIndex);
    void updateClock();
    void clearAllSolos();

    // EQ Graph interactions (which updates multiple model parameters)
    void onEqBandSelected(int bandIdx);
    void onEqGraphChanged(int bandIdx, double freq, double gain, double q);

private:
    void initChannelModel();
    void setupUI();
    void applyStyleSheet();
    
    // Updates UI bindings to reference the newly selected channel's parameters
    void populateUIFromSelectedChannel();
    void updateBottomEncodersLayout();
    void updateInsertSlotsLayout();
    void selectInsertSlot(int slotIdx);
    void populateInsertSlotUI();

    // Mixer Channels database (MVVM Model)
    QList<MixerChannel*> m_channels;
    int m_selectedChannelIndex;
    int m_activeEqBandIndex;

    // --- Observable Parameter Binders (MVVM View-Model layer) ---
    // Preamp panel binders
    ParameterBinder m_bindGain;
    ParameterBinder m_bindTrim;
    ParameterBinder m_bindPhantom;
    ParameterBinder m_bindPhase;
    ParameterBinder m_bindLoCutActive;
    ParameterBinder m_bindLoCutFreq;
    ParameterBinder m_bindHiCutActive;
    ParameterBinder m_bindHiCutFreq;
    ParameterBinder m_bindTilt;

    // EQ panel binders
    ParameterBinder m_bindEqActive;
    ParameterBinder m_bindEqGain;
    ParameterBinder m_bindEqFreq;
    ParameterBinder m_bindEqQ;

    // Dynamics panel binders
    ParameterBinder m_bindGateActive;
    ParameterBinder m_bindGateThreshold;
    ParameterBinder m_bindGateAttack;
    ParameterBinder m_bindGateHold;
    ParameterBinder m_bindGateRelease;
    ParameterBinder m_bindGateDepth;
    
    ParameterBinder m_bindCompActive;
    ParameterBinder m_bindCompThreshold;
    ParameterBinder m_bindCompRatio;
    ParameterBinder m_bindCompAttack;
    ParameterBinder m_bindCompHold;
    ParameterBinder m_bindCompRelease;
    ParameterBinder m_bindCompScLoCut;
    ParameterBinder m_bindCompScHiCut;

    // Main mix panel binders
    ParameterBinder m_bindScreenFader;
    ParameterBinder m_bindScreenPan;
    ParameterBinder m_bindScreenMute;
    ParameterBinder m_bindScreenSolo;

    // Bottom 7 parameter encoders binders
    QVector<ParameterBinder*> m_bottomEncBinders;

    // Physical strips binders
    QVector<ParameterBinder*> m_physFaderBinders;
    QVector<ParameterBinder*> m_physMuteBinders;
    QVector<ParameterBinder*> m_physSoloBinders;

    // Inserts panel binders
    ParameterBinder m_bindInsType;
    ParameterBinder m_bindInsBypass;
    ParameterBinder m_bindInsMix;
    ParameterBinder m_bindInsParam1;
    ParameterBinder m_bindInsParam2;
    QVector<QMetaObject::Connection> m_insertUIConnections;

    // OSC Network Client (Controller layer listener)
    OscClient *m_oscClient;

    // --- Touchscreen UI Elements ---
    // Top Bar
    QPushButton *m_homeButton;
    QLabel *m_selectedChannelIndicator;
    QLabel *m_clockLabel;
    QPushButton *m_clrSoloButton;
    LevelMeter *m_mainLRMeter;

    // Left Menu Tabs
    QPushButton *m_menuBtnOverview;
    QPushButton *m_menuBtnInput;
    QPushButton *m_menuBtnGate;
    QPushButton *m_menuBtnComp;
    QPushButton *m_menuBtnEq;
    QPushButton *m_menuBtnMain;
    QPushButton *m_menuBtnInserts;
    QList<QPushButton*> m_menuButtons;

    // Center Display Stack
    QStackedWidget *m_screenStack;
    
    // Overview Screen Page
    QWidget *m_pageOverview;
    
    // Overview Dashboard Labels
    QLabel *m_ovInputGain;
    QLabel *m_ovInputFilters;
    QLabel *m_ovInputTilt;
    QLabel *m_ovGateState;
    QLabel *m_ovGateParams;
    QLabel *m_ovCompState;
    QLabel *m_ovCompParams;
    QLabel *m_ovEqBands[4];
    QLabel *m_ovMixLevel;
    QLabel *m_ovMixPan;
    QLabel *m_ovMixMuteSolo;
    QLabel *m_ovInsertsCount;
    QLabel *m_ovInsertSlots[4];
    QVector<QMetaObject::Connection> m_overviewConnections;
    void updateOverviewLabels();

    // Detail Screen Pages
    QWidget *m_pageInput;
    QWidget *m_pageGate;
    QWidget *m_pageComp;
    QWidget *m_pageEq;
    QWidget *m_pageMain;
    QWidget *m_pageInserts;

    // Inserts detail widgets
    QSpinBox *m_insCountSpin;
    QFrame *m_insTabsContainer;
    QHBoxLayout *m_insTabsLayout;
    QList<QPushButton*> m_insTabButtons;
    QComboBox *m_insTypeCombo;
    QPushButton *m_insBypassBtn;
    RotaryKnob *m_insMixKnob;
    RotaryKnob *m_insParam1Knob;
    RotaryKnob *m_insParam2Knob;
    int m_activeInsertSlotIndex;

    // --- On-Screen Details Widgets ---
    // Input/Filter detail widgets
    RotaryKnob *m_inpGainKnob;
    RotaryKnob *m_inpTrimKnob;
    QPushButton *m_inpPhantomBtn;
    QPushButton *m_inpPhaseBtn;
    QPushButton *m_inpLoCutBtn;
    RotaryKnob *m_inpLoCutFreqKnob;
    QPushButton *m_inpHiCutBtn;
    RotaryKnob *m_inpHiCutFreqKnob;
    RotaryKnob *m_inpTiltKnob;

    // EQ detail widgets
    EqGraphWidget *m_eqGraph;
    QPushButton *m_eqBandButtons[4];
    QPushButton *m_eqBandActiveBtn;
    RotaryKnob *m_eqGainKnob;
    RotaryKnob *m_eqFreqKnob;
    RotaryKnob *m_eqQKnob;

    // Dynamics detail widgets
    QPushButton *m_gateActiveBtn;
    RotaryKnob *m_gateThresholdKnob;
    RotaryKnob *m_gateAttackKnob;
    RotaryKnob *m_gateHoldKnob;
    RotaryKnob *m_gateReleaseKnob;
    RotaryKnob *m_gateDepthKnob;

    QPushButton *m_compActiveBtn;
    RotaryKnob *m_compThresholdKnob;
    RotaryKnob *m_compRatioKnob;
    RotaryKnob *m_compAttackKnob;
    RotaryKnob *m_compHoldKnob;
    RotaryKnob *m_compReleaseKnob;
    RotaryKnob *m_compScLoCutKnob;
    RotaryKnob *m_compScHiCutKnob;

    // Main detail widgets
    QSlider *m_screenFader;
    LevelMeter *m_screenMeter;
    RotaryKnob *m_screenPanKnob;
    QPushButton *m_screenMuteBtn;
    QPushButton *m_screenSoloBtn;

    // --- Screen Parameter Encoders (Bottom row of 7) ---
    QFrame *m_bottomEncodersPanel;
    QList<RotaryKnob*> m_bottomEncoders;
    QList<QLabel*> m_bottomEncoderLabels;

    // --- Physical Console Simulation (Bottom row) ---
    QList<ScribbleStrip*> m_physScribbles;
    QList<QPushButton*> m_physSoloBtns;
    QList<QPushButton*> m_physMuteBtns;
    QList<LevelMeter*> m_physMeters;
    QList<QSlider*> m_physFaders;
    QList<QLabel*> m_physDbLabels;

    QTimer *m_clockTimer;
};

#endif // MAINWINDOW_H
