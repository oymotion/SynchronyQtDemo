#include "MainWindow.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <utility>

#include "WaveformWidget.h"
#include "SpectrumWidget.h"
#include "SpectrumCompute.h"
#include "CubeWidget.h"

// The phone layout stacks the same widgets under a QTabWidget; it can also
// be forced on desktop builds via the DEMO_MOBILE_LAYOUT CMake option.
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID) || defined(DEMO_MOBILE_LAYOUT)
#define DEMO_MOBILE_UI 1
#endif

namespace {
const int SCAN_DEVICE_PERIOD_MS = 3000;
const int PACKAGE_COUNT = 32;
const int CMD_TIMEOUT_MS = 5000;
const int PLOT_UPDATE_INTERVAL_MS = 50;
const int FFT_UPDATE_INTERVAL_MS = 200;   // spectrum recompute interval
// The demo's own version.
const char* const DEMO_VERSION = "0.1.10";
const int POWER_REFRESH_PERIOD_MS = 60000;
// Battery reading stable band (%): hold the displayed value while a valid
// reading differs by less than this.
const int POWER_STABLE_BAND = 4;
const unsigned int REPLAY_DELEGATE_TIMEOUT_MS = 5000;
// Multi start/stop defaults; mixed device models use the relaxed set
// (60000, -1, 5) inline at the call site.
const int MULTI_START_TIMEOUT_MS = 30000;
const int MULTI_STOP_TIMEOUT_MS = 10000;

// Data Notification keys.
const char* const kNtfKeys[] = {"NTF_EEG", "NTF_EMG", "NTF_GEST", "NTF_PPG", "NTF_SPO2", "NTF_IMU"};
const char* const kFilterKeys[] = {"FILTER_50HZ", "FILTER_60HZ", "FILTER_HPF", "FILTER_LPF"};
// EEG Sample Rate radio candidates.
const int kSampleRateCandidates[] = {250, 500, 1000, 2000};

// Device-info row texts.
QString linkText(const sensor::DeviceInfo& info) {
    if (info.PeripheralLatency < 0 || info.ConnectionIntervalMs <= 0) {
        return QStringLiteral("Link: --");
    }
    return QStringLiteral("Link: %1ms / latency %2 / timeout %3ms")
        .arg(info.ConnectionIntervalMs)
        .arg(info.PeripheralLatency)
        .arg(info.SupervisionTimeoutMs);
}

QString mtuText(const sensor::DeviceInfo& info) {
    if (info.MTUSize <= 0) {
        return QStringLiteral("MTU: --");
    }
    return QStringLiteral("MTU: %1").arg(info.MTUSize);
}

int readyState() { return static_cast<int>(sensor::BLEDevice::State::Ready); }
int disconnectedState() { return static_cast<int>(sensor::BLEDevice::State::Disconnected); }

}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    _controller = sensor::SensorController::getInstance();
    _bridge = std::make_shared<SdkBridge>();
    _controller->setDelegate(_bridge);

    setupUi();

    connect(_bridge.get(), &SdkBridge::btEnableChanged,
            this, &MainWindow::onBtEnableChanged, Qt::QueuedConnection);
    connect(_bridge.get(), &SdkBridge::scanResults,
            this, &MainWindow::onScanResults, Qt::QueuedConnection);
    connect(_bridge.get(), &SdkBridge::stateChanged,
            this, &MainWindow::onStateChanged, Qt::QueuedConnection);
    connect(_bridge.get(), &SdkBridge::errorOccurred,
            this, &MainWindow::onError, Qt::QueuedConnection);
    connect(_bridge.get(), &SdkBridge::powerChanged,
            this, &MainWindow::onPowerChanged, Qt::QueuedConnection);
    connect(_bridge.get(), &SdkBridge::deviceInfoUpdated,
            this, &MainWindow::onDeviceInfoUpdate, Qt::QueuedConnection);
    connect(_bridge.get(), &SdkBridge::dataTransferStateChanged,
            this, &MainWindow::onDataTransferStateChanged, Qt::QueuedConnection);
    connect(_bridge.get(), &SdkBridge::autoReconnectRequested,
            this, &MainWindow::onAutoReconnectRequested, Qt::QueuedConnection);

    // Data path: the sink only enqueues; the worker thread drains the queue
    // into the DeviceState rings and the plot timer refreshes the UI.
    _bridge->dataSink = [this](const std::string& mac, QSharedPointer<sensor::SensorData> owned,
                               sensor::SensorDataView borrowed) {
        enqueueData(mac, std::move(owned), borrowed);
    };
    _dataWorker = std::thread([this] { drainDataQueue(); });

    // Apply the debug log path before any scan/connect (timestamped
    // subdirectory under ~/Documents/sensorsdklog).
    if (_debugLogEnabled) {
        applySdkDebugLog();
    }

    _plotTimer = new QTimer(this);
    connect(_plotTimer, &QTimer::timeout, this, &MainWindow::onPlotTick);
    _plotTimer->start(PLOT_UPDATE_INTERVAL_MS);

    _uiKickTimer = new QTimer(this);
    connect(_uiKickTimer, &QTimer::timeout, this, [this] {
        if (isVisible() && !isMinimized()) {
            update();
        }
    });
    _uiKickTimer->start(200);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
#ifdef DEMO_MOBILE_UI
    // Phone layout: the pages are stacked under a tab bar. The settings
    // groups live at the bottom of the Devices tab; the merged page scrolls
    // on small phone screens.
    auto* tabs = new QTabWidget(this);
    // Phone convention: the tab bar sits at the bottom of the screen.
    tabs->setTabPosition(QTabWidget::South);
    auto* deviceTab = new QWidget(this);
    auto* deviceTabLayout = new QVBoxLayout(deviceTab);
    // Bottom clearance above the tab bar to avoid mis-tapping page controls.
    deviceTabLayout->setContentsMargins(0, 0, 0, 20);
    deviceTabLayout->addWidget(buildDevicePage());
    deviceTabLayout->addWidget(buildSettingsPage());
    auto* deviceScroll = new QScrollArea(this);
    deviceScroll->setWidgetResizable(true);
    deviceScroll->setWidget(deviceTab);
    tabs->addTab(deviceScroll, QStringLiteral("Devices"));
    // The waveform page carries the IMU widgets (3D quaternion cube +
    // ACC/GYRO/Quat/Euler 2D waveform).
    tabs->addTab(buildWaveformPage(), QStringLiteral("IMU"));
    tabs->addTab(buildBioPage(), QStringLiteral("Bio"));
    setCentralWidget(tabs);
#else
    auto* central = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(central);

    // Left column: 3D cube on top, 2D waveform + type selector + values below.
    mainLayout->addWidget(buildWaveformPage(), 3);

    // Right column: controls on top, bio (EMG / EEG) channels below.
    auto* rightLayout = new QVBoxLayout();
    auto* controlsLayout = new QVBoxLayout();
    controlsLayout->addWidget(buildDevicePage());
    controlsLayout->addWidget(buildSettingsPage());
    controlsLayout->addStretch();
    rightLayout->addLayout(controlsLayout, 1);
    rightLayout->addWidget(buildBioPage(), 4);
    mainLayout->addLayout(rightLayout, 7);

    setCentralWidget(central);
    resize(1600, 900);
#endif

    setWindowTitle(QStringLiteral("SensorSDKCXX IMU + Quaternion + EMG + EEG Demo (Multi) (sensor-sdk v%1, demo v%2)")
                       .arg(QString::fromStdString(_controller->getVersion()),
                            QString::fromUtf8(DEMO_VERSION)));

    retargetWaveforms();
}

// The page layouts use zero contents margins.

QWidget* MainWindow::buildDevicePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* devCol = new QVBoxLayout();
    auto* devHeader = new QHBoxLayout();
    _chkAutoReconnect = new QCheckBox(QStringLiteral("Auto Reconnect"), page);
    _chkAutoReconnect->setChecked(true);
    connect(_chkAutoReconnect, &QCheckBox::toggled, this, &MainWindow::onAutoReconnectToggled);
    devHeader->addWidget(_chkAutoReconnect);
    devHeader->addWidget(new QLabel(QStringLiteral("Discovered Devices:"), page));
    _chkCloneData = new QCheckBox(QStringLiteral("Clone Data"), page);
    // Checked: every batch is deep-copied before entering the data queue.
    // Unchecked (default): zero-copy.
    _chkCloneData->setChecked(false);
    connect(_chkCloneData, &QCheckBox::toggled, this,
            [this](bool checked) { _bridge->setCloneData(checked); });
    devHeader->addWidget(_chkCloneData);
    devHeader->addStretch();
    _btnMultiSync = new QPushButton(QStringLiteral("Multi Start"), page);
    _btnMultiSync->setEnabled(false);
    connect(_btnMultiSync, &QPushButton::clicked, this, &MainWindow::onMultiSyncClicked);
    devHeader->addWidget(_btnMultiSync);
    _btnMultiReplay = new QPushButton(QStringLiteral("Multi Replay Bin"), page);
    connect(_btnMultiReplay, &QPushButton::clicked, this, &MainWindow::onMultiReplayClicked);
    devHeader->addWidget(_btnMultiReplay);
    // SDK version next to the scan controls (also shown in the window title).
    _sdkLabel = new QLabel(
        QStringLiteral("SDK: %1").arg(QString::fromStdString(_controller->getVersion())), page);
    devHeader->addWidget(_sdkLabel);
    devCol->addLayout(devHeader);
    _deviceList = new QListWidget(page);
    // Keep the selected row readable when the list loses focus: mirror the
    // active highlight colors.
    QPalette listPal = _deviceList->palette();
    listPal.setColor(QPalette::Inactive, QPalette::Highlight,
                     listPal.color(QPalette::Active, QPalette::Highlight));
    listPal.setColor(QPalette::Inactive, QPalette::HighlightedText,
                     listPal.color(QPalette::Active, QPalette::HighlightedText));
    _deviceList->setPalette(listPal);
    // Accent color for the selected (current) device row.
    _deviceList->setStyleSheet(QStringLiteral(
        "QListWidget::item:selected { background-color: #2F6FDB; color: white; }"));
#ifdef DEMO_MOBILE_UI
    // Phones: keep the list short so the merged settings groups stay reachable.
    _deviceList->setMaximumHeight(120);
#else
    _deviceList->setMaximumHeight(90);
#endif
    connect(_deviceList, &QListWidget::itemClicked, this, &MainWindow::onDeviceSelected);
    devCol->addWidget(_deviceList);

    _btnScan = new QPushButton(QStringLiteral("Start Scan"), page);
    connect(_btnScan, &QPushButton::clicked, this, &MainWindow::onStartScan);
    _btnStopScan = new QPushButton(QStringLiteral("Stop Scan"), page);
    _btnStopScan->setEnabled(false);
    connect(_btnStopScan, &QPushButton::clicked, this, &MainWindow::onStopScan);
    _btnConnect = new QPushButton(QStringLiteral("Connect"), page);
    _btnConnect->setEnabled(false);
    connect(_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    _btnDisconnect = new QPushButton(QStringLiteral("Disconnect"), page);
    _btnDisconnect->setEnabled(false);
    connect(_btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);

    _btnReplay = new QPushButton(QStringLiteral("Replay Bin File"), page);
    connect(_btnReplay, &QPushButton::clicked, this, &MainWindow::onReplayClicked);
    _btnAnalyze = new QPushButton(QStringLiteral("Analyze Bin"), page);
    connect(_btnAnalyze, &QPushButton::clicked, this, &MainWindow::onAnalyzeClicked);
    _btnReplayPause = new QPushButton(QStringLiteral("Pause Replay"), page);
    _btnReplayPause->setEnabled(false);
    connect(_btnReplayPause, &QPushButton::clicked, this, &MainWindow::onReplayPauseResume);
    _btnReplayStop = new QPushButton(QStringLiteral("Stop Replay"), page);
    _btnReplayStop->setEnabled(false);
    connect(_btnReplayStop, &QPushButton::clicked, this, &MainWindow::onReplayStop);

#ifdef DEMO_MOBILE_UI
    // Portrait phones: stack vertically; the device list takes the full
    // width on top, then two compact horizontal button rows.
    layout->addLayout(devCol);
    auto* btnRow = new QHBoxLayout();
    btnRow->addWidget(_btnScan);
    btnRow->addWidget(_btnStopScan);
    btnRow->addWidget(_btnConnect);
    btnRow->addWidget(_btnDisconnect);
    layout->addLayout(btnRow);
    auto* replayRow = new QHBoxLayout();
    replayRow->addWidget(_btnReplay);
    replayRow->addWidget(_btnAnalyze);
    replayRow->addWidget(_btnReplayPause);
    replayRow->addWidget(_btnReplayStop);
    layout->addLayout(replayRow);
#else
    auto* scanRow = new QHBoxLayout();
    auto* btnCol = new QVBoxLayout();
    btnCol->addWidget(_btnScan);
    btnCol->addWidget(_btnStopScan);
    btnCol->addWidget(_btnConnect);
    btnCol->addWidget(_btnDisconnect);
    btnCol->addStretch();
    scanRow->addLayout(btnCol);
    scanRow->addLayout(devCol, 1);
    auto* replayCol = new QVBoxLayout();
    replayCol->addWidget(_btnReplay);
    replayCol->addWidget(_btnAnalyze);
    replayCol->addWidget(_btnReplayPause);
    replayCol->addWidget(_btnReplayStop);
    replayCol->addStretch();
    scanRow->addLayout(replayCol);
    layout->addLayout(scanRow);
#endif

    _statusLabel = new QLabel(QStringLiteral("Not Connected"), page);
    layout->addWidget(_statusLabel);
    _rateLabel = new QLabel(QString(), page);
    layout->addWidget(_rateLabel);

    _modelLabel = new QLabel(QStringLiteral("Model: --"), page);
    _hwLabel = new QLabel(QStringLiteral("HW Version: --"), page);
    _fwLabel = new QLabel(QStringLiteral("FW Version: --"), page);
    _linkLabel = new QLabel(QStringLiteral("Link: --"), page);
    _mtuLabel = new QLabel(QStringLiteral("MTU: --"), page);
    _powerLabel = new QLabel(QStringLiteral("Power: --%"), page);
#ifdef DEMO_MOBILE_UI
    // Portrait phones: two columns instead of one long row.
    auto* infoGrid = new QGridLayout();
    infoGrid->addWidget(_modelLabel, 0, 0);
    infoGrid->addWidget(_hwLabel, 0, 1);
    infoGrid->addWidget(_fwLabel, 1, 0);
    infoGrid->addWidget(_powerLabel, 1, 1);
    infoGrid->addWidget(_linkLabel, 2, 0);
    infoGrid->addWidget(_mtuLabel, 2, 1);
    infoGrid->setColumnStretch(0, 1);
    infoGrid->setColumnStretch(1, 1);
    layout->addLayout(infoGrid);
#else
    auto* infoRow = new QHBoxLayout();
    infoRow->addWidget(_modelLabel);
    infoRow->addWidget(_hwLabel);
    infoRow->addWidget(_fwLabel);
    infoRow->addWidget(_linkLabel);
    infoRow->addWidget(_mtuLabel);
    infoRow->addWidget(_powerLabel);
    infoRow->addStretch();
    layout->addLayout(infoRow);
#endif

    return page;
}

