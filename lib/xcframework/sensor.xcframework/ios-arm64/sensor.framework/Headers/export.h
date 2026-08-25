/*
 * Copyright 2017, OYMotion Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
*
*/
/*!
* \file export.h
* \brief 
*
* \version 0.1
* \date 2017.4.20
*/
#pragma once
#ifdef _WIN32
#ifdef SENSORSDK_EXPORTS
#define SENSORSDK_API __declspec(dllexport)
#else
#define SENSORSDK_API __declspec(dllimport)
#endif
#define CALLING_CONVENTION __cdecl
#else
#define SENSORSDK_API __attribute__ ((visibility ("default")))
#define CALLING_CONVENTION
#endif

#ifdef __cplusplus
extern "C"
{
#endif


    enum class RetCodes : int
    {
        STATUS_OK = 0,
        PORT_ALREADY_OPEN_ERROR = 1,
        UNABLE_TO_OPEN_PORT_ERROR = 2,
        SET_PORT_ERROR = 3,
        BOARD_WRITE_ERROR = 4,
        INCOMMING_MSG_ERROR = 5,
        INITIAL_MSG_ERROR = 6,
        BOARD_NOT_READY_ERROR = 7,
        STREAM_ALREADY_RUN_ERROR = 8,
        INVALID_BUFFER_SIZE_ERROR = 9,
        STREAM_THREAD_ERROR = 10,
        STREAM_THREAD_IS_NOT_RUNNING = 11,
        EMPTY_BUFFER_ERROR = 12,
        INVALID_ARGUMENTS_ERROR = 13,
        UNSUPPORTED_BOARD_ERROR = 14,
        BOARD_NOT_CREATED_ERROR = 15,
        ANOTHER_BOARD_IS_CREATED_ERROR = 16,
        GENERAL_ERROR = 17,
        SYNC_TIMEOUT_ERROR = 18,
        JSON_NOT_FOUND_ERROR = 19,
        NO_SUCH_DATA_IN_JSON_ERROR = 20,
        CLASSIFIER_IS_NOT_PREPARED_ERROR = 21,
        ANOTHER_CLASSIFIER_IS_PREPARED_ERROR = 22,
        UNSUPPORTED_CLASSIFIER_AND_METRIC_COMBINATION_ERROR = 23
    };

    // Note: no SENSORSDK_API on the typedefs - export/visibility attributes
    // on a typedef are meaningless and clang rejects them under -Werror.
    typedef void (CALLING_CONVENTION* deviceListCallback)(int, const char*);
    typedef void (CALLING_CONVENTION* errorCallback)(const char*, const char*);
    typedef void (CALLING_CONVENTION* stateChangeCallback)(const char*, int);
    typedef void (CALLING_CONVENTION* dataCallback)(const char*, int, sensor::SensorData::Sample*);
    typedef void (CALLING_CONVENTION* deviceInfoCallback)(const char*, sensor::DeviceInfo*);
    typedef void (CALLING_CONVENTION* boolCallback)(int, const char*);
    typedef void (CALLING_CONVENTION* intCallback)(int, const char*);
    typedef void (CALLING_CONVENTION* strCallback)(const char*, const char*);
    // Return non-zero to take over the auto-reconnect recovery yourself
    // (restore != 0 means a previous session exists), zero to let the SDK
    // run its default recovery flow.
    typedef int (CALLING_CONVENTION* autoReconnectCallback)(const char*, int);


    SENSORSDK_API int CALLING_CONVENTION init();
    SENSORSDK_API void CALLING_CONVENTION sdk_terminate();
    SENSORSDK_API int CALLING_CONVENTION set_device_callback(deviceListCallback callback);
    SENSORSDK_API int CALLING_CONVENTION set_error_callback(errorCallback callback);
    SENSORSDK_API int CALLING_CONVENTION set_state_change_callback(stateChangeCallback callback);
    SENSORSDK_API int CALLING_CONVENTION set_data_callback(dataCallback callback);

    SENSORSDK_API int CALLING_CONVENTION start_scan_synchroni_device(int timeoutInSeconds);
    SENSORSDK_API int CALLING_CONVENTION stop_scan_synchroni_device();
    SENSORSDK_API int CALLING_CONVENTION scan_synchroni_device(int timeoutInSeconds, char* deviceList, unsigned int len);
    SENSORSDK_API int CALLING_CONVENTION is_synchroni_open(const char* mac);
    SENSORSDK_API int CALLING_CONVENTION open_synchroni(const char* mac);
    SENSORSDK_API int CALLING_CONVENTION close_synchroni(const char* mac);
    SENSORSDK_API int CALLING_CONVENTION start_synchroni(const char* mac);
    SENSORSDK_API int CALLING_CONVENTION stop_synchroni(const char* mac);
    SENSORSDK_API int CALLING_CONVENTION get_synchroni_error_msg(const char* mac, char* errMsg, unsigned int len);
    SENSORSDK_API int CALLING_CONVENTION get_synchroni_device_info(const char* mac, sensor::DeviceInfo* out_info);
    SENSORSDK_API int CALLING_CONVENTION set_synchroni_param(const char* mac, const char* key, const char* value);
    SENSORSDK_API int CALLING_CONVENTION get_synchroni_param(const char* mac, const char* key, char* value, unsigned int len);
    SENSORSDK_API int CALLING_CONVENTION set_synchroni_auto_reconnect(const char* mac, int enabled);
    SENSORSDK_API int CALLING_CONVENTION set_auto_reconnect_callback(autoReconnectCallback callback);
    SENSORSDK_API int CALLING_CONVENTION set_debug_enabled(int enabled);
    SENSORSDK_API int CALLING_CONVENTION set_data_log_enabled(int enabled);
    SENSORSDK_API int CALLING_CONVENTION set_log_path(int enabled, const char* path);
    SENSORSDK_API int CALLING_CONVENTION read_synchroni_rawdata(const char* mac, int ntf_dataType, int* data, unsigned int len);

    // Raw BLE bin capture inspection and offline replay.
    // outInfo receives "mac|deviceName|durationSec|valid" (NUL-terminated,
    // truncated to len). Returns STATUS_OK or an error code.
    SENSORSDK_API int CALLING_CONVENTION get_bin_file_info(const char* path, char* outInfo, unsigned int len);
    // Starts replaying a bin capture; parsed data arrives through the normal
    // data callback for the given mac. Returns STATUS_OK or an error code.
    SENSORSDK_API int CALLING_CONVENTION replay_bin_file(const char* path, const char* mac, int realtime, unsigned int timeoutMs);
    SENSORSDK_API int CALLING_CONVENTION pause_bin_replay(const char* mac);
    SENSORSDK_API int CALLING_CONVENTION resume_bin_replay(const char* mac);
    SENSORSDK_API int CALLING_CONVENTION stop_bin_replay(const char* mac);
    // Parses a bin capture into CSV (blocking). outResult receives the csv
    // path on success or an "Error: ..." string. Returns STATUS_OK when the
    // result is not an error.
    SENSORSDK_API int CALLING_CONVENTION parse_bin_to_csv(const char* binPath, const char* csvPath, char* outResult, unsigned int len);

#ifdef __cplusplus
}
#endif

#define DESTORY_SENSOR_CONTROLLER_DELAY struct _SensorControllerCleaner { ~_SensorControllerCleaner() { sensor::SensorController::destory(); } }_sensorControllerCleaner;