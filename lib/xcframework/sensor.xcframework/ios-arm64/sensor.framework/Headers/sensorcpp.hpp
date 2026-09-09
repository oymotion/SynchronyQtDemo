#ifndef SENSORCPP_HPP
#define SENSORCPP_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "sen_capi.h"

namespace sensor {

class SensorData;
class SensorProfile;
class SensorController;

struct BLEDevice {
    enum State : int {
        Disconnected = SEN_STATE_DISCONNECTED,
        Connecting = SEN_STATE_CONNECTING,
        Connected = SEN_STATE_CONNECTED,
        Ready = SEN_STATE_READY,
        Disconnecting = SEN_STATE_DISCONNECTING,
        Invalid = SEN_STATE_INVALID
    };

    std::string name;
    std::string mac;
    int rssi = 0;
};

struct DeviceInfo {
    std::string deviceName;
    std::string modelName;
    std::string hardwareVersion;
    std::string firmwareVersion;
    uint16_t MTUSize = 0;
    uint8_t isMTUFine = 0;
    uint8_t EMGGain = 0;
    uint8_t EEGGain = 0;
    uint8_t ECGGain = 0;
    uint8_t EMGChannelCount = 0;
    uint8_t EEGChannelCount = 0;
    uint8_t ECGChannelCount = 0;
    uint8_t BRTHChannelCount = 0;
    uint8_t AccChannelCount = 0;
    uint8_t GyroChannelCount = 0;
    uint8_t MagAngleChannelCount = 0;
    uint16_t EMGSampleRate = 0;
    uint16_t EEGSampleRate = 0;
    uint16_t ECGSampleRate = 0;
    uint16_t BRTHSampleRate = 0;
    uint16_t AccSampleRate = 0;
    uint16_t GyroSampleRate = 0;
    uint16_t MagAngleSampleRate = 0;
    uint8_t ImuChannelCount = 0;
    uint16_t ImuSampleRate = 0;
    uint8_t EulerChannelCount = 0;
    uint16_t EulerSampleRate = 0;
    uint8_t QuatChannelCount = 0;
    uint16_t QuatSampleRate = 0;
    uint8_t PpgChannelCount = 0;
    uint16_t PpgSampleRate = 0;
    uint8_t Spo2ChannelCount = 0;
    uint16_t Spo2SampleRate = 0;
    uint8_t ImpeChannelCount = 0;
    uint16_t ImpeSampleRate = 0;
    uint16_t EmgMaxSampleRate = 0;
    uint16_t EegMaxSampleRate = 0;
    uint16_t EcgMaxSampleRate = 0;
    double ConnectionIntervalMs = 0;
    int32_t PeripheralLatency = -1;
    int32_t SupervisionTimeoutMs = 0;
    std::string backend;
};

struct BinFileInfo {
    std::string mac;
    std::string deviceName;
    double durationSec = 0;
    bool valid = false;
    DeviceInfo deviceInfo;
};

namespace detail {

inline std::string fixedString(const char* p, size_t maxLen) {
    size_t n = 0;
    while (n < maxLen && p[n] != '\0') {
        ++n;
    }
    return std::string(p, n);
}

inline std::string strOrEmpty(const char* s) {
    return s != nullptr ? std::string(s) : std::string();
}

inline void reportCallbackException(const char* what) noexcept {
    if (what != nullptr) {
        std::fprintf(stderr, "sensorcpp callback exception: %s\n", what);
    } else {
        std::fprintf(stderr, "sensorcpp callback exception: unknown\n");
    }
}

inline BLEDevice bleDeviceFromNative(const sen_ble_device_t& d) {
    BLEDevice out;
    out.name = fixedString(d.name, sizeof(d.name));
    out.mac = fixedString(d.mac, sizeof(d.mac));
    out.rssi = d.rssi;
    return out;
}

inline DeviceInfo deviceInfoFromNative(const sen_device_info_t& i) {
    DeviceInfo out;
    out.deviceName = fixedString(i.deviceName, sizeof(i.deviceName));
    out.modelName = fixedString(i.modelName, sizeof(i.modelName));
    out.hardwareVersion = fixedString(i.hardwareVersion, sizeof(i.hardwareVersion));
    out.firmwareVersion = fixedString(i.firmwareVersion, sizeof(i.firmwareVersion));
    out.MTUSize = i.MTUSize;
    out.isMTUFine = i.isMTUFine;
    out.EMGGain = i.EMGGain;
    out.EEGGain = i.EEGGain;
    out.ECGGain = i.ECGGain;
    out.EMGChannelCount = i.EMGChannelCount;
    out.EEGChannelCount = i.EEGChannelCount;
    out.ECGChannelCount = i.ECGChannelCount;
    out.BRTHChannelCount = i.BRTHChannelCount;
    out.AccChannelCount = i.AccChannelCount;
    out.GyroChannelCount = i.GyroChannelCount;
    out.MagAngleChannelCount = i.MagAngleChannelCount;
    out.EMGSampleRate = i.EMGSampleRate;
    out.EEGSampleRate = i.EEGSampleRate;
    out.ECGSampleRate = i.ECGSampleRate;
    out.BRTHSampleRate = i.BRTHSampleRate;
    out.AccSampleRate = i.AccSampleRate;
    out.GyroSampleRate = i.GyroSampleRate;
    out.MagAngleSampleRate = i.MagAngleSampleRate;
    out.ImuChannelCount = i.ImuChannelCount;
    out.ImuSampleRate = i.ImuSampleRate;
    out.EulerChannelCount = i.EulerChannelCount;
    out.EulerSampleRate = i.EulerSampleRate;
    out.QuatChannelCount = i.QuatChannelCount;
    out.QuatSampleRate = i.QuatSampleRate;
    out.PpgChannelCount = i.PpgChannelCount;
    out.PpgSampleRate = i.PpgSampleRate;
    out.Spo2ChannelCount = i.Spo2ChannelCount;
    out.Spo2SampleRate = i.Spo2SampleRate;
    out.ImpeChannelCount = i.ImpeChannelCount;
    out.ImpeSampleRate = i.ImpeSampleRate;
    out.EmgMaxSampleRate = i.EmgMaxSampleRate;
    out.EegMaxSampleRate = i.EegMaxSampleRate;
    out.EcgMaxSampleRate = i.EcgMaxSampleRate;
    out.ConnectionIntervalMs = i.ConnectionIntervalMs;
    out.PeripheralLatency = i.PeripheralLatency;
    out.SupervisionTimeoutMs = i.SupervisionTimeoutMs;
    out.backend = fixedString(i.backend, sizeof(i.backend));
    return out;
}

inline BinFileInfo binFileInfoFromNative(const sen_bin_file_info_t& i) {
    BinFileInfo out;
    out.mac = fixedString(i.mac, sizeof(i.mac));
    out.deviceName = fixedString(i.deviceName, sizeof(i.deviceName));
    out.durationSec = i.durationSec;
    out.valid = i.valid != 0;
    out.deviceInfo = deviceInfoFromNative(i.deviceInfo);
    return out;
}

template <typename F>
struct CallContext {
    F fn;
};

template <typename F>
inline void* makeCallContext(F fn) {
    return new CallContext<F>{std::move(fn)};
}

}

using CompletionCallback = std::function<void(bool ok, const std::string& error)>;
using ParamCallback = std::function<void(const std::string& result, const std::string& error)>;
using BatteryCallback = std::function<void(int level, const std::string& error)>;
using DeviceInfoCallback = std::function<void(const DeviceInfo& info, const std::string& error)>;
using MultiResultCallback =
    std::function<void(const std::map<std::string, std::pair<bool, std::string>>&)>;

namespace detail {

inline void completionTrampoline(void* ctx, int result, const char* errorMsg) noexcept {
    std::unique_ptr<CallContext<CompletionCallback>> holder(
        static_cast<CallContext<CompletionCallback>*>(ctx));
    try {
        if (holder && holder->fn) {
            holder->fn(result != 0, strOrEmpty(errorMsg));
        }
    } catch (const std::exception& e) {
        reportCallbackException(e.what());
    } catch (...) {
        reportCallbackException(nullptr);
    }
}

inline void paramTrampoline(void* ctx, const char* result, const char* errorMsg) noexcept {
    std::unique_ptr<CallContext<ParamCallback>> holder(
        static_cast<CallContext<ParamCallback>*>(ctx));
    try {
        if (holder && holder->fn) {
            holder->fn(strOrEmpty(result), strOrEmpty(errorMsg));
        }
    } catch (const std::exception& e) {
        reportCallbackException(e.what());
    } catch (...) {
        reportCallbackException(nullptr);
    }
}

inline void batteryTrampoline(void* ctx, int result, const char* errorMsg) noexcept {
    std::unique_ptr<CallContext<BatteryCallback>> holder(
        static_cast<CallContext<BatteryCallback>*>(ctx));
    try {
        if (holder && holder->fn) {
            holder->fn(result, strOrEmpty(errorMsg));
        }
    } catch (const std::exception& e) {
        reportCallbackException(e.what());
    } catch (...) {
        reportCallbackException(nullptr);
    }
}

inline void infoTrampoline(void* ctx, const sen_device_info_t* info,
                           const char* errorMsg) noexcept {
    std::unique_ptr<CallContext<DeviceInfoCallback>> holder(
        static_cast<CallContext<DeviceInfoCallback>*>(ctx));
    try {
        if (holder && holder->fn) {
            DeviceInfo copy;
            if (info != nullptr) {
                copy = deviceInfoFromNative(*info);
            }
            holder->fn(copy, strOrEmpty(errorMsg));
        }
    } catch (const std::exception& e) {
        reportCallbackException(e.what());
    } catch (...) {
        reportCallbackException(nullptr);
    }
}

inline void multiResultTrampoline(void* ctx, const char* const* macs, const int* oks,
                                  const char* const* errors, size_t count) noexcept {
    std::unique_ptr<CallContext<MultiResultCallback>> holder(
        static_cast<CallContext<MultiResultCallback>*>(ctx));
    try {
        if (!holder || !holder->fn) {
            return;
        }
        std::map<std::string, std::pair<bool, std::string>> result;
        for (size_t i = 0; i < count; ++i) {
            const char* mac = macs != nullptr ? macs[i] : nullptr;
            if (mac == nullptr) {
                continue;
            }
            const bool ok = oks != nullptr && oks[i] != 0;
            const char* err = errors != nullptr ? errors[i] : nullptr;
            result[mac] = std::make_pair(ok, strOrEmpty(err));
        }
        holder->fn(result);
    } catch (const std::exception& e) {
        reportCallbackException(e.what());
    } catch (...) {
        reportCallbackException(nullptr);
    }
}

struct AnswerForwarder {
    sen_auto_reconnect_answer_cb answer = nullptr;
    void* answerCtx = nullptr;
    std::atomic<bool> answered{false};
};

}

class SensorDataView {
public:
    using Sample = sen_sample_t;