QWidget* MainWindow::buildWaveformPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
#ifdef DEMO_MOBILE_UI
    // Bottom clearance above the tab bar to avoid mis-tapping page controls.
    layout->setContentsMargins(0, 0, 0, 20);
#else
    layout->setContentsMargins(0, 0, 0, 0);
#endif

    layout->addWidget(new QLabel(QStringLiteral("3D Quaternion Visualization"), page));
    _cube = new CubeWidget(page);
    _cube->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
#ifdef DEMO_MOBILE_UI
    // Phones: keep the cube compact so the waveform gets most of the space.
    _cube->setMaximumHeight(220);
#endif
    layout->addWidget(_cube, 1);

    layout->addWidget(new QLabel(QStringLiteral("2D Waveform + FFT Spectrum (ACC/GYRO/Quat/Euler)"), page));
    _wave2d = new WaveformWidget(page);
    _wave2d->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(_wave2d, 3);
    // FFT spectrum of the active 2D type, computed on a worker thread.
    _spectrum = new SpectrumWidget(page);
    _spectrum->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(_spectrum, 2);

    auto* typeLayout = new QHBoxLayout();
    typeLayout->addWidget(new QLabel(QStringLiteral("Display Data Type:"), page));
    _typeCombo = new QComboBox(page);
    _typeCombo->addItem(QStringLiteral("Acceleration (ACC)"));
    _typeCombo->addItem(QStringLiteral("Gyroscope (GYRO)"));
    _typeCombo->addItem(QStringLiteral("Quaternion (Quat)"));
    _typeCombo->addItem(QStringLiteral("Euler Angle (Euler)"));
    connect(_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onTypeChanged);
    typeLayout->addWidget(_typeCombo, 1);

    // Live Filter band combo: band-passes the bio waveforms (EMG/EEG/ECG/
    // BRTH/PPG/SpO2) before they reach the display rings.
    typeLayout->addWidget(new QLabel(QStringLiteral("Live Filter:"), page));
    _filterCombo = new QComboBox(page);
    _filterCombo->addItems(LiveFilter::bandLabels());
    connect(_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLiveFilterChanged);
    typeLayout->addWidget(_filterCombo, 1);
    layout->addLayout(typeLayout);

    auto* valueBox = new QGroupBox(QStringLiteral("Real-time Values"), page);
    _valueLayout = new QVBoxLayout(valueBox);
    layout->addWidget(valueBox);

    return page;
}

QWidget* MainWindow::buildBioPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
#ifdef DEMO_MOBILE_UI
    // Bottom clearance above the tab bar to avoid mis-tapping page controls.
    layout->setContentsMargins(0, 0, 0, 20);
#else
    layout->setContentsMargins(0, 0, 0, 0);
#endif

    // Bio panel: page controls on top (EEG paging, hidden in EMG mode or
    // when a single page suffices), then 8 stacked waveforms.
    _pageControls = new QWidget(page);
    auto* pageRow = new QHBoxLayout(_pageControls);
    pageRow->setContentsMargins(0, 0, 0, 0);
    _btnPrevPage = new QPushButton(QStringLiteral("Prev"), page);
    connect(_btnPrevPage, &QPushButton::clicked, this, &MainWindow::onPrevPage);
    pageRow->addWidget(_btnPrevPage);
    _pageLabel = new QLabel(QStringLiteral("Page 1 / 1"), page);
    _pageLabel->setAlignment(Qt::AlignCenter);
    pageRow->addWidget(_pageLabel, 1);
    _btnNextPage = new QPushButton(QStringLiteral("Next"), page);
    connect(_btnNextPage, &QPushButton::clicked, this, &MainWindow::onNextPage);
    pageRow->addWidget(_btnNextPage);
    _pageControls->setVisible(false);
    layout->addWidget(_pageControls);

    _bioTitleLabel = new QLabel(QStringLiteral("EMG / EEG Waveform"), page);
    layout->addWidget(_bioTitleLabel);
    // Each row: left spectrum + right waveform, 50/50. The spectrum is hidden
    // unless layoutBio binds a channel to the row (EMG/EEG modes only).
    for (int i = 0; i < 8; ++i) {
        auto* row = new QWidget(page);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* s = new SpectrumWidget(row);
        s->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        s->setVisible(false);
        auto* w = new WaveformWidget(row);
        w->setAutoYRange();
        w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
#ifdef DEMO_MOBILE_UI
        w->setMinimumHeight(40);
#else
        w->setMinimumHeight(60);
#endif
        rowLayout->addWidget(s, 1);
        rowLayout->addWidget(w, 1);
        _bioSpectra.append(s);
        _bioWidgets.append(w);
        layout->addWidget(row, 1);
    }

    return page;
}

QWidget* MainWindow::buildSettingsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* lostBox = new QGroupBox(QStringLiteral("Packet Loss Stats"), page);
    auto* lostLayout = new QVBoxLayout(lostBox);
    _lostPacketLabel = new QLabel(QStringLiteral("Packet Loss Stats: None"), page);
    _lostPacketLabel->setWordWrap(true);
    lostLayout->addWidget(_lostPacketLabel);

    auto* gestureBox = new QGroupBox(QStringLiteral("Gesture"), page);
    auto* gestureLayout = new QVBoxLayout(gestureBox);
    _gestureLabel = new QLabel(QStringLiteral("Gesture:\n  gesture: -- (0-8)\n  raw gesture: -- (0-8)\n  possiblity: -- (0-100)\n  strength: -- (0-100)"), page);
    gestureLayout->addWidget(_gestureLabel);

    auto* debugGroup = new QGroupBox(QStringLiteral("Debug Log"), page);
    auto* debugLayout = new QVBoxLayout(debugGroup);
    _chkDebugLog = new QCheckBox(QStringLiteral("Enable SDK Debug Log"), page);
    _chkDebugLog->setChecked(true);
    connect(_chkDebugLog, &QCheckBox::stateChanged, this, &MainWindow::onDebugLogToggled);
    debugLayout->addWidget(_chkDebugLog);
    _chkBinData = new QCheckBox(QStringLiteral("Enable Debug Bin Data"), page);
    _chkBinData->setChecked(true);
    connect(_chkBinData, &QCheckBox::stateChanged, this, &MainWindow::onBinDataToggled);
    debugLayout->addWidget(_chkBinData);

    auto* ntfGroup = new QGroupBox(QStringLiteral("Data Notification"), page);
    auto* ntfLayout = new QHBoxLayout(ntfGroup);
    const QMap<QString, QString> ntfLabels = {
        {QStringLiteral("NTF_EEG"), QStringLiteral("EEG")},
        {QStringLiteral("NTF_EMG"), QStringLiteral("EMG")},
        {QStringLiteral("NTF_GEST"), QStringLiteral("GESTURE")},
        {QStringLiteral("NTF_PPG"), QStringLiteral("PPG")},
        {QStringLiteral("NTF_SPO2"), QStringLiteral("SpO2")},
        {QStringLiteral("NTF_IMU"), QStringLiteral("IMU")},
    };
    for (const char* key : kNtfKeys) {
        auto* cb = new QCheckBox(ntfLabels.value(QString::fromLatin1(key)), page);
        cb->setChecked(true);
        cb->setEnabled(false);
        connect(cb, &QCheckBox::stateChanged, this, [this](int) { onNtfToggled(); });
        _ntfBoxes.insert(QString::fromLatin1(key), cb);
        ntfLayout->addWidget(cb);
    }

    auto* filterGroup = new QGroupBox(QStringLiteral("Filter"), page);
    auto* filterLayout = new QHBoxLayout(filterGroup);
    const QMap<QString, QString> filterLabels = {
        {QStringLiteral("FILTER_50HZ"), QStringLiteral("50Hz")},
        {QStringLiteral("FILTER_60HZ"), QStringLiteral("60Hz")},
        {QStringLiteral("FILTER_HPF"), QStringLiteral("HPF")},
        {QStringLiteral("FILTER_LPF"), QStringLiteral("LPF")},
    };
    for (const char* key : kFilterKeys) {
        auto* cb = new QCheckBox(filterLabels.value(QString::fromLatin1(key)), page);
        cb->setChecked(true);
        cb->setEnabled(false);
        connect(cb, &QCheckBox::stateChanged, this, [this](int) { onFilterToggled(); });
        _filterBoxes.insert(QString::fromLatin1(key), cb);
        filterLayout->addWidget(cb);
    }

    auto* rateGroup = new QGroupBox(QStringLiteral("EEG Sample Rate"), page);
    auto* rateLayout = new QHBoxLayout(rateGroup);
    _sampleRateGroup = new QButtonGroup(page);
    for (int rate : kSampleRateCandidates) {
        auto* rb = new QRadioButton(QStringLiteral("%1 Hz").arg(rate), page);
        // Exclusivity comes from the button group so the refresh path can
        // uncheck every radio.
        rb->setAutoExclusive(false);
        rb->setEnabled(false);
        connect(rb, &QRadioButton::toggled, this, [this](bool) { onSampleRateToggled(); });
        _sampleRateRadios.insert(rate, rb);
        _sampleRateGroup->addButton(rb);
        rateLayout->addWidget(rb);
    }

#ifdef DEMO_MOBILE_UI
    // Portrait phones: stack the groups vertically.
    layout->addWidget(lostBox);
    layout->addWidget(gestureBox);
    layout->addWidget(debugGroup);
    layout->addWidget(ntfGroup);
    layout->addWidget(filterGroup);
    layout->addWidget(rateGroup);
#else
    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(lostBox, 1);
    statusRow->addWidget(gestureBox, 1);
    layout->addLayout(statusRow);
    auto* optionsRow = new QHBoxLayout();
    optionsRow->addWidget(debugGroup, 1);
    optionsRow->addWidget(ntfGroup, 1);
    optionsRow->addWidget(filterGroup, 1);
    optionsRow->addWidget(rateGroup, 1);
    layout->addLayout(optionsRow);
#endif
    layout->addStretch();

    return page;
}

// -- Scan -------------------------------------------------------------------

void MainWindow::onStartScan() {
    if (!_controller->isEnable()) {
        appLog(QStringLiteral("User: start scan rejected (Bluetooth disabled)"), "W");
        _statusLabel->setText(QStringLiteral("Please enable Bluetooth first"));
        return;
    }
    appLog(QStringLiteral("User: start scan"));
    if (!_controller->isScaning()) {
        _controller->startScan(SCAN_DEVICE_PERIOD_MS);
    }
    _scanning = true;
    _btnScan->setEnabled(false);
    _btnStopScan->setEnabled(true);
}

void MainWindow::onStopScan() {
    appLog(QStringLiteral("Stop scan"));
    _controller->stopScan();
    _scanning = false;
    _btnScan->setEnabled(_replayMacs.isEmpty());
    _btnStopScan->setEnabled(false);
}

void MainWindow::onBtEnableChanged(bool enabled) {
    if (!enabled) {
        _statusLabel->setText(QStringLiteral("Please enable Bluetooth first"));
    }
}

void MainWindow::onScanResults(QVector<DeviceEntry> devices) {
    // Every scan-result batch is a full snapshot; merge it into the list in
    // place and keep scanning until the user presses Stop Scan.
    for (const auto& d : devices) {
        // No name filter: every scanned device is listed.
        int found = -1;
        for (int i = 0; i < _discovered.size(); ++i) {
            if (_discovered[i].mac == d.mac) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            _discovered.append(d);
            auto* item = new QListWidgetItem(
                QStringLiteral("RSSI: %1, Name: %2, Address: %3").arg(d.rssi).arg(d.name, d.mac));
            item->setData(Qt::UserRole, d.mac);
            _deviceList->addItem(item);
        } else {
            _discovered[found].rssi = d.rssi;
            const bool connected = _deviceStates.contains(d.mac);
            updateDeviceItemText(d.mac, connected);
        }
    }
    // Keep the list ordered by RSSI descending (covers add + update paths).
    sortDeviceList();
    updateButtonStates();
}

