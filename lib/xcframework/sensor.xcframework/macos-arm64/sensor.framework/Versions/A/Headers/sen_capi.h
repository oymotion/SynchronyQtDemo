#ifndef SEN_CAPI_H
#define SEN_CAPI_H

// Flat C API mirroring the C++ object model:
//   SensorController  -> sen_controller_t (scan, device registry, bin family)
//   SensorProfile     -> sen_profile_t    (per-device connect/init/stream/params)
//   delegates         -> callback tables with a user context pointer
//   callback-async ops -> per-call completion callbacks
//
// This API coexists with the legacy polling API in export.h; it is the
// intended surface for new bindings (Python / C# / Android JNI). Bindings
// must stay thin: all behavior (error strings, subscription masks, session
// recovery) lives inside the SDK.
//
// Ownership & lifetime rules:
// - sen_profile_t* handles are owned by their sen_controller_t; they stay
//   valid until sen_controller_destroy(). Never use a handle after that.
// - sen_data_view_t and every const char* delivered to a callback are
//   BORROWED: valid only for the duration of that callback invocation.
//   Consumers that keep data must copy inside the callback.
// - Output strings/structs are caller-allocated buffers; functions never
//   write beyond the given length.
//
// Threading:
// - All functions may be called from any thread unless noted otherwise.
// - Callbacks fire on internal SDK threads. Do not call blocking SDK
//   functions from inside a callback.
//
// ABI:
// - C linkage, POD types only, no exceptions cross the boundary.
// - Extensible structs carry a structSize first field; callers set it to
//   sizeof(the struct) and the SDK writes at most that many bytes, so
//   consumers built against an older header keep working.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Export macro kept local (NOT export.h): export.h references C++ types and
// would make this header unusable for pure C consumers.
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
  /* C11 member alignment (MSVC 2019+ / clang / gcc all support it) */
  #define SEN_ALIGN8 _Alignas(8)
#endif

/* ABI/API version of this header. MUST be incremented by 1 on EVERY change
   to the definitions in this file (structs, callbacks, functions); read at
   runtime via sen_capi_version(). */
#define SEN_CAPI_VERSION 7

typedef struct sen_controller sen_controller_t;
typedef struct sen_profile sen_profile_t;

/* Device link state (same numbering as BLEDevice::State). */
enum SenDeviceState {
    SEN_STATE_DISCONNECTED = 0,
    SEN_STATE_CONNECTING = 1,
    SEN_STATE_CONNECTED = 2,
    SEN_STATE_READY = 3,
    SEN_STATE_DISCONNECTING = 4,
    SEN_STATE_INVALID = 5
};

/* SensorData::Type numbering (NTF_*). */
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

