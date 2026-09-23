#import <Foundation/Foundation.h>
#import "SenSdkDefines.h"
#import "SensorData.h"
#import "SensorProfile.h"

NS_ASSUME_NONNULL_BEGIN

@protocol SensorControllerDelegate <NSObject>
- (void)onEnableChanged:(bool)enabled;
- (void)onScanResult:(NSArray<BLEDevice*>*)bleDevices;
@end

@interface SensorController : NSObject
@property (atomic, weak, nullable) id<SensorControllerDelegate> delegate;
@property (atomic, assign, readonly) bool isEnable;
@property (atomic, assign, readonly) bool isScanning;

- (void)onSuspend;

+ (instancetype)getInstance;
+ (void)terminate;

+ (uint32_t)capiVersion;

- (BOOL)startScan:(NSTimeInterval)scanInterval;
- (BOOL)stopScan;
- (void)scanOnceWithPeriodMs:(int)periodMs
                  completion:(nullable void (^)(NSArray<BLEDevice*>* devices))completion;

- (SensorProfile*)requireSensor:(NSString*)deviceMac;
- (nullable SensorProfile*)getSensor:(NSString*)deviceMac;
- (NSArray<SensorProfile*>*)getSensors;
- (NSArray<SensorProfile*>*)getConnectedSensors;

- (void)multiStartDataNotification:(NSArray<SensorProfile*>*)sensors
                         timeoutMs:(int)timeoutMs
              maxDelayDispersionMs:(int)maxDelayDispersionMs
                       maxAttempts:(int)maxAttempts
                        completion:(nullable void (^)(NSDictionary<NSString*, NSNumber*>* results,
                                                      NSDictionary<NSString*, NSString*>* errors))completion;
- (void)multiStartDataNotification:(NSArray<SensorProfile*>*)sensors
                        completion:(nullable void (^)(NSDictionary<NSString*, NSNumber*>* results,
                                                      NSDictionary<NSString*, NSString*>* errors))completion;
- (void)multiStopDataNotification:(NSArray<SensorProfile*>*)sensors
                        timeoutMs:(int)timeoutMs
                       completion:(nullable void (^)(NSDictionary<NSString*, NSNumber*>* results,
                                                     NSDictionary<NSString*, NSString*>* errors))completion;
- (void)multiStopDataNotification:(NSArray<SensorProfile*>*)sensors
                       completion:(nullable void (^)(NSDictionary<NSString*, NSNumber*>* results,
                                                     NSDictionary<NSString*, NSString*>* errors))completion;

- (NSString*)getVersion;

- (void)getParam:(NSString*)key
      completion:(nullable void (^)(NSString* result, NSError* _Nullable err))completion;
- (void)setParam:(NSString*)key value:(NSString*)value
      completion:(nullable void (^)(NSString* result, NSError* _Nullable err))completion;

- (void)checkSetupDongle:(nullable void (^)(NSString* result, NSError* _Nullable err))completion;

- (void)log:(nullable NSString*)message level:(nullable NSString*)level;
- (void)log:(nullable NSString*)message;

- (nullable BinFileInfo*)getBinFileInfo:(NSString*)path;
- (nullable SensorProfile*)replayBinFile:(NSString*)path deviceMac:(NSString*)deviceMac
                                realtime:(BOOL)realtime timeout:(NSTimeInterval)timeout;
- (nullable SensorProfile*)replayBinFile:(NSString*)path deviceMac:(NSString*)deviceMac
                                realtime:(BOOL)realtime;
- (NSArray*)multiReplayBinFile:(NSArray<NSString*>*)paths
                       sensors:(NSArray<SensorProfile*>*)sensors
                      realtime:(BOOL)realtime timeout:(NSTimeInterval)timeout;
- (NSArray*)multiReplayBinFile:(NSArray<NSString*>*)paths
                       sensors:(NSArray<SensorProfile*>*)sensors
                      realtime:(BOOL)realtime;
- (NSString*)pauseBinReplay:(NSString*)deviceMac;
- (NSString*)resumeBinReplay:(NSString*)deviceMac;
- (NSString*)stopBinReplay:(NSString*)deviceMac;
- (void)parseBinToCsv:(NSString*)binPath csvPath:(NSString*)csvPath
           completion:(nullable void (^)(NSString* result, NSError* _Nullable err))completion;
@end

NS_ASSUME_NONNULL_END
