#ifndef SENSORDATA_H
#define SENSORDATA_H

#include <cstring>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace sensor{

struct SensorData; // owning counterpart, defined below

// Flat, view-style sample container.
//
// A SensorDataView handed to SensorProfileDelegate::onSensorNotifyData is a
// BORROWED VIEW into SDK-owned memory. The view is a plain copyable bundle of
// pointers + indices: copies stay cheap, but every copy keeps pointing at the
// same SDK memory and follows the same lifetime rules. Consumers that need to
// KEEP the data must call clone(), which deep-copies the valid window
// (channelCount * sampleCount samples) into an owning SensorData.
//
// Layout invariant: samples are packed [channel][sample] with the
// per-channel stride == sampleCount, so
//   channelSamples[channelIndex * sampleCount + sampleIndex]
// is the slot of (channelIndex, sampleIndex). Channels masked out of
// channelMask read as zeros; use isChannelEnabled() to skip them.
//
// Error model (no exceptions): every single-point accessor
// (getChannelSample/getData/getTimeStampInMs/...) is an UNCHECKED raw slot
// read for hot-path performance. Probe isDataValid() once per batch first
// (staleness is batch-atomic -- one (0, 0) probe guarantees the whole
// batch) and keep the indices in range, like the demos do in their data
// callback; reading without the probe may return stale-slot garbage.
struct SensorDataView {

    enum Type{
        NTF_ACC_DATA = 1,
        NTF_GYO_DATA = 2,
        NTF_EULER_DATA = 4,
        NTF_QUATERNION = 5,
        NTF_GEST = 7,
        NTF_EMG_RAW_DATA = 8,
		NTF_MAG_ANGLE_DATA = 13,
        NTF_EEG = 16,
        NTF_ECG = 17,
        NTF_IMPEDANCE = 18,
        NTF_IMU = 19,
        NTF_ADS = 20,
        NTF_BRTH = 21,
        NTF_IMPEDANCE_EXT = 22,
        NTF_SPO2 = 23,
        NTF_PPG = 24,
    };

    // Broadcast metadata shared by every view of one stream. The SDK owns
    // the storage, so a view never copies the metadata and
    // lostPackageCount/delay updates stay visible across views of the same
    // stream. All numeric fields are fixed-width (platform-independent).
    struct Info {
        char deviceMac[18] = {};
        Type dataType = static_cast<Type>(0);
        int32_t lostPackageCount = 0;         // lost PACKAGE count
        float sampleRate = 0;             // float (e.g. GEST 15.625 Hz)
        int32_t channelCount = 0;
        // alignas(8) on the 64-bit members: on 32-bit ABIs (Android
        // x86/armv7) uint64_t/double are only 4-aligned, which would shift
        // every later field -- pin the layout so it is identical on 32- and
        // 64-bit platforms (the bindings hardcode these offsets).
        alignas(8) uint64_t channelMask = 0;
        int32_t sampleCount = 0;          // valid samples per channel; also the per-channel stride
        // Low 32 bits of the steady-clock milliseconds when the stream was
        // started; re-stamped on every stream (re)start; 0 when the stream
        // never started. Also acts as the session tag of a view: a view
        // whose startTimeStamp differs from the current Info value belongs
        // to a previous session and is stale (see isDataValid).
        uint32_t startTimeStamp = 0;
        // Arrival time of the first data packet after the stream start,
        // minus startTimeStamp. 0 until the first packet of the current
        // start arrives.
        uint32_t delay = 0;
        // Wall-clock Unix time in SECONDS (double, LSL-style) when the
        // stream was started; re-stamped together with startTimeStamp on
        // every (re)start. On replay it is restored from the bin record
        // timestamps, so replayed samples report the same absolute
        // timestamps as the capture. 0 = unknown (e.g. a capture without a
        // stream_start record).
        alignas(8) double startTimeSec = 0;
        // Device name from the profile's cached DeviceInfo (stamped once
        // when the stream is created, like deviceMac); empty when unknown.
        // Python SDK 0.9.2 getDeviceName parity.
        char deviceName[32] = {};
    };

    int32_t startSampleIndex = 0;   // absolute sampleIndex of slot (channel, 0)

    // Session tag: snapshot of the stream's Info.startTimeStamp at broadcast
    // time. A stream (re)start re-stamps the shared Info, which immediately
    // invalidates every view of the previous session (isDataValid compares
    // the two) -- without this, a reused ring slot of the new session could
    // alias the old view's expected sampleIndex.
    uint32_t startTimeStamp = 0;

    // BORROWED pointer to the stream's Info (null for a default-constructed
    // view). Valid for the lifetime of the stream; cloned instances point at
    // their own copy.
    const Info* info = nullptr;

