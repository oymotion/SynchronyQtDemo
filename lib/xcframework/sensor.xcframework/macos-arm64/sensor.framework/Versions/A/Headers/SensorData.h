//
//  SensorData.h
//  sensorobjc — data model objects, mirroring the Android binding / Python
//  SDK naming over the sen_capi sample ABI.
//

#import <Foundation/Foundation.h>
#import "SenSdkDefines.h"

NS_ASSUME_NONNULL_BEGIN

/// A scanned BLE device (no CoreBluetooth dependency — the OS BLE stack
/// lives inside the SDK).
@interface BLEDevice : NSObject
@property (nonatomic, copy, readonly) NSString* name;
@property (nonatomic, copy, readonly) NSString* mac;
@property (nonatomic, assign, readonly) int rssi;
- (instancetype)initWithName:(NSString*)name mac:(NSString*)mac rssi:(int)rssi;
@end

@interface Sample : NSObject
/// LSL-style absolute timestamp (Unix seconds, double): stream-start wall
/// clock + first-packet delay + sampleIndex/sampleRate, computed at decode
/// time; 0 when the anchor is unknown.
@property (atomic, assign) double absTimeStampInSec;
@property (atomic, assign) int sampleIndex;
@property (atomic, assign) int channelIndex;
@property (atomic, assign) BOOL isLost;
@property (atomic, assign) int rawData;
@property (atomic, assign) float data;
@property (atomic, assign) float impedance;
@property (atomic, assign) float saturation;
@end

/// Mirror of sen_capi sen_device_info_t.
@interface DeviceInfo : NSObject
@property (atomic, strong, nullable) NSString* deviceName;
@property (atomic, strong, nullable) NSString* modelName;
@property (atomic, strong, nullable) NSString* hardwareVersion;
@property (atomic, strong, nullable) NSString* firmwareVersion;
@property (atomic, assign) int mtuSize;
@property (atomic, assign) bool isMtuFine;
@property (atomic, assign) int EMGGain;
@property (atomic, assign) int EEGGain;
@property (atomic, assign) int ECGGain;
@property (atomic, assign) int EEGChannelCount;
@property (atomic, assign) int ECGChannelCount;
@property (atomic, assign) int BRTHChannelCount;
@property (atomic, assign) int AccChannelCount;
@property (atomic, assign) int GyroChannelCount;
@property (atomic, assign) int PPGChannelCount;
@property (atomic, assign) int Spo2ChannelCount;
@property (atomic, assign) int QuatChannelCount;
@property (atomic, assign) int EulerChannelCount;
@property (atomic, assign) int MagAngleChannelCount;
@property (atomic, assign) int ImpeChannelCount;
@property (atomic, assign) int EmgChannelCount;
@property (atomic, assign) int ImuChannelCount;
@property (atomic, assign) float EmgSampleRate;
@property (atomic, assign) float EegSampleRate;
@property (atomic, assign) float EcgSampleRate;
@property (atomic, assign) float BrthSampleRate;
@property (atomic, assign) float AccSampleRate;
@property (atomic, assign) float GyroSampleRate;
@property (atomic, assign) float QuatSampleRate;
@property (atomic, assign) float EulerSampleRate;
@property (atomic, assign) float MagAngleSampleRate;
@property (atomic, assign) float PpgSampleRate;
@property (atomic, assign) float Spo2SampleRate;
@property (atomic, assign) float ImpeSampleRate;
@property (atomic, assign) float ImuSampleRate;
/// Max sample rates from the device capability queries; 0 = not reported or
/// not supported.
@property (atomic, assign) float EmgMaxSampleRate;
@property (atomic, assign) float EegMaxSampleRate;
@property (atomic, assign) float EcgMaxSampleRate;
/// Link connection interval in ms; 0 = unknown (the C++ BLE backends do not
/// expose it).
@property (atomic, assign) float ConnectionIntervalMs;
/// Peripheral latency in events; -1 = unknown (0 is a legal value).
@property (atomic, assign) int PeripheralLatency;
/// Supervision timeout in ms; 0 = unknown.
@property (atomic, assign) int SupervisionTimeoutMs;
@end

/// Summary of a raw BLE bin capture (sen_bin_file_info_t mirror).
@interface BinFileInfo : NSObject
@property (nonatomic, copy, readonly) NSString* mac;
@property (nonatomic, copy, readonly) NSString* deviceName;
@property (nonatomic, assign, readonly) double durationSec;
@property (nonatomic, assign, readonly) BOOL valid;
/// DeviceInfo decoded from the first CONFIG record of the capture;
/// zero/empty when the file has no decodable config.
@property (nonatomic, strong, readonly) DeviceInfo* deviceInfo;
- (instancetype)initWithMac:(NSString*)mac deviceName:(NSString*)deviceName
                durationSec:(double)durationSec valid:(BOOL)valid
                 deviceInfo:(DeviceInfo*)deviceInfo;
@end