    enum Type : int {
        NTF_ACC = SEN_NTF_ACC,
        NTF_GYRO = SEN_NTF_GYRO,
        NTF_EULER_DATA = SEN_NTF_EULER,
        NTF_QUATERNION = SEN_NTF_QUATERNION,
        NTF_GEST = SEN_NTF_GEST,
        NTF_EMG_RAW_DATA = SEN_NTF_EMG,
        NTF_MAG_ANGLE = SEN_NTF_MAG_ANGLE,
        NTF_EEG = SEN_NTF_EEG,
        NTF_ECG = SEN_NTF_ECG,
        NTF_IMPEDANCE = SEN_NTF_IMPEDANCE,
        NTF_IMU = SEN_NTF_IMU,
        NTF_ADS = SEN_NTF_ADS,
        NTF_BRTH = SEN_NTF_BRTH,
        NTF_IMPEDANCE_EXT = SEN_NTF_IMPEDANCE_EXT,
        NTF_SPO2 = SEN_NTF_SPO2,
        NTF_PPG = SEN_NTF_PPG
    };

    SensorDataView() = default;
    explicit SensorDataView(const sen_data_view_t& view)
        : _startSampleIndex(view.startSampleIndex),
          _startTimeStamp(view.startTimeStamp),
          _info(view.info),
          _samples(view.samples),
          _samplesBytes(view.samplesBytes) {}

