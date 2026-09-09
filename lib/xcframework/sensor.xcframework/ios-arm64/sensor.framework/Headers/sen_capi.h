#ifndef SEN_CAPI_H
#define SEN_CAPI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

#define SEN_CAPI_VERSION 14

typedef struct sen_controller sen_controller_t;
typedef struct sen_profile sen_profile_t;

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
    SEN_NTF_EULER = 4,
    SEN_NTF_QUATERNION = 5,
    SEN_NTF_GEST = 7,
    SEN_NTF_EMG = 8,
    SEN_NTF_MAG_ANGLE = 13,
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
    uint8_t  MagAngleChannelCount;
    uint16_t EMGSampleRate;
    uint16_t EEGSampleRate;
    uint16_t ECGSampleRate;
    uint16_t BRTHSampleRate;
    uint16_t AccSampleRate;
    uint16_t GyroSampleRate;
    uint16_t MagAngleSampleRate;
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
} sen_device_info_t;

typedef struct {
    uint32_t structSize;
    char     mac[18];
    char     deviceName[32];
    double   durationSec;
    uint8_t  valid;
    uint8_t  _reserved[7];
    sen_device_info_t deviceInfo;
} sen_bin_file_info_t;

typedef void (*sen_scan_result_cb)(void* ctx, const sen_ble_device_t* devices, size_t count);
typedef void (*sen_enable_changed_cb)(void* ctx, int enabled);

typedef void (*sen_data_cb)(void* ctx, sen_profile_t* profile,
                            const sen_data_view_t* views, size_t viewCount);
typedef void (*sen_state_cb)(void* ctx, sen_profile_t* profile, int newState);
typedef void (*sen_error_cb)(void* ctx, sen_profile_t* profile, const char* errorMsg);
typedef void (*sen_power_cb)(void* ctx, sen_profile_t* profile, int power);
typedef void (*sen_auto_reconnect_answer_cb)(void* answerCtx, int handled);
typedef void (*sen_auto_reconnect_cb)(void* ctx, sen_profile_t* profile, int hasLastSession,
                                      sen_auto_reconnect_answer_cb answer, void* answerCtx);
typedef void (*sen_device_info_update_cb)(void* ctx, sen_profile_t* profile,
                                          const sen_device_info_t* info);
typedef void (*sen_data_transfer_state_cb)(void* ctx, sen_profile_t* profile,
                                           int isTransferring);

typedef struct {
    uint32_t structSize;
    sen_data_cb           onData;
    sen_state_cb          onStateChange;
    sen_error_cb          onError;
    sen_power_cb          onPowerChange;
    sen_auto_reconnect_cb onAutoReconnect;
    sen_device_info_update_cb onDeviceInfoUpdate;
    sen_data_transfer_state_cb onDataTransferStateChange;
} sen_profile_cbs_t;

typedef struct {
    uint32_t structSize;
    sen_scan_result_cb   onScanResult;
    sen_enable_changed_cb onEnableChanged;
} sen_controller_cbs_t;

typedef void (*sen_completion_cb)(void* ctx, int result, const char* errorMsg);
typedef void (*sen_param_cb)(void* ctx, const char* result, const char* errorMsg);
typedef void (*sen_battery_cb)(void* ctx, int result, const char* errorMsg);
typedef void (*sen_info_cb)(void* ctx, const sen_device_info_t* info, const char* errorMsg);

typedef void (*sen_multi_result_cb)(void* ctx, const char* const* macs,
                                    const int* oks, const char* const* errors,
                                    size_t count);

SEN_API void sen_terminate(void);

SEN_API uint32_t sen_capi_version(void);

static inline int sen_capi_version_check(void)
{
    uint32_t libVersion = sen_capi_version();
    if (libVersion != (uint32_t)SEN_CAPI_VERSION) {
        fprintf(stderr,
                "[sen_capi] WARNING: header SEN_CAPI_VERSION %d does not match the loaded library (version %u); rebuild the library or update the consumer\n",
                (int)SEN_CAPI_VERSION, (unsigned)libVersion);
        return 0;
    }
    return 1;
}

SEN_API sen_controller_t* sen_controller_create(void);
SEN_API void sen_controller_destroy(sen_controller_t* ctrl);
SEN_API void sen_controller_set_callbacks(sen_controller_t* ctrl,
                                                const sen_controller_cbs_t* cbs, void* ctx);

SEN_API int sen_controller_is_enable(sen_controller_t* ctrl);
SEN_API int sen_controller_is_scanning(sen_controller_t* ctrl);
SEN_API int sen_controller_start_scan(sen_controller_t* ctrl, int periodInMS);
SEN_API int sen_controller_stop_scan(sen_controller_t* ctrl);

