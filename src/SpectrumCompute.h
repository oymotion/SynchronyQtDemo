#ifndef SPECTRUMCOMPUTE_H
#define SPECTRUMCOMPUTE_H

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

// Hann-windowed one-sided magnitude spectrum of the snapshot channels;
// amplitudes are normalized by the window gain.
inline void computeSpectrum(const std::vector<std::vector<float>>& channels, float rate,
                     std::vector<float>& freqs, std::vector<std::vector<float>>& mags) {
    freqs.clear();
    mags.clear();
    if (channels.empty() || rate <= 0) {
        return;
    }
    const size_t n = channels[0].size();
    if (n < 16) {
        return;
    }
    size_t nfft = 1;
    while (nfft < n) {
        nfft <<= 1;
    }
    // Hann window over the actual samples.
    // Local pi constant.
    const double pi = 3.14159265358979323846;
    std::vector<double> window(n);
    double winSum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        window[i] = 0.5 - 0.5 * std::cos(2.0 * pi * i / (n - 1));
        winSum += window[i];
    }
    freqs.resize(nfft / 2 + 1);
    for (size_t k = 0; k <= nfft / 2; ++k) {
        freqs[k] = static_cast<float>(k * rate / nfft);
    }
    std::vector<std::complex<double>> buf(nfft);
    for (const auto& ch : channels) {
        const size_t m = std::min(n, ch.size());
        for (size_t i = 0; i < nfft; ++i) {
            buf[i] = i < m ? std::complex<double>(ch[i] * window[i], 0.0)
                           : std::complex<double>(0.0, 0.0);
        }
        // Iterative radix-2 FFT.
        for (size_t i = 1, j = 0; i < nfft; ++i) {
            size_t bit = nfft >> 1;
            for (; j & bit; bit >>= 1) {
                j ^= bit;
            }
            j ^= bit;
            if (i < j) {
                std::swap(buf[i], buf[j]);
            }
        }
        for (size_t len = 2; len <= nfft; len <<= 1) {
            const double ang = -2.0 * pi / len;
            const std::complex<double> wlen(std::cos(ang), std::sin(ang));
            for (size_t i = 0; i < nfft; i += len) {
                std::complex<double> w(1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k) {
                    const auto u = buf[i + k];
                    const auto v = buf[i + k + len / 2] * w;
                    buf[i + k] = u + v;
                    buf[i + k + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
        std::vector<float> row(nfft / 2 + 1);
        for (size_t k = 0; k <= nfft / 2; ++k) {
            row[k] = static_cast<float>(2.0 * std::abs(buf[k]) / std::max(winSum, 1e-12));
        }
        mags.push_back(std::move(row));
    }
}

#endif // SPECTRUMCOMPUTE_H
