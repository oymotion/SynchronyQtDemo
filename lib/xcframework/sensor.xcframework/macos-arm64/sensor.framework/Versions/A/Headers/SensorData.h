#import <Foundation/Foundation.h>
#import "SenSdkDefines.h"

NS_ASSUME_NONNULL_BEGIN

@interface BLEDevice : NSObject
@property (nonatomic, copy, readonly) NSString* name;
@property (nonatomic, copy, readonly) NSString* mac;
@property (nonatomic, assign, readonly) int rssi;
- (instancetype)initWithName:(NSString*)name mac:(NSString*)mac rssi:(int)rssi;
@end

@interface Sample : NSObject
@property (atomic, assign) double absTimeStampInSec;
@property (atomic, assign) int sampleIndex;
@property (atomic, assign) int channelIndex;
@property (atomic, assign) BOOL isLost;
@property (atomic, assign) int rawData;
@property (atomic, assign) float data;
@property (atomic, assign) float impedance;
@property (atomic, assign) float saturation;
@end

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
@property (atomic, assign) float EmgMaxSampleRate;
@property (atomic, assign) float EegMaxSampleRate;
@property (atomic, assign) float EcgMaxSampleRate;
@property (atomic, assign) float ConnectionIntervalMs;
@property (atomic, assign) int PeripheralLatency;
@property (atomic, assign) int SupervisionTimeoutMs;
@property (atomic, strong, nullable) NSString* backend;
@end

@interface BinFileInfo : NSObject
@property (nonatomic, copy, readonly) NSString* mac;
@property (nonatomic, copy, readonly) NSString* deviceName;
@property (nonatomic, assign, readonly) double durationSec;
@property (nonatomic, assign, readonly) BOOL valid;
@property (nonatomic, strong, readonly) DeviceInfo* deviceInfo;
- (instancetype)initWithMac:(NSString*)mac deviceName:(NSString*)deviceName
                durationSec:(double)durationSec valid:(BOOL)valid
                 deviceInfo:(DeviceInfo*)deviceInfo;
@end

@interface SensorData : NSObject
@property (nonatomic, copy, readonly) NSString* deviceMac;
@property (nonatomic, copy, readonly) NSString* deviceName;
@property (atomic, assign, readonly) NotifyDataType dataType;
@property (atomic, assign, readonly) int lostPackageCount;
@property (atomic, assign, readonly) float sampleRate;
@property (atomic, assign, readonly) int channelCount;
@property (atomic, assign, readonly) unsigned long long channelMask;
@property (atomic, assign, readonly) int sampleCount;
@property (atomic, assign, readonly) int startSampleIndex;
@property (atomic, assign, readonly) unsigned int startTimeStamp;
@property (atomic, assign, readonly) unsigned int delay;
@property (atomic, assign, readonly) double startTimeSec;

@property (nonatomic, readonly) const void* infoPointer;
@property (nonatomic, readonly) const void* samplesPointer;

@property (nonatomic, strong, readonly) NSData* rawSamples;
@property (nonatomic, strong, readonly) NSData* rawInfo;

- (SensorData*)clone;

@property (nonatomic, strong, readonly) NSArray<NSArray<Sample*>*>* channelSamples;

- (BOOL)isDataValidAtChannel:(int)ch index:(int)i;
- (BOOL)isDataValid;
- (BOOL)isChannelEnabledAtChannel:(int)ch NS_SWIFT_NAME(isChannelEnabled(atChannel:));
- (float)getDataAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getData(atChannel:index:));
- (int)getSampleIndexAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getSampleIndex(atChannel:index:));
- (int)getRawDataAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getRawData(atChannel:index:));
- (float)getImpedanceAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getImpedance(atChannel:index:));
- (float)getSaturationAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getSaturation(atChannel:index:));
- (BOOL)isLostAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(isLost(atChannel:index:));
- (int)getTimeStampInMsAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getTimeStampInMs(atChannel:index:));
- (double)getAbsTimeStampInSecAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getAbsTimeStampInSec(atChannel:index:));
- (nullable Sample*)getChannelSampleAtChannel:(int)ch index:(int)i NS_SWIFT_NAME(getChannelSample(atChannel:index:));
@end

NS_ASSUME_NONNULL_END
