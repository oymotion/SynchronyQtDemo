#ifndef DEVICESTATE_H
#define DEVICESTATE_H

#include <QMap>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QVector>
#include <memory>
#include <sensorcpp.hpp>

#include "LiveFilter.h"

// Multi-channel circular sample buffer. Lazily sized from the first data
// batch.
struct RingBuffer {
    float sampleRate = 0;
    int channels = 0;
    int length = 0;
    int writeIndex = 0;                 // next write position (oldest sample)
    bool allocated = false;
    QVector<QVector<float>> samples;    // [channel][length]

    // Allocates once; later calls with different parameters are ignored.
    void ensure(int channelCount, float rate, double bufferSeconds);
    // Rebuilds an allocated ring at a new sample rate. No-op when the rate
    // did not change; returns true when the ring was rebuilt.
    bool reallocate(float rate, double bufferSeconds);
    // Writes one batch: all channels share the same positions, then the
    // write index advances once by the longest channel's sample count.
    void appendBatch(const QVector<QVector<float>>& channelValues);
    float latest(int channel) const;
    void clear();
};

// Per-device display state.
class DeviceState {
public:
    explicit DeviceState(sensor::SensorProfile* p);

    sensor::SensorProfile* profile = nullptr;
    QString name;
    QString mac;
    bool isReplay = false;
    bool flowStarted = false;           // connect -> init -> start chain issued

    sensor::DeviceInfo info{};
    bool hasInfo = false;
    int lastPower = -1;

    QMutex bufMutex;                    // guards the ring buffers + impedance
    RingBuffer acc;
    RingBuffer gyro;
    RingBuffer emg;
    RingBuffer eeg;
    RingBuffer ecg;
    RingBuffer brth;
    RingBuffer ppg;
    RingBuffer spo2;
    RingBuffer quat;
    RingBuffer euler;
    QVector<float> emgImpedance;        // per channel, negative = none
    QVector<float> eegImpedance;
    QVector<float> ecgImpedance;
    QVector<float> brthImpedance;

    // Right-side bio panel mode: PPG-capable devices show a fixed EEG +
    // PPG + SpO2 plot set, EEG-capable devices show paged EEG (+ ECG /
    // BRTH) channels, otherwise EMG channels. Falls back to the allocated
    // buffers when no DeviceInfo is available (bin replay).
    enum class BioKind { None, EMG, EEG, PPG };
    BioKind bioKind() const;

    // Latest gesture record (NTF_GEST): gesture / raw gesture / possibility /
    // strength.
    int gesture = -1;
    int rawGesture = -1;
    int possibility = -1;
    int strength = -1;

    // Actual sample-rate accounting (settled once per second by the UI).
    mutable QMutex rateMutex;
    QMap<int, qint64> rateCounts;       // type -> samples in current window
    QMap<int, double> actualRates;      // type -> measured samples/second
    QMap<int, float> nominalRates;      // type -> batch-reported sample rate
    QMap<int, int> nominalChannels;     // type -> batch-reported channel count
    qint64 rateWindowStartMs = 0;
    // Stream-start wall clock (Unix seconds, 0 = unknown) and first-packet
    // delay (ms, 0 = not reported) of the current session, captured from the
    // data batches.
    double streamStartTimeSec = 0;
    quint32 streamDelayMs = 0;

    QMap<QString, int> lostCounts;      // type label -> latest lost sample count

    // Live Filter band selection (bio waveforms only): the UI combo sets the
    // band; each channel batch is filtered before it enters the display rings.
    LiveFilter liveFilter;

    // Cached NTF/FILTER switch states: key -> (enabled, checked)
    QMap<QString, QPair<bool, bool>> ntfStates;
    QMap<QString, QPair<bool, bool>> filterStates;
    // Cached EEG sample-rate control state: the candidates reported by
    // getParam "EEG_SAMPLE_RATE_LIST" and the current bound rate reported by
    // getParam "EEG_SAMPLE_RATE" (0 = none / unknown).
    QVector<int> sampleRateOptions;
    int sampleRateCurrent = 0;
    // Cached EMG/IMU/PPG sample-rate control states, same layout as the EEG
    // state above ("EMG_SAMPLE_RATE_LIST" / "IMU_SAMPLE_RATE_LIST" /
    // "PPG_SAMPLE_RATE_LIST" candidates plus the matching current keys).
    QVector<int> emgSampleRateOptions;
    int emgSampleRateCurrent = 0;
    QVector<int> imuSampleRateOptions;
    int imuSampleRateCurrent = 0;
    QVector<int> ppgSampleRateOptions;
    int ppgSampleRateCurrent = 0;

    // Data entry (called on the worker thread draining the data queue; the
    // queued batch is an owned clone or a zero-copy view, per the Clone Data
    // checkbox). Probes isDataValid() once per batch, gates channels with
    // isChannelEnabled, and reads single values through the single-point
    // accessors.
    void appendData(const sensor::SensorDataView& data);
    // Sample-rate change handling (called after a DeviceInfo update push):
    // rebuilds every allocated ring whose nominal rate changed. Returns true
    // when any ring was rebuilt.
    bool syncSampleRates();
    void clearBuffers();
    void updateActualRates();
    QString buildStatusText() const;
    QString buildRateText() const;

private:
    // NTF_IMU aggregate handling: feeds the acc/gyro/euler/quat channel
    // windows of the parent batch into the matching display rings with
    // per-segment-type rate bookkeeping.
    void appendImuSegments(const sensor::SensorDataView& data);
};

// Short label for a data stream type ("ACC", "GYRO", "EMG", ...).
QString sensorTypeName(int type);

#endif // DEVICESTATE_H
