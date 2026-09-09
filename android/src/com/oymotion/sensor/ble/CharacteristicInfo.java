package com.oymotion.sensor.ble;

public final class CharacteristicInfo {
    public String uuid;
    public int properties;
    public DescriptorInfo[] descriptors;

    public CharacteristicInfo(String uuid, int properties, DescriptorInfo[] descriptors) {
        this.uuid = uuid;
        this.properties = properties;
        this.descriptors = descriptors;
    }
}
