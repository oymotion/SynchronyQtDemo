#include "DeviceState.h"

#include <QDateTime>
#include <cstring>

namespace {
const double BIO_BUFFER_SECONDS = 1.0;
const double IMU_BUFFER_SECONDS = 5.0;
}

void RingBuffer::ensure(int channelCount, float rate, double bufferSeconds) {
    if (allocated || channelCount <= 0 || rate <= 0) {
        return;
    }
    channels = channelCount;
    sampleRate = rate;
    length = qMax(1, static_cast<int>(rate * bufferSeconds));
    samples.clear();
    samples.resize(channels);
    for (auto& ch : samples) {
        ch.fill(0.0f, length);
    }
    writeIndex = 0;
    allocated = true;
}

bool RingBuffer::reallocate(float rate, double bufferSeconds) {
    if (!allocated || rate <= 0 || rate == sampleRate) {
        return false;
    }
    sampleRate = rate;
    length = qMax(1, static_cast<int>(rate * bufferSeconds));
    samples.clear();
    samples.resize(channels);
    for (auto& ch : samples) {
        ch.fill(0.0f, length);
    }
    writeIndex = 0;
    return true;
}

void RingBuffer::appendBatch(const QVector<QVector<float>>& channelValues) {
    if (!allocated || channelValues.isEmpty()) {
        return;
    }
    int n = 0;
    for (const auto& vals : channelValues) {
        n = qMax(n, vals.size());
    }
    n = qMin(n, length);
    if (n <= 0) {
        return;
    }
    for (int ch = 0; ch < qMin(channelValues.size(), channels); ++ch) {
        const auto& vals = channelValues[ch];
        const int count = qMin(vals.size(), n);
        // Keep only the tail if a batch exceeds the buffer length.
        const int start = vals.size() - count;
        for (int i = 0; i < count; ++i) {
            samples[ch][(writeIndex + i) % length] = vals[start + i];
        }
    }
    writeIndex = (writeIndex + n) % length;
}

float RingBuffer::latest(int channel) const {
    if (!allocated || channel < 0 || channel >= channels || length <= 0) {
        return 0.0f;
    }
    return samples[channel][(writeIndex + length - 1) % length];
}

void RingBuffer::clear() {
    for (auto& ch : samples) {
        ch.fill(0.0f);
    }
    writeIndex = 0;
}

DeviceState::DeviceState(std::shared_ptr<sensor::SensorProfile> p)
    : profile(std::move(p)) {
    if (profile) {
        auto dev = profile->getDevice();
        name = QString::fromUtf8(dev.name);
        mac = QString::fromUtf8(dev.mac);
    }
    rateWindowStartMs = QDateTime::currentMSecsSinceEpoch();
}