    // FIXED CROSS-PLATFORM ABI (little-endian): the flat language bindings
    // read samples straight out of this memory via native pointers
    // (DirectByteBuffer / IntPtr / memoryview), so the layout is part of
    // the public contract and must never drift. All fields are fixed-width.
    //
    //   offset  0  double absTimeStampInSec  (LSL-style: stream-start wall
    //              clock + first-packet delay + sampleIndex/sampleRate,
    //              computed at decode time; 0 when the anchor is unknown)
    //   offset  8  int32  channelIndex
    //   offset 12  int32  sampleIndex
    //   offset 16  int32  rawData
    //   offset 20  float  data
    //   offset 24  float  impedance
    //   offset 28  float  saturation
    //   offset 32  uint8  isLost
    //   offset 33-39 padding
    //   sizeof == 40
    struct Sample {
        // alignas(8): on 32-bit ABIs (Android x86/armv7) a double is only
        // 4-aligned, which would shrink the tail padding and break the fixed
        // 40-byte layout -- pin it.
        alignas(8) double absTimeStampInSec = 0;
        int32_t channelIndex = 0;
        int32_t sampleIndex = 0;
        int32_t rawData = 0;
        float data = 0;
        float impedance = 0;
        float saturation = 0;
        bool isLost = false;
    };
    Sample* channelSamples = nullptr;

    // Public metadata accessors (null-safe: a default-constructed view has
    // no Info and reads as empty).
    const Info* getInfo() const { return info; }
    const char* getDeviceMac() const { return info != nullptr ? info->deviceMac : ""; }
    const char* getDeviceName() const { return info != nullptr ? info->deviceName : ""; }
    Type getDataType() const { return info != nullptr ? info->dataType : static_cast<Type>(0); }
    int getLostPackageCount() const { return info != nullptr ? info->lostPackageCount : 0; }
    float getSampleRate() const { return info != nullptr ? info->sampleRate : 0; }
    int getChannelCount() const { return info != nullptr ? info->channelCount : 0; }
    unsigned long long getChannelMask() const { return info != nullptr ? info->channelMask : 0; }
    int getSampleCount() const { return info != nullptr ? info->sampleCount : 0; }
    uint32_t getStartTimeStamp() const { return info != nullptr ? info->startTimeStamp : 0; }
    uint32_t getDelay() const { return info != nullptr ? info->delay : 0; }
    double getStartTimeSec() const { return info != nullptr ? info->startTimeSec : 0; }

    // Staleness probe: true only when the slot exists, still belongs to the
    // stream session this view was broadcast in (the per-view startTimeStamp
    // matches the stream's current Info.startTimeStamp), and still holds the
    // sample of this batch. Out of range / null buffer / previous session /
    // rewritten slot all return false. This is exactly the condition under
    // which the single-point accessors below return a real value instead of
    // zero, so consumers can detect stale data explicitly. Both indices
    // default to 0, so a bare isDataValid() probes the batch head.
    bool isDataValid(int channelIndex = 0, int sampleIndex = 0) const {
        if (channelSamples == nullptr || info == nullptr
            || channelIndex < 0 || channelIndex >= getChannelCount()
            || sampleIndex < 0 || sampleIndex >= getSampleCount()
            || info->startTimeStamp != startTimeStamp) {
            return false;
        }
        const Sample* s = channelSamples + (size_t)channelIndex * (size_t)getSampleCount() + sampleIndex;
        return s->sampleIndex == startSampleIndex + sampleIndex;
    }

    // True when channel channelIndex is enabled in channelMask; false when
    // channelIndex is out of [0, 64).
    bool isChannelEnabled(int channelIndex) const {
        if (channelIndex < 0 || channelIndex >= 64) {
            return false;
        }
        return ((getChannelMask() >> channelIndex) & 1) != 0;
    }

    // Raw slot pointer, UNCHECKED: no bounds/session/staleness validation is
    // done here. Callers must probe isDataValid() once per batch first
    // (staleness is batch-atomic -- one (0, 0) probe guarantees the whole
    // batch, see isDataValid) and keep the indices in range. This is the
    // hot-path accessor the demos use after their per-batch probe.
    Sample* getChannelSample(int channelIndex, int sampleIndex) {
        return const_cast<Sample*>(static_cast<const SensorDataView*>(this)->getChannelSample(channelIndex, sampleIndex));
    }
    const Sample* getChannelSample(int channelIndex, int sampleIndex) const {
        return channelSamples + (size_t)channelIndex * (size_t)getSampleCount() + sampleIndex;
    }

    // Raw single-field accessors, UNCHECKED like getChannelSample (no
    // isDataValid gate -- hot-path performance): probe isDataValid() once
    // per batch first; without the probe a stale/rewritten slot reads as
    // garbage instead of 0 / false.
    float    getData(int channelIndex, int sampleIndex) const          { return getChannelSample(channelIndex, sampleIndex)->data; }
    // Sample timestamp in milliseconds, computed from the slot's absolute
    // index over the nominal rate (sampleIndex * 1000 / sampleRate); 0 when
    // the rate is unknown. UNCHECKED like the other raw accessors.
    int32_t  getTimeStampInMs(int channelIndex, int sampleIndex) const {
        return info != nullptr && info->sampleRate > 0
            ? (int32_t)((double)getChannelSample(channelIndex, sampleIndex)->sampleIndex * 1000.0 / (double)info->sampleRate)
            : 0;
    }
    int32_t  getSampleIndex(int channelIndex, int sampleIndex) const   { return getChannelSample(channelIndex, sampleIndex)->sampleIndex; }
    int32_t  getRawData(int channelIndex, int sampleIndex) const       { return getChannelSample(channelIndex, sampleIndex)->rawData; }
    float    getImpedance(int channelIndex, int sampleIndex) const     { return getChannelSample(channelIndex, sampleIndex)->impedance; }
    float    getSaturation(int channelIndex, int sampleIndex) const    { return getChannelSample(channelIndex, sampleIndex)->saturation; }
    bool     isLost(int channelIndex, int sampleIndex) const           { return getChannelSample(channelIndex, sampleIndex)->isLost; }