    SensorDataView(const SensorDataView&) = default;
    SensorDataView& operator=(const SensorDataView&) = default;
    ~SensorDataView() = default;

    bool isDataValid(int channel = 0, int index = 0) const {
        return slotAt(channel, index) != nullptr;
    }

    bool isChannelEnabled(int channel) const {
        if (channel < 0 || channel >= 64) {
            return false;
        }
        return ((getChannelMask() >> channel) & 1ULL) != 0;
    }

    float getData(int channel, int index) const {
        const sen_sample_t* s = slotAt(channel, index);
        return s != nullptr ? s->data : 0.0f;
    }

    int getTimeStampInMs(int channel, int index) const {
        const float rate = getSampleRate();
        return rate > 0
                   ? static_cast<int>(getSampleIndex(channel, index) * 1000.0 / rate)
                   : 0;
    }

    double getAbsTimeStampInSec(int channel, int index) const {
        const sen_sample_t* s = slotAt(channel, index);
        return s != nullptr ? s->absTimeStampInSec : 0.0;
    }

    int32_t getSampleIndex(int channel, int index) const {
        const sen_sample_t* s = slotAt(channel, index);
        return s != nullptr ? s->sampleIndex : 0;
    }

    int32_t getRawData(int channel, int index) const {
        const sen_sample_t* s = slotAt(channel, index);
        return s != nullptr ? s->rawData : 0;
    }

