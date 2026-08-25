#ifndef SENSORPROFILE_H
#define SENSORPROFILE_H

#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>
#include "SensorData.hpp"

namespace sensor {

struct  BLEDevice{
    enum  class State{
        Disconnected,
        Connecting,
        Connected,
        Ready,
        Disconnecting,
        Invalid
    };

    char name[32];
    char mac[18];
    int16_t rssi;
};

struct DeviceInfo {
    char deviceName[32];
    char modelName[32];
    char hardwareVersion[32];
    char firmwareVersion[32];
    uint16_t MTUSize;
    uint8_t isMTUFine;
    uint8_t EMGGain;
    uint8_t EEGGain;
    uint8_t ECGGain;
    uint8_t EMGChannelCount;
    uint8_t EEGChannelCount;
    uint8_t ECGChannelCount;
    uint8_t BRTHChannelCount;
    uint8_t AccChannelCount;
    uint8_t GyroChannelCount;
    uint8_t MagAngleChannelCount;
    uint16_t EMGSampleRate;
    uint16_t EEGSampleRate;
    uint16_t ECGSampleRate;
    uint16_t BRTHSampleRate;
    uint16_t AccSampleRate;
    uint16_t GyroSampleRate;
    uint16_t MagAngleSampleRate;
    uint8_t ImuChannelCount;
    uint16_t ImuSampleRate;
    uint8_t EulerChannelCount;
    uint16_t EulerSampleRate;
    uint8_t QuatChannelCount;
    uint16_t QuatSampleRate;
    uint8_t PpgChannelCount;
    uint16_t PpgSampleRate;
    uint8_t Spo2ChannelCount;
    uint16_t Spo2SampleRate;
    uint8_t ImpeChannelCount;
    uint16_t ImpeSampleRate;
    // Max sample rates from the device capability queries (get_emg/eeg/ecg
    // raw data cap); 0 = the device did not report one or the query is not
    // supported (aligned with Python SDK 0.6.1 DeviceInfo)
    uint16_t EmgMaxSampleRate;
    uint16_t EegMaxSampleRate;
    uint16_t EcgMaxSampleRate;
    // Link connection parameters (aligned with Python SDK 0.7.0 DeviceInfo).
    // The Python SDK reads them from its bumble backend; the C++ BLE backends
    // do not expose them, so they always report the unknown values.
    double ConnectionIntervalMs; // connection interval in ms; 0 = unknown
    int32_t PeripheralLatency = -1; // peripheral latency in events; -1 = unknown (0 is a legal value)
    int32_t SupervisionTimeoutMs; // supervision timeout in ms; 0 = unknown
};

class SensorProfile;

class SensorProfileDelegate {
public:
    virtual void onErrorCallback(std::shared_ptr<SensorProfile> profile, std::string errorMsg) {};
    virtual void onStateChange(std::shared_ptr<SensorProfile> profile, BLEDevice::State newState) {};
    // Delivers all batches accumulated since the last callback in one call;
    // each element is one minPackageSampleCount-sized batch of a single
    // stream. The views are borrowed (see SensorDataView): clone() any batch
    // that must outlive the callback.
    virtual void onSensorNotifyData(std::shared_ptr<SensorProfile> profile, const std::vector<SensorDataView>& rawDataList) {};
    // Called before the SDK restores the session after an auto reconnect.
    // hasLastSession is true when there was a previous (initialized) session.
    // Answer asynchronously through the passed callback, exactly once and from
    // any thread: answer(true) takes over the recovery yourself (SDK skips its
    // default init -> setParam replay -> startDataNotification flow),
    // answer(false) lets the SDK run the default recovery. If no answer
    // arrives within 10 s the SDK falls back to the default recovery.
    virtual void onAutoReconnect(std::shared_ptr<SensorProfile> profile, bool hasLastSession,
                                 std::function<void(bool handled)> answer) { answer(false); };
    // Battery level push from the profile's polling loop (started by init's
    // powerRefreshInterval); only valid readings are reported, held inside a
    // +/-4% stable band.
    virtual void onPowerChange(std::shared_ptr<SensorProfile> profile, int power) {};
    // DeviceInfo field change push (aligned with Python SDK 0.7.0
    // onDeviceInfoUpdate): fired after the cached DeviceInfo was updated in
    // place, e.g. when setParam("EEG_SAMPLE_RATE") rewrote the bound EEG/ECG
    // sample rates, or when a bin replay hits a CONFIG record that changes
    // sample rates / channel counts mid-capture (Python 0.7.1; the replay's
    // first CONFIG establishes the initial state and does not fire).
    // The passed info is the profile's up-to-date cached copy.
    virtual void onDeviceInfoUpdate(std::shared_ptr<SensorProfile> profile, const DeviceInfo& info) {};
    // Data stream on/off state change push: fired when the data stream
    // actually starts (successful startDataNotification, replay data start)
    // or stops (stopDataNotification, link loss, replay end), only on a real
    // state change.
    virtual void onDataTransferStateChange(std::shared_ptr<SensorProfile> profile, bool isTransferring) {};
    virtual ~SensorProfileDelegate() {};
};

class SensorProfile{
public:
    virtual BLEDevice getDevice() = 0;
    virtual BLEDevice::State getDeviceState() = 0;
    // True when the device is connected and Ready (init/setParam etc. can be
    // called) -- aligned with the Python SDK 0.7.2 isReady property.
    bool isReady() { return getDeviceState() == BLEDevice::State::Ready; }

