# Sensor SDK demo (Qt C++)

OYMotion Sensor SDK for C++, plus a Qt 6 Widgets demo app. This repository is
self-contained: the prebuilt SDK libraries for every supported platform ship
under `lib/` and the public headers under `include/`.

## Brief

OYMotion SDK is the software development kit for developers to access OYMotion
products. This project shows how to use the C++ SDK (the `sensor` library,
through the `sensorcpp` C++ binding) from a Qt app: scan, connect, configure
and stream EEG/ECG/EMG/IMU/PPG/SpO2 data, and replay recorded bin files
offline.

## Installation

Nothing to build first — the prebuilt SDK ships in this repository:

```
include/                                  public SDK headers
lib/windows/x64/Release/                  sensor.dll + sensor.lib
lib/windows/x86/Release/                  sensor32.dll + sensor32.lib (32-bit)
lib/linux/x86_64/libsensor.so
lib/linux/x86/libsensor.so                (32-bit)
lib/linux/arm64/libsensor.so              (ARM64)
lib/android/<abi>/libsensor.so            arm64-v8a, armeabi-v7a, x86, x86_64
lib/xcframework/sensor.xcframework        ios-arm64, ios-simulator (static)
lib/macos/libsensor.dylib                 macOS (universal)
```

The demo's CMake picks the library for the target platform automatically;
each choice has a configure-time option to override it:

| Option | Platform | Values (default first) |
|---|---|---|
| `SENSOR_SDK_ROOT` | all | package root containing `include/` + `lib/` |
| `SENSOR_SDK_WIN_ARCH` | Windows | `x64`, `x86` |
| `SENSOR_SDK_LINUX_ARCH` | Linux | `x86_64`, `x86`, `arm64` |
| `SENSOR_SDK_XCFRAMEWORK` | iOS | path to `sensor.xcframework` |
| `SENSOR_SDK_MAC_DYLIB` | macOS | path to `libsensor.dylib` |
| — | Android | follows the Qt kit ABI (`CMAKE_ANDROID_ARCH_ABI`) |

To link a self-built SDK instead, point `SENSOR_SDK_ROOT` at a directory with
the same `include/` + `lib/` layout.

The public ABI is `include/sen_capi.h`; C++ consumers use the header-only
C++11 binding `include/sensorcpp.hpp` — add `include/` to your include paths
and include `sensorcpp.hpp`; nothing else needs to be compiled into your
target.

## 1. Initialize the SDK

```cpp
#include "sensorcpp.hpp"

sensor::SensorController controller;   // RAII: the destructor destroys it

// Optional: redirect SDK logs (call BEFORE enabling DEBUG_ENABLED)
controller.setParam("LOG_PATH", "<session log dir>");
controller.setParam("DEBUG_ENABLED", "True");

// Scan results and bluetooth on/off arrive on the controller callback table
sensor::SensorControllerCallbacks controllerCbs;
controllerCbs.onScanResult = [](const std::vector<sensor::BLEDevice>& devices) {
    // BLEDevice: name / mac / rssi; devices not yet connected
};
controllerCbs.onEnableChanged = [](bool enabled) {};
controller.setCallbacks(controllerCbs);
```

Set the callback table BEFORE starting the scan; mutating it while events are
in flight is a data race. Use `getVersion()` to get the SDK version string.

## Controller methods

### 2. Start scan

```cpp
controller.startScan(6000);
```

`onScanResult` is invoked every `periodInMs` with the current device
list.

### 3. Stop scan / check state

```cpp
controller.stopScan();
bool scanning = controller.isScanning();
bool btOn = controller.isEnable();
```

### 4. Create / get a Profile

```cpp
sensor::SensorProfile* profile = controller.requireSensor(bleDevice.mac);
sensor::SensorProfile* same    = controller.getSensor(mac);  // nullptr when not created
auto connected = controller.getConnectedSensors();
```

`Profile` pointers are non-owning: they are owned by the `Controller` and die
with it.

### 5. Terminate

```cpp
sensor::SensorController::terminate();
```