SEN_API sen_profile_t* sen_controller_require_sensor(sen_controller_t* ctrl, const char* mac);
SEN_API sen_profile_t* sen_controller_get_sensor(sen_controller_t* ctrl, const char* mac);
SEN_API size_t sen_controller_get_sensors(sen_controller_t* ctrl, sen_profile_t** out, size_t capacity);
SEN_API size_t sen_controller_get_connected_sensors(sen_controller_t* ctrl, sen_profile_t** out, size_t capacity);

SEN_API int sen_controller_get_bin_file_info(sen_controller_t* ctrl, const char* path,
                                                   sen_bin_file_info_t* out);
SEN_API sen_profile_t* sen_controller_replay_bin_file(sen_controller_t* ctrl, const char* path,
                                                            const char* deviceMac, int realtime,
                                                            uint32_t timeoutMs);
SEN_API size_t sen_controller_multi_replay_bin_file(sen_controller_t* ctrl,
                                                    const char* const* paths,
                                                    const char* const* macs,
                                                    size_t count, int realtime,
                                                    uint32_t timeoutMs,
                                                    sen_profile_t** outProfiles);
SEN_API void sen_controller_pause_bin_replay(sen_controller_t* ctrl, const char* deviceMac,
                                                   char* buf, size_t len);
SEN_API void sen_controller_resume_bin_replay(sen_controller_t* ctrl, const char* deviceMac,
                                                    char* buf, size_t len);
SEN_API void sen_controller_stop_bin_replay(sen_controller_t* ctrl, const char* deviceMac,
                                                  char* buf, size_t len);
SEN_API void sen_controller_parse_bin_to_csv(sen_controller_t* ctrl, const char* binPath,
                                                   const char* csvPath, char* buf, size_t len);

SEN_API void sen_controller_get_version(sen_controller_t* ctrl, char* buf, size_t len);

SEN_API void sen_controller_log(sen_controller_t* ctrl, const char* message, const char* level);

SEN_API void sen_controller_on_suspend(sen_controller_t* ctrl);

SEN_API void sen_controller_multi_start_data(sen_controller_t* ctrl,
                                             sen_profile_t* const* profiles, size_t count,
                                             int timeoutMs, int maxDelayDispersionMs, int maxAttempts,
                                             sen_multi_result_cb cb, void* ctx);
SEN_API void sen_controller_multi_stop_data(sen_controller_t* ctrl,
                                            sen_profile_t* const* profiles, size_t count,
                                            int timeoutMs,
                                            sen_multi_result_cb cb, void* ctx);

SEN_API int sen_check_setup_dongle(char* out, int32_t out_cap);

SEN_API void sen_controller_get_param(sen_controller_t* ctrl, const char* key,
                                      char* buf, size_t len);
SEN_API void sen_controller_set_param(sen_controller_t* ctrl, const char* key,
                                      const char* value, char* buf, size_t len);

SEN_API void sen_profile_set_callbacks(sen_profile_t* profile,
                                             const sen_profile_cbs_t* cbs, void* ctx);

SEN_API void sen_profile_get_device(sen_profile_t* profile, sen_ble_device_t* out);
SEN_API int  sen_profile_get_state(sen_profile_t* profile);

SEN_API void sen_profile_connect(sen_profile_t* profile, sen_completion_cb cb, void* ctx);
SEN_API void sen_profile_disconnect(sen_profile_t* profile, sen_completion_cb cb, void* ctx);

SEN_API int  sen_profile_has_init(sen_profile_t* profile);
SEN_API int  sen_profile_has_start_data_notification(sen_profile_t* profile);

SEN_API void sen_profile_init(sen_profile_t* profile, int packageSampleCount, int timeoutMs,
                                    int powerRefreshIntervalMs, sen_completion_cb cb, void* ctx);
SEN_API void sen_profile_start_data(sen_profile_t* profile, int timeoutMs,
                                          sen_completion_cb cb, void* ctx);
SEN_API void sen_profile_stop_data(sen_profile_t* profile, int timeoutMs,
                                         sen_completion_cb cb, void* ctx);

SEN_API void sen_profile_get_battery_level(sen_profile_t* profile, int timeoutMs,
                                                 sen_battery_cb cb, void* ctx);
SEN_API void sen_profile_fetch_device_info(sen_profile_t* profile, int timeoutMs,
                                                 sen_info_cb cb, void* ctx);
SEN_API void sen_profile_get_device_info(sen_profile_t* profile, sen_device_info_t* out);

SEN_API void sen_profile_set_param(sen_profile_t* profile, int timeoutMs,
                                         const char* key, const char* value,
                                         sen_param_cb cb, void* ctx);
SEN_API void sen_profile_get_param(sen_profile_t* profile, int timeoutMs,
                                         const char* key, sen_param_cb cb, void* ctx);

SEN_API void sen_profile_set_auto_reconnect(sen_profile_t* profile, int enabled);

SEN_API void sen_profile_log(sen_profile_t* profile, const char* message, const char* level);

#ifdef __cplusplus
}
#endif

#endif