    float getImpedance(int channel, int index) const {
        const sen_sample_t* s = slotAt(channel, index);
        return s != nullptr ? s->impedance : 0.0f;
    }

    float getSaturation(int channel, int index) const {
        const sen_sample_t* s = slotAt(channel, index);
        return s != nullptr ? s->saturation : 0.0f;
    }

    bool isLost(int channel, int index) const {
        const sen_sample_t* s = slotAt(channel, index);
        return s != nullptr && s->isLost != 0;
    }

    const Sample* getChannelSample(int channel, int index) const {
        return slotAt(channel, index);
    }

    int32_t getStartSampleIndex() const { return _startSampleIndex; }
    std::string getDeviceMac() const {
        return _info != nullptr
                   ? detail::fixedString(_info->deviceMac, sizeof(_info->deviceMac))
                   : std::string();
    }
    std::string getDeviceName() const {
        return _info != nullptr
                   ? detail::fixedString(_info->deviceName, sizeof(_info->deviceName))
                   : std::string();
    }
    Type getDataType() const {
        return static_cast<Type>(_info != nullptr ? _info->dataType : 0);
    }
    int32_t getLostPackageCount() const {
        return _info != nullptr ? _info->lostPackageCount : 0;
    }
    float getSampleRate() const { return _info != nullptr ? _info->sampleRate : 0.0f; }
    int32_t getChannelCount() const { return _info != nullptr ? _info->channelCount : 0; }
    uint64_t getChannelMask() const { return _info != nullptr ? _info->channelMask : 0; }
    int32_t getSampleCount() const { return _info != nullptr ? _info->sampleCount : 0; }
    uint32_t getStartTimeStamp() const {
        return _info != nullptr ? _info->startTimeStamp : 0;
    }
    uint32_t getDelay() const { return _info != nullptr ? _info->delay : 0; }
    double getStartTimeSec() const {
        return _info != nullptr ? _info->startTimeSec : 0.0;
    }

    size_t getSamplesBytes() const { return _samplesBytes; }
    const Sample* getSamples() const { return _samples; }

    SensorData clone() const;

protected:
    int32_t _startSampleIndex = 0;
    uint32_t _startTimeStamp = 0;
    const sen_data_info_t* _info = nullptr;
    const sen_sample_t* _samples = nullptr;
    size_t _samplesBytes = 0;

private:
    const sen_sample_t* slotAt(int channel, int index) const {
        const int32_t channelCount = getChannelCount();
        const int32_t sampleCount = getSampleCount();
        if (channel < 0 || channel >= channelCount || index < 0 || index >= sampleCount) {
            return nullptr;
        }
        if (_info == nullptr || _startTimeStamp != _info->startTimeStamp) {
            return nullptr;
        }
        if (_samples == nullptr) {
            return nullptr;
        }
        const size_t slot =
            static_cast<size_t>(channel) * static_cast<size_t>(sampleCount) +
            static_cast<size_t>(index);
        if ((slot + 1) * sizeof(sen_sample_t) > _samplesBytes) {
            return nullptr;
        }
        const sen_sample_t* s = _samples + slot;
        if (s->sampleIndex != _startSampleIndex + index) {
            return nullptr;
        }
        return s;
    }
};

class SensorData : public SensorDataView {
public:
    explicit SensorData(const SensorDataView& view) : SensorDataView(view) {
        if (_info != nullptr) {
            _ownedInfo = *_info;
            _info = &_ownedInfo;
        }
        if (_samples != nullptr && _samplesBytes > 0) {
            const sen_sample_t* src = _samples;
            _ownedSamples.resize(_samplesBytes / sizeof(sen_sample_t));
            std::memcpy(_ownedSamples.data(), src, _samplesBytes);
            _samples = _ownedSamples.data();
        } else {
            _samples = nullptr;
            _samplesBytes = 0;
        }
    }

    SensorData(const SensorData&) = delete;
    SensorData& operator=(const SensorData&) = delete;
    SensorData(SensorData&& other) noexcept
        : SensorDataView(other),
          _ownedInfo(other._ownedInfo),
          _ownedSamples(std::move(other._ownedSamples)) {
        repoint();
    }
    SensorData& operator=(SensorData&& other) noexcept {
        if (this != &other) {
            SensorDataView::operator=(other);
            _ownedInfo = other._ownedInfo;
            _ownedSamples = std::move(other._ownedSamples);
            repoint();
        }
        return *this;
    }
    ~SensorData() = default;

private:
    void repoint() {
        if (_info != nullptr) {
            _info = &_ownedInfo;
        }
        if (_samples != nullptr) {
            _samples = _ownedSamples.empty() ? nullptr : _ownedSamples.data();
        }
    }

