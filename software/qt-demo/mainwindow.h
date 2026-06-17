#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QVector>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QTimer>

#include "rotaryknob.h"
#include "levelmeter.h"
#include "scribblestrip.h"
#include "audioparameter.h"
#include "mixerchannel.h"
#include "parameterbinder.h"
#include "oscclient.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

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
    void updateOverviewLabels();

    // Designer-generated UI (forms/mainwindow.ui)
    Ui::MainWindow *ui;

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

    // Bottom parameter encoders binders
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

    // --- Widgets created/collected at runtime (dynamic, not in .ui) ---
    // Left menu tabs (collected from the .ui buttons in display order)
    QList<QPushButton*> m_menuButtons;

    // Overview dashboard live-update connections
    QVector<QMetaObject::Connection> m_overviewConnections;

    // Inserts: dynamic slot tab buttons
    QList<QPushButton*> m_insTabButtons;
    int m_activeInsertSlotIndex;

    // --- Screen Parameter Encoders (Bottom row, instantiated from encoderstrip.ui) ---
    QList<RotaryKnob*> m_bottomEncoders;

    // --- Physical Console Simulation (instantiated from channelstrip.ui) ---
    QList<ScribbleStrip*> m_physScribbles;
    QList<QPushButton*> m_physSoloBtns;
    QList<QPushButton*> m_physMuteBtns;
    QList<LevelMeter*> m_physMeters;
    QList<QSlider*> m_physFaders;
    QList<QLabel*> m_physDbLabels;

    QTimer *m_clockTimer;
};

#endif // MAINWINDOW_H