/// One broadcast batch of a single stream (sen_data_view_t mirror).
///
/// Memory model — zero-copy borrow: the instance holds the borrowed
/// sen_data_view_t pointers directly, with no per-batch copy. rawSamples /
/// rawInfo are no-copy NSData views over the same memory. The metadata
/// properties (dataType .. delay) read live through infoPointer, so
/// lostPackageCount / delay updates remain visible past the data callback.
///
/// LIFETIME:
/// 1. The borrowed sample content goes stale once it is overwritten by
///    newer data (the window is on the order of seconds). A slot whose
///    sampleIndex != startSampleIndex + index is stale — probe with
///    isDataValidAtChannel:index: (the no-arg isDataValid probes the batch
///    head).
/// 2. The borrow ends at stream teardown: stopDataNotification / disconnect
///    / reconnect re-init / end of a bin replay. Do not access the instance
///    afterwards. To keep data across callbacks or events, call clone — the
///    clone owns copies of both blocks and is fully decoupled from SDK
///    memory.
///
/// Layout invariant (fixed ABI, little-endian, 40 bytes per sen_sample_t):
/// samples are packed [channel][sample] with the per-channel stride ==
/// sampleCount, so slot (channel, index) sits at byte offset
/// (channel * sampleCount + index) * 40. Channels masked out of channelMask
/// carry zeroed slots.
///
/// The get*AtChannel:index: accessors are the object-free hot path; they
/// return zero on out-of-range slots (no exceptions).
@interface SensorData : NSObject
// deviceMac / deviceName are fixed per stream; decoded once at construction.
@property (nonatomic, copy, readonly) NSString* deviceMac;
@property (nonatomic, copy, readonly) NSString* deviceName;
// The metadata below reads live through the borrowed sen_data_info_t
// (lostPackageCount / delay keep updating past the callback).
@property (atomic, assign, readonly) NotifyDataType dataType;
@property (atomic, assign, readonly) int lostPackageCount; // lost PACKAGE count
@property (atomic, assign, readonly) float sampleRate;
@property (atomic, assign, readonly) int channelCount;
@property (atomic, assign, readonly) unsigned long long channelMask;
@property (atomic, assign, readonly) int sampleCount;      // valid samples per channel
@property (atomic, assign, readonly) int startSampleIndex; // absolute index of slot (ch, 0)
// Low 32 bits of the steady-clock ms when the stream-start command was
// issued (the bin record ts on replay); re-stamped on every (re)start.
@property (atomic, assign, readonly) unsigned int startTimeStamp;
// First raw packet arrival minus startTimeStamp; 0 until the first packet
// of the current start.
@property (atomic, assign, readonly) unsigned int delay;
// Wall-clock Unix time in seconds (double) at stream start (on replay
// restored from the bin record timestamps; 0 = unknown). This is the
// anchor of every sample's absTimeStampInSec.
@property (atomic, assign, readonly) double startTimeSec;

// Raw borrowed pointers (NULL when the view carries no payload): direct
// reference/copy source for consumers implementing their own clone. Same
// lifetime rules as above. infoPointer is a sen_data_info_t (see sen_capi.h),
// samplesPointer is channelCount*sampleCount sen_sample_t entries (40 bytes
// each, little-endian fixed ABI).
@property (nonatomic, readonly) const void* infoPointer;
@property (nonatomic, readonly) const void* samplesPointer;

// Zero-copy NSData views over the borrowed blocks (empty when absent).
// rawInfo is sizeof(sen_data_info_t) bytes; rawSamples is
// channelCount*sampleCount*40 bytes (the length captured at construction).
@property (nonatomic, strong, readonly) NSData* rawSamples;
@property (nonatomic, strong, readonly) NSData* rawInfo;

/// Deep copy that DECOUPLES from SDK memory: one bulk copy of the sample
/// block plus a copy of the Info, both owned by the clone. Safe to keep
/// across callbacks and past stream teardown. The copy is NOT stale-checked
/// (a rewritten slot is copied as-is).
- (SensorData*)clone;

/// Lazily materialized [channel][sample] Sample objects (stale slots read as
/// zeroed samples). Prefer the get*AtChannel accessors on hot paths.
@property (nonatomic, strong, readonly) NSArray<NSArray<Sample*>*>* channelSamples;

/// Non-throwing probe (mirrors C++ SensorDataView::isDataValid): true when
/// slot (ch, i) is readable — in range, a payload is present, the view still
/// belongs to the stream's current session, and the slot is not stale.
/// Channels masked out of channelMask carry zeroed slots and therefore
/// probe false.
- (BOOL)isDataValidAtChannel:(int)ch index:(int)i;
/// No-arg form probing the batch head (slot (0, 0)).
- (BOOL)isDataValid;
/// True when channel ch is enabled in channelMask; false when ch is out of
/// [0, 64).
- (BOOL)isChannelEnabledAtChannel:(int)ch NS_SWIFT_NAME(isChannelEnabled(atChannel:));
// NS_SWIFT_NAME pins the Swift signatures (the importer would otherwise
// mangle the get-prefixed names, e.g. getDataAtChannel: -> getAtChannel:).
- (float)getDataAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getData(atChannel:index:));
- (int)getSampleIndexAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getSampleIndex(atChannel:index:));
- (int)getRawDataAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getRawData(atChannel:index:));
- (float)getImpedanceAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getImpedance(atChannel:index:));
- (float)getSaturationAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getSaturation(atChannel:index:));
- (BOOL)isLostAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(isLost(atChannel:index:));
/// Sample timestamp in milliseconds, computed from the slot's absolute
/// index over the nominal rate (sampleIndex * 1000 / sampleRate); 0 when
/// the rate is unknown.
- (int)getTimeStampInMsAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getTimeStampInMs(atChannel:index:));
/// Absolute sample timestamp in LSL format (double seconds since the Unix
/// epoch), computed at decode time and stored in the slot; 0 on
/// out-of-range / stale slot or when the anchor is unknown.
- (double)getAbsTimeStampInSecAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getAbsTimeStampInSec(atChannel:index:));
/// Materializes one Sample; nil on out-of-range or stale slot.
- (nullable Sample*)getChannelSampleAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getChannelSample(atChannel:index:));
@end

NS_ASSUME_NONNULL_END