    sen_data_info_t _ownedInfo{};
    std::vector<sen_sample_t> _ownedSamples;
};

inline SensorData SensorDataView::clone() const {
    return SensorData(*this);
}

struct SensorControllerCallbacks {
    std::function<void(const std::vector<BLEDevice>&)> onScanResult;
    std::function<void(bool enabled)> onEnableChanged;
};

struct SensorProfileCallbacks {
    std::function<void(SensorProfile*, const std::vector<SensorDataView>&)> onData;
    std::function<void(SensorProfile*, int newState)> onStateChange;
    std::function<void(SensorProfile*, const std::string& error)> onError;
    std::function<void(SensorProfile*, int power)> onPowerChange;
    std::function<void(SensorProfile*, bool hasLastSession, std::function<void(bool)> answer)>
        onAutoReconnect;
    std::function<void(SensorProfile*, const DeviceInfo&)> onDeviceInfoUpdate;
    std::function<void(SensorProfile*, bool isTransferring)> onDataTransferStateChange;
};

class SensorProfile {
public:
    SensorProfile(const SensorProfile&) = delete;
    SensorProfile& operator=(const SensorProfile&) = delete;
    ~SensorProfile() = default;

    void setCallbacks(const SensorProfileCallbacks& cbs) { _cbs = cbs; }

    BLEDevice getDevice() const {
        sen_ble_device_t d{};
        sen_profile_get_device(_handle, &d);
        return detail::bleDeviceFromNative(d);
    }
    BLEDevice::State getState() const {
        return static_cast<BLEDevice::State>(sen_profile_get_state(_handle));
    }
    bool isReady() const { return getState() == BLEDevice::Ready; }

    void connect(CompletionCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_profile_connect(_handle, callCtx ? &detail::completionTrampoline : nullptr, callCtx);
    }
    void disconnect(CompletionCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_profile_disconnect(_handle, callCtx ? &detail::completionTrampoline : nullptr, callCtx);
    }

    bool hasInit() const { return sen_profile_has_init(_handle) != 0; }
    bool hasStartDataNotification() const {
        return sen_profile_has_start_data_notification(_handle) != 0;
    }

    void init(int packageSampleCount, int timeoutMs, int powerRefreshIntervalMs,
              CompletionCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_profile_init(_handle, packageSampleCount, timeoutMs, powerRefreshIntervalMs,
                         callCtx ? &detail::completionTrampoline : nullptr, callCtx);
    }
    void startData(int timeoutMs, CompletionCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_profile_start_data(_handle, timeoutMs,
                               callCtx ? &detail::completionTrampoline : nullptr, callCtx);
    }
    void stopData(int timeoutMs, CompletionCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_profile_stop_data(_handle, timeoutMs,
                              callCtx ? &detail::completionTrampoline : nullptr, callCtx);
    }

    void getBatteryLevel(int timeoutMs, BatteryCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_profile_get_battery_level(_handle, timeoutMs,
                                      callCtx ? &detail::batteryTrampoline : nullptr, callCtx);
    }
    void fetchDeviceInfo(int timeoutMs, DeviceInfoCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_profile_fetch_device_info(_handle, timeoutMs,
                                      callCtx ? &detail::infoTrampoline : nullptr, callCtx);
    }
    DeviceInfo getDeviceInfo() const {
        sen_device_info_t native{};
        native.structSize = sizeof(native);
        sen_profile_get_device_info(_handle, &native);
        return detail::deviceInfoFromNative(native);
    }

    void setParam(int timeoutMs, const std::string& key, const std::string& value,
                  ParamCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_profile_set_param(_handle, timeoutMs, key.c_str(), value.c_str(),
                              callCtx ? &detail::paramTrampoline : nullptr, callCtx);
    }
    void getParam(int timeoutMs, const std::string& key, ParamCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_profile_get_param(_handle, timeoutMs, key.c_str(),
                              callCtx ? &detail::paramTrampoline : nullptr, callCtx);
    }

    void setAutoReconnect(bool enabled) {
        sen_profile_set_auto_reconnect(_handle, enabled ? 1 : 0);
    }