// -- Device list / selection -------------------------------------------------

QString MainWindow::selectedMac() const {
    auto* item = _deviceList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

std::shared_ptr<DeviceState> MainWindow::stateFor(const QString& mac) const {
    QMutexLocker lock(&_statesMutex);
    return _deviceStates.value(mac, nullptr);
}

std::shared_ptr<DeviceState> MainWindow::currentState() const {
    return stateFor(_currentMac);
}

void MainWindow::onDeviceSelected(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const QString mac = item->data(Qt::UserRole).toString();
    auto st = stateFor(mac);
    // Selecting an unconnected device clears the display.
    _currentMac = st ? mac : QString();
    retargetWaveforms();
    refreshInfoPanel();
    updateButtonStates();
}

void MainWindow::updateButtonStates() {
    const QString mac = selectedMac();
    const bool connected = !mac.isEmpty() && _deviceStates.contains(mac);
    const bool replaying = !_replayMacs.isEmpty();
    _btnConnect->setEnabled(!replaying && !mac.isEmpty() && !connected);
    _btnDisconnect->setEnabled(!replaying && connected);
    _btnScan->setEnabled(!replaying && !_scanning);
    int liveCount = 0;
    for (const auto& st : _deviceStates) {
        if (!st->isReplay) {
            ++liveCount;
        }
    }
    _btnMultiSync->setText(_streamingMacs.isEmpty() ? QStringLiteral("Multi Start")
                                                    : QStringLiteral("Multi Stop"));
    _btnMultiSync->setEnabled(!replaying && liveCount >= 1);
}

void MainWindow::updateDeviceItemText(const QString& mac, bool connected) {
    for (int i = 0; i < _deviceList->count(); ++i) {
        auto* item = _deviceList->item(i);
        if (item->data(Qt::UserRole).toString() != mac) {
            continue;
        }
        QString name = mac;
        int rssi = 0;
        for (const auto& d : _discovered) {
            if (d.mac == mac) {
                name = d.name;
                rssi = d.rssi;
                break;
            }
        }
        QString text = QStringLiteral("RSSI: %1, Name: %2, Address: %3").arg(rssi).arg(name, mac);
        // The [Streaming] mark comes from the SDK's data-transfer state
        // push; a connected device whose stream is off keeps the plain
        // [Connected] mark.
        if (_streamingMacs.contains(mac)) {
            text = QStringLiteral("[Streaming] ") + text;
        } else if (connected) {
            text = QStringLiteral("[Connected] ") + text;
        }
        item->setText(text);
        break;
    }
}

void MainWindow::sortDeviceList() {
    // Sort the items by RSSI descending; items without a _discovered entry
    // (e.g. replay items) sort last. The item's Qt::UserRole holds the MAC
    // and is left untouched.
    const QString mac = selectedMac();
    QVector<QListWidgetItem*> items;
    items.reserve(_deviceList->count());
    while (_deviceList->count() > 0) {
        items.append(_deviceList->takeItem(0));
    }
    const auto rssiOf = [this](const QListWidgetItem* item) {
        const QString itemMac = item->data(Qt::UserRole).toString();
        for (const auto& d : _discovered) {
            if (d.mac == itemMac) {
                return d.rssi;
            }
        }
        return std::numeric_limits<int>::min();
    };
    std::stable_sort(items.begin(), items.end(),
                     [&](const QListWidgetItem* a, const QListWidgetItem* b) {
                         return rssiOf(a) > rssiOf(b);
                     });
    for (auto* item : items) {
        _deviceList->addItem(item);
    }
    // takeItem() drops the current row; restore the previous selection.
    if (!mac.isEmpty()) {
        for (int i = 0; i < _deviceList->count(); ++i) {
            if (_deviceList->item(i)->data(Qt::UserRole).toString() == mac) {
                _deviceList->setCurrentRow(i);
                break;
            }
        }
    }
}

// -- Connect chain -----------------------------------------------------------

void MainWindow::onConnectClicked() {
    const QString mac = selectedMac();
    if (mac.isEmpty()) {
        appLog(QStringLiteral("User: connect rejected (no device selected)"), "W");
        _statusLabel->setText(QStringLiteral("Please select a device in the list first"));
        return;
    }
    if (_deviceStates.contains(mac)) {
        return;
    }
    QString name;
    for (const auto& d : _discovered) {
        if (d.mac == mac) {
            name = d.name;
            break;
        }
    }
    appLog(QStringLiteral("User: connect %1 (%2)").arg(name, mac));
    auto profile = _controller->getSensor(mac.toStdString());
    if (!profile) {
        appLog(QStringLiteral("App: failed to create SensorProfile for %1").arg(mac), "E");
        _statusLabel->setText(QStringLiteral("Failed to create SensorProfile"));
        return;
    }
    profile->setDelegate(_bridge);
    profile->setAutoReconnect(_chkAutoReconnect->isChecked());

    auto st = std::make_shared<DeviceState>(profile);
    // A band selected before this device appeared still applies to it.
    st->liveFilter.setBand(_filterCombo->currentIndex());
    st->name = name;
    {
        QMutexLocker lock(&_statesMutex);
        _deviceStates.insert(mac, st);
    }

    _btnConnect->setEnabled(false);
    _currentMac = mac;
    retargetWaveforms();
    refreshInfoPanel();
    // Set after refreshInfoPanel, which rewrites the status.
    _statusLabel->setText(QStringLiteral("Connecting: %1 ...").arg(st->name));

    if (!profile->isReady()) {
        profile->connect({}); // fire-and-forget; the GUI reacts to state-change signals
    } else {
        onProfileReady(mac);
    }
}

void MainWindow::onDisconnectClicked() {
    auto st = currentState();
    if (!st || st->isReplay) {
        return;
    }
    appLog(QStringLiteral("User: disconnect %1").arg(st->mac), "I", st);
    // Grey out every NTF and filter switch as soon as the disconnect starts.
    for (auto* cb : _ntfBoxes) {
        cb->setEnabled(false);
    }
    for (auto* cb : _filterBoxes) {
        cb->setEnabled(false);
    }
    for (auto* rb : _sampleRateRadios) {
        rb->setEnabled(false);
    }
    if (st->profile->getDeviceState() == sensor::BLEDevice::State::Disconnected) {
        // The link is already down: run the teardown inline instead of
        // waiting for a Disconnected event.
        onStateChanged(st->mac, disconnectedState());
        return;
    }
    _btnDisconnect->setEnabled(false);
    _btnConnect->setEnabled(false);
    _statusLabel->setText(QStringLiteral("Disconnecting..."));
    st->profile->disconnect({}); // fire-and-forget; the GUI reacts to state-change signals
    // Teardown finishes in onStateChanged(Disconnected).
}

void MainWindow::onProfileReady(const QString& mac) {
    auto st = stateFor(mac);
    if (!st) {
        return;
    }
    if (!st->profile->hasInit()) {
        _statusLabel->setText(QStringLiteral("Initializing %1 ...").arg(st->name));
        st->profile->init(PACKAGE_COUNT, CMD_TIMEOUT_MS,
                          [this, mac](bool result, std::string err) {
                              postToGui([this, mac, result, err]() {
                                  continueAfterInit(mac, result, QString::fromStdString(err));
                              });
                          },
                          POWER_REFRESH_PERIOD_MS);
    } else {
        continueAfterInit(mac, true, QString());
    }
}

void MainWindow::continueAfterInit(const QString& mac, bool ok, const QString& err) {
    auto st = stateFor(mac);
    if (!st) {
        return;
    }
    if (!ok) {
        appLog(QStringLiteral("App: failed to initialize %1 (%2)").arg(st->name, mac), "E", st);
        _statusLabel->setText(QStringLiteral("Failed to initialize %1: %2").arg(st->name, err));
        updateButtonStates();
        return;
    }
    st->profile->fetchDeviceInfo(CMD_TIMEOUT_MS,
                                 [this, mac](sensor::DeviceInfo info, std::string err) {
                                     postToGui([this, mac, info, err]() {
                                         continueAfterInfo(mac, info, QString::fromStdString(err));
                                     });
                                 });
}

void MainWindow::continueAfterInfo(const QString& mac, const sensor::DeviceInfo& info,
                                   const QString& err) {
    auto st = stateFor(mac);
    if (!st) {
        return;
    }
    if (err.isEmpty()) {
        st->info = info;
        st->hasInfo = true;
    } else {
        qWarning("[DemoEMG] fetchDeviceInfo failed: %s", qPrintable(err));
    }
    if (!st->profile->hasStartDataNotification()) {
        st->profile->startDataNotification(CMD_TIMEOUT_MS,
                                           [this, mac](bool result, std::string err) {
                                               postToGui([this, mac, result, err]() {
                                                   continueAfterStart(mac, result, QString::fromStdString(err));
                                               });
                                           });
    } else {
        continueAfterStart(mac, true, QString());
    }
}

void MainWindow::continueAfterStart(const QString& mac, bool ok, const QString& err) {
    auto st = stateFor(mac);
    if (!st) {
        return;
    }
    if (!ok) {
        appLog(QStringLiteral("App: failed to start data stream on %1").arg(mac), "E", st);
        _statusLabel->setText(QStringLiteral("Failed to start data stream: %1").arg(err));
        updateButtonStates();
        return;
    }
    appLog(QStringLiteral("App: device connected and streaming: %1 (%2)").arg(st->name, mac), "I", st);
    updateDeviceItemText(mac, true);
    applySessionParams(st, [this, mac]() {
        auto st = stateFor(mac);
        if (!st) {
            return;
        }
        if (_restoreParamsMacs.remove(mac)) {
            restoreSavedParams(mac, 0);
        } else {
            refreshControlStates(st);
        }
    });
    if (_currentMac == mac) {
        refreshInfoPanel();
        retargetWaveforms();
    }
    updateButtonStates();

    // Fetch the battery level once.
    st->profile->getBatteryLevel(CMD_TIMEOUT_MS, [this, mac](int result, std::string) {
        postToGui([this, mac, result]() {
            auto st = stateFor(mac);
            if (st && result >= 0
                && (st->lastPower < 0 || result - st->lastPower >= POWER_STABLE_BAND
                    || st->lastPower - result >= POWER_STABLE_BAND)) {
                st->lastPower = result;
                if (mac == _currentMac) {
                    _powerLabel->setText(QStringLiteral("Power: %1%").arg(result));
                }
            }
        });
    });
}

void MainWindow::applySessionParams(const std::shared_ptr<DeviceState>& st,
                                    std::function<void()> done) {
    const QString mac = st->mac;
    // The two setParams run one after another; done fires after the last one
    // completed.
    auto applyBinPath = [this, mac, done]() {
        auto st = stateFor(mac);
        if (!st || !st->profile || !_binDataEnabled) {
            if (done) {
                done();
            }
            return;
        }
        const QString path = _lastDataPaths.value(mac, QStringLiteral("True"));
        sendSetParam(st->profile, QStringLiteral("DEBUG_BLE_DATA_PATH"), path,
                     [this, mac, done](QString, bool isError) {
                         if (done) {
                             done();
                         }
                         if (isError) {
                             return;
                         }
                         auto st = stateFor(mac);
                         if (!st) {
                             return;
                         }
                         st->profile->getParam(5000, "DEBUG_BLE_DATA_PATH",
                             [this, mac](std::string result, std::string) {
                                 postToGui([this, mac, result]() {
                                     const QString cur = QString::fromStdString(result);
                                     if (!cur.isEmpty() && !cur.startsWith(QStringLiteral("Error"))) {
                                         _lastDataPaths[mac] = cur;
                                     }
                                 });
                             });
                     });
    };
    if (_debugLogEnabled) {
        const QString path = _lastLogPaths.value(mac, QStringLiteral("True"));
        sendSetParam(st->profile, QStringLiteral("DEBUG_LOG_PATH"), path,
                     [this, mac, applyBinPath](QString, bool isError) {
                         applyBinPath();
                         if (isError) {
                             return;
                         }
                         auto st = stateFor(mac);
                         if (!st) {
                             return;
                         }
                         st->profile->getParam(5000, "DEBUG_LOG_PATH",
                             [this, mac](std::string result, std::string) {
                                 postToGui([this, mac, result]() {
                                     const QString cur = QString::fromStdString(result);
                                     if (!cur.isEmpty() && !cur.startsWith(QStringLiteral("Error"))) {
                                         _lastLogPaths[mac] = cur;
                                     }
                                 });
                             });
                     });
    } else {
        applyBinPath();
    }
}

void MainWindow::recordSavedParam(const QString& mac, const QString& key,
                                  const QString& value, const QString& result) {
    if (mac.isEmpty() || result.startsWith(QStringLiteral("Error"))) {
        return;
    }
    auto& saved = _savedParamsByMac[mac];
    for (auto& kv : saved) {
        if (kv.first == key) {
            kv.second = value;
            return;
        }
    }
    saved.append({key, value});
}

void MainWindow::restoreSavedParams(const QString& mac, int index) {
    auto st = stateFor(mac);
    const auto saved = _savedParamsByMac.value(mac);
    if (!st || !st->profile) {
        return;
    }
    if (index >= saved.size()) {
        refreshControlStates(st);
        clearUiData();
        return;
    }
    const QString key = saved[index].first;
    const QString value = saved[index].second;
    sendSetParam(st->profile, key, value,
                 [this, mac, index, key, value](QString msg, bool) {
                     appLog(QStringLiteral("App: restore setParam(%1, %2) -> %3")
                                .arg(key, value, msg),
                            "I", stateFor(mac));
                     restoreSavedParams(mac, index + 1);
                 });
}

// -- SDK bridge events -------------------------------------------------------

void MainWindow::enqueueData(const std::string& mac, QSharedPointer<sensor::SensorData> owned,
                             sensor::SensorDataView borrowed) {
    {
        std::lock_guard<std::mutex> lock(_dataQueueMutex);
        // Cap the backlog: drop the oldest batches when full.
        while (_dataQueue.size() >= 1000) {
            _dataQueue.pop_front();
        }
        _dataQueue.push_back({QString::fromUtf8(mac.c_str()), std::move(owned), borrowed});
    }
    _dataQueueCv.notify_one();
}

void MainWindow::drainDataQueue() {
    std::deque<QueuedItem> pending;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(_dataQueueMutex);
            _dataQueueCv.wait(lock, [this] { return _dataWorkerStop.load() || !_dataQueue.empty(); });
            if (_dataWorkerStop.load() && _dataQueue.empty()) {
                return;
            }
            pending.swap(_dataQueue);
        }
        for (const auto& item : pending) {
            // stateFor returns a shared_ptr copy that keeps the DeviceState
            // alive during the append.
            auto st = stateFor(item.mac);
            if (!st) {
                continue;
            }
            if (item.owned) {
                // Clone mode: owned deep copy.
                st->appendData(*item.owned);
            }
            else if (item.borrowed.channelSamples != nullptr) {
                // Zero-copy mode: the queued view borrows the SDK's buffers.
                st->appendData(item.borrowed);
            }
        }
        pending.clear();
    }
}

