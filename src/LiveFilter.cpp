#include "LiveFilter.h"

#include <QMutexLocker>
#include <cmath>
#include <complex>

namespace {
// (label, low Hz, high Hz); index 0 = Off.
const struct { const char* label; double lo; double hi; } kBands[] = {
    {"Off", 0.0, 0.0},
    {"\xCE\xB4 0.5-4Hz", 0.5, 4.0},
    {"\xCE\xB8 4-8Hz", 4.0, 8.0},
    {"\xCE\xB1 8-13Hz", 8.0, 13.0},
    {"\xCE\xB2 13-30Hz", 13.0, 30.0},
    {"\xCE\xB3 30-45Hz", 30.0, 45.0},
};
const int kBandCount = sizeof(kBands) / sizeof(kBands[0]);
const int kOrder = 4;
// Local pi constant.
const double kPi = 3.14159265358979323846;
}

QStringList LiveFilter::bandLabels() {
    QStringList labels;
    for (int i = 0; i < kBandCount; ++i) {
        labels.append(QString::fromUtf8(kBands[i].label));
    }
    return labels;
}

void LiveFilter::setBand(int bandIndex) {
    QMutexLocker lock(&_mutex);
    _band = (bandIndex > 0 && bandIndex < kBandCount) ? bandIndex : 0;
    // A band switch rebuilds every stream's filter state.
    _streams.clear();
}

int LiveFilter::band() const {
    QMutexLocker lock(&_mutex);
    return _band;
}

void LiveFilter::reset() {
    QMutexLocker lock(&_mutex);
    _streams.clear();
}

bool LiveFilter::design(int bandIndex, double fs, std::vector<Biquad>& sections) {
    if (bandIndex <= 0 || bandIndex >= kBandCount || fs <= 0) {
        return false;
    }
    const double lo = kBands[bandIndex].lo;
    const double hi = kBands[bandIndex].hi;
    if (hi >= fs / 2.0) {
        return false;   // band top beyond the Nyquist frequency
    }
    using C = std::complex<double>;
    // Prewarp to the analog domain.
    const double w1 = 2.0 * fs * std::tan(kPi * lo / fs);
    const double w2 = 2.0 * fs * std::tan(kPi * hi / fs);
    const double bw = w2 - w1;
    const double wo = std::sqrt(w1 * w2);

    // Analog lowpass prototype poles.
    std::vector<C> poles;
    std::vector<C> zeros;   // z-plane zeros, filled below
    for (int k = 0; k < kOrder; ++k) {
        const double ang = kPi * (2.0 * k + 1 + kOrder) / (2.0 * kOrder);
        const C p = std::polar(1.0, ang);
        // Lowpass to bandpass pole mapping.
        const C mid = 0.5 * bw * p;
        const C disc = std::sqrt(mid * mid - wo * wo);
        const C sp1 = mid + disc;
        const C sp2 = mid - disc;
        // Bilinear transform.
        const double fs2 = 2.0 * fs;
        poles.push_back((fs2 + sp1) / (fs2 - sp1));
        poles.push_back((fs2 + sp2) / (fs2 - sp2));
    }
    // Prototype zeros at s=0 and at infinity.
    for (int k = 0; k < kOrder; ++k) {
        zeros.push_back(1.0);
        zeros.push_back(-1.0);
    }
    // Overall gain.
    const double fs2 = 2.0 * fs;
    C gain = std::pow(bw, kOrder);
    gain *= std::pow(fs2, kOrder);
    C denom = 1.0;
    for (int k = 0; k < kOrder; ++k) {
        const double ang = kPi * (2.0 * k + 1 + kOrder) / (2.0 * kOrder);
        const C p = std::polar(1.0, ang);
        const C mid = 0.5 * bw * p;
        const C disc = std::sqrt(mid * mid - wo * wo);
        denom *= (fs2 - (mid + disc)) * (fs2 - (mid - disc));
    }
    const double kz = std::real(gain / denom);

    // Group into biquads.
    while (!poles.empty()) {
        const C p = poles.back();
        poles.pop_back();
        size_t best = 0;
        for (size_t i = 1; i < poles.size(); ++i) {
            if (std::abs(poles[i] - std::conj(p)) < std::abs(poles[best] - std::conj(p))) {
                best = i;
            }
        }
        const C pc = poles[best];
        poles.erase(poles.begin() + best);
        C zpair[2];
        for (int j = 0; j < 2; ++j) {
            size_t bz = 0;
            for (size_t i = 1; i < zeros.size(); ++i) {
                const double d = std::min(std::abs(zeros[i] - p), std::abs(zeros[i] - pc));
                const double db = std::min(std::abs(zeros[bz] - p), std::abs(zeros[bz] - pc));
                if (d < db) {
                    bz = i;
                }
            }
            zpair[j] = zeros[bz];
            zeros.erase(zeros.begin() + bz);
        }
        Biquad s{};
        s.a1 = -std::real(p + pc);
        s.a2 = std::real(p * pc);
        s.b0 = 1.0;
        s.b1 = -std::real(zpair[0] + zpair[1]);
        s.b2 = std::real(zpair[0] * zpair[1]);
        sections.push_back(s);
    }
    // Overall gain on the first section.
    sections[0].b0 *= kz;
    sections[0].b1 *= kz;
    sections[0].b2 *= kz;
    return true;
}

void LiveFilter::apply(int dataType, int channel, QVector<float>& vals, float sampleRate) {
    if (vals.isEmpty()) {
        return;
    }
    QMutexLocker lock(&_mutex);
    if (_band == 0) {
        return;
    }
    StreamState& st = _streams[dataType];
    if (st.band != _band || st.rate != sampleRate) {
        // Band or sample-rate change: redesign and reset every channel state.
        st.sections.clear();
        st.zi0.clear();
        st.channels.clear();
        st.band = _band;
        st.rate = sampleRate;
        if (!design(_band, sampleRate, st.sections)) {
            // Invalid band/rate: pass through.
            return;
        }
        // Unit-step steady state per section.
        double u = 1.0;
        for (const auto& s : st.sections) {
            const double g = (s.b0 + s.b1 + s.b2) / (1.0 + s.a1 + s.a2);
            const double y = g * u;
            st.zi0.push_back({y - s.b0 * u, s.b2 * u - s.a2 * y});
            u = y;
        }
    }
    if (st.sections.empty()) {
        return;
    }
    auto& chState = st.channels[channel];
    if (chState.empty()) {
        chState = st.zi0;
    }
    for (auto& v : vals) {
        double x = v;
        for (size_t s = 0; s < st.sections.size(); ++s) {
            const Biquad& q = st.sections[s];
            auto& z = chState[s];
            const double y = q.b0 * x + z[0];
            z[0] = q.b1 * x - q.a1 * y + z[1];
            z[1] = q.b2 * x - q.a2 * y;
            x = y;
        }
        v = static_cast<float>(x);
    }
}
