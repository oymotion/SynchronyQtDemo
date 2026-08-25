package com.oymotion.sensor.ble;

/**
 * Plain data object describing a BLE service discovered on a peripheral.
 */
public class ServiceInfo {
    public String uuid;
    public CharacteristicInfo[] characteristics;

    public ServiceInfo(String uuid, CharacteristicInfo[] characteristics) {
        this.uuid = uuid;
        this.characteristics = characteristics;
    }
}
