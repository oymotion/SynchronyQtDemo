//
//  SensorProfile.h
//  sensorobjc — per-device connection & data profile, mirroring the Android
//  binding / Python SDK SensorProfile over the sen_capi profile handle.
//
//  Threading: all async ops are completion-block style (no sync forms; a nil
//  completion = fire-and-forget). Completion blocks and delegate calls fire
//  on internal SDK threads — hop to the main queue for UI work.
//

#import <Foundation/Foundation.h>
#import "SenSdkDefines.h"
#import "SensorData.h"

NS_ASSUME_NONNULL_BEGIN

@protocol SensorProfileDelegate;

@interface SensorProfile : NSObject
@property (atomic, weak, nullable) id<SensorProfileDelegate> delegate;
@property (nonatomic, strong, readonly) BLEDevice* device;
/// Cached DeviceInfo populated during init / fetchDeviceInfo; no GATT traffic.
@property (nonatomic, strong, readonly) DeviceInfo* deviceInfo;
/// Current link state, a BLEState value.
@property (atomic, assign, readonly) BLEState deviceState;
/// True when the link is Ready (Python SDK 0.7.2 isReady parity).
@property (atomic, assign, readonly) BOOL isReady;
@property (nonatomic, readonly) NSString* stateString;
@property (atomic, assign, readonly) bool hasInited;
@property (atomic, assign, readonly) bool isDataTransfering;

/// Callback-async connect (Python asyncConnect parity): the completion fires
/// exactly once with the final result; nil = fire-and-forget.
- (void)connect:(nullable void (^)(BOOL success, NSError* _Nullable err))completion;
/// Callback-async disconnect (Python asyncDisconnect parity): stops the
/// stream first when one is running and always reports success.
- (void)disconnect:(nullable void (^)(BOOL success, NSError* _Nullable err))completion;

/// Initializes the device (Python SensorProfile.init parity).
/// packageCount: broadcast batch size in samples per channel;
/// powerRefreshInterval: battery polling period in seconds; 0 disables
/// polling (one initial reading is still pushed).
- (void)init:(int)packageCount timeout:(NSTimeInterval)timeout
    powerRefreshInterval:(NSTimeInterval)powerRefreshInterval
              completion:(nullable void (^)(BOOL success, NSError* _Nullable err))completion
    __attribute__((objc_method_family(none)));
- (void)startDataNotification:(NSTimeInterval)timeout
                   completion:(nullable void (^)(BOOL success, NSError* _Nullable err))completion;
- (void)stopDataNotification:(NSTimeInterval)timeout
                  completion:(nullable void (^)(BOOL success, NSError* _Nullable err))completion;

/// Fresh GATT battery query (unfiltered; may report failure).
- (void)getBatteryLevel:(NSTimeInterval)timeout
             completion:(void (^)(int battery, NSError* _Nullable err))completion;
/// GATT fetch; the cached deviceInfo property is populated on init/fetch.
- (void)fetchDeviceInfo:(NSTimeInterval)timeout
             completion:(void (^)(DeviceInfo* _Nullable deviceInfo, NSError* _Nullable err))completion;

/// Callback-async setParam/getParam (NTF_*, FILTER_*, DEBUG_LOG_PATH, ...).
/// The completion carries the result string (setParam echo or getParam
/// value, "K|V" aggregates) and a nil error on success.
- (void)setParam:(NSTimeInterval)timeout key:(NSString*)key value:(NSString*)value
      completion:(void (^)(NSString* result, NSError* _Nullable err))completion;
- (void)getParam:(NSTimeInterval)timeout key:(NSString*)key
      completion:(void (^)(NSString* result, NSError* _Nullable err))completion;

/// Enables/disables session recovery after an auto reconnect (default on).
- (void)setAutoReconnect:(bool)enabled;

/// Writes an application log line (tag "App") into the SDK log for this
/// device. level is judged by its first character, case-insensitive
/// d/i/w/e; anything else (including nil) is treated as "I". Never fails;
/// a nil message is a no-op.
- (void)log:(nullable NSString*)message level:(nullable NSString*)level;
/// Convenience form of log:level: with level "I".
- (void)log:(nullable NSString*)message;
@end

@protocol SensorProfileDelegate <NSObject>
/// Array-form data callback: ALL batches accumulated since the last callback
/// in one call. Each SensorData borrows SDK memory — call clone on any
/// instance you keep past the stream's lifetime.
- (void)onData:(SensorProfile*)profile dataList:(NSArray<SensorData*>*)dataList;
- (void)onStateChanged:(SensorProfile*)profile newState:(BLEState)newState;
- (void)onError:(SensorProfile*)profile err:(NSError*)err;
@optional
/// Battery pushes from init's powerRefreshInterval polling.
- (void)onPowerChanged:(SensorProfile*)profile power:(int)power;
/// DeviceInfo field change push (aligned with the Python SDK 0.7.0
/// onDeviceInfoUpdate): fired after the cached DeviceInfo was updated in
/// place (e.g. setParam "EEG_SAMPLE_RATE" rewrote the bound EEG/ECG rates).
- (void)onDeviceInfoUpdate:(SensorProfile*)profile info:(DeviceInfo*)info;
/// Data-stream on/off state push: fired only on a real state change — the
/// stream actually starts (successful startDataNotification or replay data
/// start) or stops (stopDataNotification, link loss, replay end).
/// isTransferring is YES while streaming. Fires on an internal SDK thread.
- (void)onDataTransferStateChange:(SensorProfile*)profile isTransferring:(BOOL)isTransferring;
/// Gates session recovery after an auto reconnect. Answer through the passed
/// block, exactly once and from any thread: answer(YES) takes over the
/// recovery yourself (the SDK skips its default init -> setParam replay ->
/// stream restart flow), answer(NO) runs the default recovery. If no answer
/// arrives within 10 s the SDK runs the default recovery.
- (void)onAutoReconnect:(SensorProfile*)profile hasLastSession:(BOOL)hasLastSession
                 answer:(void (^)(BOOL handled))answer;
@end

NS_ASSUME_NONNULL_END