Please MAKE SURE to call `terminate()` when the app exits; every controller
and profile handle is invalid afterwards.

## Profile methods

### 6. Register the callbacks

```cpp
sensor::SensorProfileCallbacks profileCbs;
profileCbs.onStateChange = [](sensor::SensorProfile* p, int newState) {
    // sensor::BLEDevice::State: Disconnected / Connecting / Connected / Ready /
    // Disconnecting — handle unexpected disconnects here
};
profileCbs.onError = [](sensor::SensorProfile* p, const std::string& errorMsg) {};
profileCbs.onPowerChange = [](sensor::SensorProfile* p, int power) {
    // battery 0-100 pushed periodically (see init); -1 is never reported
};
profileCbs.onData = [](sensor::SensorProfile* p,
                       const std::vector<sensor::SensorDataView>& dataList) {
    // batches after startData
    for (const auto& data : dataList) { }
};
profileCbs.onDeviceInfoUpdate = [](sensor::SensorProfile* p,
                                   const sensor::DeviceInfo& info) {};
profileCbs.onDataTransferStateChange = [](sensor::SensorProfile* p,
                                          bool isTransferring) {};
profile->setCallbacks(profileCbs);
```

Callbacks do not run on the UI thread — keep them short and marshal
UI updates to your framework's main thread (the demo uses queued Qt signals).

A further callback, `onAutoReconnect`, customizes stream recovery after an
abnormal disconnect — see 9.1.

### 7. Connect / disconnect

```cpp
profile->connect([](bool ok, const std::string& errorMsg) { });
profile->disconnect([](bool ok, const std::string& errorMsg) { });
```

Both are callback-async; an empty callback means fire-and-forget.
`disconnect()` stops an active data notification first.

### 8. Device state

```cpp
auto state = profile->getState();   // sensor::BLEDevice::State::Disconnected .. Invalid
if (profile->isReady()) { }         // Ready: commands may be sent
```

### 9. Get the BLE device

```cpp
auto dev = profile->getDevice();   // sensor::BLEDevice: name / mac / rssi
```

### 9.1 Auto reconnect and resume data stream

```cpp
profile->setAutoReconnect(true);   // default on in the demos; false to opt out
```

While enabled and the device is streaming, an abnormal disconnect is followed
by automatic reconnect -> `init()` with the previous arguments -> re-applying
the previous `setParam` values -> `startData()`. Explicit user
calls (`connect()`, `disconnect()`, `stopData()`) cancel a
pending resume.

To customize recovery, set the `onAutoReconnect(profile, hasLastSession,
answer)` callback and call `answer(true)`, then handle it yourself (the answer
may come
from any thread, at any later time); `answer(false)` falls back to the default
flow. If no answer arrives within 10 s the SDK runs the default recovery.

### 10. Device info

```cpp
auto info = profile->getDeviceInfo();   // valid after Ready + successful init
// deviceName, modelName, hardwareVersion, firmwareVersion, MTUSize,
// plus a ChannelCount / SampleRate pair per modality:
// EMG/EEG/ECG/BRTH/Acc/Gyro/MagAngle/Euler/Quat/Ppg/Spo2/Impe/Gest, the
// aggregated ImuChannelCount / ImuSampleRate (0 = no aggregated stream),
// Emg/Eeg/EcgMaxSampleRate (0 = not reported), and the BLE link parameters
// ConnectionIntervalMs / PeripheralLatency / SupervisionTimeoutMs
// (0 / -1 / 0 = unknown).
```

`fetchDeviceInfo(timeoutMs, cb)` re-queries the device; `onDeviceInfoUpdate`
reports later changes.

### 11. Init data transfer

```cpp
profile->init(32, 5000, 60000 /* powerRefreshIntervalMs */,
              [](bool ok, const std::string& errorMsg) { });
bool inited = profile->hasInit();
```

- `packageSampleCount`: samples per channel in each `sensor::SensorDataView`
  batch delivered to `onData`.
- `powerRefreshIntervalMs`: push period in ms for `onPowerChange` (0 = off).

