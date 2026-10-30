#ifndef SENSORALGO_HPP
#define SENSORALGO_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "sensortypes.hpp"
#include "sen_algo_capi.h"

namespace sensor {

class SensorAlgo {
public:
    using ParamCallback = std::function<void(const std::string& result, const std::string& error)>;
    using CompletionCallback = std::function<void(bool ok, const std::string& error)>;

    SensorAlgo() {
        sen_algo_capi_version_check();
        _handle = sen_algo_controller_create();
        if (_handle == nullptr) {
            throw std::runtime_error("sen_algo_controller_create failed");
        }
    }
    ~SensorAlgo() {
        if (_handle != nullptr) {
            sen_algo_controller_destroy(_handle);
        }
    }
    SensorAlgo(const SensorAlgo&) = delete;
    SensorAlgo& operator=(const SensorAlgo&) = delete;
    SensorAlgo(SensorAlgo&& other) noexcept {
        _handle = other._handle;
        other._handle = nullptr;
    }
    SensorAlgo& operator=(SensorAlgo&& other) noexcept {
        if (this != &other) {
            if (_handle != nullptr) {
                sen_algo_controller_destroy(_handle);
            }
            _handle = other._handle;
            other._handle = nullptr;
        }
        return *this;
    }

    static uint32_t getVersion() { return sen_algo_capi_version(); }

    void init(CompletionCallback cb = nullptr) {
        if (_handle == nullptr) {
            answerCompletion(std::move(cb), "Error: Invalid arguments");
            return;
        }
        sen_algo_controller_init(_handle, "",
                                 cb ? &completionTrampoline : nullptr,
                                 cb ? makeCallContext(std::move(cb)) : nullptr);
    }

    bool hasInit() const {
        return _handle != nullptr && sen_algo_controller_has_init(_handle) != 0;
    }

    void reset() {
        if (_handle != nullptr) {
            sen_algo_controller_reset(_handle);
        }
    }

    void setParam(const std::string& key, const std::string& value, ParamCallback cb = nullptr) {
        if (_handle == nullptr) {
            answerParam(std::move(cb), "Error: Invalid arguments");
            return;
        }
        sen_algo_controller_set_param(_handle, key.c_str(), value.c_str(),
                                      cb ? &paramTrampoline : nullptr,
                                      cb ? makeCallContext(std::move(cb)) : nullptr);
    }

    void getParam(const std::string& key, ParamCallback cb = nullptr) {
        if (_handle == nullptr) {
            answerParam(std::move(cb), "Error: Invalid arguments");
            return;
        }
        sen_algo_controller_get_param(_handle, key.c_str(),
                                      cb ? &paramTrampoline : nullptr,
                                      cb ? makeCallContext(std::move(cb)) : nullptr);
    }

    void pushSamples(const std::vector<SensorDataView>& views, const std::string& param = "",
                     CompletionCallback cb = nullptr) {
        if (_handle == nullptr) {
            answerCompletion(std::move(cb), "Error: Invalid arguments");
            return;
        }
        std::vector<sen_data_view_t> nativeViews;
        nativeViews.reserve(views.size());
        for (const auto& view : views) {
            nativeViews.push_back(view.nativeView());
        }
        sen_algo_controller_push_samples(_handle, nativeViews.data(), nativeViews.size(), param.c_str(),
                                         cb ? &completionTrampoline : nullptr,
                                         cb ? makeCallContext(std::move(cb)) : nullptr);
    }

    void getResult(const std::string& key, ParamCallback cb = nullptr) {
        if (_handle == nullptr) {
            answerParam(std::move(cb), "Error: Invalid arguments");
            return;
        }
        sen_algo_controller_get_result(_handle, key.c_str(),
                                       cb ? &paramTrampoline : nullptr,
                                       cb ? makeCallContext(std::move(cb)) : nullptr);
    }

private:
    template <typename F>
    struct CallContext {
        F fn;
    };

    template <typename F>
    static void* makeCallContext(F fn) {
        return new CallContext<F>{std::move(fn)};
    }

    static std::string strOrEmpty(const char* s) {
        return s != nullptr ? std::string(s) : std::string();
    }

    static void answerParam(ParamCallback cb, const std::string& result) {
        if (cb) {
            cb(result, std::string());
        }
    }

    static void answerCompletion(CompletionCallback cb, const std::string& error) {
        if (cb) {
            cb(false, error);
        }
    }

    static void paramTrampoline(void* ctx, const char* result, const char* errorMsg) noexcept {
        std::unique_ptr<CallContext<ParamCallback>> holder(
            static_cast<CallContext<ParamCallback>*>(ctx));
        try {
            if (holder && holder->fn) {
                holder->fn(strOrEmpty(result), strOrEmpty(errorMsg));
            }
        } catch (...) {
        }
    }

    static void completionTrampoline(void* ctx, int result, const char* errorMsg) noexcept {
        std::unique_ptr<CallContext<CompletionCallback>> holder(
            static_cast<CallContext<CompletionCallback>*>(ctx));
        try {
            if (holder && holder->fn) {
                holder->fn(result != 0, strOrEmpty(errorMsg));
            }
        } catch (...) {
        }
    }

    sen_algo_controller_t* _handle = nullptr;
};

}

#endif
