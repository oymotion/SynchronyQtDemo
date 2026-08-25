package com.oymotion.sensor.ble;

/**
 * Plain data object describing a BLE characteristic discovered on a peripheral.
 *
 * The {@code properties} field uses the standard Bluetooth GATT property bitmask:
 *   0x01 - BROADCAST
 *   0x02 - READ
 *   0x04 - WRITE_NO_RESPONSE
 *   0x08 - WRITE
 *   0x10 - NOTIFY
 *   0x20 - INDICATE
 *   0x40 - AUTHENTICATED_SIGNED_WRITES
 *   0x80 - EXTENDED_PROPS
 */
public class CharacteristicInfo {
    public String uuid;
    public int properties;
    public DescriptorInfo[] descriptors;

    public CharacteristicInfo(String uuid, int properties, DescriptorInfo[] descriptors) {
        this.uuid = uuid;
        this.properties = properties;
        this.descriptors = descriptors;
    }
}
