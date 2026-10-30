#ifndef SEN_CAPI_H
#define SEN_CAPI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "sen_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SEN_CAPI_VERSION 22

typedef struct sen_controller sen_controller_t;
typedef struct sen_profile sen_profile_t;

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
SEN_API void sen_controller_scan_once(sen_controller_t* ctrl, int periodInMS, sen_scan_result_cb cb, void* ctx);

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
                                                   const char* csvPath, sen_param_cb cb, void* ctx);

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

SEN_API void sen_controller_check_setup_dongle(sen_controller_t* ctrl, sen_param_cb cb, void* ctx);

SEN_API void sen_controller_get_param(sen_controller_t* ctrl, const char* key,
                                      sen_param_cb cb, void* ctx);
SEN_API void sen_controller_set_param(sen_controller_t* ctrl, const char* key,
                                      const char* value, sen_param_cb cb, void* ctx);

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
SEN_API int sen_profile_get_auto_reconnect(sen_profile_t* profile);

SEN_API void sen_profile_log(sen_profile_t* profile, const char* message, const char* level);

#ifdef __cplusplus
}
#endif

#endif