void MainWindow::onStateChanged(QString mac, int state) {
    auto st = stateFor(mac);
    if (state == readyState()) {
        if (st && !st->flowStarted) {
            st->flowStarted = true;
            onProfileReady(mac);
        } else if (st && mac == _currentMac) {
            // A repeated Ready for an already-running device: just refresh
            // the display.
            _statusLabel->setText(st->buildStatusText());
        }
        return;
    }
    if (state != disconnectedState() || !st || st->isReplay) {
        return;
    }
    // Every disconnect tears the UI state down; with Auto Reconnect on, the
    // app-driven recovery (onAutoReconnectRequested) rebuilds it once the
    // link is back.
    _restoreParamsMacs.remove(mac);
    // Drop the cached parameter states with the link; the boxes fall back to
    // visible + disabled + unchecked.
    st->ntfStates.clear();
    st->filterStates.clear();
    st->sampleRateOptions.clear();
    st->sampleRateCurrent = 0;
    {
        QMutexLocker lock(&_statesMutex);
        _deviceStates.remove(mac);
    }
    appLog(QStringLiteral("App: device disconnected, removed from UI: %1").arg(mac));
    _streamingMacs.remove(mac);
    updateDeviceItemText(mac, false);
    if (_currentMac == mac) {
        _currentMac.clear();
        retargetWaveforms();
        refreshInfoPanel();
        _statusLabel->setText(QStringLiteral("Disconnected (device)"));
        _rateLabel->clear();
    }
    updateButtonStates();
}

void MainWindow::onAutoReconnectRequested(QString mac, bool restore) {
    // App-driven recovery: re-select the row and re-drive the connect flow;
    // the recorded setParam history replays after the stream start.
    for (int i = 0; i < _deviceList->count(); ++i) {
        auto* item = _deviceList->item(i);
        if (item->data(Qt::UserRole).toString() == mac) {
            _deviceList->setCurrentItem(item);
            break;
        }
    }
    if (_deviceStates.contains(mac)) {
        return;
    }
    auto profile = _controller->getSensor(mac.toStdString());
    if (!profile) {
        return;
    }
    if (restore) {
        _restoreParamsMacs.insert(mac);
    }
    QString name;
    for (const auto& d : _discovered) {
        if (d.mac == mac) {
            name = d.name;
            break;
        }
    }
    auto st = std::make_shared<DeviceState>(profile);
    st->liveFilter.setBand(_filterCombo->currentIndex());
    st->name = name;
    {
        QMutexLocker lock(&_statesMutex);
        _deviceStates.insert(mac, st);
    }
    _currentMac = mac;
    retargetWaveforms();
    refreshInfoPanel();
    if (!profile->isReady()) {
        profile->connect({}); // the Ready state-change drives the flow
    } else {
        st->flowStarted = true;
        onProfileReady(mac);
    }
}

void MainWindow::onError(QString mac, QString message) {
    qWarning("[DemoEMG] error from %s: %s", qPrintable(mac), qPrintable(message));
    auto st = stateFor(mac);
    appLog(QStringLiteral("App: error callback: %1").arg(message), "E", st);
    if (st && mac == _currentMac) {
        _statusLabel->setText(QStringLiteral("Error: %1").arg(message));
    }
}

void MainWindow::onPowerChanged(QString mac, int power) {
    // Pushed by the SDK's powerRefreshInterval polling; take it as-is.
    auto st = stateFor(mac);
    if (!st || power < 0) {
        return;
    }
    st->lastPower = power;
    if (mac == _currentMac) {
        _powerLabel->setText(QStringLiteral("Power: %1%").arg(power));
    }
}

void MainWindow::onDeviceInfoUpdate(QString mac) {
    // Pushed after the profile's cached DeviceInfo changed; re-read the cache
    // and refresh the labels that show it.
    auto st = stateFor(mac);
    if (!st) {
        return;
    }
    st->info = st->profile->getDeviceInfo();
    st->hasInfo = true;
    // A changed sample rate invalidates the ring time windows; rebuild the
    // affected rings.
    st->syncSampleRates();
    // Keep the sample-rate radio checked state in sync with the current
    // bound rate; the radios' enable state is untouched.
    if (st->info.EEGSampleRate > 0 && (int)st->info.EEGSampleRate != st->sampleRateCurrent) {
        st->sampleRateCurrent = (int)st->info.EEGSampleRate;
        if (mac == _currentMac) {
            setSampleRateChecked(st->sampleRateCurrent);
        }
    }
    if (mac == _currentMac) {
        _linkLabel->setText(linkText(st->info));
        _mtuLabel->setText(mtuText(st->info));
        if (st->flowStarted) {
            // The status/rate texts carry the per-type sample rates.
            _statusLabel->setText(st->buildStatusText());
            _rateLabel->setText(st->buildRateText());
        }
    }
}

void MainWindow::onDataTransferStateChanged(QString mac, bool isTransferring) {
    // SDK push: the data stream actually started or stopped (also covers
    // auto-reconnect stream restores and replay EOF).
    auto st = stateFor(mac);
    if (isTransferring) {
        _streamingMacs.insert(mac);
    } else {
        _streamingMacs.remove(mac);
    }
    appLog(QStringLiteral("App: data stream %1 %2")
               .arg(isTransferring ? QStringLiteral("ON") : QStringLiteral("OFF"), mac),
           "I", st);
    if (_replayMacs.contains(mac)) {
        updateReplayItemText(mac);
        if (!isTransferring) {
            // Replay EOF (or a user stop): finish the member here.
            finishReplayMember(mac);
        }
        return;
    }
    updateDeviceItemText(mac, st != nullptr);
}

void MainWindow::updateReplayItemText(const QString& mac) {
    auto st = stateFor(mac);
    if (!st) {
        return;
    }
    const QString prefix = _streamingMacs.contains(mac)
        ? QStringLiteral("[Streaming] [Replay] ") : QStringLiteral("[Replay] ");
    for (int i = 0; i < _deviceList->count(); ++i) {
        auto* item = _deviceList->item(i);
        if (item->data(Qt::UserRole).toString() == mac) {
            item->setText(QStringLiteral("%1%2, Address: %3").arg(prefix, st->name, mac));
            break;
        }
    }
}

// -- setParam / getParam controls --------------------------------------------

void MainWindow::sendSetParam(const std::shared_ptr<sensor::SensorProfile>& profile,
                              const QString& key, const QString& value,
                              std::function<void(QString, bool)> completion) {
    profile->setParam(CMD_TIMEOUT_MS, key.toStdString(), value.toStdString(),
                      [this, completion](std::string result, std::string err) {
                          // Command-outcome failures arrive as all-caps
                          // "ERROR: ..." in the result slot
                          const bool isError = result.rfind("Error", 0) == 0
                                            || result.rfind("ERROR:", 0) == 0;
                          const QString msg = !err.empty() ? QString::fromStdString(err)
                                                           : QString::fromStdString(result);
                          postToGui([this, completion, msg, isError]() {
                              if (completion) {
                                  completion(msg, isError);
                              }
                          });
                      });
}

void MainWindow::onNtfToggled() {
    if (_updatingControls) {
        return;
    }
    auto st = currentState();
    if (!st || !st->profile->isReady()) {
        return;
    }
    auto* cb = qobject_cast<QCheckBox*>(sender());
    const QString key = _ntfBoxes.key(cb);
    if (key.isEmpty()) {
        return;
    }
    const QString mac = st->mac;
    const QString value = cb->isChecked() ? QStringLiteral("ON") : QStringLiteral("OFF");
    sendSetParam(st->profile, key, value,
                 [this, mac, key, value](QString msg, bool isError) {
                     appLog(QStringLiteral("User: setParam(%1, %2) -> %3").arg(key, value, msg));
                     recordSavedParam(mac, key, value, msg);
                     if (isError) {
                         QMessageBox::warning(this, QStringLiteral("Set Parameter Failed"),
                                              QStringLiteral("Failed to set %1:\n%2").arg(key, msg));
                         return;
                     }
                     refreshControlStates(currentState());
                     clearUiData();
                 });
}

void MainWindow::onFilterToggled() {
    if (_updatingControls) {
        return;
    }
    auto st = currentState();
    if (!st || !st->profile->isReady()) {
        return;
    }
    auto* cb = qobject_cast<QCheckBox*>(sender());
    const QString key = _filterBoxes.key(cb);
    if (key.isEmpty()) {
        return;
    }
    const QString mac = st->mac;
    const QString value = cb->isChecked() ? QStringLiteral("ON") : QStringLiteral("OFF");
    sendSetParam(st->profile, key, value,
                 [this, mac, key, value](QString msg, bool isError) {
                     appLog(QStringLiteral("User: setParam(%1, %2) -> %3").arg(key, value, msg));
                     recordSavedParam(mac, key, value, msg);
                     if (isError) {
                         QMessageBox::warning(this, QStringLiteral("Set Parameter Failed"),
                                              QStringLiteral("Failed to set %1:\n%2").arg(key, msg));
                         return;
                     }
                     refreshControlStates(currentState());
                     clearUiData();
                 });
}

void MainWindow::onSampleRateToggled() {
    if (_updatingControls) {
        return;
    }
    auto st = currentState();
    if (!st || !st->profile->isReady()) {
        return;
    }
    auto* rb = qobject_cast<QRadioButton*>(sender());
    const int rate = _sampleRateRadios.key(rb, 0);
    // Only the checked edge acts; the unchecked edge fires as the previously
    // checked radio loses the check.
    if (rate <= 0 || !rb->isChecked()) {
        return;
    }
    const QString mac = st->mac;
    const QString value = QString::number(rate);
    sendSetParam(st->profile, QStringLiteral("EEG_SAMPLE_RATE"), value,
                 [this, mac, value](QString msg, bool isError) {
                     appLog(QStringLiteral("User: setParam(EEG_SAMPLE_RATE, %1) -> %2").arg(value, msg));
                     recordSavedParam(mac, QStringLiteral("EEG_SAMPLE_RATE"), value, msg);
                     if (isError) {
                         QMessageBox::warning(this, QStringLiteral("Set Parameter Failed"),
                                              QStringLiteral("Failed to set EEG_SAMPLE_RATE:\n%1").arg(msg));
                         return;
                     }
                     refreshControlStates(currentState());
                     clearUiData();
                 });
}

void MainWindow::refreshControlStates(const std::shared_ptr<DeviceState>& st) {
    if (!st) {
        return;
    }
    // Chain NTF -> FILTER -> EEG_SAMPLE_RATE_LIST -> EEG_SAMPLE_RATE and
    // apply once all answers are in, back on the GUI thread
    st->profile->getParam(5000, "NTF",
        [this, mac = st->mac](std::string ntfResult, std::string) {
            postToGui([this, mac, ntfResult]() {
                auto st = stateFor(mac);
                if (!st) {
                    return;
                }
                st->profile->getParam(5000, "FILTER",
                    [this, mac, ntfResult](std::string filterResult, std::string) {
                        postToGui([this, mac, ntfResult, filterResult]() {
                            auto st = stateFor(mac);
                            if (!st) {
                                return;
                            }
                            st->profile->getParam(5000, "EEG_SAMPLE_RATE_LIST",
                                [this, mac, ntfResult, filterResult](std::string rateListResult, std::string) {
                                    postToGui([this, mac, ntfResult, filterResult, rateListResult]() {
                                        auto st = stateFor(mac);
                                        if (!st) {
                                            return;
                                        }
                                        st->profile->getParam(5000, "EEG_SAMPLE_RATE",
                                            [this, mac, ntfResult, filterResult, rateListResult](std::string rateResult, std::string) {
                                                postToGui([this, mac, ntfResult, filterResult, rateListResult, rateResult]() {
                                                    auto st = stateFor(mac);
                                                    if (!st) {
                                                        return;
                                                    }
                                                    applyRefreshedControlStates(st,
                                                        QString::fromStdString(ntfResult),
                                                        QString::fromStdString(filterResult),
                                                        QString::fromStdString(rateListResult),
                                                        QString::fromStdString(rateResult));
                                                });
                                            });
                                    });
                                });
                        });
                    });
            });
        });
}

