#import <Foundation/Foundation.h>
#import "SensorData.h"

NS_ASSUME_NONNULL_BEGIN

@interface SensorAlgo : NSObject

+ (uint32_t)algoCapiVersion;

- (void)initWithCompletion:(nullable void (^)(BOOL success, NSError* _Nullable error))completion __attribute__((objc_method_family(none)));
- (BOOL)hasInit;
- (void)reset;
- (void)setParam:(NSString*)key value:(NSString*)value
      completion:(nullable void (^)(NSString* result, NSError* _Nullable error))completion;
- (void)getParam:(NSString*)key
      completion:(nullable void (^)(NSString* result, NSError* _Nullable error))completion
      NS_SWIFT_ASYNC_NAME(getParam(_:));
- (void)pushSamples:(NSArray<SensorData*>*)samples param:(NSString*)param
         completion:(nullable void (^)(BOOL success, NSError* _Nullable error))completion;
- (void)getResult:(NSString*)key
       completion:(nullable void (^)(NSString* result, NSError* _Nullable error))completion
       NS_SWIFT_ASYNC_NAME(getResult(_:));

@end

NS_ASSUME_NONNULL_END
