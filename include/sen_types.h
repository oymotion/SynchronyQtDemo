#ifndef SEN_TYPES_H
#define SEN_TYPES_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
  #if defined(SENSORSDK_EXPORTS)
    #define SEN_API __declspec(dllexport)
  #else
    #define SEN_API __declspec(dllimport)
  #endif
#else
  #define SEN_API __attribute__ ((visibility ("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
  #define SEN_ALIGN8 alignas(8)
#else
  #define SEN_ALIGN8 _Alignas(8)
#endif

enum SenDeviceState {
    SEN_STATE_DISCONNECTED = 0,
    SEN_STATE_CONNECTING = 1,
    SEN_STATE_CONNECTED = 2,
    SEN_STATE_READY = 3,
    SEN_STATE_DISCONNECTING = 4,
    SEN_STATE_INVALID = 5
};

enum SenDataType {
    SEN_NTF_ACC = 1,
    SEN_NTF_GYRO = 2,
    SEN_NTF_MAG = 3,
    SEN_NTF_EULER = 4,
    SEN_NTF_QUATERNION = 5,
    SEN_NTF_GEST = 7,
    SEN_NTF_EMG = 8,
    SEN_NTF_ANGLE = 13,
    SEN_NTF_EEG = 16,
    SEN_NTF_ECG = 17,
    SEN_NTF_IMPEDANCE = 18,
    SEN_NTF_IMU = 19,
    SEN_NTF_ADS = 20,
    SEN_NTF_BRTH = 21,
    SEN_NTF_IMPEDANCE_EXT = 22,
    SEN_NTF_SPO2 = 23,
    SEN_NTF_PPG = 24
};

#define SEN_SAMPLE_SIZE 40
typedef struct {
    SEN_ALIGN8 double absTimeStampInSec;
    int32_t channelIndex;
    int32_t sampleIndex;
    int32_t rawData;
    float   data;
    float   impedance;
    float   saturation;
    uint8_t isLost;
    uint8_t _reserved[7];
} sen_sample_t;

typedef struct {
    char     deviceMac[18];
    int32_t  dataType;
    int32_t  lostPackageCount;
    float    sampleRate;
    int32_t  channelCount;
    SEN_ALIGN8 uint64_t channelMask;
    int32_t  sampleCount;
    uint32_t startTimeStamp;
    uint32_t delay;
    SEN_ALIGN8 double startTimeSec;
    char     deviceName[32];
} sen_data_info_t;

typedef struct {

    int32_t  startSampleIndex;
    uint32_t startTimeStamp;
    const sen_data_info_t* info;
    const sen_sample_t* samples;
    size_t   samplesBytes;
} sen_data_view_t;

typedef struct {
    char    name[32];
    char    mac[18];
    int16_t rssi;
} sen_ble_device_t;

typedef struct {
    uint32_t structSize;
    char     deviceName[32];
    char     modelName[32];
    char     hardwareVersion[32];
    char     firmwareVersion[32];
    uint16_t MTUSize;
    uint8_t  isMTUFine;
    uint8_t  EMGGain;
    uint8_t  EEGGain;
    uint8_t  ECGGain;
    uint8_t  EMGChannelCount;
    uint8_t  EEGChannelCount;
    uint8_t  ECGChannelCount;
    uint8_t  BRTHChannelCount;
    uint8_t  AccChannelCount;
    uint8_t  GyroChannelCount;
    uint8_t  AngleChannelCount;
    uint16_t EMGSampleRate;
    uint16_t EEGSampleRate;
    uint16_t ECGSampleRate;
    uint16_t BRTHSampleRate;
    uint16_t AccSampleRate;
    uint16_t GyroSampleRate;
    uint16_t AngleSampleRate;
    uint8_t  ImuChannelCount;
    uint16_t ImuSampleRate;
    uint8_t  EulerChannelCount;
    uint16_t EulerSampleRate;
    uint8_t  QuatChannelCount;
    uint16_t QuatSampleRate;
    uint8_t  PpgChannelCount;
    uint16_t PpgSampleRate;
    uint8_t  Spo2ChannelCount;
    uint16_t Spo2SampleRate;
    uint8_t  ImpeChannelCount;
    uint16_t ImpeSampleRate;
    uint16_t EmgMaxSampleRate;
    uint16_t EegMaxSampleRate;
    uint16_t EcgMaxSampleRate;
    double   ConnectionIntervalMs;
    int32_t  PeripheralLatency;
    int32_t  SupervisionTimeoutMs;
    char     backend[16];
    uint8_t  GestChannelCount;
    uint16_t GestSampleRate;
    uint8_t  MagChannelCount;
    uint16_t MagSampleRate;
} sen_device_info_t;

#define SEN_BIN_MAX_SENSOR_DATAS 14

typedef struct {
    SEN_ALIGN8 double   sampleRate;
    SEN_ALIGN8 double   k;
    SEN_ALIGN8 uint64_t channelMask;
    int32_t  dataType;
    uint8_t  typeIndex;
    uint8_t  channelCount;
    uint8_t  packageIndexLength;
    uint8_t  resolutionBits;
    uint8_t  resolutionSigned;
    int8_t   rawDataBias;
    uint16_t packageSampleCount;
    uint16_t minPackageSampleCount;
} sen_bin_sensor_data_config_t;

typedef struct {
    uint32_t structSize;
    char     mac[18];
    char     deviceName[32];
    double   durationSec;
    uint8_t  valid;
    uint8_t  _reserved[7];
    sen_device_info_t deviceInfo;
    uint32_t configVersion;
    int32_t  chipType;
    uint8_t  isUniversalStream;
    uint8_t  isNewEmg;
    uint8_t  isContainQat6;
    uint8_t  ppgModel;
    SEN_ALIGN8 int64_t featureMap;
    SEN_ALIGN8 int64_t notifyDataFlag;
    uint32_t sensorDataCount;
    sen_bin_sensor_data_config_t sensorDatas[SEN_BIN_MAX_SENSOR_DATAS];
    SEN_ALIGN8 uint64_t firstDataTsMs;
    SEN_ALIGN8 uint64_t lastDataTsMs;
} sen_bin_file_info_t;

#ifdef __cplusplus
}
#endif

#endif