    virtual void setDelegate(std::weak_ptr<SensorProfileDelegate> delegate) = 0;

    // Callback-async: both return immediately and deliver the final result
    // to cb exactly once, posted on the profile's command runloop (invoked
    // inline only when no runloop exists). cb may be empty (fire-and-forget).
    // connect: cb(true, "") when the link reaches Ready (or was already
    // Connected/Ready); cb(false, ...) with "Error: BLE controller is not
    // enabled" / "Error: Device is busy" (Connecting/Disconnecting) /
    // "Error: Connect failed" (link dropped before Ready) / "Error: Connect
    // timeout" (25 s deadline).
    // disconnect: stops the stream first when one is running (result
    // ignored), tears the link down, then cb(true, ""); also cb(true, "")
    // immediately when there is nothing to tear down.
    virtual void connect(std::function<void(bool result, std::string errorMsg)> cb) = 0;
    virtual void disconnect(std::function<void(bool result, std::string errorMsg)> cb) = 0;

    virtual bool hasStartDataNotification() = 0;
    virtual void startDataNotification(int timeoutInMS, std::function<void(bool result, std::string errorMsg)> cb) = 0;
    virtual void stopDataNotification(int timeoutInMS, std::function<void(bool result, std::string errorMsg)> cb) = 0;

    virtual bool hasInit() = 0;
    // powerRefreshInterval: battery polling period in ms; 0 disables polling
    virtual void init(int inPackageSampleCount, int timeoutInMS, std::function<void(bool result, std::string errorMsg)> cb, int powerRefreshInterval = 0) = 0;
    virtual void getBatteryLevel(int timeoutInMS, std::function<void(int result, std::string errorMsg)> cb) = 0;
    virtual void fetchDeviceInfo(int timeoutInMS, std::function<void(DeviceInfo result, std::string errorMsg)> cb) = 0;
    // Returns the cached DeviceInfo populated during init()/fetchDeviceInfo(); no GATT traffic
    virtual DeviceInfo getDeviceInfo() = 0;
    virtual void setParam(int timeoutInMS, std::string key, std::string value, std::function<void(std::string result, std::string errorMsg)> cb) = 0;
    // Queries a param through the profile's command loop. "FILTER" / "NTF"
    // return an aggregated "KEY|ON|KEY|OFF|..." string; possible error
    // strings: "Error: Please connect first" / "Error: Not initialized" /
    // "Error: Not supported". Answers come from the local cache, so the
    // callback fires promptly and timeoutInMS is accepted for signature
    // compatibility only.
    virtual void getParam(int timeoutInMS, std::string key, std::function<void(std::string result, std::string errorMsg)> cb) = 0;
    // Enables/disables session recovery after an auto reconnect (default true).
    // Link-level reconnect itself is not affected by this switch.
    virtual void setAutoReconnect(bool enabled) = 0;

    // App-facing log: writes one application event into this device's profile
    // log channel (falls back to the controller channel while no profile log
    // is open), so app and SDK records share one timeline (aligned with the
    // Python SDK 0.8.0 SensorProfile.log). level: "D"/"I"/"W"/"E"
    // (case-insensitive, anything else = "I"); "D" is gated by
    // SensorController::setDebugEnabled, the other levels are always written.
    // Never throws.
    virtual void log(std::string message, std::string level = "I") = 0;

    SensorProfile() {};
    virtual ~SensorProfile() {};
};
}

#endif // SENSORPROFILE_H
