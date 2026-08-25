//
//  SensorController.h
//  sensorobjc — scan controller singleton, mirroring the Android binding /
//  Python SDK SensorController over the sen_capi controller handle.
//
//  Threading: delegate calls fire on internal SDK threads.
//

#import <Foundation/Foundation.h>
#import "SenSdkDefines.h"
#import "SensorData.h"
#import "SensorProfile.h"

NS_ASSUME_NONNULL_BEGIN

@protocol SensorControllerDelegate <NSObject>
/// Bluetooth adapter power state changes (Python onEnableCallback).
- (void)onEnableChanged:(bool)enabled;
/// Device list updates while scanning (Python onDeviceFoundCallback).
- (void)onScanResult:(NSArray<BLEDevice*>*)bleDevices;
@end

@interface SensorController : NSObject
@property (atomic, weak, nullable) id<SensorControllerDelegate> delegate;
@property (atomic, assign, readonly) bool isEnable;
@property (atomic, assign, readonly) bool isScanning;

/// Tells the SDK the app went to the background (iOS scenePhase .background /
/// Android onStop): every profile's pending records are flushed to the open
/// capture file and pending log records are flushed to disk. Capture and
/// streaming keep running; stopping them is the app's decision.
- (void)onSuspend;

+ (instancetype)getInstance;
/// Destroys the shared controller and terminates the whole SDK: all scans
/// and connections stop and every profile handle is invalidated. Call once
/// at application shutdown; repeated calls are safe.
+ (void)terminate;

/// SEN_CAPI_VERSION of the linked library, so the caller can detect a
/// wrapper/library mismatch at runtime.
+ (uint32_t)capiVersion;

/// scanInterval in seconds (period between repeated result pushes).
- (BOOL)startScan:(NSTimeInterval)scanInterval;
- (BOOL)stopScan;

/// Creates and registers a profile when the MAC is unknown, so it also works
/// for devices not discovered by the scanner.
- (SensorProfile*)requireSensor:(NSString*)deviceMac;
/// nil when the MAC is unknown (no scan result / no prior requireSensor).
- (nullable SensorProfile*)getSensor:(NSString*)deviceMac;
- (NSArray<SensorProfile*>*)getSensors;
/// Profiles whose link state is Connected or Ready.
- (NSArray<SensorProfile*>*)getConnectedSensors;

/// Synchronized multi-device stream start: the start writes of all valid
/// devices are released together, then the first-packet-delay spread across
/// them is validated (pass a negative maxDelayDispersionMs to skip the
/// check), retrying up to maxAttempts times. Already-streaming devices are
/// restarted. Each sensor must be Ready and initialized; devices that fail
/// validation get their own failure entry and do not affect the others.
/// The completion fires exactly once on an internal SDK thread with the
/// per-device results (mac -> @(BOOL)) and failure reasons (mac -> error
/// string, failed devices only); nil = fire-and-forget.
- (void)multiStartDataNotification:(NSArray<SensorProfile*>*)sensors
                         timeoutMs:(int)timeoutMs
              maxDelayDispersionMs:(int)maxDelayDispersionMs
                       maxAttempts:(int)maxAttempts
                        completion:(nullable void (^)(NSDictionary<NSString*, NSNumber*>* results,
                                                      NSDictionary<NSString*, NSString*>* errors))completion;
/// Synchronized multi-device stream stop: the stop writes of all valid
/// devices are released together. Non-streaming devices report success
/// immediately. The completion contract matches
/// multiStartDataNotification:timeoutMs:maxDelayDispersionMs:maxAttempts:completion:
/// (nil = fire-and-forget).
- (void)multiStopDataNotification:(NSArray<SensorProfile*>*)sensors
                        timeoutMs:(int)timeoutMs
                       completion:(nullable void (^)(NSDictionary<NSString*, NSNumber*>* results,
                                                     NSDictionary<NSString*, NSString*>* errors))completion;

- (void)setDebugEnabled:(bool)enabled;
- (void)setDataLogEnabled:(bool)enabled;
- (void)setLogPath:(bool)enabled path:(NSString*)path;
- (NSString*)getVersion;

/// Writes an application log line (tag "App") into the SDK log. level is
/// judged by its first character, case-insensitive d/i/w/e; anything else
/// (including nil) is treated as "I". Never fails; a nil message is a no-op.
- (void)log:(nullable NSString*)message level:(nullable NSString*)level;
/// Convenience form of log:level: with level "I".
- (void)log:(nullable NSString*)message;

// Bin capture inspection and offline replay.

/// nil when the file cannot be parsed; check info.valid.
- (nullable BinFileInfo*)getBinFileInfo:(NSString*)path;
/// Replays a bin capture through the normal parse pipeline on a background
/// thread; set the returned profile's delegate to receive the data. nil on
/// failure. deviceMac must be non-empty (use the mac from getBinFileInfo,
/// or any placeholder such as @"REPLAY").
- (nullable SensorProfile*)replayBinFile:(NSString*)path deviceMac:(NSString*)deviceMac
                                realtime:(BOOL)realtime timeout:(NSTimeInterval)timeout;
/// Synchronized multi-bin replay: every (paths[i], deviceMacs[i]) capture
/// replays on one shared clock aligned by record timestamps (the earliest
/// record in the group is t=0, so concurrently recorded captures keep their
/// original relative offsets). Pausing/resuming any member freezes/resumes
/// the whole group; stopBinReplay: stays per device. Returns an array aligned
/// with the inputs; an NSNull entry marks a member that failed
/// (empty/duplicate mac, mac already replaying or streaming live, unreadable
/// file). paths and deviceMacs must be equally sized.
- (NSArray*)multiReplayBinFile:(NSArray<NSString*>*)paths
                    deviceMacs:(NSArray<NSString*>*)macs
                      realtime:(BOOL)realtime timeout:(NSTimeInterval)timeout;
/// These return @"OK" or an @"Error: ..." string. stopBinReplay joins the
/// replay thread — call it off the main thread.
- (NSString*)pauseBinReplay:(NSString*)deviceMac;
- (NSString*)resumeBinReplay:(NSString*)deviceMac;
- (NSString*)stopBinReplay:(NSString*)deviceMac;
/// Offline full-speed parse of a bin capture into CSV. BLOCKS the caller;
/// returns the csv path on success or an @"Error: ..." string.
- (NSString*)parseBinToCsv:(NSString*)binPath csvPath:(NSString*)csvPath;
@end

NS_ASSUME_NONNULL_END
