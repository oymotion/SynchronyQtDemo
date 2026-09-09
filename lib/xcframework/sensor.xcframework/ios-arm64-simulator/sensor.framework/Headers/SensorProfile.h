#import <Foundation/Foundation.h>
#import "SenSdkDefines.h"
#import "SensorData.h"

NS_ASSUME_NONNULL_BEGIN

@protocol SensorProfileDelegate;

@interface SensorProfile : NSObject
@property (atomic, weak, nullable) id<SensorProfileDelegate> delegate;
@property (nonatomic, strong, readonly) BLEDevice* device;
@property (nonatomic, strong, readonly) DeviceInfo* deviceInfo;
@property (atomic, assign, readonly) BLEState deviceState;
@property (atomic, assign, readonly) BOOL isReady;
@property (nonatomic, readonly) NSString* stateString;
@property (atomic, assign, readonly) bool hasInited;
@property (atomic, assign, readonly) bool isDataTransfering;

- (void)connect:(nullable void (^)(BOOL success, NSError* _Nullable err))completion;
- (void)disconnect:(nullable void (^)(BOOL success, NSError* _Nullable err))completion;

- (void)init:(int)packageCount timeout:(NSTimeInterval)timeout
    powerRefreshInterval:(NSTimeInterval)powerRefreshInterval
              completion:(nullable void (^)(BOOL success, NSError* _Nullable err))completion
    __attribute__((objc_method_family(none)));
- (void)startDataNotification:(NSTimeInterval)timeout
                   completion:(nullable void (^)(BOOL success, NSError* _Nullable err))completion;
- (void)stopDataNotification:(NSTimeInterval)timeout
                  completion:(nullable void (^)(BOOL success, NSError* _Nullable err))completion;

- (void)getBatteryLevel:(NSTimeInterval)timeout
             completion:(void (^)(int battery, NSError* _Nullable err))completion;
- (void)fetchDeviceInfo:(NSTimeInterval)timeout
             completion:(void (^)(DeviceInfo* _Nullable deviceInfo, NSError* _Nullable err))completion;

- (void)setParam:(NSTimeInterval)timeout key:(NSString*)key value:(NSString*)value
      completion:(void (^)(NSString* result, NSError* _Nullable err))completion;
- (void)getParam:(NSTimeInterval)timeout key:(NSString*)key
      completion:(void (^)(NSString* result, NSError* _Nullable err))completion;

- (void)setAutoReconnect:(bool)enabled;

- (void)log:(nullable NSString*)message level:(nullable NSString*)level;
- (void)log:(nullable NSString*)message;
@end

@protocol SensorProfileDelegate <NSObject>
- (void)onData:(SensorProfile*)profile dataList:(NSArray<SensorData*>*)dataList;
- (void)onStateChanged:(SensorProfile*)profile newState:(BLEState)newState;
- (void)onError:(SensorProfile*)profile err:(NSError*)err;
@optional
- (void)onPowerChanged:(SensorProfile*)profile power:(int)power;
- (void)onDeviceInfoUpdate:(SensorProfile*)profile info:(DeviceInfo*)info;
- (void)onDataTransferStateChange:(SensorProfile*)profile isTransferring:(BOOL)isTransferring;
- (void)onAutoReconnect:(SensorProfile*)profile hasLastSession:(BOOL)hasLastSession
                 answer:(void (^)(BOOL handled))answer;
@end

NS_ASSUME_NONNULL_END