### 12. Data notification

```cpp
profile->startData(5000, [](bool ok, const std::string& errorMsg) { });
profile->stopData(5000, [](bool ok, const std::string& errorMsg) { });
bool streaming = profile->hasStartDataNotification();
```

Data type list (`sensor::SensorDataView::Type`):

```
SensorDataView::NTF_ACC = 1            acceleration, unit g
SensorDataView::NTF_GYRO = 2           gyroscope, unit degree/s
SensorDataView::NTF_EULER_DATA = 4     euler angle, unit degree
SensorDataView::NTF_QUATERNION = 5     quaternion (w, x, y, z)
SensorDataView::NTF_GEST = 7           gesture id
SensorDataView::NTF_EMG_RAW_DATA = 8   unit uV
SensorDataView::NTF_MAG_ANGLE = 13
SensorDataView::NTF_EEG = 16           unit uV
SensorDataView::NTF_ECG = 17           unit uV
SensorDataView::NTF_IMPEDANCE = 18     electrode impedance
SensorDataView::NTF_IMU = 19           aggregated IMU batch (acc 0-2 / gyro 3-5 / euler 6-8 /
                                       quat 9-12; see DeviceInfo.ImuChannelCount)
SensorDataView::NTF_ADS = 20
SensorDataView::NTF_BRTH = 21          respiration, unit uV
SensorDataView::NTF_IMPEDANCE_EXT = 22
SensorDataView::NTF_SPO2 = 23          SpO2 percentage
SensorDataView::NTF_PPG = 24           PPG raw samples
```

Process data in `onData`. Each `sensor::SensorDataView` batch offers:

- metadata: `getDeviceMac()` / `getDataType()` / `getSampleRate()` /
  `getChannelCount()` / `getSampleCount()` / `getChannelMask()` /
  `isChannelEnabled(ch)` / `getLostPackageCount()` / `getStartTimeStamp()` /
  `getStartTimeSec()` / `getDelay()` / `isDataValid(ch, i)`
- per-point accessors (`channel`, `index`): `getData` / `getRawData` /
  `getImpedance` / `getSaturation` / `getSampleIndex` / `getTimeStampInMs` /
  `getAbsTimeStampInSec` / `isLost`, or `getChannelSample(channel, index)`
  for the whole `Sample`
- `clone()` for an owned deep copy (`sensor::SensorData`); a borrowed batch is
  only guaranteed readable inside the callback — `clone()` whatever you keep.
  Probe `isDataValid()` once per batch, then read the points freely.

### 12.1 Synchronized start/stop on multiple devices

```cpp
controller.multiStartData({sensor1, sensor2},
    timeoutMs, maxDelayDispersionMs, maxAttempts,
    [](const std::map<std::string, std::pair<bool, std::string>>& results) {
        // one {ok, errorMsg} entry per device mac
    });
controller.multiStopData({sensor1, sensor2}, timeoutMs, cb);
```

Every sensor must be `Ready` and initialized; devices already streaming are
stopped first so every stream (re)starts together. After each start round the
first-packet delays are compared: when the dispersion (max - min) exceeds
`maxDelayDispersionMs` (a negative value disables the check), the group is
stopped and restarted, up to `maxAttempts` rounds. `multiStop` reports
non-streaming devices as successful. `timeoutMs <= 0` selects a default.

### 13. Battery level

```cpp
profile->getBatteryLevel(5000, [](int level, const std::string& errorMsg) {
    // 0-100, -1 = no valid reading yet
});
```

### 14. setParam / getParam

`setParam(timeoutMs, key, value, cb)` configures the profile after `Ready` +
`init`. The callback result is `"OK"` on success or a string starting with
`"Error"`. Changing an `NTF_*` key while streaming restarts the data
notification so the setting takes effect; `FILTER_*` keys are applied on the
fly without interrupting the stream.

