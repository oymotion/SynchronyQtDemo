//
//  SenSdkDefines.h
//  sensorobjc — Objective-C wrapper over the sen_capi flat C API.
//
//  Enum names and values mirror the Android binding / Python SDK
//  (DeviceState and DataType), matching the sen_capi SenDataType /
//  SenDeviceState numbering.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, BLEState) {
    BLEStateDisconnected = 0,
    BLEStateConnecting = 1,
    BLEStateConnected = 2,
    BLEStateReady = 3,
    BLEStateDisconnecting = 4,
    BLEStateInvalid = 5,
};

/// SensorData stream type; values match sen_capi's enum SenDataType.
typedef NS_ENUM(NSInteger, NotifyDataType) {
    NTF_ACC = 1,
    NTF_GYRO = 2,
    NTF_EULER = 4,
    NTF_QUATERNION = 5,
    NTF_GEST = 7,
    NTF_EMG = 8,
    NTF_MAG_ANGLE = 13,
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

extern NSString* const SenSdkErrorDomain;

NS_ASSUME_NONNULL_END