void MainWindow::applyRefreshedControlStates(const std::shared_ptr<DeviceState>& st,
                                             const QString& ntfResult, const QString& filterResult,
                                             const QString& rateListResult, const QString& rateResult) {
    const int emgCh = st->hasInfo ? st->info.EMGChannelCount : 0;
    const int eegCh = st->hasInfo ? st->info.EEGChannelCount : 0;
    const int imuCh = st->hasInfo ? qMax<int>(st->info.AccChannelCount, st->info.GyroChannelCount) : 0;
    const int ppgCh = st->hasInfo ? st->info.PpgChannelCount : 0;
    const int spo2Ch = st->hasInfo ? st->info.Spo2ChannelCount : 0;
    const QMap<QString, int> channelMap = {
        {QStringLiteral("NTF_EEG"), eegCh},
        {QStringLiteral("NTF_EMG"), emgCh},
        // GEST shares the EMG channel count.
        {QStringLiteral("NTF_GEST"), emgCh},
        {QStringLiteral("NTF_PPG"), ppgCh},
        {QStringLiteral("NTF_SPO2"), spo2Ch},
        {QStringLiteral("NTF_IMU"), imuCh},
    };

    QMap<QString, QPair<bool, bool>> ntf;
    if (!ntfResult.startsWith(QStringLiteral("Error"))) {
        const QStringList items = ntfResult.split(QLatin1Char('|'));
        for (int i = 0; i + 1 < items.size(); i += 2) {
            const QString key = items[i];
            // Keys without a capability entry (or a checkbox, e.g. NTF_ECG)
            // count as unsupported (0); they are kept in the map so a
            // successful query with only unsupported keys does not look like
            // "no state info".
            const bool enabled = channelMap.value(key, 0) > 0;
            ntf.insert(key, {enabled, enabled && items[i + 1] == QStringLiteral("ON")});
        }
    }

    QMap<QString, QPair<bool, bool>> filters;
    const bool hasFilter = !filterResult.isEmpty() && !filterResult.startsWith(QStringLiteral("Error"));
    QMap<QString, QString> parsed;
    if (hasFilter) {
        const QStringList items = filterResult.split(QLatin1Char('|'));
        for (int i = 0; i + 1 < items.size(); i += 2) {
            parsed.insert(items[i], items[i + 1]);
        }
    }
    for (auto it = _filterBoxes.constBegin(); it != _filterBoxes.constEnd(); ++it) {
        filters.insert(it.key(), {hasFilter, hasFilter && parsed.value(it.key()) == QStringLiteral("ON")});
    }

    // EEG Sample Rate radios: candidates from the "|"-separated list answer
    // (empty on error / unsupported), current bound rate from the rate answer
    // (0 on error or when no EEG/ECG stream exists).
    QVector<int> rateOptions;
    if (!rateListResult.startsWith(QStringLiteral("Error"))) {
        const QStringList items = rateListResult.split(QLatin1Char('|'), Qt::SkipEmptyParts);
        for (const QString& item : items) {
            bool ok = false;
            const int rate = item.toInt(&ok);
            if (ok) {
                rateOptions.append(rate);
            }
        }
    }
    int rateCurrent = 0;
    if (!rateResult.startsWith(QStringLiteral("Error"))) {
        bool ok = false;
        const int rate = rateResult.toInt(&ok);
        if (ok) {
            rateCurrent = rate;
        }
    }

    st->ntfStates = ntf;
    st->filterStates = filters;
    st->sampleRateOptions = rateOptions;
    st->sampleRateCurrent = rateCurrent;
    if (st == currentState()) {
        applyControlStates(ntf, filters, rateOptions, rateCurrent);
    }
}

void MainWindow::applyControlStates(const QMap<QString, QPair<bool, bool>>& ntf,
                                    const QMap<QString, QPair<bool, bool>>& filters,
                                    const QVector<int>& rateOptions, int rateCurrent) {
    _updatingControls = true;
    for (auto it = _ntfBoxes.constBegin(); it != _ntfBoxes.constEnd(); ++it) {
        const auto state = ntf.value(it.key(), {false, false});
        // Unsupported boxes are hidden once state info exists; with no state
        // info (no device / failed query) all boxes stay visible but
        // disabled. Filters are never hidden.
        it.value()->setVisible(state.first || ntf.isEmpty());
        it.value()->setEnabled(state.first);
        it.value()->setChecked(state.second);
    }
    for (auto it = _filterBoxes.constBegin(); it != _filterBoxes.constEnd(); ++it) {
        const auto state = filters.value(it.key(), {false, false});
        it.value()->setEnabled(state.first);
        it.value()->setChecked(state.second);
    }
    // Sample-rate radios: enabled only when listed as a candidate; the
    // exclusive group must be dropped to uncheck every radio (no known
    // current rate), then restored.
    if (_sampleRateGroup != nullptr && !_sampleRateRadios.contains(rateCurrent)) {
        _sampleRateGroup->setExclusive(false);
    }
    for (auto it = _sampleRateRadios.constBegin(); it != _sampleRateRadios.constEnd(); ++it) {
        it.value()->setEnabled(rateOptions.contains(it.key()));
        it.value()->setChecked(it.key() == rateCurrent);
    }
    if (_sampleRateGroup != nullptr) {
        _sampleRateGroup->setExclusive(true);
    }
    _updatingControls = false;
}

void MainWindow::setSampleRateChecked(int rate) {
    _updatingControls = true;
    // The exclusive group must be dropped to uncheck every radio when the
    // rate is not a candidate (same pattern as applyControlStates).
    if (_sampleRateGroup != nullptr && !_sampleRateRadios.contains(rate)) {
        _sampleRateGroup->setExclusive(false);
    }
    for (auto it = _sampleRateRadios.constBegin(); it != _sampleRateRadios.constEnd(); ++it) {
        it.value()->setChecked(it.key() == rate);
    }
    if (_sampleRateGroup != nullptr) {
        _sampleRateGroup->setExclusive(true);
    }
    _updatingControls = false;
}

void MainWindow::clearUiData() {
    auto st = currentState();
    if (st) {
        st->clearBuffers();
    }
}

// -- Debug log / bin data toggles --------------------------------------------

void MainWindow::applySdkDebugLog() {
    // The session's controller log, per-device profile logs and bin exports
    // all go into a "<timestamp>_<sdk version>" subdir of
    // Documents/sensorsdklog; set the path before setDebugEnabled(true).
    const QString version = QString::fromStdString(_controller->getVersion()).replace('.', '_');
    const QString dir = QDir::homePath() + QStringLiteral("/Documents/sensorsdklog/")
                        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
                        + QStringLiteral("_") + version;
    _controller->setLogPath(true, dir.toStdString());
    _controller->setDebugEnabled(true);
    qInfo("[DemoEMG] setLogPath -> %s", qPrintable(dir));
}

void MainWindow::appLog(const QString& msg, const char* level,
                        const std::shared_ptr<DeviceState>& st) {
    const auto target = st ? st : currentState();
    if (target && target->profile) {
        target->profile->log(msg.toStdString(), level);
    } else {
        _controller->log(msg.toStdString(), level);
    }
}

void MainWindow::onDebugLogToggled(int state) {
    _debugLogEnabled = (state == Qt::Checked);
    appLog(QStringLiteral("User: SDK debug log %1")
               .arg(_debugLogEnabled ? QStringLiteral("ON") : QStringLiteral("OFF")));
    if (_debugLogEnabled) {
        applySdkDebugLog();
    } else {
        _controller->setDebugEnabled(false);
    }
    const QString value = _debugLogEnabled ? QStringLiteral("True") : QStringLiteral("False");
    for (const auto& st : _deviceStates) {
        if (st->profile->isReady() && st->profile->hasInit()) {
            sendSetParam(st->profile, QStringLiteral("DEBUG_LOG_PATH"), value,
                         [this, st, value](QString msg, bool isError) {
                             st->profile->log(QStringLiteral("App: setParam(DEBUG_LOG_PATH, %1) -> %2")
                                                  .arg(value, msg).toStdString());
                             if (isError) {
                                 QMessageBox::warning(this, QStringLiteral("Set Parameter Failed"),
                                                      QStringLiteral("Failed to set DEBUG_LOG_PATH:\n%1").arg(msg));
                             }
                         });
        }
    }
}

void MainWindow::onBinDataToggled(int state) {
    _binDataEnabled = (state == Qt::Checked);
    appLog(QStringLiteral("User: data debug log %1")
               .arg(_binDataEnabled ? QStringLiteral("ON") : QStringLiteral("OFF")));
    const QString value = _binDataEnabled ? QStringLiteral("True") : QStringLiteral("False");
    for (const auto& st : _deviceStates) {
        if (st->profile->isReady() && st->profile->hasInit()) {
            sendSetParam(st->profile, QStringLiteral("DEBUG_BLE_DATA_PATH"), value,
                         [this, st, value](QString msg, bool isError) {
                             st->profile->log(QStringLiteral("App: setParam(DEBUG_BLE_DATA_PATH, %1) -> %2")
                                                  .arg(value, msg).toStdString());
                             if (isError) {
                                 QMessageBox::warning(this, QStringLiteral("Set Parameter Failed"),
                                                      QStringLiteral("Failed to set DEBUG_BLE_DATA_PATH:\n%1").arg(msg));
                             }
                         });
        }
    }
}

void MainWindow::onAutoReconnectToggled(bool checked) {
    appLog(QStringLiteral("User: auto reconnect %1")
               .arg(checked ? QStringLiteral("ON") : QStringLiteral("OFF")));
    for (const auto& st : _deviceStates) {
        st->profile->setAutoReconnect(checked);
    }
}

// -- Waveform targeting / labels ---------------------------------------------

void MainWindow::onTypeChanged(int /*index*/) {
    appLog(QStringLiteral("User: display data type -> %1").arg(_typeCombo->currentText()));
    retargetWaveforms();
}

void MainWindow::onLiveFilterChanged(int index) {
    appLog(QStringLiteral("User: live filter -> %1").arg(_filterCombo->currentText()));
    // The band applies to every device; each state picks it up on the next
    // batch.
    QMutexLocker lock(&_statesMutex);
    for (const auto& st : _deviceStates) {
        st->liveFilter.setBand(index);
    }
}

