#include "SdkBridge.h"

sensor::SensorControllerCallbacks SdkBridge::controllerCallbacks() {
    sensor::SensorControllerCallbacks cbs;
    cbs.onScanResult = [this](const std::vector<sensor::BLEDevice>& devices) {
        onScanResult(devices);
    };
    cbs.onEnableChanged = [this](bool enabled) { onEnableChanged(enabled); };
    return cbs;
}

sensor::SensorProfileCallbacks SdkBridge::profileCallbacks() {
    sensor::SensorProfileCallbacks cbs;
    cbs.onData = [this](sensor::SensorProfile* profile, const std::vector<sensor::SensorDataView>& batch) {
        onData(profile, batch);
    };
    cbs.onStateChange = [this](sensor::SensorProfile* profile, int newState) {
        onStateChange(profile, newState);
    };
    cbs.onError = [this](sensor::SensorProfile* profile, const std::string& errorMsg) {
        onError(profile, errorMsg);
    };
    cbs.onPowerChange = [this](sensor::SensorProfile* profile, int power) {
        onPowerChange(profile, power);
    };
    cbs.onAutoReconnect = [this](sensor::SensorProfile* profile, bool hasLastSession,
                                 std::function<void(bool)> answer) {
        onAutoReconnect(profile, hasLastSession, std::move(answer));
    };
    cbs.onDeviceInfoUpdate = [this](sensor::SensorProfile* profile, const sensor::DeviceInfo& info) {
        onDeviceInfoUpdate(profile, info);
    };
    cbs.onDataTransferStateChange = [this](sensor::SensorProfile* profile, bool isTransferring) {
        onDataTransferStateChange(profile, isTransferring);
    };
    return cbs;
}

void SdkBridge::onEnableChanged(bool enabled) {
    emit btEnableChanged(enabled);
}

void SdkBridge::onScanResult(const std::vector<sensor::BLEDevice>& bleDevices) {
    QVector<DeviceEntry> devices;
    devices.reserve(static_cast<int>(bleDevices.size()));
    for (const auto& d : bleDevices) {
        DeviceEntry entry;
        entry.name = QString::fromStdString(d.name);
        entry.mac = QString::fromStdString(d.mac);
        entry.rssi = d.rssi;
        devices.append(entry);
    }
    emit scanResults(devices);
}

void SdkBridge::onError(sensor::SensorProfile* profile, const std::string& errorMsg) {
    if (profile == nullptr) {
        return;
    }
    emit errorOccurred(QString::fromStdString(profile->getDevice().mac),
                       QString::fromStdString(errorMsg));
}

void SdkBridge::onStateChange(sensor::SensorProfile* profile, int newState) {
    if (profile == nullptr) {
        return;
    }
    emit stateChanged(QString::fromStdString(profile->getDevice().mac), newState);
}

void SdkBridge::onData(sensor::SensorProfile* profile,
                       const std::vector<sensor::SensorDataView>& batch) {
    if (profile == nullptr || !dataSink) {
        return;
    }
    // Copy the mac out of the by-value getDevice() result.
    const std::string mac = profile->getDevice().mac;
    if (_cloneData.load()) {
        // Clone Data on: deep copy + enqueue.
        for (const auto& view : batch) {
            if (view.getSamples() == nullptr || view.getSampleCount() <= 0 || view.getChannelCount() <= 0) {
                continue;
            }
            dataSink(mac, QSharedPointer<sensor::SensorData>::create(view.clone()), {});
        }
        return;
    }
    // Clone Data off (zero-copy): enqueue the borrowed view.
    for (const auto& view : batch) {
        if (view.getSamples() == nullptr || view.getSampleCount() <= 0 || view.getChannelCount() <= 0) {
            continue;
        }
        dataSink(mac, nullptr, view);
    }
}

void SdkBridge::onAutoReconnect(sensor::SensorProfile* profile,
                                bool hasLastSession,
                                std::function<void(bool handled)> answer) {
    // Answering true makes the app take over session recovery: the GUI
    // re-drives the connect/init/stream flow and replays the recorded
    // setParam history (restore = a last session existed).
    if (profile == nullptr) {
        answer(false);
        return;
    }
    profile->log(std::string("App: auto reconnect callback received, restore=")
                 + (hasLastSession ? "True" : "False"));
    emit autoReconnectRequested(QString::fromStdString(profile->getDevice().mac),
                                hasLastSession);
    answer(true);
}

void SdkBridge::onPowerChange(sensor::SensorProfile* profile, int power) {
    if (profile == nullptr) {
        return;
    }
    emit powerChanged(QString::fromStdString(profile->getDevice().mac), power);
}

void SdkBridge::onDeviceInfoUpdate(sensor::SensorProfile* profile,
                                   const sensor::DeviceInfo& /*info*/) {
    if (profile == nullptr) {
        return;
    }
    // The GUI re-reads the cached DeviceInfo when handling the queued signal.
    emit deviceInfoUpdated(QString::fromStdString(profile->getDevice().mac));
}

void SdkBridge::onDataTransferStateChange(sensor::SensorProfile* profile,
                                          bool isTransferring) {
    if (profile == nullptr) {
        return;
    }
    // Fires only on a real data-stream on/off change (stream started/stopped,
    // link loss, replay start/EOF).
    emit dataTransferStateChanged(QString::fromStdString(profile->getDevice().mac),
                                  isTransferring);
}
