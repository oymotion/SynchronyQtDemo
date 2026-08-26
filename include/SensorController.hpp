#ifndef SENSORCONTROLLER_H
#define SENSORCONTROLLER_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <utility>
#include <functional>
#include "SensorProfile.hpp"
#include "export.h"

namespace sensor {

// Per-device result of a multi-device operation: device mac -> {ok, errorMsg}.
using MultiResultCallback = std::function<void(const std::map<std::string, std::pair<bool, std::string>>&)>;

class SENSORSDK_API SensorControllerDelegate
{
public:
    virtual void onSensorControllerEnableChanged(bool enabled){};
    virtual void onSensorScanResult(std::vector<BLEDevice> bleDevices){};
    virtual ~SensorControllerDelegate(){};
};

// Summary of a raw BLE bin capture file.
struct BinFileInfo {
    std::string mac;
    std::string deviceName;
    double durationSec = 0;
    bool valid = false;
    // DeviceInfo from the first CONFIG record (aligned with the Python SDK's
    // getBinFileInfo, which returns the config record's device_info); zeroed
    // when the file has no decodable config.
    DeviceInfo deviceInfo = {};
};

class SENSORSDK_API SensorController
{
public:
    virtual ~SensorController() {};
    static std::shared_ptr<SensorController> getInstance();
    static void destory();
    virtual void setDelegate(std::weak_ptr<SensorControllerDelegate> delegate) = 0;

    virtual bool isEnable() = 0;
    virtual bool isScaning() = 0;
    virtual bool startScan(int periodInMS) = 0;
    virtual bool stopScan() = 0;

    virtual void setDebugEnabled(bool enabled) = 0;
    virtual void setDataLogEnabled(bool enabled) = 0;
    virtual void setLogPath(bool enabled, std::string path) = 0;

    virtual BLEDevice getDevice(std::string deviceMac) = 0;
    // Like getSensor, but creates and registers a profile when the MAC is unknown, so it
    // also works for devices not discovered by the scanner
    virtual std::shared_ptr<SensorProfile> requireSensor(BLEDevice device) = 0;
    virtual std::shared_ptr<SensorProfile> getSensor(std::string deviceMac) = 0;
    virtual std::vector<std::shared_ptr<SensorProfile>> getSensors() = 0;
    // Profiles whose device state is Connected or Ready
    virtual std::vector<std::shared_ptr<SensorProfile>> getConnectedSensors() = 0;
    virtual std::vector<BLEDevice> getConnectedDevices() = 0;

    // Raw BLE bin capture inspection and offline replay.
    virtual BinFileInfo getBinFileInfo(const std::string& path) = 0;
    // Replays a bin capture through the normal parse pipeline on a background
    // thread. Attach a delegate to the returned profile to receive the data;
    // the replay waits up to timeoutMs for one. Returns nullptr on failure.
    virtual std::shared_ptr<SensorProfile> replayBinFile(const std::string& path, const std::string& deviceMac, bool realtime, unsigned int timeoutMs) = 0;
    // All three return "OK" or "Error: ...".
    virtual std::string pauseBinReplay(const std::string& deviceMac) = 0;
    virtual std::string resumeBinReplay(const std::string& deviceMac) = 0;
    virtual std::string stopBinReplay(const std::string& deviceMac) = 0;
    // Offline full-speed parse of a bin capture into CSV. Blocks the caller;
    // returns the csv path on success, or a string starting with "Error:".
    virtual std::string parseBinToCsv(const std::string& binPath, const std::string& csvPath) = 0;

    // SDK version string.
    // Appended at the end to keep the existing vtable order stable.
    virtual std::string getVersion() = 0;

    // App-facing log: writes one application event (a user action, business
    // state, ...) into the SDK's controller log channel, so app and SDK
    // records share one timeline (aligned with the Python SDK 0.8.0
    // SensorController.log). level: "D"/"I"/"W"/"E" (case-insensitive,
    // anything else = "I"); "D" is gated by setDebugEnabled, the other
    // levels are always written. Never throws.
    virtual void log(std::string message, std::string level = "I") = 0;

    // iOS/Android: call when the app moves to the background. Writes a
    // "suspend" event marker into every open bin capture and flushes each
    // capture plus the SDK log queue to disk, so an app killed while
    // suspended loses as little as possible. Does NOT stop scanning,
    // streaming, or any connection. Never throws.
    // Appended at the end to keep the existing vtable order stable.
    virtual void onSuspend() = 0;

    // Synchronized multi-device stream start. Every sensor must be connected
    // (Ready) and initialized; entries that fail validation get their own
    // {false, reason} result and do not affect the others. Devices already
    // streaming are stopped first so every stream (re)starts together. After
    // the start, the first-packet delays of all devices are compared: when
    // the dispersion (max - min) exceeds maxDelayDispersionMs, or a device
    // produces no first packet within 2 s, the whole group is stopped and
    // restarted, up to maxAttempts rounds (minimum 1); a negative
    // maxDelayDispersionMs disables the dispersion check. On final failure
    // every device is left stopped. timeoutMs is the per-device command
    // timeout in ms (<= 0 selects the 25 s default). cb fires exactly once
    // on the controller callback thread with one {ok, errorMsg} entry per
    // device mac.
    // Appended at the end to keep the existing vtable order stable.
    virtual void multiStartDataNotification(const std::vector<std::shared_ptr<SensorProfile>>& sensors,
                                            int timeoutMs, int maxDelayDispersionMs, int maxAttempts,
                                            MultiResultCallback cb) = 0;
    // Synchronized multi-device stream stop. Devices that are not streaming
    // report success immediately. cb fires exactly once on the controller
    // callback thread with one {ok, errorMsg} entry per device mac.
    // Appended at the end to keep the existing vtable order stable.
    virtual void multiStopDataNotification(const std::vector<std::shared_ptr<SensorProfile>>& sensors,
                                           int timeoutMs, MultiResultCallback cb) = 0;

    // Synchronized multi-bin replay: every (path, deviceMac) capture replays
    // on one shared clock aligned by record timestamps - the earliest record
    // across the group is t=0 and original capture-time offsets are
    // preserved (a device that started streaming later delivers its first
    // data correspondingly later). Pausing/resuming any member
    // freezes/resumes the whole group; pauseBinReplay/resumeBinReplay/
    // stopBinReplay keep working per device mac. The returned vector is
    // input-order aligned; a nullptr entry marks a member that failed
    // validation (empty/duplicate mac, mac already replaying or streaming
    // live, unreadable file).
    // Appended at the end to keep the existing vtable order stable.
    virtual std::vector<std::shared_ptr<SensorProfile>> multiReplayBinFile(
        const std::vector<std::pair<std::string, std::string>>& pathMacList,
        bool realtime, unsigned int timeoutMs) = 0;

};

};


#endif // SENSORCONTROLLER_H