void MainWindow::retargetWaveforms() {
    auto st = currentState();
    const int typeIndex = _typeCombo->currentIndex();
    RingBuffer* buf = nullptr;
    QStringList labels;
    double yLow = -1.0, yHigh = 1.0;
    switch (typeIndex) {
    case 0:
        buf = st ? &st->acc : nullptr;
        labels = {QStringLiteral("ACC-X"), QStringLiteral("ACC-Y"), QStringLiteral("ACC-Z")};
        yLow = -8; yHigh = 8;
        break;
    case 1:
        buf = st ? &st->gyro : nullptr;
        labels = {QStringLiteral("GYRO-X"), QStringLiteral("GYRO-Y"), QStringLiteral("GYRO-Z")};
        yLow = -2000; yHigh = 2000;
        break;
    case 2:
        buf = st ? &st->quat : nullptr;
        labels = {QStringLiteral("W"), QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
        yLow = -1; yHigh = 1;
        break;
    default:
        buf = st ? &st->euler : nullptr;
        labels = {QStringLiteral("Pitch(Y)"), QStringLiteral("Roll(X)"), QStringLiteral("Yaw(Z)")};
        yLow = -180; yHigh = 180;
        break;
    }
    if (st) {
        _wave2d->setSource(buf, &st->bufMutex, -1);
        _wave2d->setPlaceholder(QStringLiteral("Waiting for data ..."));
    } else {
        _wave2d->setSource(nullptr, nullptr, -1);
        _wave2d->setPlaceholder(QStringLiteral("Not connected"));
    }
    _wave2d->setLabels(labels);
    _wave2d->setFixedYRange(yLow, yHigh);

    // The spectrum tracks the active 2D type: same channel labels/colors;
    // cleared until the next FFT result arrives.
    _spectrum->setLabels(labels);
    _spectrum->setPlaceholder(_wave2d->hasSource() ? QStringLiteral("Waiting for data ...")
                                                   : QStringLiteral("Not connected"));
    _spectrum->clear();

    // Real-time value labels for the active 2D type.
    while (QLayoutItem* child = _valueLayout->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    _valueLabels.clear();
    for (const QString& n : labels) {
        auto* row = new QLabel(QStringLiteral("%1: --").arg(n), this);
        row->setStyleSheet(QStringLiteral("font-family: monospace; font-size: 13px;"));
        _valueLayout->addWidget(row);
        _valueLabels.insert(n, row);
    }

    retargetBio(st);
}

void MainWindow::retargetBio(const std::shared_ptr<DeviceState>& st) {
    // Page state resets on device switch / retarget.
    _bioPage = 0;
    layoutBio(st);
}

int MainWindow::bioPageCount(const std::shared_ptr<DeviceState>& st) const {
    if (!st || st->bioKind() != DeviceState::BioKind::EEG) {
        return 1;
    }
    const int extras = (st->info.ECGChannelCount > 0 ? 1 : 0)
                       + (st->info.BRTHChannelCount > 0 ? 1 : 0);
    const int perPage = _bioWidgets.size() - extras;
    const int total = st->info.EEGChannelCount > 0 ? st->info.EEGChannelCount
                      : st->eeg.allocated ? st->eeg.channels : 0;
    return qMax(1, (total + perPage - 1) / perPage);
}

void MainWindow::updatePageControls() {
    auto st = currentState();
    const int pages = bioPageCount(st);
    // Only EEG mode can yield more than one page; hidden otherwise.
    _pageControls->setVisible(pages > 1);
    _pageLabel->setText(QStringLiteral("Page %1 / %2").arg(_bioPage + 1).arg(pages));
    _btnPrevPage->setEnabled(_bioPage > 0);
    _btnNextPage->setEnabled(_bioPage < pages - 1);
}

void MainWindow::onPrevPage() {
    if (_bioPage <= 0) {
        return;
    }
    --_bioPage;
    appLog(QStringLiteral("User: prev page -> %1").arg(_bioPage), "D");
    layoutBio(currentState());
}

void MainWindow::onNextPage() {
    auto st = currentState();
    if (_bioPage >= bioPageCount(st) - 1) {
        return;
    }
    ++_bioPage;
    appLog(QStringLiteral("User: next page -> %1").arg(_bioPage), "D");
    layoutBio(st);
}

void MainWindow::layoutBio(const std::shared_ptr<DeviceState>& st) {
    _bioTargets.clear();
    // Row spectrum binding: channel >= 0 shows the row's spectrum, -1 hides
    // it (the waveform then spans the full row width).
    ++_bioFftEpoch;
    _bioFftChannels = QVector<int>(_bioWidgets.size(), -1);
    auto setBioSpectrum = [this](int row, int channel, const QString& label) {
        if (channel >= 0) {
            _bioFftChannels[row] = channel;
            _bioSpectra[row]->setColorIndex(channel);
            _bioSpectra[row]->setLabels({label});
            _bioSpectra[row]->setPlaceholder(QString());
            _bioSpectra[row]->setVisible(true);
        } else {
            _bioSpectra[row]->clear();
            _bioSpectra[row]->setVisible(false);
        }
    };
    const auto kind = st ? st->bioKind() : DeviceState::BioKind::None;
    const QString waiting = st ? QStringLiteral("Waiting for data ...")
                               : QStringLiteral("Not connected");

    if (kind == DeviceState::BioKind::EMG) {
        // EMG device: up to 8 EMG channels, no paging.
        _bioTitleLabel->setText(QStringLiteral("EMG Waveform"));
        const int emgCh = st->emg.allocated ? qMin(st->emg.channels, _bioWidgets.size()) : 0;
        for (int i = 0; i < _bioWidgets.size(); ++i) {
            if (i < emgCh) {
                _bioWidgets[i]->setSource(&st->emg, &st->bufMutex, i);
                _bioWidgets[i]->setLabels({QStringLiteral("EMG-%1").arg(i + 1)});
                _bioWidgets[i]->setPlaceholder(QString());
                setBioSpectrum(i, i, QStringLiteral("EMG-%1").arg(i + 1));
                _bioTargets.append({&st->emgImpedance, i});
            } else {
                _bioWidgets[i]->setSource(nullptr, nullptr, i);
                _bioWidgets[i]->setLabels({});
                _bioWidgets[i]->setPlaceholder(waiting);
                _bioWidgets[i]->setSideText(QString(), Qt::white);
                setBioSpectrum(i, -1, QString());
                _bioTargets.append(BioTarget{});
            }
        }
    } else if (kind == DeviceState::BioKind::EEG) {
        // EEG device: channels per page = 8 - hasECG - hasBRTH; the current
        // page's EEG channels fill the leading widgets, ECG takes the last
        // widget (second-to-last when BRTH follows), BRTH the last.
        _bioTitleLabel->setText(QStringLiteral("EEG + ECG + BRTH Waveform"));
        const bool hasECG = st->info.ECGChannelCount > 0 || st->ecg.allocated;
        const bool hasBRTH = st->info.BRTHChannelCount > 0 || st->brth.allocated;
        const int perPage = _bioWidgets.size() - int(hasECG) - int(hasBRTH);
        const int total = st->info.EEGChannelCount > 0 ? st->info.EEGChannelCount
                          : st->eeg.allocated ? st->eeg.channels : 0;
        const int pages = qMax(1, (total + perPage - 1) / perPage);
        _bioPage = qBound(0, _bioPage, pages - 1);
        const int startCh = _bioPage * perPage;
        const int ecgIndex = _bioWidgets.size() - 1 - int(hasBRTH);
        const int brthIndex = _bioWidgets.size() - 1;
        for (int i = 0; i < _bioWidgets.size(); ++i) {
            const int eegCh = startCh + i;
            if (i < perPage && eegCh < total && st->eeg.allocated) {
                // Pass the real channel index so the waveform color stays
                // stable per channel across pages.
                _bioWidgets[i]->setSource(&st->eeg, &st->bufMutex, eegCh);
                _bioWidgets[i]->setLabels({QStringLiteral("EEG-%1").arg(eegCh + 1)});
                _bioWidgets[i]->setPlaceholder(QString());
                setBioSpectrum(i, eegCh, QStringLiteral("EEG-%1").arg(eegCh + 1));
                _bioTargets.append({&st->eegImpedance, eegCh});
            } else if (hasECG && i == ecgIndex && st->ecg.allocated) {
                _bioWidgets[i]->setSource(&st->ecg, &st->bufMutex, 0);
                _bioWidgets[i]->setLabels({QStringLiteral("ECG")});
                _bioWidgets[i]->setPlaceholder(QString());
                setBioSpectrum(i, -1, QString());
                _bioTargets.append({&st->ecgImpedance, 0});
            } else if (hasBRTH && i == brthIndex && st->brth.allocated) {
                _bioWidgets[i]->setSource(&st->brth, &st->bufMutex, 0);
                _bioWidgets[i]->setLabels({QStringLiteral("BRTH")});
                _bioWidgets[i]->setPlaceholder(QString());
                setBioSpectrum(i, -1, QString());
                _bioTargets.append({&st->brthImpedance, 0});
            } else {
                _bioWidgets[i]->setSource(nullptr, nullptr, i);
                _bioWidgets[i]->setLabels({});
                // Slots past the last EEG channel on the final page stay
                // blank.
                const bool noSuchChannel = i < perPage && eegCh >= total;
                _bioWidgets[i]->setPlaceholder(noSuchChannel ? QString() : waiting);
                _bioWidgets[i]->setSideText(QString(), Qt::white);
                setBioSpectrum(i, -1, QString());
                _bioTargets.append(BioTarget{});
            }
        }
    } else if (kind == DeviceState::BioKind::PPG) {
        // PPG device: fixed 6 plots on the leading widgets: 2x EEG fp1/fp2 +
        // 2x PPG red/ir led + 2x SpO2 spo2/heart_rate; the trailing widgets
        // stay blank. No paging; the two EEG plots carry impedance side
        // texts like the EEG mode.
        _bioTitleLabel->setText(QStringLiteral("EEG + PPG + SpO2 Waveform"));
        const struct {
            RingBuffer DeviceState::*buffer;
            int channel;
            const char* label;
            bool isEeg;
        } plotConfig[] = {
            {&DeviceState::eeg, 0, "fp1", true},
            {&DeviceState::eeg, 1, "fp2", true},
            {&DeviceState::ppg, 0, "red_led", false},
            {&DeviceState::ppg, 1, "ir_led", false},
            {&DeviceState::spo2, 0, "spo2", false},
            {&DeviceState::spo2, 1, "heart_rate", false},
        };
        const int plotCount = static_cast<int>(sizeof(plotConfig) / sizeof(plotConfig[0]));
        for (int i = 0; i < _bioWidgets.size(); ++i) {
            bool bound = false;
            if (i < plotCount) {
                const auto& cfg = plotConfig[i];
                RingBuffer* buf = &(st.get()->*cfg.buffer);
                if (buf->allocated && cfg.channel < buf->channels) {
                    // Read the configured buffer channel; the widget index only
                    // drives the curve color so each plot gets its own.
                    _bioWidgets[i]->setSource(buf, &st->bufMutex, cfg.channel, i);
                    _bioWidgets[i]->setLabels({QString::fromLatin1(cfg.label)});
                    _bioWidgets[i]->setPlaceholder(QString());
                    setBioSpectrum(i, -1, QString());
                    _bioTargets.append(cfg.isEeg ? BioTarget{&st->eegImpedance, cfg.channel}
                                                 : BioTarget{});
                    bound = true;
                }
            }
            if (!bound) {
                _bioWidgets[i]->setSource(nullptr, nullptr, i);
                _bioWidgets[i]->setLabels({});
                // Slots within the plot set wait for their buffer; the
                // trailing widgets stay blank.
                _bioWidgets[i]->setPlaceholder(i < plotCount ? waiting : QString());
                _bioWidgets[i]->setSideText(QString(), Qt::white);
                setBioSpectrum(i, -1, QString());
                _bioTargets.append(BioTarget{});
            }
        }
    } else {
        _bioTitleLabel->setText(QStringLiteral("EMG / EEG Waveform"));
        for (int i = 0; i < _bioWidgets.size(); ++i) {
            _bioWidgets[i]->setSource(nullptr, nullptr, i);
            _bioWidgets[i]->setLabels({});
            _bioWidgets[i]->setPlaceholder(waiting);
            _bioWidgets[i]->setSideText(QString(), Qt::white);
            setBioSpectrum(i, -1, QString());
            _bioTargets.append(BioTarget{});
        }
    }
    updatePageControls();
}

void MainWindow::refreshValueLabels() {
    auto st = currentState();
    if (!st) {
        return;
    }
    RingBuffer* buf = nullptr;
    switch (_typeCombo->currentIndex()) {
    case 0: buf = &st->acc; break;
    case 1: buf = &st->gyro; break;
    case 2: buf = &st->quat; break;
    default: buf = &st->euler; break;
    }
    QMutexLocker lock(&st->bufMutex);
    if (!buf->allocated) {
        return;
    }
    int row = 0;
    for (auto it = _valueLabels.constBegin(); it != _valueLabels.constEnd(); ++it, ++row) {
        if (row < buf->channels) {
            it.value()->setText(QString::asprintf("%s: %+.4f",
                                                  qPrintable(it.key()), buf->latest(row)));
        }
    }
}

void MainWindow::refreshBioSideTexts() {
    auto st = currentState();
    if (!st || _bioTargets.size() != _bioWidgets.size()) {
        return;
    }
    QMutexLocker lock(&st->bufMutex);
    for (int i = 0; i < _bioWidgets.size(); ++i) {
        const auto& target = _bioTargets[i];
        if (target.impedance == nullptr || target.channel >= target.impedance->size()
            || (*target.impedance)[target.channel] < 0) {
            continue;
        }
        const double kOhm = (*target.impedance)[target.channel] / 1000.0;
        const QColor color = kOhm <= 500 ? QColor(60, 200, 60)
                             : kOhm <= 999 ? QColor(230, 160, 40)
                                           : QColor(220, 60, 60);
        _bioWidgets[i]->setSideText(QString::asprintf("%.2f KOhm", kOhm), color);
    }
}

void MainWindow::refreshGestureLabel() {
    auto st = currentState();
    if (st && st->gesture >= 0) {
        QMutexLocker lock(&st->bufMutex);
        _gestureLabel->setText(QStringLiteral(
            "Gesture:\n  gesture: %1 (0-8)\n  raw gesture: %2 (0-8)\n  possiblity: %3 (0-100)\n  strength: %4 (0-100)")
                                   .arg(st->gesture)
                                   .arg(st->rawGesture)
                                   .arg(st->possibility)
                                   .arg(st->strength));
    } else {
        _gestureLabel->setText(QStringLiteral(
            "Gesture:\n  gesture: -- (0-8)\n  raw gesture: -- (0-8)\n  possiblity: -- (0-100)\n  strength: -- (0-100)"));
    }
}

void MainWindow::refreshInfoPanel() {
    auto st = currentState();
    if (st && st->hasInfo) {
        _modelLabel->setText(QStringLiteral("Model: %1").arg(QString::fromUtf8(st->info.modelName)));
        _hwLabel->setText(QStringLiteral("HW Version: %1").arg(QString::fromUtf8(st->info.hardwareVersion)));
        _fwLabel->setText(QStringLiteral("FW Version: %1").arg(QString::fromUtf8(st->info.firmwareVersion)));
        _linkLabel->setText(linkText(st->info));
        _mtuLabel->setText(mtuText(st->info));
    } else {
        _modelLabel->setText(QStringLiteral("Model: --"));
        _hwLabel->setText(QStringLiteral("HW Version: --"));
        _fwLabel->setText(QStringLiteral("FW Version: --"));
        _linkLabel->setText(QStringLiteral("Link: --"));
        _mtuLabel->setText(QStringLiteral("MTU: --"));
    }
    if (st && st->lastPower >= 0) {
        _powerLabel->setText(QStringLiteral("Power: %1%").arg(st->lastPower));
    } else {
        _powerLabel->setText(QStringLiteral("Power: --%"));
    }
    _statusLabel->setText(st ? st->buildStatusText() : QStringLiteral("Not Connected"));
    _rateLabel->setText(st ? st->buildRateText() : QString());
    updateLostPacketLabel();
    refreshGestureLabel();
    if (!st) {
        _cube->clearQuaternion();
    }
    applyControlStates(st ? st->ntfStates : QMap<QString, QPair<bool, bool>>{},
                       st ? st->filterStates : QMap<QString, QPair<bool, bool>>{},
                       st ? st->sampleRateOptions : QVector<int>{},
                       st ? st->sampleRateCurrent : 0);
}

void MainWindow::updateLostPacketLabel() {
    auto st = currentState();
    if (st) {
        // lostCounts is written under rateMutex by appendData.
        QMutexLocker lock(&st->rateMutex);
        if (!st->lostCounts.isEmpty()) {
            QStringList parts;
            for (auto it = st->lostCounts.constBegin(); it != st->lostCounts.constEnd(); ++it) {
                parts.append(QStringLiteral("%1: %2").arg(it.key()).arg(it.value()));
            }
            _lostPacketLabel->setText(QStringLiteral("Packet Loss Stats: ") + parts.join(QStringLiteral("  ")));
            return;
        }
    }
    _lostPacketLabel->setText(QStringLiteral("Packet Loss Stats: None"));
}

// -- Synchronized multi-device stream start/stop ------------------------------

std::vector<std::shared_ptr<sensor::SensorProfile>> MainWindow::liveReadySensors() {
    std::vector<std::shared_ptr<sensor::SensorProfile>> sensors;
    QMutexLocker lock(&_statesMutex);
    for (auto it = _deviceStates.constBegin(); it != _deviceStates.constEnd(); ++it) {
        const auto& st = it.value();
        if (!st->isReplay && st->profile && st->profile->isReady() && st->profile->hasInit()) {
            sensors.push_back(st->profile);
        }
    }
    return sensors;
}

void MainWindow::onMultiSyncClicked() {
    if (!_streamingMacs.isEmpty()) {
        doMultiStop();
    } else {
        doMultiStart();
    }
}

void MainWindow::doMultiStart() {
    const auto sensors = liveReadySensors();
    if (sensors.empty()) {
        appLog(QStringLiteral("User: multi start rejected (no connected device)"), "W");
        _statusLabel->setText(QStringLiteral("No connected device to sync-start"));
        return;
    }
    appLog(QStringLiteral("User: multi start on %1 device(s)").arg(sensors.size()));
    _btnMultiSync->setEnabled(false);

    // Same model for all devices -> default timing; mixed models -> no
    // dispersion check, longer timeout, more attempts.
    QSet<QString> models;
    for (const auto& s : sensors) {
        models.insert(QString::fromUtf8(s->getDeviceInfo().modelName));
    }
    const bool sameModel = models.size() == 1 && !models.contains(QString());
    const int startTimeout = sameModel ? MULTI_START_TIMEOUT_MS : 60000;
    const int dispersion = sameModel ? 5 : -1;
    const int attempts = sameModel ? 3 : 5;

    std::vector<std::shared_ptr<sensor::SensorProfile>> transferring;
    for (const auto& s : sensors) {
        if (s->hasStartDataNotification()) {
            transferring.push_back(s);
        }
    }

    auto startAll = [this, sensors, startTimeout, dispersion, attempts]() {
        _controller->multiStartDataNotification(sensors, startTimeout, dispersion, attempts,
            [this](const std::map<std::string, std::pair<bool, std::string>>& results) {
                postToGui([this, results]() {
                    QStringList failed;
                    for (const auto& r : results) {
                        if (!r.second.first) {
                            failed.append(QString::fromStdString(r.first));
                        }
                    }
                    if (!failed.isEmpty()) {
                        appLog(QStringLiteral("App: multi start failed on: %1").arg(failed.join(", ")), "W");
                        _statusLabel->setText(QStringLiteral("Multi start failed on: %1").arg(failed.join(", ")));
                    } else {
                        appLog(QStringLiteral("App: multi start OK: %1 device(s) started").arg(results.size()));
                        _statusLabel->setText(QStringLiteral("Multi start: %1 device(s) started").arg(results.size()));
                    }
                    updateButtonStates();
                });
            });
    };

    // Streaming devices go through one synchronized stop round first.
    if (!transferring.empty()) {
        _controller->multiStopDataNotification(transferring, MULTI_STOP_TIMEOUT_MS,
            [this, startAll](const std::map<std::string, std::pair<bool, std::string>>& results) {
                postToGui([this, results, startAll]() {
                    QStringList failed;
                    for (const auto& r : results) {
                        if (!r.second.first) {
                            failed.append(QString::fromStdString(r.first));
                        }
                    }
                    if (!failed.isEmpty()) {
                        appLog(QStringLiteral("App: multi stop failed on: %1").arg(failed.join(", ")), "W");
                        _statusLabel->setText(QStringLiteral("Multi stop failed on: %1").arg(failed.join(", ")));
                        updateButtonStates();
                        return;
                    }
                    startAll();
                });
            });
    } else {
        startAll();
    }
}

void MainWindow::doMultiStop() {
    const auto sensors = liveReadySensors();
    if (sensors.empty()) {
        appLog(QStringLiteral("User: multi stop rejected (no connected device)"), "W");
        _statusLabel->setText(QStringLiteral("No connected device to sync-stop"));
        return;
    }
    appLog(QStringLiteral("User: multi stop on %1 device(s)").arg(sensors.size()));
    _btnMultiSync->setEnabled(false);
    _controller->multiStopDataNotification(sensors, MULTI_STOP_TIMEOUT_MS,
        [this](const std::map<std::string, std::pair<bool, std::string>>& results) {
            postToGui([this, results]() {
                QStringList failed;
                for (const auto& r : results) {
                    if (!r.second.first) {
                        failed.append(QString::fromStdString(r.first));
                    }
                }
                if (!failed.isEmpty()) {
                    appLog(QStringLiteral("App: multi stop failed on: %1").arg(failed.join(", ")), "W");
                    _statusLabel->setText(QStringLiteral("Multi stop failed on: %1").arg(failed.join(", ")));
                } else {
                    appLog(QStringLiteral("App: multi stop OK: %1 device(s) stopped").arg(results.size()));
                    _statusLabel->setText(QStringLiteral("Multi stop: %1 device(s) stopped").arg(results.size()));
                }
                updateButtonStates();
            });
        });
}

// -- Bin replay --------------------------------------------------------------

void MainWindow::setReplayModeUi(bool replaying) {
    if (replaying && _controller->isScaning()) {
        _controller->stopScan();
        _scanning = false;
    }
    _btnScan->setEnabled(!replaying);
    _btnStopScan->setEnabled(false);
    _deviceList->setEnabled(!replaying);
    _chkDebugLog->setEnabled(!replaying);
    _chkBinData->setEnabled(!replaying);
    _btnReplay->setEnabled(!replaying);
    _btnMultiReplay->setEnabled(!replaying);
    _btnReplayPause->setEnabled(replaying);
    _btnReplayPause->setText(QStringLiteral("Pause Replay"));
    _btnReplayStop->setEnabled(replaying);
    if (replaying) {
        _btnConnect->setEnabled(false);
        _btnDisconnect->setEnabled(false);
        _btnMultiSync->setEnabled(false);
    } else {
        updateButtonStates();
    }
}

void MainWindow::onReplayClicked() {
    if (!_deviceStates.isEmpty()) {
        _statusLabel->setText(QStringLiteral("Please disconnect all devices before replaying a bin file"));
        return;
    }
    if (!_replayMacs.isEmpty()) {
        return;
    }
    QString startDir = QDir::homePath() + QStringLiteral("/Documents/sensorsdklog");
    if (!QDir(startDir).exists()) {
        startDir = QDir::homePath();
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select Bin File"),
                                                      startDir, QStringLiteral("Bin Files (*.bin)"));
    if (path.isEmpty()) {
        return;
    }
    startSingleReplay(path);
}

void MainWindow::startSingleReplay(const QString& path) {
    appLog(QStringLiteral("User: replay bin file: %1").arg(path));
    const auto info = _controller->getBinFileInfo(path.toStdString());
    if (!info.valid || info.mac.empty()) {
        appLog(QStringLiteral("App: invalid bin file (no config record): %1").arg(path), "W");
        _statusLabel->setText(QStringLiteral("Invalid bin file: no config record found"));
        return;
    }
    auto profile = _controller->replayBinFile(path.toStdString(), info.mac, true,
                                              REPLAY_DELEGATE_TIMEOUT_MS);
    if (!profile) {
        _statusLabel->setText(QStringLiteral("Replay failed to start"));
        return;
    }
    // The replay thread waits for a delegate before streaming data.
    profile->setDelegate(_bridge);

    _replayStopRequested = false;
    _replayPaused = false;
    _replayDoneFired = false;
    _replayMemberTotal = 1;

    const QString mac = addReplayMember(profile, info);
    for (int i = 0; i < _deviceList->count(); ++i) {
        if (_deviceList->item(i)->data(Qt::UserRole).toString() == mac) {
            _deviceList->setCurrentItem(_deviceList->item(i));
            break;
        }
    }
    _currentMac = mac;

    // Sync the sample-rate radio checked state to the bin's starting rate
    // (the radios' disabled state stays untouched).
    if (info.deviceInfo.EEGSampleRate > 0) {
        stateFor(mac)->sampleRateCurrent = (int)info.deviceInfo.EEGSampleRate;
        setSampleRateChecked((int)info.deviceInfo.EEGSampleRate);
    }

    retargetWaveforms();
    refreshInfoPanel();
    _statusLabel->setText(QStringLiteral("Replaying: %1 (duration %2s, realtime) ...")
                              .arg(QFileInfo(path).fileName())
                              .arg(info.durationSec, 0, 'f', 1));
    setReplayModeUi(true);
}

void MainWindow::onMultiReplayClicked() {
    if (!_deviceStates.isEmpty()) {
        _statusLabel->setText(QStringLiteral("Please disconnect all devices before replaying bin files"));
        return;
    }
    if (!_replayMacs.isEmpty()) {
        return;
    }
    QString startDir = QDir::homePath() + QStringLiteral("/Documents/sensorsdklog");
    if (!QDir(startDir).exists()) {
        startDir = QDir::homePath();
    }
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Select Bin Files"),
                                                            startDir, QStringLiteral("Bin Files (*.bin)"));
    if (paths.isEmpty()) {
        return;
    }
    if (paths.size() == 1) {
        startSingleReplay(paths.first());
        return;
    }
    appLog(QStringLiteral("User: replay bin files: %1").arg(paths.join(QStringLiteral("; "))));

    std::vector<std::pair<std::string, std::string>> pathMacList;
    QVector<sensor::BinFileInfo> infos;
    QSet<QString> macs;
    for (const QString& path : paths) {
        const auto info = _controller->getBinFileInfo(path.toStdString());
        if (!info.valid || info.mac.empty()) {
            appLog(QStringLiteral("App: invalid bin file (no config record): %1").arg(path), "W");
            _statusLabel->setText(QStringLiteral("Invalid bin file: %1").arg(QFileInfo(path).fileName()));
            return;
        }
        const QString mac = QString::fromStdString(info.mac);
        if (macs.contains(mac)) {
            appLog(QStringLiteral("App: duplicate device mac in selection: %1").arg(mac), "W");
            _statusLabel->setText(QStringLiteral("Duplicate device mac in selected bin files"));
            return;
        }
        macs.insert(mac);
        pathMacList.emplace_back(path.toStdString(), info.mac);
        infos.append(info);
    }

    const auto profiles = _controller->multiReplayBinFile(pathMacList, true,
                                                          REPLAY_DELEGATE_TIMEOUT_MS);
    _replayStopRequested = false;
    _replayPaused = false;
    _replayDoneFired = false;
    _replayMemberTotal = paths.size();

    QString firstMac;
    int firstRate = 0;
    for (int i = 0; i < paths.size(); ++i) {
        auto profile = profiles[i];
        if (!profile) {
            appLog(QStringLiteral("App: replay member failed to start: %1").arg(paths[i]), "W");
            continue;
        }
        // The replay thread waits for a delegate before streaming data.
        profile->setDelegate(_bridge);
        const QString mac = addReplayMember(profile, infos[i]);
        if (firstMac.isEmpty()) {
            firstMac = mac;
            firstRate = (int)infos[i].deviceInfo.EEGSampleRate;
        }
    }
    if (firstMac.isEmpty()) {
        _statusLabel->setText(QStringLiteral("Replay failed to start"));
        return;
    }
    for (int i = 0; i < _deviceList->count(); ++i) {
        if (_deviceList->item(i)->data(Qt::UserRole).toString() == firstMac) {
            _deviceList->setCurrentItem(_deviceList->item(i));
            break;
        }
    }
    _currentMac = firstMac;
    if (firstRate > 0) {
        stateFor(firstMac)->sampleRateCurrent = firstRate;
        setSampleRateChecked(firstRate);
    }

    retargetWaveforms();
    refreshInfoPanel();
    _statusLabel->setText(QStringLiteral("Replaying: %1 bin files (realtime) ...")
                              .arg(_replayMemberTotal));
    setReplayModeUi(true);
}

QString MainWindow::addReplayMember(const std::shared_ptr<sensor::SensorProfile>& profile,
                                    const sensor::BinFileInfo& info) {
    const QString mac = QString::fromStdString(info.mac);
    _replayMacs.insert(mac);
    auto st = std::make_shared<DeviceState>(profile);
    st->liveFilter.setBand(_filterCombo->currentIndex());   // see connect path
    st->isReplay = true;
    st->flowStarted = true;
    st->name = QString::fromStdString(info.deviceName);
    {
        QMutexLocker lock(&_statesMutex);
        _deviceStates.insert(mac, st);
    }
    auto* item = new QListWidgetItem(QStringLiteral("[Replay] %1, Address: %2").arg(st->name, mac));
    item->setData(Qt::UserRole, mac);
    _deviceList->addItem(item);
    return mac;
}

void MainWindow::onReplayPauseResume() {
    if (_replayMacs.isEmpty()) {
        return;
    }
    const QString action = _replayPaused ? QStringLiteral("resume") : QStringLiteral("pause");
    std::string result = "OK";
    for (const QString& mac : std::as_const(_replayMacs)) {
        result = _replayPaused
            ? _controller->resumeBinReplay(mac.toStdString())
            : _controller->pauseBinReplay(mac.toStdString());
        appLog(QStringLiteral("User: %1 replay -> %2").arg(action, QString::fromStdString(result)),
               result == "OK" ? "I" : "W", stateFor(mac));
    }
    if (result != "OK") {
        _statusLabel->setText(QStringLiteral("Replay pause/resume failed: %1")
                                  .arg(QString::fromStdString(result)));
        return;
    }
    _replayPaused = !_replayPaused;
    _btnReplayPause->setText(_replayPaused ? QStringLiteral("Resume Replay")
                                           : QStringLiteral("Pause Replay"));
    _statusLabel->setText(_replayPaused ? QStringLiteral("Replay paused")
                                        : QStringLiteral("Replaying ..."));
}

void MainWindow::onReplayStop() {
    if (_replayMacs.isEmpty()) {
        return;
    }
    _replayStopRequested = true;
    _btnReplayStop->setEnabled(false);
    _btnReplayPause->setEnabled(false);
    std::string result = "OK";
    for (const QString& mac : std::as_const(_replayMacs)) {
        const std::string r = _controller->stopBinReplay(mac.toStdString());
        appLog(QStringLiteral("User: stop replay -> %1").arg(QString::fromStdString(r)),
               r == "OK" ? "I" : "W", stateFor(mac));
        if (r != "OK") {
            result = r;
        }
    }
    if (result != "OK") {
        _statusLabel->setText(QStringLiteral("Stop replay failed: %1")
                                  .arg(QString::fromStdString(result)));
        _replayStopRequested = false;
        _btnReplayStop->setEnabled(true);
        _btnReplayPause->setEnabled(true);
        return;
    }
    _statusLabel->setText(QStringLiteral("Stopping replay ..."));
}

void MainWindow::finishReplayMember(const QString& mac) {
    if (!_replayMacs.contains(mac)) {
        return;
    }
    _replayMacs.remove(mac);
    {
        QMutexLocker lock(&_statesMutex);
        _deviceStates.remove(mac);
    }
    _streamingMacs.remove(mac);
    for (int i = 0; i < _deviceList->count(); ++i) {
        if (_deviceList->item(i)->data(Qt::UserRole).toString() == mac) {
            delete _deviceList->takeItem(i);
            break;
        }
    }
    if (_currentMac == mac) {
        _currentMac = _replayMacs.isEmpty() ? QString() : *_replayMacs.begin();
        if (!_currentMac.isEmpty()) {
            for (int i = 0; i < _deviceList->count(); ++i) {
                if (_deviceList->item(i)->data(Qt::UserRole).toString() == _currentMac) {
                    _deviceList->setCurrentItem(_deviceList->item(i));
                    break;
                }
            }
        }
        retargetWaveforms();
        refreshInfoPanel();
    }
    if (_replayMacs.isEmpty()) {
        onReplayDone(_replayStopRequested
            ? QStringLiteral("Replay stopped")
            : (_replayMemberTotal > 1
                ? QStringLiteral("Replay finished: %1 files").arg(_replayMemberTotal)
                : QStringLiteral("Replay finished")));
    }
}

void MainWindow::onReplayDone(const QString& message) {
    if (_replayDoneFired) {
        return;
    }
    _replayDoneFired = true;
    appLog(QStringLiteral("App: replay done: %1").arg(message));
    _replayStopRequested = false;
    _replayPaused = false;
    retargetWaveforms();
    refreshInfoPanel();
    _statusLabel->setText(message);
    setReplayModeUi(false);
}

// -- Bin analyze -------------------------------------------------------------

void MainWindow::onAnalyzeClicked() {
    if (_analyzeRunning) {
        return;
    }
    QString startDir = QDir::homePath() + QStringLiteral("/Documents/sensorsdklog");
    if (!QDir(startDir).exists()) {
        startDir = QDir::homePath();
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select Bin File to Analyze"),
                                                      startDir, QStringLiteral("Bin Files (*.bin)"));
    if (path.isEmpty()) {
        return;
    }
    appLog(QStringLiteral("User: analyze bin file: %1").arg(path));
    _analyzeRunning = true;
    _btnAnalyze->setEnabled(false);
    _statusLabel->setText(QStringLiteral("Analyzing: %1 ...").arg(QFileInfo(path).fileName()));

    if (_analyzeThread.joinable()) {
        _analyzeThread.join();
    }
    _analyzeThread = std::thread([this, path]() {
        QString csv = path;
        if (csv.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)) {
            csv.chop(4);
        }
        csv += QStringLiteral(".csv");
        const std::string result = _controller->parseBinToCsv(path.toStdString(), csv.toStdString());
        postToGui([this, result]() {
            _analyzeRunning = false;
            _btnAnalyze->setEnabled(true);
            const QString r = QString::fromStdString(result);
            if (r.startsWith(QStringLiteral("Error"))) {
                appLog(QStringLiteral("App: analyze failed: %1").arg(r), "E");
                _statusLabel->setText(QStringLiteral("Analyze failed: %1").arg(r));
                return;
            }
            appLog(QStringLiteral("App: CSV saved: %1").arg(r));
            _statusLabel->setText(QStringLiteral("CSV saved: %1").arg(r));
            QDesktopServices::openUrl(QUrl::fromLocalFile(r));
        });
    });
}

