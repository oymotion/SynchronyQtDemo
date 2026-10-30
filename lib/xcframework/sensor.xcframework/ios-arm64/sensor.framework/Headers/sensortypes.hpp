#ifndef SENSORTYPES_HPP
#define SENSORTYPES_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "sen_types.h"

namespace sensor {

class SensorData;

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
    uint8_t AngleChannelCount = 0;
    uint16_t EMGSampleRate = 0;
    uint16_t EEGSampleRate = 0;
    uint16_t ECGSampleRate = 0;
    uint16_t BRTHSampleRate = 0;
    uint16_t AccSampleRate = 0;
    uint16_t GyroSampleRate = 0;
    uint16_t AngleSampleRate = 0;
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
    uint8_t GestChannelCount = 0;
    uint16_t GestSampleRate = 0;
    uint8_t MagChannelCount = 0;
    uint16_t MagSampleRate = 0;
};

struct BinSensorDataConfig {
    double sampleRate = 0;
    double k = 0;
    uint64_t channelMask = 0;
    int32_t dataType = 0;
    uint8_t typeIndex = 0;
    uint8_t channelCount = 0;
    uint8_t packageIndexLength = 0;
    uint8_t resolutionBits = 0;
    uint8_t resolutionSigned = 0;
    int8_t rawDataBias = 0;
    uint16_t packageSampleCount = 0;
    uint16_t minPackageSampleCount = 0;
};

struct BinFileInfo {
    std::string mac;
    std::string deviceName;
    double durationSec = 0;
    bool valid = false;
    DeviceInfo deviceInfo;
    uint32_t configVersion = 0;
    int32_t chipType = 0;
    bool isUniversalStream = false;
    bool isNewEmg = false;
    bool isContainQat6 = false;
    uint8_t ppgModel = 0;
    int64_t featureMap = 0;
    int64_t notifyDataFlag = 0;
    std::vector<BinSensorDataConfig> sensorDatas;
    uint64_t firstDataTsMs = 0;
    uint64_t lastDataTsMs = 0;
};

namespace detail {

inline std::string fixedString(const char* p, size_t maxLen) {
    size_t n = 0;
    while (n < maxLen && p[n] != '\0') {
        ++n;
    }
    return std::string(p, n);
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
    out.AngleChannelCount = i.AngleChannelCount;
    out.EMGSampleRate = i.EMGSampleRate;
    out.EEGSampleRate = i.EEGSampleRate;
    out.ECGSampleRate = i.ECGSampleRate;
    out.BRTHSampleRate = i.BRTHSampleRate;
    out.AccSampleRate = i.AccSampleRate;
    out.GyroSampleRate = i.GyroSampleRate;
    out.AngleSampleRate = i.AngleSampleRate;
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
    out.GestChannelCount = i.GestChannelCount;
    out.GestSampleRate = i.GestSampleRate;
    out.MagChannelCount = i.MagChannelCount;
    out.MagSampleRate = i.MagSampleRate;
    return out;
}

inline BinSensorDataConfig binSensorDataConfigFromNative(const sen_bin_sensor_data_config_t& c) {
    BinSensorDataConfig out;
    out.sampleRate = c.sampleRate;
    out.k = c.k;
    out.channelMask = c.channelMask;
    out.dataType = c.dataType;
    out.typeIndex = c.typeIndex;
    out.channelCount = c.channelCount;
    out.packageIndexLength = c.packageIndexLength;
    out.resolutionBits = c.resolutionBits;
    out.resolutionSigned = c.resolutionSigned;
    out.rawDataBias = c.rawDataBias;
    out.packageSampleCount = c.packageSampleCount;
    out.minPackageSampleCount = c.minPackageSampleCount;
    return out;
}

inline BinFileInfo binFileInfoFromNative(const sen_bin_file_info_t& i) {
    BinFileInfo out;
    out.mac = fixedString(i.mac, sizeof(i.mac));
    out.deviceName = fixedString(i.deviceName, sizeof(i.deviceName));
    out.durationSec = i.durationSec;
    out.valid = i.valid != 0;
    out.deviceInfo = deviceInfoFromNative(i.deviceInfo);
    out.configVersion = i.configVersion;
    out.chipType = i.chipType;
    out.isUniversalStream = i.isUniversalStream != 0;
    out.isNewEmg = i.isNewEmg != 0;
    out.isContainQat6 = i.isContainQat6 != 0;
    out.ppgModel = i.ppgModel;
    out.featureMap = i.featureMap;
    out.notifyDataFlag = i.notifyDataFlag;
    uint32_t count = i.sensorDataCount;
    if (count > SEN_BIN_MAX_SENSOR_DATAS) {
        count = SEN_BIN_MAX_SENSOR_DATAS;
    }
    out.sensorDatas.reserve(count);
    for (uint32_t s = 0; s < count; ++s) {
        out.sensorDatas.push_back(binSensorDataConfigFromNative(i.sensorDatas[s]));
    }
    out.firstDataTsMs = i.firstDataTsMs;
    out.lastDataTsMs = i.lastDataTsMs;
    return out;
}

}

class SensorDataView {
public:
    using Sample = sen_sample_t;

    enum Type : int {
        NTF_ACC = SEN_NTF_ACC,
        NTF_GYRO = SEN_NTF_GYRO,
        NTF_MAG = SEN_NTF_MAG,
        NTF_EULER_DATA = SEN_NTF_EULER,
        NTF_QUATERNION = SEN_NTF_QUATERNION,
        NTF_GEST = SEN_NTF_GEST,
        NTF_EMG_RAW_DATA = SEN_NTF_EMG,
        NTF_ANGLE = SEN_NTF_ANGLE,
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

    sen_data_view_t nativeView() const {
        sen_data_view_t view{};
        view.startSampleIndex = _startSampleIndex;
        view.startTimeStamp = _startTimeStamp;
        view.info = _info;
        view.samples = _samples;
        view.samplesBytes = _samplesBytes;
        return view;
    }

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

}

#endif