```cpp
// Data stream toggles, "ON" / "OFF"
profile->setParam(t, "NTF_EEG", "ON", cb);
profile->setParam(t, "NTF_ECG", "ON", cb);
profile->setParam(t, "NTF_EMG", "ON", cb);
profile->setParam(t, "NTF_GEST", "ON", cb);  // NTF_GEST and NTF_EMG are mutually
                                             // exclusive on legacy EMG devices
profile->setParam(t, "NTF_BRTH", "ON", cb);
profile->setParam(t, "NTF_IMPEDANCE", "ON", cb);
profile->setParam(t, "NTF_MAG_ANGLE", "ON", cb);
profile->setParam(t, "NTF_PPG", "ON", cb);
profile->setParam(t, "NTF_SPO2", "ON", cb);
profile->setParam(t, "NTF_IMU", "ON", cb);   // all four ACC/GYRO/EULER/QUAT
                                             // streams together

// Firmware filter toggles
profile->setParam(t, "FILTER_50HZ", "ON", cb);   // 50 Hz notch
profile->setParam(t, "FILTER_60HZ", "ON", cb);   // 60 Hz notch
profile->setParam(t, "FILTER_HPF", "ON", cb);    // 0.5 Hz high-pass
profile->setParam(t, "FILTER_LPF", "ON", cb);    // 80 Hz low-pass

// EEG/ECG sample rate (bound together on devices that have both); validated
// against getParam("EEG_SAMPLE_RATE_LIST")
profile->setParam(t, "EEG_SAMPLE_RATE", "500", cb);

// EMG sample rate (new EMG devices only); validated against
// getParam("EMG_SAMPLE_RATE_LIST")
profile->setParam(t, "EMG_SAMPLE_RATE", "500", cb);

// IMU sample rate (only devices answering the extended IMU capability
// query); validated against getParam("IMU_SAMPLE_RATE_LIST")
profile->setParam(t, "IMU_SAMPLE_RATE", "100", cb);

// PPG sample rate (PPG devices only); validated against
// getParam("PPG_SAMPLE_RATE_LIST")
profile->setParam(t, "PPG_SAMPLE_RATE", "50", cb);

// NeuCir remote control (NeuCir devices only)
profile->setParam(t, "NEUCIR_SET_MODE", "APP_REMOTE", cb);
profile->setParam(t, "NEUCIR_APP_CONTROL", "OPEN", cb);   // OPEN / CLOSE / STOP

// Bin export: "True" exports the session capture on stop/disconnect into
// the SDK log directory; an absolute path exports there; "False"/"" disables
profile->setParam(t, "DEBUG_BLE_DATA_PATH", "True", cb);

// Per-profile log file in the SDK log directory ("True"), or an absolute
// custom path; "False"/"" disables
profile->setParam(t, "DEBUG_LOG_PATH", "True", cb);

// Flush this device's pending bin capture and SDK log to disk; the value
// is ignored
profile->setParam(t, "FLUSH_BLE_DATA", "", cb);
```

Aggregate queries via `getParam(timeoutMs, key, cb)`:

```cpp
profile->getParam(t, "FILTER", cb);               // "FILTER_50HZ|ON|FILTER_60HZ|ON|..."
profile->getParam(t, "NTF", cb);                  // "NTF_BRTH|ON|NTF_ECG|ON|..."
profile->getParam(t, "EEG_SAMPLE_RATE", cb);      // e.g. "250"
profile->getParam(t, "EEG_SAMPLE_RATE_LIST", cb); // e.g. "250|500"
profile->getParam(t, "EMG_SAMPLE_RATE", cb);      // e.g. "500"
profile->getParam(t, "EMG_SAMPLE_RATE_LIST", cb); // e.g. "500|1000"
profile->getParam(t, "IMU_SAMPLE_RATE", cb);      // e.g. "50"
profile->getParam(t, "IMU_SAMPLE_RATE_LIST", cb); // e.g. "50|100|200"
profile->getParam(t, "PPG_SAMPLE_RATE", cb);      // e.g. "50"
profile->getParam(t, "PPG_SAMPLE_RATE_LIST", cb); // e.g. "50|100"
```

## Bin file recording and replay