// -- Periodic refresh --------------------------------------------------------

// FFT spectrum of the active 2D type: the time-ordered ring snapshot is
// computed on a worker thread; results whose device or data type no longer
// match are dropped.
void MainWindow::maybeSubmitFft(const std::shared_ptr<DeviceState>& st) {
    if (!st || _fftBusy.load()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - _fftLastSubmitMs < FFT_UPDATE_INTERVAL_MS) {
        return;
    }
    RingBuffer* buf = nullptr;
    switch (_typeCombo->currentIndex()) {
    case 0: buf = &st->acc; break;
    case 1: buf = &st->gyro; break;
    case 2: buf = &st->quat; break;
    default: buf = &st->euler; break;
    }
    std::vector<std::vector<float>> snapshot;
    float rate = 0;
    {
        QMutexLocker lock(&st->bufMutex);
        if (!buf->allocated || buf->length < 16 || buf->sampleRate <= 0) {
            return;
        }
        rate = buf->sampleRate;
        // Reassemble the circular buffer oldest -> newest.
        snapshot.resize(buf->channels);
        for (int ch = 0; ch < buf->channels; ++ch) {
            auto& row = snapshot[ch];
            row.resize(buf->length);
            for (int i = 0; i < buf->length; ++i) {
                row[i] = buf->samples[ch][(buf->writeIndex + i) % buf->length];
            }
        }
    }
    _fftLastSubmitMs = now;
    _fftBusy = true;
    if (_fftThread.joinable()) {
        _fftThread.join();  // the previous task already reported back
    }
    const int typeIndex = _typeCombo->currentIndex();
    const QString mac = st->mac;
    _fftThread = std::thread([this, snapshot = std::move(snapshot), rate, typeIndex, mac] {
        std::vector<float> freqs;
        std::vector<std::vector<float>> mags;
        computeSpectrum(snapshot, rate, freqs, mags);
        {
            QMutexLocker lock(&_fftMutex);
            _fftFreqs = std::move(freqs);
            _fftMags = std::move(mags);
            _fftTypeIndex = typeIndex;
            _fftMac = mac;
            _fftReady = true;
        }
        _fftBusy = false;
    });
}