void DeviceState::appendData(const sensor::SensorDataView& data) {
    // Probe the batch once up front; false = invalid batch.
    const bool fresh = data.isDataValid();
    // Rate accounting covers every received type (even ones we do not plot).
    {
        QMutexLocker lock(&rateMutex);
        // Lost-packet bookkeeping (MainWindow only refreshes the label).
        if (data.getLostPackageCount() > 0) {
            lostCounts[sensorTypeName(data.getDataType())] = data.getLostPackageCount();
        }
        if (data.channelSamples != nullptr && data.getSampleCount() > 0 && data.getChannelCount() > 0) {
            qint64 valid = 0;
            for (int i = 0; i < data.getSampleCount(); ++i) {
                if (fresh && data.isChannelEnabled(0) && !data.isLost(0, i)) {
                    ++valid;
                }
            }
            if (valid > 0) {
                rateCounts[data.getDataType()] = rateCounts.value(data.getDataType(), 0) + valid;
            }
        }
        if (data.getSampleRate() > 0) {
            nominalRates[data.getDataType()] = data.getSampleRate();
        }
        if (data.getChannelCount() > 0) {
            nominalChannels[data.getDataType()] = data.getChannelCount();
        }
        // Stream-start wall clock + first-packet delay for the status bar.
        if (data.getStartTimeSec() > 0) {
            streamStartTimeSec = data.getStartTimeSec();
        }
        if (data.getDelay() > 0) {
            streamDelayMs = data.getDelay();
        }
    }

    if (data.getDataType() == sensor::SensorData::NTF_IMU) {
        // Aggregate stream: acc(0-2)/gyro(3-5)/euler(6-8)/quat(9-12) feed the
        // display rings as channel windows of the parent batch.
        appendImuSegments(data);
        return;
    }

    RingBuffer* target = nullptr;
    QVector<float>* impedance = nullptr;
    double seconds = IMU_BUFFER_SECONDS;
    switch (data.getDataType()) {
    case sensor::SensorData::NTF_ACC_DATA:
        target = &acc;
        break;
    case sensor::SensorData::NTF_GYO_DATA:
        target = &gyro;
        break;
    case sensor::SensorData::NTF_QUATERNION:
        target = &quat;
        break;
    case sensor::SensorData::NTF_EULER_DATA:
        target = &euler;
        break;
    case sensor::SensorData::NTF_GEST: {
        // Masked-out channels are skipped entirely.
        if (fresh && data.isChannelEnabled(0) && data.getSampleCount() > 0) {
            const auto* s = data.getChannelSample(0, data.getSampleCount() - 1);
            QMutexLocker lock(&bufMutex);
            gesture = static_cast<int>(s->data);
            rawGesture = s->rawData;
            possibility = static_cast<int>(s->impedance);
            strength = static_cast<int>(s->saturation);
        }
        return;
    }
    case sensor::SensorData::NTF_EMG_RAW_DATA:
        target = &emg;
        impedance = &emgImpedance;
        seconds = BIO_BUFFER_SECONDS;
        break;
    case sensor::SensorData::NTF_EEG:
        target = &eeg;
        impedance = &eegImpedance;
        // PPG devices plot EEG (fp1/fp2) in the shared 5s bio buffer set
        // instead of the 1s EEG-mode buffer.
        seconds = bioKind() == BioKind::PPG ? IMU_BUFFER_SECONDS : BIO_BUFFER_SECONDS;
        break;
    case sensor::SensorData::NTF_PPG:
        target = &ppg;
        seconds = IMU_BUFFER_SECONDS;
        break;
    case sensor::SensorData::NTF_SPO2:
        target = &spo2;
        seconds = IMU_BUFFER_SECONDS;
        break;
    case sensor::SensorData::NTF_ECG:
        target = &ecg;
        impedance = &ecgImpedance;
        seconds = BIO_BUFFER_SECONDS;
        break;
    case sensor::SensorData::NTF_BRTH:
        target = &brth;
        impedance = &brthImpedance;
        seconds = BIO_BUFFER_SECONDS;
        break;
    default:
        break;
    }
    if (target == nullptr) {
        return;
    }

    QMutexLocker lock(&bufMutex);
    target->ensure(data.getChannelCount(), data.getSampleRate(), seconds);
    if (!target->allocated) {
        return;
    }
    // Masked-out channels read as 0.0f.
    // Bio types (EMG/EEG/ECG/BRTH/PPG/SpO2) pass through the Live Filter
    // before entering the ring.
    const bool bioTarget = impedance != nullptr || target == &ppg || target == &spo2;
    QVector<QVector<float>> channelValues;
    channelValues.reserve(data.getChannelCount());
    for (int ch = 0; ch < data.getChannelCount(); ++ch) {
        const bool maskedIn = data.isChannelEnabled(ch);
        QVector<float> vals;
        vals.reserve(data.getSampleCount());
        for (int i = 0; i < data.getSampleCount(); ++i) {
            vals.append((fresh && maskedIn) ? data.getData(ch, i) : 0.0f);
        }
        if (bioTarget) {
            liveFilter.apply(data.getDataType(), ch, vals, data.getSampleRate());
        }
        channelValues.append(vals);
    }
    target->appendBatch(channelValues);

    if (impedance != nullptr) {
        for (int ch = 0; ch < data.getChannelCount(); ++ch) {
            if (!fresh || !data.isChannelEnabled(ch)) {
                continue; // corrupted batch / masked-out channel
            }
            while (impedance->size() <= ch) {
                impedance->append(-1.0f);
            }
            (*impedance)[ch] = data.getImpedance(ch, data.getSampleCount() - 1);
        }
    }
}

