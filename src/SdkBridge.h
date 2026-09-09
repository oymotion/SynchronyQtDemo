#ifndef SDKBRIDGE_H
#define SDKBRIDGE_H

#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <sensorcpp.hpp>

struct DeviceEntry {
    QString name;
    QString mac;
    int rssi = 0;
    // Consecutive scan rounds the device was absent from.
    int missedRounds = 0;
};

Q_DECLARE_METATYPE(DeviceEntry)
Q_DECLARE_METATYPE(QVector<DeviceEntry>)

// Converts SDK callbacks into Qt signals. Connect them with
// Qt::QueuedConnection so all UI work happens on the GUI thread. Must be
// owned via std::shared_ptr.
class SdkBridge : public QObject {
    Q_OBJECT
public:
    SdkBridge() = default;

    // Data sink installed by MainWindow. Fires on the SDK data thread and
    // must only enqueue. Exactly one of owned/borrowed is set, per
    // setCloneData(): in clone mode `owned` carries an owning Data and
    // `borrowed` is empty; in zero-copy mode `borrowed` carries the view.
    std::function<void(const std::string& mac, QSharedPointer<sensor::SensorData> owned,
                       sensor::SensorDataView borrowed)> dataSink;
    // Clone Data switch: on = every batch is deep-copied before enqueueing;
    // off (default) = zero-copy.
    void setCloneData(bool on) { _cloneData.store(on); }

    // Callback tables bound to this bridge.
    sensor::SensorControllerCallbacks controllerCallbacks();
    sensor::SensorProfileCallbacks profileCallbacks();

signals:
    void btEnableChanged(bool enabled);
    void scanResults(QVector<DeviceEntry> devices);
    void stateChanged(QString mac, int state);
    void errorOccurred(QString mac, QString message);
    void powerChanged(QString mac, int power);
    void deviceInfoUpdated(QString mac);
    void dataTransferStateChanged(QString mac, bool isTransferring);
    void autoReconnectRequested(QString mac, bool restore);

private:
    void onEnableChanged(bool enabled);
    void onScanResult(const std::vector<sensor::BLEDevice>& bleDevices);
    void onError(sensor::SensorProfile* profile, const std::string& errorMsg);
    void onStateChange(sensor::SensorProfile* profile, int newState);
    void onData(sensor::SensorProfile* profile, const std::vector<sensor::SensorDataView>& batch);
    void onAutoReconnect(sensor::SensorProfile* profile, bool hasLastSession,
                         std::function<void(bool handled)> answer);
    void onPowerChange(sensor::SensorProfile* profile, int power);
    void onDeviceInfoUpdate(sensor::SensorProfile* profile, const sensor::DeviceInfo& info);
    void onDataTransferStateChange(sensor::SensorProfile* profile, bool isTransferring);

    std::atomic<bool> _cloneData{false};
};

#endif // SDKBRIDGE_H
