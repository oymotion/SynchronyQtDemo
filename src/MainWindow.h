#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <SensorController.hpp>

#include "DeviceState.h"
#include "SdkBridge.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QRadioButton;
class QTimer;
class CubeWidget;
class SpectrumWidget;
class WaveformWidget;

// Demo main window over the C++ sensor SDK: multi-device scan/connect/stream,
// a synchronized multi start/stop toggle, paged EEG (+ ECG / BRTH) or EMG bio
// panel, setParam controls, debug log and bin recording toggles, auto
// reconnect, the bin replay family (single + synchronized group replay), a
// Live Filter band combo for the bio waveforms, an FFT spectrum strip below
// the 2D waveform, and per-channel FFT spectra on the EMG/EEG bio rows.
// The window title carries the demo's DEMO_VERSION.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // UI actions
    void onStartScan();
    void onStopScan();
    void onConnectClicked();
    void onDisconnectClicked();
    void onDeviceSelected(QListWidgetItem* item);
    void onTypeChanged(int index);
    void onLiveFilterChanged(int index);
    void onNtfToggled();
    void onFilterToggled();
    void onSampleRateToggled();
    void onDebugLogToggled(int state);
    void onBinDataToggled(int state);
    void onAutoReconnectToggled(bool checked);
    void onMultiSyncClicked();
    void onMultiReplayClicked();
    void onReplayClicked();
    void onReplayPauseResume();
    void onReplayStop();
    void onAnalyzeClicked();
    void onPrevPage();
    void onNextPage();

    // SDK bridge events (queued onto the GUI thread)
    void onBtEnableChanged(bool enabled);
    void onScanResults(QVector<DeviceEntry> devices);
    void onStateChanged(QString mac, int state);
    void onError(QString mac, QString message);
    void onPowerChanged(QString mac, int power);
    void onDeviceInfoUpdate(QString mac);
    void onDataTransferStateChanged(QString mac, bool isTransferring);
    void onAutoReconnectRequested(QString mac, bool restore);

    // Periodic refresh
    void onPlotTick();

