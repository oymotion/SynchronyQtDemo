#ifndef SDKBRIDGE_H
#define SDKBRIDGE_H

#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SensorController.hpp>
#include <SensorData.hpp>
#include <SensorProfile.hpp>

struct DeviceEntry {
    QString name;
    QString mac;
    int rssi = 0;
};

Q_DECLARE_METATYPE(DeviceEntry)
Q_DECLARE_METATYPE(QVector<DeviceEntry>)

// Converts SDK delegate callbacks into Qt signals. Connect them with
// Qt::QueuedConnection so all UI work happens on the GUI thread. Must be
// owned via std::shared_ptr.
class SdkBridge : public QObject,
                  public sensor::SensorControllerDelegate,
                  public sensor::SensorProfileDelegate {
    Q_OBJECT
public:
    SdkBridge() = default;

    // Data sink installed by MainWindow. Fires on the SDK data thread and
    // must only enqueue. Exactly one of owned/borrowed is set, per
    // setCloneData(): in clone mode `owned` carries an owning SensorData and
    // `borrowed` is empty; in zero-copy mode `borrowed` carries the view.
    std::function<void(const std::string& mac, QSharedPointer<sensor::SensorData> owned,
                       sensor::SensorDataView borrowed)> dataSink;
    // Clone Data switch: on = every batch is deep-copied before enqueueing;
    // off (default) = zero-copy.
    void setCloneData(bool on) { _cloneData.store(on); }

signals:
    void btEnableChanged(bool enabled);
    void scanResults(QVector<DeviceEntry> devices);
    void stateChanged(QString mac, int state);
    void errorOccurred(QString mac, QString message);
    void powerChanged(QString mac, int power);
    void deviceInfoUpdated(QString mac);
    void dataTransferStateChanged(QString mac, bool isTransferring);
    void autoReconnectRequested(QString mac, bool restore);

public:
    // SensorControllerDelegate
    void onSensorControllerEnableChanged(bool enabled) override;
    void onSensorScanResult(std::vector<sensor::BLEDevice> bleDevices) override;

    // SensorProfileDelegate
    void onErrorCallback(std::shared_ptr<sensor::SensorProfile> profile, std::string errorMsg) override;
    void onStateChange(std::shared_ptr<sensor::SensorProfile> profile,
                       sensor::BLEDevice::State newState) override;
    void onSensorNotifyData(std::shared_ptr<sensor::SensorProfile> profile,
                            const std::vector<sensor::SensorDataView>& rawDataList) override;
    void onAutoReconnect(std::shared_ptr<sensor::SensorProfile> profile,
                         bool hasLastSession,
                         std::function<void(bool handled)> answer) override;
    void onPowerChange(std::shared_ptr<sensor::SensorProfile> profile, int power) override;
    void onDeviceInfoUpdate(std::shared_ptr<sensor::SensorProfile> profile,
                            const sensor::DeviceInfo& info) override;
    void onDataTransferStateChange(std::shared_ptr<sensor::SensorProfile> profile,
                                   bool isTransferring) override;

private:
    std::atomic<bool> _cloneData{false};
};

#endif // SDKBRIDGE_H
