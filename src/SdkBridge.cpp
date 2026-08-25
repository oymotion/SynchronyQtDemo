#include "SdkBridge.h"

void SdkBridge::onSensorControllerEnableChanged(bool enabled) {
    emit btEnableChanged(enabled);
}

void SdkBridge::onSensorScanResult(std::vector<sensor::BLEDevice> bleDevices) {
    QVector<DeviceEntry> devices;
    devices.reserve(static_cast<int>(bleDevices.size()));
    for (const auto& d : bleDevices) {
        DeviceEntry entry;
        entry.name = QString::fromUtf8(d.name);
        entry.mac = QString::fromUtf8(d.mac);
        entry.rssi = d.rssi;
        devices.append(entry);
    }
    emit scanResults(devices);
}

void SdkBridge::onErrorCallback(std::shared_ptr<sensor::SensorProfile> profile, std::string errorMsg) {
    if (!profile) {
        return;
    }
    emit errorOccurred(QString::fromUtf8(profile->getDevice().mac),
                       QString::fromStdString(errorMsg));
}

void SdkBridge::onStateChange(std::shared_ptr<sensor::SensorProfile> profile,
                              sensor::BLEDevice::State newState) {
    if (!profile) {
        return;
    }
    emit stateChanged(QString::fromUtf8(profile->getDevice().mac),
                      static_cast<int>(newState));
}

void SdkBridge::onSensorNotifyData(std::shared_ptr<sensor::SensorProfile> profile,
                                   const std::vector<sensor::SensorDataView>& rawDataList) {
    if (!profile || !dataSink) {
        return;
    }
    // Copy the mac out of the by-value getDevice() result.
    const std::string mac = profile->getDevice().mac;
    if (_cloneData.load()) {
        // Clone Data on: deep copy + enqueue.
        for (const auto& rawData : rawDataList) {
            if (rawData.channelSamples == nullptr || rawData.getSampleCount() <= 0 || rawData.getChannelCount() <= 0) {
                continue;
            }
            dataSink(mac, QSharedPointer<sensor::SensorData>::create(rawData.clone()), {});
        }
        return;
    }
    // Clone Data off (zero-copy): enqueue the borrowed view.
    for (const auto& rawData : rawDataList) {
        if (rawData.channelSamples == nullptr || rawData.getSampleCount() <= 0 || rawData.getChannelCount() <= 0) {
            continue;
        }
        dataSink(mac, nullptr, rawData);
    }
}

void SdkBridge::onAutoReconnect(std::shared_ptr<sensor::SensorProfile> profile,
                                bool hasLastSession,
                                std::function<void(bool handled)> answer) {
    // Answering true makes the app take over session recovery: the GUI
    // re-drives the connect/init/stream flow and replays the recorded
    // setParam history (restore = a last session existed).
    if (!profile) {
        answer(false);
        return;
    }
    profile->log(std::string("App: auto reconnect callback received, restore=")
                 + (hasLastSession ? "True" : "False"));
    emit autoReconnectRequested(QString::fromUtf8(profile->getDevice().mac),
                                hasLastSession);
    answer(true);
}

void SdkBridge::onPowerChange(std::shared_ptr<sensor::SensorProfile> profile, int power) {
    if (!profile) {
        return;
    }
    emit powerChanged(QString::fromUtf8(profile->getDevice().mac), power);
}

void SdkBridge::onDeviceInfoUpdate(std::shared_ptr<sensor::SensorProfile> profile,
                                   const sensor::DeviceInfo& /*info*/) {
    if (!profile) {
        return;
    }
    // The GUI re-reads the cached DeviceInfo when handling the queued signal.
    emit deviceInfoUpdated(QString::fromUtf8(profile->getDevice().mac));
}

void SdkBridge::onDataTransferStateChange(std::shared_ptr<sensor::SensorProfile> profile,
                                          bool isTransferring) {
    if (!profile) {
        return;
    }
    // Fires only on a real data-stream on/off change (stream started/stopped,
    // link loss, replay start/EOF).
    emit dataTransferStateChanged(QString::fromUtf8(profile->getDevice().mac),
                                  isTransferring);
}