void DeviceState::appendImuSegments(const sensor::SensorDataView& data) {
    static const struct { sensor::SensorData::Type type; int offset; int count; } segs[] = {
        {sensor::SensorData::NTF_ACC_DATA, 0, 3},
        {sensor::SensorData::NTF_GYO_DATA, 3, 3},
        {sensor::SensorData::NTF_EULER_DATA, 6, 3},
        {sensor::SensorData::NTF_QUATERNION, 9, 4},
    };
    const int sampleCount = data.getSampleCount();
    // Probe the parent batch once up front; false = invalid batch.
    const bool fresh = data.isDataValid();
    for (const auto& seg : segs) {
        if (data.getChannelCount() < seg.offset + seg.count || data.channelSamples == nullptr) {
            continue;
        }
        // Per-segment-type rate bookkeeping; lost-packet bookkeeping stays
        // with the top-level batch.
        {
            QMutexLocker lock(&rateMutex);
            qint64 valid = 0;
            for (int i = 0; i < sampleCount; ++i) {
                if (fresh && data.isChannelEnabled(0) && !data.isLost(seg.offset, i)) {
                    ++valid;
                }
            }
            if (valid > 0) {
                rateCounts[seg.type] = rateCounts.value(seg.type, 0) + valid;
            }
            if (data.getSampleRate() > 0) {
                nominalRates[seg.type] = data.getSampleRate();
            }
            nominalChannels[seg.type] = seg.count;
        }

        RingBuffer* target = nullptr;
        switch (seg.type) {
        case sensor::SensorData::NTF_ACC_DATA: target = &acc; break;
        case sensor::SensorData::NTF_GYO_DATA: target = &gyro; break;
        case sensor::SensorData::NTF_QUATERNION: target = &quat; break;
        case sensor::SensorData::NTF_EULER_DATA: target = &euler; break;
        default: break;
        }
        if (target == nullptr) {
            continue;
        }
        QMutexLocker lock(&bufMutex);
        target->ensure(seg.count, data.getSampleRate(), IMU_BUFFER_SECONDS);
        if (!target->allocated) {
            continue;
        }
        // Masked-out channels read as 0.0f.
        QVector<QVector<float>> channelValues;
        channelValues.reserve(seg.count);
        for (int ch = 0; ch < seg.count; ++ch) {
            const bool maskedIn = data.isChannelEnabled(ch);
            QVector<float> vals;
            vals.reserve(sampleCount);
            for (int i = 0; i < sampleCount; ++i) {
                vals.append((fresh && maskedIn) ? data.getData(seg.offset + ch, i) : 0.0f);
            }
            channelValues.append(vals);
        }
        target->appendBatch(channelValues);
    }
}

bool DeviceState::syncSampleRates() {
    if (!hasInfo) {
        return false;
    }
    QMutexLocker lock(&bufMutex);
    bool changed = false;
    // PPG devices plot EEG (fp1/fp2) in the shared 5s bio buffer set instead
    // of the 1s EEG-mode buffer (same rule as appendData).
    changed |= eeg.reallocate(info.EEGSampleRate,
                              bioKind() == BioKind::PPG ? IMU_BUFFER_SECONDS : BIO_BUFFER_SECONDS);
    changed |= ecg.reallocate(info.ECGSampleRate, BIO_BUFFER_SECONDS);
    changed |= acc.reallocate(info.AccSampleRate, IMU_BUFFER_SECONDS);
    changed |= gyro.reallocate(info.GyroSampleRate, IMU_BUFFER_SECONDS);
    changed |= euler.reallocate(info.EulerSampleRate, IMU_BUFFER_SECONDS);
    changed |= quat.reallocate(info.QuatSampleRate, IMU_BUFFER_SECONDS);
    return changed;
}