// FIXED CROSS-PLATFORM ABI (little-endian), identical to
// SensorData::Sample in the C++ API:
//   offset  0  double absTimeStampInSec  (LSL-style absolute timestamp:
//              stream-start wall clock + first-packet delay +
//              sampleIndex/sampleRate, computed at decode time; 0 when the
//              anchor is unknown)
//   offset  8  int32  channelIndex
//   offset 12  int32  sampleIndex
//   offset 16  int32  rawData
//   offset 20  float  data
//   offset 24  float  impedance
//   offset 28  float  saturation
//   offset 32  uint8  isLost
//   offset 33-39 padding
//   sizeof == 40
// Language bindings are expected to read samples straight from the
// sen_data_view_t.samples pointer via native buffer access (DirectByteBuffer
// / IntPtr / memoryview) and these offsets, instead of per-sample
// marshalling; clone == one raw memcpy of sen_data_view_t.samplesBytes bytes.
#define SEN_SAMPLE_SIZE 40
typedef struct {
    /* SEN_ALIGN8: on 32-bit ABIs (Android x86/armv7) a double is only
       4-aligned, which would shrink the tail padding and break the fixed
       40-byte layout -- pin it. */
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

// Broadcast metadata shared by every view of one stream (mirrors
// SensorData::Info field-for-field).
typedef struct {
    char     deviceMac[18];
    int32_t  dataType;         /* enum SenDataType */
    int32_t  lostPackageCount; /* lost PACKAGE count */
    float    sampleRate;       /* float (GEST streams report e.g. 15.625) */
    int32_t  channelCount;
    /* SEN_ALIGN8 on the 64-bit members: on 32-bit ABIs uint64_t/double are
       only 4-aligned, which would shift every later field -- pin the layout
       so it is identical on 32- and 64-bit platforms (the bindings hardcode
       these offsets). */
    SEN_ALIGN8 uint64_t channelMask;
    int32_t  sampleCount;      /* valid samples per channel; per-channel stride */
    uint32_t startTimeStamp;   /* stream-start stamp: steady-clock ms live
                                  (low 32 bits), the bin record ts on replay;
                                  re-stamped on every (re)start */
    uint32_t delay;            /* first data packet arrival - startTimeStamp;
                                  0 until the first packet of the current start */
    SEN_ALIGN8 double startTimeSec; /* wall-clock Unix seconds (double) at stream
                                  start; on replay restored from the bin record
                                  timestamps (0 = unknown) */
    char     deviceName[32];   /* device name from the cached DeviceInfo,
                                  stamped once when the stream is created
                                  (like deviceMac); empty when unknown */
} sen_data_info_t;

// One broadcast batch of a single stream. Layout invariant: samples are
// packed [channel][sample] with the per-channel stride == sampleCount, so
// samples[channelIndex * sampleCount + sampleIndex] is the slot of
// (channelIndex, sampleIndex). Channels masked out of channelMask carry
// zeroed slots. A slot whose sampleIndex != startSampleIndex + sampleIndex
// is stale and must be skipped.
typedef struct {

    int32_t  startSampleIndex; /* absolute sampleIndex of slot (channel, 0) */
    uint32_t startTimeStamp;   /* snapshot of the stream's Info.startTimeStamp at broadcast time;
                                      a view whose startTimeStamp differs from
                                      the current Info value belongs to a
                                      previous session and is stale */
    const sen_data_info_t* info;   /* borrowed; lives with the stream (valid
        until stream teardown); lostPackageCount /
        delay updates stay visible */
    const sen_sample_t* samples; /* borrowed, callback scope only */
    size_t   samplesBytes;  /* byte size of the samples block
                               (channelCount * sampleCount * SEN_SAMPLE_SIZE);
                               0 when samples is NULL */
} sen_data_view_t;

typedef struct {
    char    name[32];
    char    mac[18];
    int16_t rssi;
} sen_ble_device_t;

// Mirror of DeviceInfo; structSize-versioned for forward growth.
typedef struct {
    uint32_t structSize; /* in: sizeof(sen_device_info_t) */
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
    /* Max sample rates from the device capability queries; 0 = not reported
       or not supported (aligned with the Python SDK 0.6.1 DeviceInfo) */
    uint16_t EmgMaxSampleRate;
    uint16_t EegMaxSampleRate;
    uint16_t EcgMaxSampleRate;
    /* Link connection parameters (aligned with the Python SDK 0.7.0
       DeviceInfo). The Python SDK reads them from its bumble backend; the
       C++ BLE backends do not expose them, so they always report the
       unknown values. */
    double   ConnectionIntervalMs;  /* connection interval in ms; 0 = unknown */
    int32_t  PeripheralLatency;     /* latency in events; -1 = unknown (0 is legal) */
    int32_t  SupervisionTimeoutMs;  /* supervision timeout in ms; 0 = unknown */
} sen_device_info_t;

// Summary of a raw BLE bin capture (aligned with getBinFileInfo).
// Extensible: set structSize to sizeof(sen_bin_file_info_t); deviceInfo is
// filled only when structSize covers it.
typedef struct {
    uint32_t structSize; /* in: sizeof(sen_bin_file_info_t) */
    char     mac[18];
    char     deviceName[32];
    double   durationSec;
    uint8_t  valid;
    uint8_t  _reserved[7];
    /* DeviceInfo from the first CONFIG record; zeroed when the file has no
       decodable config or when the caller's structSize predates this field. */
    sen_device_info_t deviceInfo;
} sen_bin_file_info_t;

/* ---- callback types ---------------------------------------------------- */

// Controller-level.
typedef void (*sen_scan_result_cb)(void* ctx, const sen_ble_device_t* devices, size_t count);
typedef void (*sen_enable_changed_cb)(void* ctx, int enabled);

// Profile-level delegate table (all borrowed args are callback-scope).
typedef void (*sen_data_cb)(void* ctx, sen_profile_t* profile,
                            const sen_data_view_t* views, size_t viewCount);
typedef void (*sen_state_cb)(void* ctx, sen_profile_t* profile, int newState);
typedef void (*sen_error_cb)(void* ctx, sen_profile_t* profile, const char* errorMsg);
typedef void (*sen_power_cb)(void* ctx, sen_profile_t* profile, int power);
// Return non-zero to take over session recovery yourself (SDK skips its
// default init -> setParam replay -> stream restart flow), zero for the
// default recovery.
typedef int (*sen_auto_reconnect_cb)(void* ctx, sen_profile_t* profile, int hasLastSession);
// DeviceInfo field change push (aligned with the Python SDK 0.7.0
// onDeviceInfoUpdate): fired after the cached DeviceInfo was updated in
// place (e.g. setParam "EEG_SAMPLE_RATE" rewrote the bound EEG/ECG rates,
// or a bin replay hit a CONFIG record that changes sample rates / channel
// counts mid-capture -- Python 0.7.1; the replay's first CONFIG does not
// fire). The info pointer is callback-scope.
typedef void (*sen_device_info_update_cb)(void* ctx, sen_profile_t* profile,
                                          const sen_device_info_t* info);
/* Data stream on/off state change push: fired when the data stream actually
   starts (successful sen_profile_start_data, replay data start) or stops
   (sen_profile_stop_data, link loss, replay end), only on a real change.
   isTransferring: 1 = streaming, 0 = stopped. */
typedef void (*sen_data_transfer_state_cb)(void* ctx, sen_profile_t* profile,
                                           int isTransferring);

typedef struct {
    uint32_t structSize; /* in: sizeof(sen_profile_cbs_t) */
    sen_data_cb           onData;
    sen_state_cb          onStateChange;
    sen_error_cb          onError;
    sen_power_cb          onPowerChange;
    sen_auto_reconnect_cb onAutoReconnect;
    sen_device_info_update_cb onDeviceInfoUpdate;
    sen_data_transfer_state_cb onDataTransferStateChange;
} sen_profile_cbs_t;

typedef struct {
    uint32_t structSize; /* in: sizeof(sen_controller_cbs_t) */
    sen_scan_result_cb   onScanResult;
    sen_enable_changed_cb onEnableChanged;
} sen_controller_cbs_t;

// Per-operation completions. errorMsg is empty (not NULL) on success.
// All args are callback-scope.
typedef void (*sen_completion_cb)(void* ctx, int result, const char* errorMsg);
typedef void (*sen_param_cb)(void* ctx, const char* result, const char* errorMsg);
typedef void (*sen_battery_cb)(void* ctx, int result, const char* errorMsg);
typedef void (*sen_info_cb)(void* ctx, const sen_device_info_t* info, const char* errorMsg);

/* ---- controller -------------------------------------------------------- */

/* Terminates the SDK (wraps SensorController::destory()): stops all scans
   and connections and releases SDK-wide resources. Call once at application
   shutdown; every controller/profile handle is invalid after this call. */
SEN_API void sen_terminate(void);

/* Returns SEN_CAPI_VERSION of the loaded library, so consumers can detect a
   header/library mismatch at runtime. */
SEN_API uint32_t sen_capi_version(void);

/* Startup self-check for C/C++ consumers: compares THIS HEADER's
   SEN_CAPI_VERSION (the consumer's compile-time version) against the loaded
   library's sen_capi_version() and prints a warning to stderr on mismatch.
   Returns 1 when they match, 0 otherwise. */
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

SEN_API void sen_controller_set_debug_enabled(sen_controller_t* ctrl, int enabled);
SEN_API void sen_controller_set_data_log_enabled(sen_controller_t* ctrl, int enabled);
SEN_API void sen_controller_set_log_path(sen_controller_t* ctrl, int enabled, const char* path);

// Creates and registers a profile when the MAC is unknown, so it also works
// for devices not discovered by the scanner. Handle owned by the controller.
SEN_API sen_profile_t* sen_controller_require_sensor(sen_controller_t* ctrl, const char* mac);
SEN_API sen_profile_t* sen_controller_get_sensor(sen_controller_t* ctrl, const char* mac);
// Returns the number of profiles; when out != NULL, writes up to capacity
// handles (owned by the controller, do not free).
SEN_API size_t sen_controller_get_sensors(sen_controller_t* ctrl, sen_profile_t** out, size_t capacity);
SEN_API size_t sen_controller_get_connected_sensors(sen_controller_t* ctrl, sen_profile_t** out, size_t capacity);

/* Bin capture inspection and offline replay. */
SEN_API int sen_controller_get_bin_file_info(sen_controller_t* ctrl, const char* path,
                                                   sen_bin_file_info_t* out);
// Replays a bin capture through the normal parse pipeline on a background
// thread; attach callbacks to the returned profile to receive the data.
// Returns NULL on failure.
SEN_API sen_profile_t* sen_controller_replay_bin_file(sen_controller_t* ctrl, const char* path,
                                                            const char* deviceMac, int realtime,
                                                            uint32_t timeoutMs);
// These write "OK" or "Error: ..." into buf (NUL-terminated, truncated to len).
SEN_API void sen_controller_pause_bin_replay(sen_controller_t* ctrl, const char* deviceMac,
                                                   char* buf, size_t len);
SEN_API void sen_controller_resume_bin_replay(sen_controller_t* ctrl, const char* deviceMac,
                                                    char* buf, size_t len);
SEN_API void sen_controller_stop_bin_replay(sen_controller_t* ctrl, const char* deviceMac,
                                                  char* buf, size_t len);
// Offline full-speed parse of a bin capture into CSV. Blocks the caller;
// writes the csv path on success or an "Error: ..." string into buf.
SEN_API void sen_controller_parse_bin_to_csv(sen_controller_t* ctrl, const char* binPath,
                                                   const char* csvPath, char* buf, size_t len);

SEN_API void sen_controller_get_version(sen_controller_t* ctrl, char* buf, size_t len);

/* Writes an application log line into the SDK log (tag "App", controller
   channel). level is one of "D"/"I"/"W"/"E" (first char, case-insensitive;
   anything else is treated as "I"); "D" lines are gated by
   sen_controller_set_debug_enabled. Never fails. */
SEN_API void sen_controller_log(sen_controller_t* ctrl, const char* message, const char* level);

/* iOS/Android: call when the app moves to the background. Writes a "suspend"
   event marker into every open bin capture and flushes each capture plus the
   SDK log queue to disk, so an app killed while suspended loses as little as
   possible. Does NOT stop scanning, streaming, or any connection. */
SEN_API void sen_controller_on_suspend(sen_controller_t* ctrl);

/* ---- profile ----------------------------------------------------------- */

SEN_API void sen_profile_set_callbacks(sen_profile_t* profile,
                                             const sen_profile_cbs_t* cbs, void* ctx);

SEN_API void sen_profile_get_device(sen_profile_t* profile, sen_ble_device_t* out);
SEN_API int  sen_profile_get_state(sen_profile_t* profile);

/* Callback-async: cb may be NULL (fire-and-forget); a non-NULL cb is invoked
   exactly once with the final result (1/0) and an empty errorMsg on success.
   All cb args are callback-scope. */
SEN_API void sen_profile_connect(sen_profile_t* profile, sen_completion_cb cb, void* ctx);
SEN_API void sen_profile_disconnect(sen_profile_t* profile, sen_completion_cb cb, void* ctx);

SEN_API int  sen_profile_has_init(sen_profile_t* profile);
SEN_API int  sen_profile_has_start_data_notification(sen_profile_t* profile);

// powerRefreshIntervalMs: battery polling period; 0 disables polling.
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
// Cached DeviceInfo populated during init/fetchDeviceInfo; no GATT traffic.
// out->structSize must be set by the caller.
SEN_API void sen_profile_get_device_info(sen_profile_t* profile, sen_device_info_t* out);

SEN_API void sen_profile_set_param(sen_profile_t* profile, int timeoutMs,
                                         const char* key, const char* value,
                                         sen_param_cb cb, void* ctx);
SEN_API void sen_profile_get_param(sen_profile_t* profile, int timeoutMs,
                                         const char* key, sen_param_cb cb, void* ctx);

// Enables/disables session recovery after an auto reconnect (default on).
SEN_API void sen_profile_set_auto_reconnect(sen_profile_t* profile, int enabled);

/* Writes an application log line into the SDK log (tag "App", routed to the
   profile's per-device channel, falling back to the controller channel).
   Same level rules as sen_controller_log. Never fails. */
SEN_API void sen_profile_log(sen_profile_t* profile, const char* message, const char* level);

#ifdef __cplusplus
}
#endif

#endif // SEN_CAPI_H