    // Absolute sample timestamp in LSL format (double seconds since the Unix
    // epoch), computed at decode time and stored in the slot: stream-start
    // wall clock (Info.startTimeSec) + first-packet delay (Info.delay) +
    // sampleIndex/sampleRate. The per-sample
    // resolution is 1/sampleRate seconds at any rate -- including rates above
    // 1000 Hz, where the int-ms getTimeStampInMs collapses. UNCHECKED raw
    // access like the other accessors (probe isDataValid once per batch
    // first); a stale slot or an unknown anchor reads as 0.
    double getAbsTimeStampInSec(int channelIndex, int sampleIndex) const {
        return getChannelSample(channelIndex, sampleIndex)->absTimeStampInSec;
    }

    // Deep copy of the valid window into an OWNING SensorData (the only way
    // to keep batch data past the callback). Declared here, defined after
    // SensorData below.
    SensorData clone() const;
};

// The OWNING counterpart of SensorDataView, produced only by clone(): it
// holds its own copies of the sample window AND the Info (a clone never
// points at the stream's shared Info), and the session tag rides along, so a
// clone keeps passing its own checks after the source stream restarts.
// Move-only; all SensorDataView accessors are inherited unchanged.
struct SensorData : public SensorDataView {
    std::unique_ptr<Sample[]> _ownSamples; // non-null only for owning (cloned) instances
    std::unique_ptr<Info> _ownInfo;        // non-null only for owning (cloned) instances

    SensorData() = default;
    SensorData(const SensorData&) = delete;
    SensorData& operator=(const SensorData&) = delete;

    // Move-only, and the inherited info/channelSamples pointers must be
    // re-pointed at THIS object's owned storage when the source was owning
    // (a default move would copy the source's self-pointers and leave them
    // dangling). A non-owning (borrowed-pointer) source keeps its pointers.
    SensorData(SensorData&& o) noexcept { *this = std::move(o); }
    SensorData& operator=(SensorData&& o) noexcept {
        if (this == &o) {
            return *this;
        }
        startSampleIndex = o.startSampleIndex;
        startTimeStamp = o.startTimeStamp;
        _ownInfo = std::move(o._ownInfo);
        _ownSamples = std::move(o._ownSamples);
        info = _ownInfo ? _ownInfo.get() : o.info;
        channelSamples = _ownSamples ? _ownSamples.get() : o.channelSamples;
        o.startSampleIndex = 0;
        o.startTimeStamp = 0;
        o.info = nullptr;
        o.channelSamples = nullptr;
        return *this;
    }
};

// Compact deep copy of the valid window; the result owns its samples AND its
// own copy of the Info.
inline SensorData SensorDataView::clone() const {
    SensorData out;
    out.startSampleIndex = startSampleIndex;
    out.startTimeStamp = startTimeStamp;
    if (info != nullptr) {
        out._ownInfo.reset(new Info(*info));
        out.info = out._ownInfo.get();
    }
    const int channels = getChannelCount();
    const int samples = getSampleCount();
    if (channelSamples != nullptr && channels > 0 && samples > 0) {
        const size_t total = (size_t)channels * (size_t)samples;
        out._ownSamples.reset(new Sample[total]);
        memcpy(out._ownSamples.get(), channelSamples, total * sizeof(Sample));
        out.channelSamples = out._ownSamples.get();
    }
    return out;
}

// Cross-platform ABI guarantees for Sample (see the layout table above).
static_assert(offsetof(SensorDataView::Sample, absTimeStampInSec) == 0, "Sample ABI drift");
static_assert(offsetof(SensorDataView::Sample, channelIndex) == 8, "Sample ABI drift");
static_assert(offsetof(SensorDataView::Sample, sampleIndex) == 12, "Sample ABI drift");
static_assert(offsetof(SensorDataView::Sample, rawData) == 16, "Sample ABI drift");
static_assert(offsetof(SensorDataView::Sample, data) == 20, "Sample ABI drift");
static_assert(offsetof(SensorDataView::Sample, impedance) == 24, "Sample ABI drift");
static_assert(offsetof(SensorDataView::Sample, saturation) == 28, "Sample ABI drift");
static_assert(offsetof(SensorDataView::Sample, isLost) == 32, "Sample ABI drift");
static_assert(sizeof(SensorDataView::Sample) == 40, "Sample ABI drift");

}

#endif // SENSORDATA_H