    void log(const std::string& message, const std::string& level = "I") const {
        try {
            sen_profile_log(_handle, message.c_str(), level.c_str());
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

private:
    friend class SensorController;
    explicit SensorProfile(sen_profile_t* handle) : _handle(handle) {
        sen_profile_cbs_t cbs{};
        cbs.structSize = sizeof(cbs);
        cbs.onData = &SensorProfile::onDataCb;
        cbs.onStateChange = &SensorProfile::onStateChangeCb;
        cbs.onError = &SensorProfile::onErrorCb;
        cbs.onPowerChange = &SensorProfile::onPowerChangeCb;
        cbs.onAutoReconnect = &SensorProfile::onAutoReconnectCb;
        cbs.onDeviceInfoUpdate = &SensorProfile::onDeviceInfoUpdateCb;
        cbs.onDataTransferStateChange = &SensorProfile::onDataTransferStateChangeCb;
        sen_profile_set_callbacks(_handle, &cbs, this);
    }

    sen_profile_t* handle() const { return _handle; }

    static void onDataCb(void* ctx, sen_profile_t* ,
                         const sen_data_view_t* views, size_t viewCount) {
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self == nullptr || !self->_cbs.onData || views == nullptr) {
                return;
            }
            std::vector<SensorDataView> batch;
            batch.reserve(viewCount);
            for (size_t i = 0; i < viewCount; ++i) {
                batch.emplace_back(views[i]);
            }
            self->_cbs.onData(self, batch);
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    static void onStateChangeCb(void* ctx, sen_profile_t* , int newState) {
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self != nullptr && self->_cbs.onStateChange) {
                self->_cbs.onStateChange(self, newState);
            }
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    static void onErrorCb(void* ctx, sen_profile_t* , const char* errorMsg) {
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self != nullptr && self->_cbs.onError) {
                self->_cbs.onError(self, detail::strOrEmpty(errorMsg));
            }
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    static void onPowerChangeCb(void* ctx, sen_profile_t* , int power) {
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self != nullptr && self->_cbs.onPowerChange) {
                self->_cbs.onPowerChange(self, power);
            }
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    static void onAutoReconnectCb(void* ctx, sen_profile_t* ,
                                  int hasLastSession,
                                  sen_auto_reconnect_answer_cb answer,
                                  void* answerCtx) {
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self == nullptr || !self->_cbs.onAutoReconnect) {
                return;
            }
            auto forwarder = std::make_shared<detail::AnswerForwarder>();
            forwarder->answer = answer;
            forwarder->answerCtx = answerCtx;
            std::function<void(bool)> answerFn = [forwarder](bool handled) {
                if (forwarder->answer != nullptr &&
                    !forwarder->answered.exchange(true, std::memory_order_acq_rel)) {
                    forwarder->answer(forwarder->answerCtx, handled ? 1 : 0);
                }
            };
            self->_cbs.onAutoReconnect(self, hasLastSession != 0, std::move(answerFn));
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    static void onDeviceInfoUpdateCb(void* ctx, sen_profile_t* ,
                                     const sen_device_info_t* info) {
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self != nullptr && self->_cbs.onDeviceInfoUpdate && info != nullptr) {
                self->_cbs.onDeviceInfoUpdate(self, detail::deviceInfoFromNative(*info));
            }
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    static void onDataTransferStateChangeCb(void* ctx, sen_profile_t* ,
                                            int isTransferring) {
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self != nullptr && self->_cbs.onDataTransferStateChange) {
                self->_cbs.onDataTransferStateChange(self, isTransferring != 0);
            }
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    sen_profile_t* _handle;
    SensorProfileCallbacks _cbs;
};

class SensorController {
public:
    SensorController() {
        sen_capi_version_check();
        _handle = sen_controller_create();
        if (_handle == nullptr) {
            throw std::runtime_error("sen_controller_create failed");
        }
        _cbCtx.reset(new CallbackContext());
        sen_controller_cbs_t cbs{};
        cbs.structSize = sizeof(cbs);
        cbs.onScanResult = &SensorController::onScanResultCb;
        cbs.onEnableChanged = &SensorController::onEnableChangedCb;
        sen_controller_set_callbacks(_handle, &cbs, _cbCtx.get());
    }
    ~SensorController() { reset(); }
    SensorController(const SensorController&) = delete;
    SensorController& operator=(const SensorController&) = delete;
    SensorController(SensorController&& other) noexcept {
        std::lock_guard<std::mutex> lock(other._profilesMutex);
        _handle = other._handle;
        other._handle = nullptr;
        _cbCtx = std::move(other._cbCtx);
        _profiles = std::move(other._profiles);
    }
    SensorController& operator=(SensorController&& other) noexcept {
        if (this != &other) {
            reset();
            std::lock_guard<std::mutex> lock(other._profilesMutex);
            _handle = other._handle;
            other._handle = nullptr;
            _cbCtx = std::move(other._cbCtx);
            _profiles = std::move(other._profiles);
        }
        return *this;
    }

    void setCallbacks(const SensorControllerCallbacks& cbs) {
        if (_cbCtx) {
            _cbCtx->cbs = cbs;
        }
    }

    bool isEnable() const { return sen_controller_is_enable(_handle) != 0; }
    bool isScanning() const { return sen_controller_is_scanning(_handle) != 0; }
    bool startScan(int periodInMs) {
        return sen_controller_start_scan(_handle, periodInMs) != 0;
    }
    bool stopScan() { return sen_controller_stop_scan(_handle) != 0; }

    SensorProfile* requireSensor(const std::string& mac) {
        return wrapProfile(sen_controller_require_sensor(_handle, mac.c_str()));
    }
    SensorProfile* getSensor(const std::string& mac) {
        return wrapProfile(sen_controller_get_sensor(_handle, mac.c_str()));
    }
    std::vector<SensorProfile*> getSensors() {
        return collectProfiles(&sen_controller_get_sensors);
    }
    std::vector<SensorProfile*> getConnectedSensors() {
        return collectProfiles(&sen_controller_get_connected_sensors);
    }

    BinFileInfo getBinFileInfo(const std::string& path) {
        sen_bin_file_info_t native{};
        native.structSize = sizeof(native);
        BinFileInfo out;
        if (sen_controller_get_bin_file_info(_handle, path.c_str(), &native) != 0) {
            out = detail::binFileInfoFromNative(native);
        }
        return out;
    }
    SensorProfile* replayBinFile(const std::string& path, const std::string& deviceMac,
                                 bool realtime = true, uint32_t timeoutMs = 30000) {
        return wrapProfile(sen_controller_replay_bin_file(
            _handle, path.c_str(), deviceMac.c_str(), realtime ? 1 : 0, timeoutMs));
    }
    std::vector<SensorProfile*> multiReplayBinFile(
        const std::vector<std::pair<std::string, std::string>>& pathMacList,
        bool realtime = true, uint32_t timeoutMs = 30000) {
        const size_t count = pathMacList.size();
        std::vector<const char*> paths(count);
        std::vector<const char*> macs(count);
        for (size_t i = 0; i < count; ++i) {
            paths[i] = pathMacList[i].first.c_str();
            macs[i] = pathMacList[i].second.c_str();
        }
        std::vector<sen_profile_t*> outHandles(count, nullptr);
        sen_controller_multi_replay_bin_file(
            _handle, count > 0 ? paths.data() : nullptr,
            count > 0 ? macs.data() : nullptr, count, realtime ? 1 : 0, timeoutMs,
            count > 0 ? outHandles.data() : nullptr);
        std::vector<SensorProfile*> out(count, nullptr);
        for (size_t i = 0; i < count; ++i) {
            out[i] = wrapProfile(outHandles[i]);
        }
        return out;
    }
    std::string pauseBinReplay(const std::string& deviceMac) {
        const std::string mac = deviceMac;
        return readOutString(
            [&](char* buf, size_t len) {
                sen_controller_pause_bin_replay(_handle, mac.c_str(), buf, len);
            },
            1024);
    }
    std::string resumeBinReplay(const std::string& deviceMac) {
        const std::string mac = deviceMac;
        return readOutString(
            [&](char* buf, size_t len) {
                sen_controller_resume_bin_replay(_handle, mac.c_str(), buf, len);
            },
            1024);
    }
    std::string stopBinReplay(const std::string& deviceMac) {
        const std::string mac = deviceMac;
        return readOutString(
            [&](char* buf, size_t len) {
                sen_controller_stop_bin_replay(_handle, mac.c_str(), buf, len);
            },
            1024);
    }
    std::string parseBinToCsv(const std::string& binPath, const std::string& csvPath) {
        return readOutString(
            [&](char* buf, size_t len) {
                sen_controller_parse_bin_to_csv(_handle, binPath.c_str(), csvPath.c_str(),
                                                buf, len);
            },
            4096);
    }

    std::string getVersion() {
        return readOutString(
            [&](char* buf, size_t len) {
                sen_controller_get_version(_handle, buf, len);
            },
            256);
    }

    std::string getParam(const std::string& key) {
        return readOutString(
            [&](char* buf, size_t len) {
                sen_controller_get_param(_handle, key.c_str(), buf, len);
            },
            1024);
    }
    std::string setParam(const std::string& key, const std::string& value) {
        return readOutString(
            [&](char* buf, size_t len) {
                sen_controller_set_param(_handle, key.c_str(), value.c_str(), buf, len);
            },
            1024);
    }

    void log(const std::string& message, const std::string& level = "I") {
        try {
            sen_controller_log(_handle, message.c_str(), level.c_str());
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    void onSuspend() {
        try {
            sen_controller_on_suspend(_handle);
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    void multiStartData(const std::vector<SensorProfile*>& profiles, int timeoutMs,
                        int maxDelayDispersionMs, int maxAttempts,
                        MultiResultCallback cb = nullptr) {
        std::vector<sen_profile_t*> handles;
        handles.reserve(profiles.size());
        for (SensorProfile* p : profiles) {
            if (p != nullptr) {
                handles.push_back(p->handle());
            }
        }
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_controller_multi_start_data(
            _handle, handles.empty() ? nullptr : handles.data(), handles.size(),
            timeoutMs, maxDelayDispersionMs, maxAttempts,
            callCtx ? &detail::multiResultTrampoline : nullptr, callCtx);
    }
    void multiStopData(const std::vector<SensorProfile*>& profiles, int timeoutMs,
                       MultiResultCallback cb = nullptr) {
        std::vector<sen_profile_t*> handles;
        handles.reserve(profiles.size());
        for (SensorProfile* p : profiles) {
            if (p != nullptr) {
                handles.push_back(p->handle());
            }
        }
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_controller_multi_stop_data(
            _handle, handles.empty() ? nullptr : handles.data(), handles.size(),
            timeoutMs, callCtx ? &detail::multiResultTrampoline : nullptr, callCtx);
    }

    static void terminate() { sen_terminate(); }
    static uint32_t capiVersion() { return sen_capi_version(); }
    static std::pair<bool, std::string> checkSetupDongle() {
        std::string buf(256, '\0');
        const int ok = sen_check_setup_dongle(&buf[0], static_cast<int32_t>(buf.size()));
        const size_t n = buf.find('\0');
        if (n != std::string::npos) {
            buf.resize(n);
        }
        return std::make_pair(ok != 0, buf);
    }

private:
    struct CallbackContext {
        SensorControllerCallbacks cbs;
    };

    SensorProfile* wrapProfile(sen_profile_t* handle) {
        if (handle == nullptr) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(_profilesMutex);
        auto it = _profiles.find(handle);
        if (it != _profiles.end()) {
            return it->second.get();
        }
        std::unique_ptr<SensorProfile> profile(new SensorProfile(handle));
        SensorProfile* raw = profile.get();
        _profiles.emplace(handle, std::move(profile));
        return raw;
    }

    std::vector<SensorProfile*> collectProfiles(
        size_t (*fn)(sen_controller_t*, sen_profile_t**, size_t)) {
        std::vector<SensorProfile*> out;
        const size_t count = fn(_handle, nullptr, 0);
        if (count == 0) {
            return out;
        }
        std::vector<sen_profile_t*> handles(count);
        const size_t written = fn(_handle, handles.data(), count);
        out.reserve(written);
        for (size_t i = 0; i < written; ++i) {
            SensorProfile* p = wrapProfile(handles[i]);
            if (p != nullptr) {
                out.push_back(p);
            }
        }
        return out;
    }

    template <typename F>
    std::string readOutString(F&& fill, size_t capacity) {
        std::string buf(capacity, '\0');
        fill(&buf[0], buf.size());
        const size_t n = buf.find('\0');
        if (n != std::string::npos) {
            buf.resize(n);
        }
        return buf;
    }

    void reset() {
        if (_handle == nullptr) {
            return;
        }
        sen_controller_destroy(_handle);
        _handle = nullptr;
        {
            std::lock_guard<std::mutex> lock(_profilesMutex);
            _profiles.clear();
        }
    }

    static void onScanResultCb(void* ctx, const sen_ble_device_t* devices, size_t count) {
        try {
            CallbackContext* cbCtx = static_cast<CallbackContext*>(ctx);
            if (cbCtx == nullptr || !cbCtx->cbs.onScanResult || devices == nullptr) {
                return;
            }
            std::vector<BLEDevice> list;
            list.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                list.push_back(detail::bleDeviceFromNative(devices[i]));
            }
            cbCtx->cbs.onScanResult(list);
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    static void onEnableChangedCb(void* ctx, int enabled) {
        try {
            CallbackContext* cbCtx = static_cast<CallbackContext*>(ctx);
            if (cbCtx != nullptr && cbCtx->cbs.onEnableChanged) {
                cbCtx->cbs.onEnableChanged(enabled != 0);
            }
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    sen_controller_t* _handle = nullptr;
    std::unique_ptr<CallbackContext> _cbCtx;
    std::map<sen_profile_t*, std::unique_ptr<SensorProfile>> _profiles;
    std::mutex _profilesMutex;
};

}

#endif
