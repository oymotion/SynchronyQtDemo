#ifndef SENSORCONTROLLER_H
#define SENSORCONTROLLER_H

#include <string>
#include <vector>
#include <memory>
#include "SensorProfile.hpp"
#include "export.h"

namespace sensor {



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

};

};


#endif // SENSORCONTROLLER_H