private:
    void setupUi();
    // Page builders shared by both layouts: the desktop arranges the pages
    // in two columns, the phone layout (Q_OS_IOS / Q_OS_ANDROID /
    // DEMO_MOBILE_LAYOUT) stacks them under a QTabWidget.
    QWidget* buildDevicePage();
    QWidget* buildWaveformPage();
    QWidget* buildBioPage();
    QWidget* buildSettingsPage();
    void applySdkDebugLog();

    // Writes one app event line into the SDK log: with a device state it
    // lands in that device's log, otherwise in the general SDK log.
    // level: "D"/"I"/"W"/"E"; never throws.
    void appLog(const QString& msg, const char* level = "I",
                const std::shared_ptr<DeviceState>& st = nullptr);

    std::shared_ptr<DeviceState> stateFor(const QString& mac) const;
    std::shared_ptr<DeviceState> currentState() const;
    QString selectedMac() const;
    void updateButtonStates();
    void updateDeviceItemText(const QString& mac, bool connected);
    // Rebuilds one replay list row text, adding the [Streaming] mark while
    // the replay data flows.
    void updateReplayItemText(const QString& mac);
    void sortDeviceList();

    // Synchronized multi-device stream start/stop.
    void doMultiStart();
    void doMultiStop();
    std::vector<std::shared_ptr<sensor::SensorProfile>> liveReadySensors();

    // Connect chain (each step bounces back to the GUI thread).
    void onProfileReady(const QString& mac);
    void continueAfterInit(const QString& mac, bool ok, const QString& err);
    void continueAfterInfo(const QString& mac, const sensor::DeviceInfo& info, const QString& err);
    void continueAfterStart(const QString& mac, bool ok, const QString& err);
    void applySessionParams(const std::shared_ptr<DeviceState>& st,
                            std::function<void()> done = nullptr);
    // Records one successful user setParam for the auto-reconnect restore
    // replay; replays the recorded key/value list one command at a time.
    void recordSavedParam(const QString& mac, const QString& key,
                          const QString& value, const QString& result);
    void restoreSavedParams(const QString& mac, int index);

    void sendSetParam(const std::shared_ptr<sensor::SensorProfile>& profile,
                      const QString& key, const QString& value,
                      std::function<void(QString result, bool isError)> completion = nullptr);
    void refreshControlStates(const std::shared_ptr<DeviceState>& st);
    void applyRefreshedControlStates(const std::shared_ptr<DeviceState>& st,
                                     const QString& ntfResult, const QString& filterResult,
                                     const QString& rateListResult, const QString& rateResult);
    void applyControlStates(const QMap<QString, QPair<bool, bool>>& ntf,
                            const QMap<QString, QPair<bool, bool>>& filters,
                            const QVector<int>& rateOptions, int rateCurrent);
    // Only updates the sample-rate radios' checked state (enable state and
    // setParam untouched); used by paths that bypass refreshControlStates.
    void setSampleRateChecked(int rate);
    void clearUiData();

    void retargetWaveforms();
    // Bio panel (right side). retargetBio resets the page and rebuilds the
    // widget bindings; layoutBio rebuilds keeping the current page.
    void retargetBio(const std::shared_ptr<DeviceState>& st);
    void layoutBio(const std::shared_ptr<DeviceState>& st);
    int bioPageCount(const std::shared_ptr<DeviceState>& st) const;
    void updatePageControls();
    void refreshValueLabels();
    void refreshBioSideTexts();
    void refreshGestureLabel();
    void refreshInfoPanel();
    void updateLostPacketLabel();

    void setReplayModeUi(bool replaying);
    void startSingleReplay(const QString& path);
    // Registers one started replay member: device state + [Replay] list row.
    QString addReplayMember(const std::shared_ptr<sensor::SensorProfile>& profile,
                            const sensor::BinFileInfo& info);
    void finishReplayMember(const QString& mac);
    void onReplayDone(const QString& message);

    // Helpers to bounce an std::function-style callback onto the GUI thread.
    template <typename F>
    void postToGui(F&& fn) {
        QMetaObject::invokeMethod(this, std::forward<F>(fn), Qt::QueuedConnection);
    }

    std::shared_ptr<sensor::SensorController> _controller;
    std::shared_ptr<SdkBridge> _bridge;

    QVector<DeviceEntry> _discovered;
    QMap<QString, std::shared_ptr<DeviceState>> _deviceStates;
    // Guards _deviceStates; critical sections are lookup/insert/remove only.
    mutable QMutex _statesMutex;
    // Successful user setParam history per device (insertion order, one entry
    // per key), replayed by the app-driven auto-reconnect recovery.
    QMap<QString, QVector<QPair<QString, QString>>> _savedParamsByMac;
    // Devices whose next successful stream start should be followed by the
    // saved-param restore replay.
    QSet<QString> _restoreParamsMacs;
    // MACs with an actively transferring data stream.
    QSet<QString> _streamingMacs;
    QString _currentMac;

    // Replay state: one entry per replaying member (a single replay is a
    // one-member group; a group replay keeps one [Replay] row per member).
    QSet<QString> _replayMacs;
    bool _replayStopRequested = false;
    bool _replayPaused = false;
    bool _replayDoneFired = false;
    int _replayMemberTotal = 0;

    // Per-device log/bin export paths reused across reconnects.
    QMap<QString, QString> _lastLogPaths;
    QMap<QString, QString> _lastDataPaths;
    bool _debugLogEnabled = true;
    bool _binDataEnabled = true;

    bool _updatingControls = false;
    bool _shuttingDown = false;
    bool _scanning = false;

    std::thread _analyzeThread;
    std::atomic<bool> _analyzeRunning{false};

    // Data queue: the dataSink only enqueues each batch; the worker thread
    // drains the queue into the DeviceState rings and the plot timer
    // refreshes the UI.
    struct QueuedItem {
        QString mac;
        QSharedPointer<sensor::SensorData> owned; // set in clone mode
        sensor::SensorDataView borrowed;          // set in zero-copy mode
    };
    void enqueueData(const std::string& mac, QSharedPointer<sensor::SensorData> owned,
                     sensor::SensorDataView borrowed);
    void drainDataQueue();
    std::deque<QueuedItem> _dataQueue;
    std::mutex _dataQueueMutex;
    std::condition_variable _dataQueueCv;
    std::thread _dataWorker;
    std::atomic<bool> _dataWorkerStop{false};

    int _tickCount = 0;

    // FFT spectrum below the 2D waveform: onPlotTick snapshots the active 2D
    // ring every FFT_UPDATE_INTERVAL_MS; the result is pushed to the spectrum
    // widget when the data type and device still match.
    void maybeSubmitFft(const std::shared_ptr<DeviceState>& st);
    void pollFftResult();
    std::thread _fftThread;
    std::atomic<bool> _fftBusy{false};
    QMutex _fftMutex;
    bool _fftReady = false;
    int _fftTypeIndex = -1;
    QString _fftMac;
    std::vector<float> _fftFreqs;
    std::vector<std::vector<float>> _fftMags;
    qint64 _fftLastSubmitMs = 0;

    // Per-channel spectra in the EMG/EEG bio rows: shares the FFT worker
    // above; _bioFftChannels is the current row -> ring channel binding
    // (-1 = no spectrum on that row), _bioFftEpoch invalidates results
    // computed before the latest layoutBio.
    void maybeSubmitBioFft(const std::shared_ptr<DeviceState>& st);
    void pollBioFftResult();
    bool _bioFftReady = false;
    int _bioFftResultEpoch = -1;
    QString _bioFftMac;
    std::vector<float> _bioFftFreqs;
    std::vector<std::vector<float>> _bioFftMags;
    qint64 _bioFftLastSubmitMs = 0;
    int _bioFftEpoch = 0;
    QVector<int> _bioFftChannels;

    // Widgets
    QListWidget* _deviceList = nullptr;
    QPushButton* _btnScan = nullptr;
    QPushButton* _btnStopScan = nullptr;
    QPushButton* _btnConnect = nullptr;
    QPushButton* _btnDisconnect = nullptr;
    QPushButton* _btnReplay = nullptr;
    QPushButton* _btnAnalyze = nullptr;
    QPushButton* _btnReplayPause = nullptr;
    QPushButton* _btnReplayStop = nullptr;
    QPushButton* _btnMultiSync = nullptr;
    QPushButton* _btnMultiReplay = nullptr;
    QCheckBox* _chkAutoReconnect = nullptr;
    QCheckBox* _chkCloneData = nullptr;
    QComboBox* _typeCombo = nullptr;
    QComboBox* _filterCombo = nullptr;
    QLabel* _statusLabel = nullptr;
    QLabel* _sdkLabel = nullptr;
    QLabel* _rateLabel = nullptr;
    QLabel* _modelLabel = nullptr;
    QLabel* _hwLabel = nullptr;
    QLabel* _fwLabel = nullptr;
    QLabel* _linkLabel = nullptr;
    QLabel* _mtuLabel = nullptr;
    QLabel* _powerLabel = nullptr;
    QLabel* _lostPacketLabel = nullptr;
    QMap<QString, QLabel*> _valueLabels;
    class QVBoxLayout* _valueLayout = nullptr;
    QCheckBox* _chkDebugLog = nullptr;
    QCheckBox* _chkBinData = nullptr;
    QMap<QString, QCheckBox*> _ntfBoxes;
    QMap<QString, QCheckBox*> _filterBoxes;
    // EEG sample-rate radios (rate Hz -> radio).
    QMap<int, QRadioButton*> _sampleRateRadios;
    QButtonGroup* _sampleRateGroup = nullptr;
    WaveformWidget* _wave2d = nullptr;
    SpectrumWidget* _spectrum = nullptr;
    CubeWidget* _cube = nullptr;
    QLabel* _gestureLabel = nullptr;

    // Bio panel: 8 stacked waveforms showing EMG channels, or paged EEG
    // channels plus ECG / BRTH on the trailing widgets. EMG/EEG channel rows
    // split 50/50: per-channel spectrum on the left, waveform on the right.
    QLabel* _bioTitleLabel = nullptr;
    QWidget* _pageControls = nullptr;
    QPushButton* _btnPrevPage = nullptr;
    QPushButton* _btnNextPage = nullptr;
    QLabel* _pageLabel = nullptr;
    QVector<WaveformWidget*> _bioWidgets;
    QVector<SpectrumWidget*> _bioSpectra;
    int _bioPage = 0;
    // Per-widget impedance binding (set by layoutBio) for the side texts.
    struct BioTarget {
        const QVector<float>* impedance = nullptr;
        int channel = 0;
    };
    QVector<BioTarget> _bioTargets;

    QTimer* _plotTimer = nullptr;
    // Qt-on-Android workaround: pulse a visible-widget update so SDK-driven
    // UI changes (scan results, button states, status texts) render promptly.
    QTimer* _uiKickTimer = nullptr;
};

#endif // MAINWINDOW_H
