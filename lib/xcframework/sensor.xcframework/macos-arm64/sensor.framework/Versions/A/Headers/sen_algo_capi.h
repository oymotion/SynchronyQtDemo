#ifndef SEN_ALGO_CAPI_H
#define SEN_ALGO_CAPI_H

#include <stdio.h>

#include "sen_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SEN_ALGO_CAPI_VERSION 7  /* PySDKAlgo 2c564348e4ca7a657c7cc68e051f097aec10f456 */

typedef struct sen_algo_controller sen_algo_controller_t;

typedef void (*sen_algo_completion_cb)(void* ctx, int result, const char* errorMsg);
typedef void (*sen_algo_param_cb)(void* ctx, const char* result, const char* errorMsg);

SEN_API uint32_t sen_algo_capi_version(void);

static inline int sen_algo_capi_version_check(void)
{
    uint32_t libVersion = sen_algo_capi_version();
    if (libVersion != (uint32_t)SEN_ALGO_CAPI_VERSION) {
        fprintf(stderr,
                "[sen_algo_capi] WARNING: header SEN_ALGO_CAPI_VERSION %d does not match the loaded library (version %u); rebuild the library or update the consumer\n",
                (int)SEN_ALGO_CAPI_VERSION, (unsigned)libVersion);
        return 0;
    }
    return 1;
}

SEN_API sen_algo_controller_t* sen_algo_controller_create(void);
SEN_API void sen_algo_controller_destroy(sen_algo_controller_t* ctrl);
SEN_API void sen_algo_controller_init(sen_algo_controller_t* ctrl, const char* param, sen_algo_completion_cb cb, void* ctx);
SEN_API int sen_algo_controller_has_init(sen_algo_controller_t* ctrl);
SEN_API void sen_algo_controller_reset(sen_algo_controller_t* ctrl);
SEN_API void sen_algo_controller_set_param(sen_algo_controller_t* ctrl, const char* key, const char* value, sen_algo_param_cb cb, void* ctx);
SEN_API void sen_algo_controller_get_param(sen_algo_controller_t* ctrl, const char* key, sen_algo_param_cb cb, void* ctx);
SEN_API void sen_algo_controller_push_samples(sen_algo_controller_t* ctrl, const sen_data_view_t* views, size_t viewCount, const char* param, sen_algo_completion_cb cb, void* ctx);
SEN_API void sen_algo_controller_get_result(sen_algo_controller_t* ctrl, const char* key, sen_algo_param_cb cb, void* ctx);

#ifdef __cplusplus
}
#endif

#endif