Every session's raw BLE capture can be exported as a `.bin` file (see
`DEBUG_BLE_DATA_PATH`) and replayed offline; parsed results arrive via
`onData`, same as live data.

```cpp
auto info = controller.getBinFileInfo(path);   // BinFileInfo: mac, deviceName,
                                               // durationSec, deviceInfo, valid

// Replay (refused while the target profile is streaming live data); the
// target profile comes from requireSensor — the bin's info.mac works even
// for a device that was never scanned
sensor::SensorProfile* replay = controller.replayBinFile(
    path, controller.requireSensor(info.mac), realtime, timeoutMs);

controller.pauseBinReplay(mac);    // "OK" or an error string
controller.resumeBinReplay(mac);
controller.stopBinReplay(mac);

// Synchronized group replay: parallel path/sensor lists; every capture
// starts on one shared clock anchored at the earliest data record, so
// concurrently recorded captures keep their original relative offsets;
// pausing any member pauses the whole group. The returned vector is
// input-order aligned with nullptr entries for members that failed
// validation.
auto group = controller.multiReplayBinFile({path1, path2}, {sensor1, sensor2},
                                           realtime, timeoutMs);

// Offline conversion to CSV; callback-async, the callback receives the CSV
// file path on success or an "Error: ..." string (empty callback =
// fire-and-forget)
controller.parseBinToCsv(binPath, csvPath,
    [](const std::string& result, const std::string& error) { /* ... */ });
```

Replay end-of-file is reported through `onDataTransferStateChange`
(`isTransferring` flips to false).

## Logging controls

```cpp
controller.setParam("DEBUG_ENABLED", "True");  // opens the controller log in the log dir
controller.setParam("LOG_PATH", dir);          // redirect; dir is created if missing
controller.setParam("LOG_PATH", "True");       // back to the default
                                               // (~/Documents/sensorsdklog)
controller.setParam("LOG_PATH", "False");      // file output off
controller.setParam("DATA_LOG_ENABLED", "True"); // raw data records on
```

Ordering: `LOG_PATH` before `DEBUG_ENABLED=True` so the whole session
lands in one directory.

Applications can write their own entries into the same timeline:

```cpp
controller.log("User clicked start", "I");     // controller log
profile->log("User toggled filter 50Hz", "I"); // profile log when enabled
```

`level` accepts `"D"` / `"I"` / `"W"` / `"E"`; `"D"` follows the
`DEBUG_ENABLED` switch, the others are always emitted. Entries are tagged
`[App]`.

On Android/iOS, call `controller.onSuspend()` when the app goes to the
background to flush pending log and bin records.

## Demo app

The bundled Qt 6 Widgets app (`DemoEMG`) exercises the whole API: multi-device
scan/connect with an RSSI-sorted list and a current-device display, bio panel
with 8 waveforms (EMG mode, paged EEG + ECG/BRTH mode, or the fixed EEG + PPG
+ SpO2 chart mode) with a Live Filter band picker and impedance readouts (shown while the IMPE switch is on), IMU
waveforms with FFT spectra and a 3D quaternion cube, gesture box,
NTF/FILTER/sample-rate controls (unsupported controls are hidden once the
device info is known), battery display, Auto Reconnect and Clone Data toggles,
SDK debug log and bin export toggles, and bin replay / analyze-to-CSV. A Multi
Start/Stop toggle button starts (or stops) streaming on all connected devices
in sync via `multiStartData` / `multiStopData`, and a
Multi Replay Bin button replays several captures at once through
`multiReplayBinFile` with group-wide pause/resume. The window title shows the
demo version next to the SDK version.

### Build the demo

Prerequisite: Qt 6 — Windows: MSVC 2022 kit (`msvc2022_64` for the default
x64 build, or the 32-bit `msvc2022` kit for x86); macOS: Qt 6.8.3 `macos` kit
at `~/Qt/6.8.3/macos`. Override with `-DCMAKE_PREFIX_PATH=<your qt kit>` at
configure time.

Run from this directory. Windows x64:

```
cmake --preset qt-demo
cmake --build --preset qt-demo-debug              # -> build/Debug/DemoEMG.exe
```

Windows x86 (32-bit, needs the `msvc2022` kit):

```
cmake --preset qt-demo-x86
cmake --build --preset qt-demo-x86-release        # -> build_x86/Release/DemoEMG.exe
```

A post-build step copies the SDK dll and runs `windeployqt`, so the folder is
self-contained. The Windows package ships Release libraries only: a Debug
build of the demo also links and deploys the Release dll.

macOS:

```
cmake --preset qt-demo-mac
cmake --build --preset qt-demo-mac                # -> build_mac/DemoEMG
```

A post-build step copies `libsensor.dylib` next to the executable, so the
folder is self-contained.

Linux (set `QT_LINUX_KIT` to the Qt 6 kit, e.g. `~/Qt/6.10.2/gcc_64` — or
`/usr` when using the distro Qt 6 packages; add `-DSENSOR_SDK_LINUX_ARCH=x86`
for a 32-bit build with a 32-bit Qt kit):

```
cmake --preset qt-demo-linux
cmake --build --preset qt-demo-linux              # -> build_linux/DemoEMG
```

On an ARM64 machine with an ARM64 Qt kit, use the dedicated preset (equivalent
to `-DSENSOR_SDK_LINUX_ARCH=arm64` on the x86_64 one):

```
cmake --preset qt-demo-linux-arm64
cmake --build --preset qt-demo-linux-arm64        # -> build_linux_arm64/DemoEMG
```

The Linux preset was NOT compile-verified on the dev machine (Windows-only).

### Mobile (iOS / Android)

On iOS and Android the demo switches to a phone layout: the same widgets are
stacked under a `QTabWidget` whose tab bar sits at the bottom of the screen —
three tabs (Devices / IMU / Bio), with the settings groups merged into the
bottom of the (scrollable) Devices tab. To try the phone layout on a desktop
build:

```
cmake --preset qt-demo-mobile-preview
cmake --build --preset qt-demo-mobile-preview     # -> build_mobile_preview/DemoEMG
```

Prerequisites for real mobile builds:

- Install the Qt iOS and/or Android kits with the Qt Maintenance Tool (same
  Qt 6 version as the desktop kit; Android also needs the Android SDK/NDK).
- Point these environment variables at the mobile kits:
  - `QT_IOS_KIT` — e.g. `~/Qt/6.8.3/ios`
  - `QT_ANDROID_KIT` — e.g. `~/Qt/6.8.3/android_arm64_v8a`

iOS:

```
cmake --preset qt-demo-ios
cmake --build --preset qt-demo-ios                # -> build_ios/Debug/DemoEMG.app
```

- Signing: set `DEMO_IOS_DEV_TEAM` (env var or `-DDEMO_IOS_DEV_TEAM=<id>`) to
  your Apple development team; when empty Xcode picks its default.
- A real device is required — the iOS simulator has no Bluetooth. The app asks
  for Bluetooth permission on first use (`NSBluetoothAlwaysUsageDescription`
  in `ios/Info.plist`).

Android:

```
cmake --preset qt-demo-android
cmake --build --preset qt-demo-android            # -> build_android/ (APK via androiddeployqt)
```

- The package source dir `android/` carries the manifest (Bluetooth
  permissions, Qt activity) and the SDK's Java BLE bridge
  (`com.oymotion.sensor.ble.*`); `main.cpp` registers it at startup. The
  `ANDROID_ABI` preset value selects the matching prebuilt
  `lib/android/<abi>/libsensor.so` (arm64-v8a / armeabi-v7a / x86 / x86_64).
- Grant the Bluetooth permissions when Android asks on first run.

> **Note:** the iOS/Android presets were NOT compile-verified on the dev
> machine (no mobile Qt kits installed there). Only the Windows desktop
> build is tested there.

## Notes

- The app is console-subsystem on purpose: qInfo/qWarning output is useful
  while testing the SDK.
- Shutdown calls `sensor::SensorController::terminate()` in `closeEvent`.

## License

MIT