void MainWindow::pollFftResult() {
    QMutexLocker lock(&_fftMutex);
    if (!_fftReady) {
        return;
    }
    _fftReady = false;
    // Drop stale results: the user may have switched the data type or the
    // device while the worker was computing.
    auto st = currentState();
    if (!st || _fftTypeIndex != _typeCombo->currentIndex() || _fftMac != st->mac) {
        return;
    }
    _spectrum->setResult(_fftFreqs, _fftMags);
}

// Per-channel spectra of the EMG/EEG bio rows: the bound rows' ring channels
// are snapshotted oldest -> newest and computed on the shared FFT worker;
// results whose device or bio layout no longer match are dropped.
void MainWindow::maybeSubmitBioFft(const std::shared_ptr<DeviceState>& st) {
    if (!st || _fftBusy.load()) {
        return;
    }
    const auto kind = st->bioKind();
    if (kind != DeviceState::BioKind::EMG && kind != DeviceState::BioKind::EEG) {
        return;
    }
    QVector<int> channels;
    for (const int c : _bioFftChannels) {
        if (c >= 0) {
            channels.append(c);
        }
    }
    if (channels.isEmpty()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - _bioFftLastSubmitMs < FFT_UPDATE_INTERVAL_MS) {
        return;
    }
    RingBuffer* buf = kind == DeviceState::BioKind::EMG ? &st->emg : &st->eeg;
    std::vector<std::vector<float>> snapshot;
    float rate = 0;
    {
        QMutexLocker lock(&st->bufMutex);
        if (!buf->allocated || buf->length < 16 || buf->sampleRate <= 0) {
            return;
        }
        for (const int c : channels) {
            if (c >= buf->channels) {
                return;
            }
        }
        rate = buf->sampleRate;
        // Reassemble the circular buffer oldest -> newest.
        snapshot.resize(channels.size());
        for (int r = 0; r < channels.size(); ++r) {
            auto& row = snapshot[r];
            row.resize(buf->length);
            for (int i = 0; i < buf->length; ++i) {
                row[i] = buf->samples[channels[r]][(buf->writeIndex + i) % buf->length];
            }
        }
    }
    _bioFftLastSubmitMs = now;
    _fftBusy = true;
    if (_fftThread.joinable()) {
        _fftThread.join();  // the previous task already reported back
    }
    const int epoch = _bioFftEpoch;
    const QString mac = st->mac;
    _fftThread = std::thread([this, snapshot = std::move(snapshot), rate, epoch, mac] {
        std::vector<float> freqs;
        std::vector<std::vector<float>> mags;
        computeSpectrum(snapshot, rate, freqs, mags);
        {
            QMutexLocker lock(&_fftMutex);
            _bioFftFreqs = std::move(freqs);
            _bioFftMags = std::move(mags);
            _bioFftResultEpoch = epoch;
            _bioFftMac = mac;
            _bioFftReady = true;
        }
        _fftBusy = false;
    });
}

void MainWindow::pollBioFftResult() {
    QMutexLocker lock(&_fftMutex);
    if (!_bioFftReady) {
        return;
    }
    _bioFftReady = false;
    // Drop stale results: the device or the bio layout may have changed
    // while the worker was computing.
    auto st = currentState();
    if (!st || _bioFftMac != st->mac || _bioFftResultEpoch != _bioFftEpoch) {
        return;
    }
    int row = 0;
    for (int i = 0; i < _bioSpectra.size(); ++i) {
        if (i >= _bioFftChannels.size() || _bioFftChannels[i] < 0) {
            continue;
        }
        if (row < static_cast<int>(_bioFftMags.size())) {
            _bioSpectra[i]->setResult(_bioFftFreqs, {_bioFftMags[static_cast<size_t>(row)]});
        }
        ++row;
    }
}

void MainWindow::onPlotTick() {
    if (isMinimized()) {
        return;
    }
    _wave2d->update();
    for (auto* w : _bioWidgets) {
        w->update();
    }

    auto st = currentState();
    pollFftResult();
    maybeSubmitFft(st);
    pollBioFftResult();
    maybeSubmitBioFft(st);

    // The bio buffers are lazily sized on the first batch; target the widgets
    // once the buffer for the device's bio mode exists.
    const bool bioReady = st && ((st->bioKind() == DeviceState::BioKind::EMG && st->emg.allocated)
                                 || (st->bioKind() == DeviceState::BioKind::EEG && st->eeg.allocated)
                                 || (st->bioKind() == DeviceState::BioKind::PPG && st->ppg.allocated));
    if (bioReady && !_bioWidgets.isEmpty() && !_bioWidgets.first()->hasSource()) {
        retargetBio(st);
    }

    refreshValueLabels();
    refreshBioSideTexts();
    refreshGestureLabel();

    // 3D cube follows the latest quaternion sample
    if (st && st->quat.allocated && st->quat.channels >= 4) {
        QMutexLocker lock(&st->bufMutex);
        _cube->setQuaternion(st->quat.latest(0), st->quat.latest(1),
                             st->quat.latest(2), st->quat.latest(3));
    } else if (!st) {
        _cube->clearQuaternion();
    }

    ++_tickCount;
    if (_tickCount % (1000 / PLOT_UPDATE_INTERVAL_MS) == 0) {
        if (st) {
            st->updateActualRates();
            // Refresh the lost-packet label once per second.
            updateLostPacketLabel();
            if (st->flowStarted) {
                _statusLabel->setText(st->buildStatusText());
                _rateLabel->setText(st->buildRateText());
            }
        }
    }
}

// -- Shutdown ----------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* event) {
    _shuttingDown = true;
    appLog(QStringLiteral("App: demo window closing"));
    _plotTimer->stop();
    _dataWorkerStop.store(true);
    _dataQueueCv.notify_all();
    if (_dataWorker.joinable()) {
        _dataWorker.join();
    }
    if (_fftThread.joinable()) {
        _fftThread.join();
    }
    if (_analyzeThread.joinable()) {
        _analyzeThread.join();
    }
    sensor::SensorController::destory();
    event->accept();
}