void DeviceState::clearBuffers() {
    QMutexLocker lock(&bufMutex);
    acc.clear();
    gyro.clear();
    emg.clear();
    eeg.clear();
    ecg.clear();
    brth.clear();
    ppg.clear();
    spo2.clear();
    quat.clear();
    euler.clear();
    emgImpedance.fill(-1.0f);
    eegImpedance.fill(-1.0f);
    ecgImpedance.fill(-1.0f);
    brthImpedance.fill(-1.0f);
    gesture = rawGesture = possibility = strength = -1;
}

DeviceState::BioKind DeviceState::bioKind() const {
    // PPG-capable devices take precedence (their EEG fp1/fp2 channels are
    // shown inside the PPG plot set, not in the paged EEG layout).
    if (info.PpgSampleRate > 0 || ppg.allocated) {
        return BioKind::PPG;
    }
    if (info.EEGChannelCount > 0 || eeg.allocated) {
        return BioKind::EEG;
    }
    if (info.EMGChannelCount > 0 || emg.allocated) {
        return BioKind::EMG;
    }
    return BioKind::None;
}

void DeviceState::updateActualRates() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lock(&rateMutex);
    const double elapsed = (now - rateWindowStartMs) / 1000.0;
    if (elapsed <= 0.0) {
        return;
    }
    actualRates.clear();
    for (auto it = rateCounts.constBegin(); it != rateCounts.constEnd(); ++it) {
        actualRates[it.key()] = it.value() / elapsed;
    }
    rateCounts.clear();
    rateWindowStartMs = now;
}

QString DeviceState::buildStatusText() const {
    // Only claim "Connected" once the connect chain actually reached Ready;
    // a freshly created DeviceState (connect still in flight) must not
    // overwrite the "Connecting: ..." status with a fake Connected.
    QString head = isReplay ? QStringLiteral("Replaying: %1").arg(name)
                 : flowStarted ? QStringLiteral("Connected: %1").arg(name)
                               : QStringLiteral("Not Connected");
    QMutexLocker lock(&rateMutex);
    // Fixed display order; only types that have delivered data are shown.
    const QList<QPair<int, QString>> order = {
        {sensor::SensorData::NTF_ACC_DATA, QStringLiteral("ACC")},
        {sensor::SensorData::NTF_GYO_DATA, QStringLiteral("Gyro")},
        {sensor::SensorData::NTF_IMU, QStringLiteral("IMU")},
        {sensor::SensorData::NTF_QUATERNION, QStringLiteral("Quat")},
        {sensor::SensorData::NTF_EULER_DATA, QStringLiteral("Euler")},
        {sensor::SensorData::NTF_EMG_RAW_DATA, QStringLiteral("EMG")},
        {sensor::SensorData::NTF_EEG, QStringLiteral("EEG")},
        {sensor::SensorData::NTF_PPG, QStringLiteral("PPG")},
        {sensor::SensorData::NTF_SPO2, QStringLiteral("SpO2")},
        {sensor::SensorData::NTF_ECG, QStringLiteral("ECG")},
        {sensor::SensorData::NTF_BRTH, QStringLiteral("BRTH")},
        {sensor::SensorData::NTF_GEST, QStringLiteral("GEST")},
    };
    QStringList parts{head};
    for (const auto& entry : order) {
        const float rate = nominalRates.value(entry.first, 0);
        const int ch = nominalChannels.value(entry.first, 0);
        if (rate <= 0 && ch <= 0) {
            continue;
        }
        if (ch > 0) {
            parts.append(QStringLiteral("%1 %2ch @ %3Hz").arg(entry.second).arg(ch).arg(rate));
        } else {
            parts.append(QStringLiteral("%1 @ %2Hz").arg(entry.second).arg(rate));
        }
    }
    return parts.join(QStringLiteral(" | "));
}

