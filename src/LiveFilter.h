#ifndef LIVEFILTER_H
#define LIVEFILTER_H

#include <QMap>
#include <QMutex>
#include <QStringList>
#include <QVector>
#include <array>
#include <vector>

// Real-time band filter for the bio waveforms (EMG/EEG/ECG/BRTH/PPG/SpO2).
// The selected band is a UI choice shared by every device; switching the
// band (or a stream sample-rate change) rebuilds the filter state.
class LiveFilter {
public:
    // Band index 0 is Off. bandLabels() feeds the combo box.
    static QStringList bandLabels();

    // UI thread. Out-of-range indexes select Off.
    void setBand(int bandIndex);
    int band() const;

    // Data thread: filters one channel batch in place. Pass-through when Off,
    // when the rate is unknown/invalid, or when the band top exceeds the
    // Nyquist frequency (e.g. the 1 Hz SpO2 stream).
    void apply(int dataType, int channel, QVector<float>& vals, float sampleRate);

    // Drops all designed filters and channel states (device switch / clear).
    void reset();

private:
    struct Biquad {
        double b0, b1, b2, a1, a2;
    };
    struct StreamState {
        int band = 0;
        float rate = 0;
        std::vector<Biquad> sections;
        // Per-section unit-step steady state, used as each channel's initial
        // filter state.
        std::vector<std::array<double, 2>> zi0;
        // channel -> per-section running state.
        QMap<int, std::vector<std::array<double, 2>>> channels;
    };

    // Butterworth bandpass design (order 4). Returns false when the band is
    // invalid for the rate.
    static bool design(int bandIndex, double fs, std::vector<Biquad>& sections);

    mutable QMutex _mutex;
    int _band = 0;
    QMap<int, StreamState> _streams;    // key: SensorData::Type value
};

#endif // LIVEFILTER_H
