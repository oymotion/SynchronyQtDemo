package com.oymotion.sensor.ble;

public final class ServiceInfo {
    public String uuid;
    public CharacteristicInfo[] characteristics;

    public ServiceInfo(String uuid, CharacteristicInfo[] characteristics) {
        this.uuid = uuid;
        this.characteristics = characteristics;
    }
}