QString DeviceState::buildRateText() const {
    QMutexLocker lock(&rateMutex);
    const QList<QPair<int, QString>> order = {
        {sensor::SensorData::NTF_ACC_DATA, QStringLiteral("ACC")},
        {sensor::SensorData::NTF_GYO_DATA, QStringLiteral("Gyro")},
        {sensor::SensorData::NTF_IMU, QStringLiteral("IMU")},
        {sensor::SensorData::NTF_QUATERNION, QStringLiteral("Quat")},
        {sensor::SensorData::NTF_EULER_DATA, QStringLiteral("Euler")},
        {sensor::SensorData::NTF_EMG_RAW_DATA, QStringLiteral("EMG")},
        {sensor::SensorData::NTF_EEG, QStringLiteral("EEG")},
        {sensor::SensorData::NTF_PPG, QStringLiteral("PPG")},
        {sensor::SensorData::NTF_SPO2, QStringLiteral("SpO2")},
        {sensor::SensorData::NTF_ECG, QStringLiteral("ECG")},
        {sensor::SensorData::NTF_BRTH, QStringLiteral("BRTH")},
        {sensor::SensorData::NTF_GEST, QStringLiteral("GEST")},
    };
    QStringList entries;
    for (const auto& entry : order) {
        if (!actualRates.contains(entry.first)) {
            continue;
        }
        const double actual = actualRates[entry.first];
        const float nominal = nominalRates.value(entry.first, 0);
        entries.append(QStringLiteral("%1 %2 / %3Hz")
                           .arg(entry.second)
                           .arg(actual, 0, 'f', 1)
                           .arg(nominal > 0 ? QString::number(nominal) : QStringLiteral("--")));
    }
    if (streamStartTimeSec > 0) {
        // Local wall clock with milliseconds.
        const qint64 startMs = (qint64)(streamStartTimeSec * 1000.0);
        entries.append(QStringLiteral("start %1")
                           .arg(QDateTime::fromMSecsSinceEpoch(startMs)
                                    .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))));
    }
    if (streamDelayMs > 0) {
        entries.append(QStringLiteral("delay %1ms").arg(streamDelayMs));
    }
    return entries.isEmpty() ? QString() : QStringLiteral("Actual: ") + entries.join(QStringLiteral(" | "));
}

QString sensorTypeName(int type) {
    switch (type) {
    case sensor::SensorData::NTF_ACC_DATA: return QStringLiteral("ACC");
    case sensor::SensorData::NTF_GYO_DATA: return QStringLiteral("GYRO");
    case sensor::SensorData::NTF_EULER_DATA: return QStringLiteral("EULER");
    case sensor::SensorData::NTF_QUATERNION: return QStringLiteral("QUAT");
    case sensor::SensorData::NTF_GEST: return QStringLiteral("GEST");
    case sensor::SensorData::NTF_EMG_RAW_DATA: return QStringLiteral("EMG");
    case sensor::SensorData::NTF_MAG_ANGLE_DATA: return QStringLiteral("MAG");
    case sensor::SensorData::NTF_EEG: return QStringLiteral("EEG");
    case sensor::SensorData::NTF_PPG: return QStringLiteral("PPG");
    case sensor::SensorData::NTF_SPO2: return QStringLiteral("SPO2");
    case sensor::SensorData::NTF_ECG: return QStringLiteral("ECG");
    case sensor::SensorData::NTF_IMPEDANCE: return QStringLiteral("IMP");
    case sensor::SensorData::NTF_IMU: return QStringLiteral("IMU");
    case sensor::SensorData::NTF_ADS: return QStringLiteral("ADS");
    case sensor::SensorData::NTF_BRTH: return QStringLiteral("BRTH");
    case sensor::SensorData::NTF_IMPEDANCE_EXT: return QStringLiteral("IMP_EXT");
    default: return QStringLiteral("TYPE_%1").arg(type);
    }
}
