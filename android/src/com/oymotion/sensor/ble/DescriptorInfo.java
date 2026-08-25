package com.oymotion.sensor.ble;

/**
 * Plain data object describing a BLE descriptor discovered on a peripheral.
 */
public class DescriptorInfo {
    public String uuid;

    public DescriptorInfo(String uuid) {
        this.uuid = uuid;
    }
}
