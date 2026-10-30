#ifndef SENSORCPP_HPP
#define SENSORCPP_HPP

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sen_capi.h"

#include "sensortypes.hpp"

namespace sensor {

class SensorProfile;
class SensorController;

namespace detail {

inline std::string strOrEmpty(const char* s) {
    return s != nullptr ? std::string(s) : std::string();
}

inline bool isValidMac(const std::string& mac) {
    if (mac.size() != 17) {
        return false;
    }
    for (size_t i = 0; i < mac.size(); ++i) {
        const char c = mac[i];
        if ((i + 1) % 3 == 0) {
            if (c != ':') {
                return false;
            }
        } else if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

inline void reportCallbackException(const char* what) noexcept {
    if (what != nullptr) {
        std::fprintf(stderr, "sensorcpp callback exception: %s\n", what);
    } else {
        std::fprintf(stderr, "sensorcpp callback exception: unknown\n");
    }
}

inline std::unordered_set<std::thread::id>& sdkThreadIds() {
    static std::unordered_set<std::thread::id>* ids =
        new std::unordered_set<std::thread::id>();
    return *ids;
}

inline std::mutex& sdkThreadIdsMutex() {
    static std::mutex* m = new std::mutex();
    return *m;
}

inline void markSdkThread() {
    std::lock_guard<std::mutex> lock(sdkThreadIdsMutex());
    sdkThreadIds().insert(std::this_thread::get_id());
}

inline bool isSdkThread() {
    std::lock_guard<std::mutex> lock(sdkThreadIdsMutex());
    return sdkThreadIds().count(std::this_thread::get_id()) != 0;
}

inline void throwOnSdkThread() {
    if (isSdkThread()) {
        throw std::logic_error("blocking sync call from an SDK callback thread");
    }
}

inline std::string paramResultOrError(const std::string& result, const std::string& error) {
    if (!error.empty() && (result.empty() || result == "Error")) {
        return error;
    }
    return result;
}

inline std::atomic<int>& assumedCmdTimeoutMsRef() {
    static std::atomic<int> value{10000};
    return value;
}

inline int effectiveTimeoutMs(int timeoutMs) {
    return timeoutMs > 0 ? timeoutMs : assumedCmdTimeoutMsRef().load();
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
using MultiResultMap = std::map<std::string, std::pair<bool, std::string>>;
using MultiResultCallback = std::function<void(const MultiResultMap&)>;
using ScanOnceCallback = std::function<void(const std::vector<BLEDevice>&)>;

namespace detail {

inline void completionTrampoline(void* ctx, int result, const char* errorMsg) noexcept {
    markSdkThread();
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
    markSdkThread();
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
    markSdkThread();
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
    markSdkThread();
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
    markSdkThread();
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

inline void scanOnceTrampoline(void* ctx, const sen_ble_device_t* devices, size_t count) noexcept {
    markSdkThread();
    std::unique_ptr<CallContext<ScanOnceCallback>> holder(
        static_cast<CallContext<ScanOnceCallback>*>(ctx));
    try {
        if (!holder || !holder->fn) {
            return;
        }
        std::vector<BLEDevice> list;
        if (devices != nullptr) {
            list.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                list.push_back(bleDeviceFromNative(devices[i]));
            }
        }
        holder->fn(list);
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
        if (!_hasBleDeviceCache) {
            sen_ble_device_t d{};
            sen_profile_get_device(_handle, &d);
            _bleDeviceCache = detail::bleDeviceFromNative(d);
            _hasBleDeviceCache = true;
        }
        return _bleDeviceCache;
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
        BatteryCallback wrapped = [this, cb](int level, const std::string& error) {
            if (level >= 0) {
                _powerCache = level;
            }
            if (cb) {
                cb(level, error);
            }
        };
        void* callCtx = detail::makeCallContext(std::move(wrapped));
        sen_profile_get_battery_level(_handle, timeoutMs,
                                      &detail::batteryTrampoline, callCtx);
    }
    void fetchDeviceInfo(int timeoutMs, DeviceInfoCallback cb = nullptr) {
        DeviceInfoCallback wrapped = [this, cb](const DeviceInfo& info, const std::string& error) {
            if (error.empty()) {
                _deviceInfoCache = info;
                _hasDeviceInfoCache = true;
            }
            if (cb) {
                cb(info, error);
            }
        };
        void* callCtx = detail::makeCallContext(std::move(wrapped));
        sen_profile_fetch_device_info(_handle, timeoutMs,
                                      &detail::infoTrampoline, callCtx);
    }
    const DeviceInfo* getDeviceInfo() const {
        if (!_hasDeviceInfoCache) {
            sen_device_info_t native{};
            native.structSize = sizeof(native);
            sen_profile_get_device_info(_handle, &native);
            if (native.deviceName[0] == '\0' && native.modelName[0] == '\0') {
                return nullptr;
            }
            _deviceInfoCache = detail::deviceInfoFromNative(native);
            _hasDeviceInfoCache = true;
        }
        return &_deviceInfoCache;
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

    std::future<bool> connectAsync() {
        auto promise = std::make_shared<std::promise<bool>>();
        std::future<bool> fut = promise->get_future();
        connect([promise](bool ok, const std::string&) { promise->set_value(ok); });
        return fut;
    }
    bool connectSync() {
        detail::throwOnSdkThread();
        std::future<bool> fut = connectAsync();
        if (fut.wait_for(std::chrono::seconds(40)) == std::future_status::timeout) {
            return false;
        }
        return fut.get();
    }
    std::future<bool> disconnectAsync() {
        auto promise = std::make_shared<std::promise<bool>>();
        std::future<bool> fut = promise->get_future();
        disconnect([promise](bool ok, const std::string&) { promise->set_value(ok); });
        return fut;
    }
    bool disconnectSync() {
        detail::throwOnSdkThread();
        std::future<bool> fut = disconnectAsync();
        if (fut.wait_for(std::chrono::seconds(25)) == std::future_status::timeout) {
            return false;
        }
        return fut.get();
    }
    std::future<bool> initAsync(int packageSampleCount, int timeoutMs = 0,
                                int powerRefreshIntervalMs = 0) {
        auto promise = std::make_shared<std::promise<bool>>();
        std::future<bool> fut = promise->get_future();
        init(packageSampleCount, timeoutMs, powerRefreshIntervalMs,
             [promise](bool ok, const std::string&) { promise->set_value(ok); });
        return fut;
    }
    bool initSync(int packageSampleCount, int timeoutMs = 0, int powerRefreshIntervalMs = 0) {
        detail::throwOnSdkThread();
        std::future<bool> fut = initAsync(packageSampleCount, timeoutMs,
                                          powerRefreshIntervalMs);
        if (fut.wait_for(std::chrono::milliseconds(detail::effectiveTimeoutMs(timeoutMs)) +
                         std::chrono::seconds(15)) == std::future_status::timeout) {
            return false;
        }
        return fut.get();
    }
    std::future<bool> startDataAsync(int timeoutMs = 0) {
        auto promise = std::make_shared<std::promise<bool>>();
        std::future<bool> fut = promise->get_future();
        startData(timeoutMs,
                  [promise](bool ok, const std::string&) { promise->set_value(ok); });
        return fut;
    }
    bool startDataSync(int timeoutMs = 0) {
        detail::throwOnSdkThread();
        std::future<bool> fut = startDataAsync(timeoutMs);
        if (fut.wait_for(std::chrono::milliseconds(detail::effectiveTimeoutMs(timeoutMs)) +
                         std::chrono::seconds(15)) == std::future_status::timeout) {
            return false;
        }
        return fut.get();
    }
    std::future<bool> stopDataAsync(int timeoutMs = 0) {
        auto promise = std::make_shared<std::promise<bool>>();
        std::future<bool> fut = promise->get_future();
        stopData(timeoutMs,
                 [promise](bool ok, const std::string&) { promise->set_value(ok); });
        return fut;
    }
    bool stopDataSync(int timeoutMs = 0) {
        detail::throwOnSdkThread();
        std::future<bool> fut = stopDataAsync(timeoutMs);
        if (fut.wait_for(std::chrono::milliseconds(detail::effectiveTimeoutMs(timeoutMs)) +
                         std::chrono::seconds(15)) == std::future_status::timeout) {
            return false;
        }
        return fut.get();
    }
    std::future<std::string> setParamAsync(int timeoutMs = 0, const std::string& key = "",
                                           const std::string& value = "") {
        auto promise = std::make_shared<std::promise<std::string>>();
        std::future<std::string> fut = promise->get_future();
        setParam(timeoutMs, key, value,
                 [promise](const std::string& result, const std::string& error) {
                     promise->set_value(detail::paramResultOrError(result, error));
                 });
        return fut;
    }
    std::string setParamSync(int timeoutMs = 0, const std::string& key = "",
                             const std::string& value = "") {
        detail::throwOnSdkThread();
        std::future<std::string> fut = setParamAsync(timeoutMs, key, value);
        if (fut.wait_for(std::chrono::milliseconds(detail::effectiveTimeoutMs(timeoutMs)) +
                         std::chrono::seconds(15)) == std::future_status::timeout) {
            return "Error: Timeout";
        }
        return fut.get();
    }
    std::future<std::string> getParamAsync(int timeoutMs = 0, const std::string& key = "") {
        auto promise = std::make_shared<std::promise<std::string>>();
        std::future<std::string> fut = promise->get_future();
        getParam(timeoutMs, key,
                 [promise](const std::string& result, const std::string& error) {
                     promise->set_value(detail::paramResultOrError(result, error));
                 });
        return fut;
    }
    std::string getParamSync(int timeoutMs = 0, const std::string& key = "") {
        detail::throwOnSdkThread();
        std::future<std::string> fut = getParamAsync(timeoutMs, key);
        if (fut.wait_for(std::chrono::milliseconds(detail::effectiveTimeoutMs(timeoutMs)) +
                         std::chrono::seconds(15)) == std::future_status::timeout) {
            return "Error: Timeout";
        }
        return fut.get();
    }
    std::future<int> getBatteryLevelAsync(int timeoutMs = 0) {
        auto promise = std::make_shared<std::promise<int>>();
        std::future<int> fut = promise->get_future();
        getBatteryLevel(timeoutMs,
                        [promise](int level, const std::string&) {
                            promise->set_value(level);
                        });
        return fut;
    }
    int getBatteryLevelSync(int timeoutMs = 0) {
        if (_powerCache >= 0) {
            return _powerCache;
        }
        detail::throwOnSdkThread();
        std::future<int> fut = getBatteryLevelAsync(timeoutMs);
        if (fut.wait_for(std::chrono::milliseconds(detail::effectiveTimeoutMs(timeoutMs)) +
                         std::chrono::seconds(15)) == std::future_status::timeout) {
            return -1;
        }
        return fut.get();
    }
    std::future<DeviceInfo> fetchDeviceInfoAsync(int timeoutMs = 0) {
        auto promise = std::make_shared<std::promise<DeviceInfo>>();
        std::future<DeviceInfo> fut = promise->get_future();
        fetchDeviceInfo(timeoutMs,
                        [promise](const DeviceInfo& info, const std::string&) {
                            promise->set_value(info);
                        });
        return fut;
    }
    bool fetchDeviceInfoSync(DeviceInfo& out) {
        return fetchDeviceInfoSync(0, out);
    }
    bool fetchDeviceInfoSync(int timeoutMs, DeviceInfo& out) {
        detail::throwOnSdkThread();
        auto promise = std::make_shared<std::promise<std::pair<bool, DeviceInfo>>>();
        std::future<std::pair<bool, DeviceInfo>> fut = promise->get_future();
        fetchDeviceInfo(timeoutMs,
                        [promise](const DeviceInfo& info, const std::string& error) {
                            promise->set_value(std::make_pair(error.empty(), info));
                        });
        if (fut.wait_for(std::chrono::milliseconds(detail::effectiveTimeoutMs(timeoutMs)) +
                         std::chrono::seconds(15)) == std::future_status::timeout) {
            return false;
        }
        const std::pair<bool, DeviceInfo> result = fut.get();
        out = result.second;
        return result.first;
    }

    void setAutoReconnect(bool enabled) {
        sen_profile_set_auto_reconnect(_handle, enabled ? 1 : 0);
    }

    bool getAutoReconnect() const {
        return sen_profile_get_auto_reconnect(_handle) != 0;
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
        detail::markSdkThread();
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
        detail::markSdkThread();
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self == nullptr) {
                return;
            }
            if (newState == BLEDevice::Disconnected) {
                self->_hasDeviceInfoCache = false;
                self->_hasBleDeviceCache = false;
                self->_powerCache = -1;
            }
            if (self->_cbs.onStateChange) {
                self->_cbs.onStateChange(self, newState);
            }
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    static void onErrorCb(void* ctx, sen_profile_t* , const char* errorMsg) {
        detail::markSdkThread();
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
        detail::markSdkThread();
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self == nullptr) {
                return;
            }
            if (power >= 0) {
                self->_powerCache = power;
            }
            if (self->_cbs.onPowerChange) {
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
        detail::markSdkThread();
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self == nullptr) {
                return;
            }
            if (!self->_cbs.onAutoReconnect) {
                if (answer != nullptr) {
                    answer(answerCtx, 0);
                }
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
        detail::markSdkThread();
        try {
            SensorProfile* self = static_cast<SensorProfile*>(ctx);
            if (self == nullptr || info == nullptr) {
                return;
            }
            self->_deviceInfoCache = detail::deviceInfoFromNative(*info);
            self->_hasDeviceInfoCache = true;
            if (self->_cbs.onDeviceInfoUpdate) {
                self->_cbs.onDeviceInfoUpdate(self, self->_deviceInfoCache);
            }
        } catch (const std::exception& e) {
            detail::reportCallbackException(e.what());
        } catch (...) {
            detail::reportCallbackException(nullptr);
        }
    }

    static void onDataTransferStateChangeCb(void* ctx, sen_profile_t* ,
                                            int isTransferring) {
        detail::markSdkThread();
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
    mutable DeviceInfo _deviceInfoCache;
    mutable bool _hasDeviceInfoCache = false;
    mutable BLEDevice _bleDeviceCache;
    mutable bool _hasBleDeviceCache = false;
    int _powerCache = -1;
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
        if (!detail::isValidMac(mac)) {
            return nullptr;
        }
        return wrapProfile(sen_controller_require_sensor(_handle, mac.c_str()));
    }
    SensorProfile* requireSensor(const BLEDevice& device) {
        SensorProfile* profile = requireSensor(device.mac);
        if (profile != nullptr && !profile->_hasBleDeviceCache) {
            profile->_bleDeviceCache = device;
            profile->_hasBleDeviceCache = true;
        }
        return profile;
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
    SensorProfile* replayBinFile(const std::string& path, SensorProfile* sensor,
                                 bool realtime = true, uint32_t timeoutMs = 0) {
        if (sensor == nullptr) {
            return nullptr;
        }
        const std::string mac = sensor->getDevice().mac;
        return wrapProfile(sen_controller_replay_bin_file(
            _handle, path.c_str(), mac.c_str(), realtime ? 1 : 0, timeoutMs));
    }
    std::vector<SensorProfile*> multiReplayBinFile(
        const std::vector<std::string>& paths,
        const std::vector<SensorProfile*>& sensors,
        bool realtime = true, uint32_t timeoutMs = 0) {
        const size_t count = paths.size() < sensors.size() ? paths.size() : sensors.size();
        std::vector<const char*> pathPtrs(count);
        std::vector<std::string> macs(count);
        std::vector<const char*> macPtrs(count);
        for (size_t i = 0; i < count; ++i) {
            pathPtrs[i] = paths[i].c_str();
            macs[i] = sensors[i] != nullptr ? sensors[i]->getDevice().mac : std::string();
            macPtrs[i] = macs[i].c_str();
        }
        std::vector<sen_profile_t*> outHandles(count, nullptr);
        sen_controller_multi_replay_bin_file(
            _handle, count > 0 ? pathPtrs.data() : nullptr,
            count > 0 ? macPtrs.data() : nullptr, count, realtime ? 1 : 0, timeoutMs,
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
    void parseBinToCsv(const std::string& binPath, const std::string& csvPath,
                       ParamCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_controller_parse_bin_to_csv(_handle, binPath.c_str(), csvPath.c_str(),
                                        callCtx ? &detail::paramTrampoline : nullptr, callCtx);
    }

    std::string getVersion() {
        return readOutString(
            [&](char* buf, size_t len) {
                sen_controller_get_version(_handle, buf, len);
            },
            256);
    }

    void getParam(const std::string& key, ParamCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_controller_get_param(_handle, key.c_str(),
                                 callCtx ? &detail::paramTrampoline : nullptr, callCtx);
    }
    static std::atomic<int>& assumedCmdTimeoutMs() {
        return detail::assumedCmdTimeoutMsRef();
    }

    void setParam(const std::string& key, const std::string& value, ParamCallback cb = nullptr) {
        ParamCallback wrapped = [key, value, cb](const std::string& result,
                                                 const std::string& error) {
            if (key == "CMD_TIMEOUT_MS" && result == "OK") {
                try {
                    const int ms = std::stoi(value);
                    if (ms > 0) {
                        detail::assumedCmdTimeoutMsRef().store(ms);
                    }
                } catch (...) {
                }
            }
            if (cb) {
                cb(result, error);
            }
        };
        void* callCtx = detail::makeCallContext(std::move(wrapped));
        sen_controller_set_param(_handle, key.c_str(), value.c_str(),
                                 &detail::paramTrampoline, callCtx);
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

    void multiStartData(const std::vector<SensorProfile*>& profiles, int timeoutMs = 0,
                        int maxDelayDispersionMs = 5, int maxAttempts = 3,
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
    void checkSetupDongle(ParamCallback cb = nullptr) {
        void* callCtx = cb ? detail::makeCallContext(std::move(cb)) : nullptr;
        sen_controller_check_setup_dongle(_handle,
                                          callCtx ? &detail::paramTrampoline : nullptr, callCtx);
    }

    std::future<std::string> getParamAsync(const std::string& key) {
        auto promise = std::make_shared<std::promise<std::string>>();
        std::future<std::string> fut = promise->get_future();
        getParam(key, [promise](const std::string& result, const std::string& error) {
            promise->set_value(detail::paramResultOrError(result, error));
        });
        return fut;
    }
    std::string getParamSync(const std::string& key) {
        detail::throwOnSdkThread();
        std::future<std::string> fut = getParamAsync(key);
        if (fut.wait_for(std::chrono::seconds(20)) == std::future_status::timeout) {
            return "Error: Timeout";
        }
        return fut.get();
    }
    std::future<std::string> setParamAsync(const std::string& key,
                                           const std::string& value) {
        auto promise = std::make_shared<std::promise<std::string>>();
        std::future<std::string> fut = promise->get_future();
        setParam(key, value, [promise](const std::string& result, const std::string& error) {
            promise->set_value(detail::paramResultOrError(result, error));
        });
        return fut;
    }
    std::string setParamSync(const std::string& key, const std::string& value) {
        detail::throwOnSdkThread();
        std::future<std::string> fut = setParamAsync(key, value);
        if (fut.wait_for(std::chrono::seconds(20)) == std::future_status::timeout) {
            return "Error: Timeout";
        }
        return fut.get();
    }
    std::future<std::string> parseBinToCsvAsync(const std::string& binPath,
                                                const std::string& csvPath) {
        auto promise = std::make_shared<std::promise<std::string>>();
        std::future<std::string> fut = promise->get_future();
        parseBinToCsv(binPath, csvPath,
                      [promise](const std::string& result, const std::string& error) {
                          promise->set_value(detail::paramResultOrError(result, error));
                      });
        return fut;
    }
    std::string parseBinToCsvSync(const std::string& binPath, const std::string& csvPath) {
        detail::throwOnSdkThread();
        std::future<std::string> fut = parseBinToCsvAsync(binPath, csvPath);
        if (fut.wait_for(std::chrono::seconds(320)) == std::future_status::timeout) {
            return "Error: Timeout";
        }
        return fut.get();
    }
    std::future<std::string> checkSetupDongleAsync() {
        auto promise = std::make_shared<std::promise<std::string>>();
        std::future<std::string> fut = promise->get_future();
        checkSetupDongle([promise](const std::string& result, const std::string& error) {
            promise->set_value(detail::paramResultOrError(result, error));
        });
        return fut;
    }
    std::string checkSetupDongleSync() {
        detail::throwOnSdkThread();
        std::future<std::string> fut = checkSetupDongleAsync();
        if (fut.wait_for(std::chrono::seconds(320)) == std::future_status::timeout) {
            return "Error: Timeout";
        }
        return fut.get();
    }
    std::future<MultiResultMap> multiStartDataAsync(
        const std::vector<SensorProfile*>& profiles, int timeoutMs = 0,
        int maxDelayDispersionMs = 5, int maxAttempts = 3) {
        auto promise = std::make_shared<std::promise<MultiResultMap>>();
        std::future<MultiResultMap> fut = promise->get_future();
        multiStartData(profiles, timeoutMs, maxDelayDispersionMs, maxAttempts,
                       [promise](const MultiResultMap& result) {
                           promise->set_value(result);
                       });
        return fut;
    }
    MultiResultMap multiStartDataSync(const std::vector<SensorProfile*>& profiles,
                                      int timeoutMs = 0, int maxDelayDispersionMs = 5,
                                      int maxAttempts = 3) {
        detail::throwOnSdkThread();
        std::future<MultiResultMap> fut =
            multiStartDataAsync(profiles, timeoutMs, maxDelayDispersionMs, maxAttempts);
        const int backstopTimeoutMs = timeoutMs > 0 ? timeoutMs : 60000;
        if (fut.wait_for(std::chrono::milliseconds(backstopTimeoutMs) +
                         std::chrono::seconds(10)) == std::future_status::timeout) {
            return MultiResultMap();
        }
        return fut.get();
    }
    std::future<MultiResultMap> multiStopDataAsync(
        const std::vector<SensorProfile*>& profiles, int timeoutMs) {
        auto promise = std::make_shared<std::promise<MultiResultMap>>();
        std::future<MultiResultMap> fut = promise->get_future();
        multiStopData(profiles, timeoutMs, [promise](const MultiResultMap& result) {
            promise->set_value(result);
        });
        return fut;
    }
    MultiResultMap multiStopDataSync(const std::vector<SensorProfile*>& profiles,
                                     int timeoutMs) {
        detail::throwOnSdkThread();
        std::future<MultiResultMap> fut = multiStopDataAsync(profiles, timeoutMs);
        if (fut.wait_for(std::chrono::milliseconds(timeoutMs) +
                         std::chrono::seconds(10)) == std::future_status::timeout) {
            return MultiResultMap();
        }
        return fut.get();
    }
    std::vector<BLEDevice> scan(int periodInMs) {
        detail::throwOnSdkThread();
        auto promise = std::make_shared<std::promise<std::vector<BLEDevice>>>();
        std::future<std::vector<BLEDevice>> fut = promise->get_future();
        ScanOnceCallback cb = [promise](const std::vector<BLEDevice>& devices) {
            promise->set_value(devices);
        };
        void* callCtx = detail::makeCallContext(std::move(cb));
        sen_controller_scan_once(_handle, periodInMs, &detail::scanOnceTrampoline, callCtx);
        if (fut.wait_for(std::chrono::milliseconds(periodInMs) +
                         std::chrono::seconds(45)) == std::future_status::timeout) {
            return std::vector<BLEDevice>();
        }
        return fut.get();
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
        detail::markSdkThread();
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
        detail::markSdkThread();
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
