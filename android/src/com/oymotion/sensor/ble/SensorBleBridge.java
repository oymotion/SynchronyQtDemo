package com.oymotion.sensor.ble;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class SensorBleBridge {

    static {
        System.loadLibrary("sensor");
    }

    public static native void nativeOnScanResult(SensorScanCallback callback, ScanResult result);
    public static native void nativeOnScanFailed(SensorScanCallback callback, int errorCode);
    public static native void nativeOnConnectionStateChanged(GattCallbacks callback, BluetoothGatt gatt,
                                                             String address, int state);
    public static native void nativeOnServicesDiscovered(GattCallbacks callback, String address,
                                                         ServiceInfo[] services);
    public static native void nativeOnCharacteristicChanged(GattCallbacks callback, String address,
                                                            String serviceUuid, String charUuid, byte[] data);
    public static native void nativeOnMtuChanged(GattCallbacks callback, String address, int mtu);
    public static native void nativeOnConnectionUpdated(GattCallbacks callback, String address, int interval,
                                                        int latency, int timeout);
    public static native void nativeOnNotifyStateChanged(GattCallbacks callback, String address, String charUuid,
                                                         boolean enabled);

    private native void nativeSetBridge();

    private final Context context;
    private final Context applicationContext;
    private final BluetoothAdapter bluetoothAdapter;

    public SensorBleBridge(Context context) {
        this.context = context;
        this.applicationContext = context.getApplicationContext();
        BluetoothManager manager =
            (BluetoothManager) applicationContext.getSystemService(Context.BLUETOOTH_SERVICE);
        this.bluetoothAdapter = manager != null ? manager.getAdapter() : null;
    }

    public void setBridge() {
        nativeSetBridge();
    }

    private static final int REQUEST_CODE_BLE_PERMISSIONS = 0xB1E;

    @SuppressWarnings("unused")
    public boolean requestBlePermissions() {
        List<String> missing = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= 31) {
            if (context.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)
                    != PackageManager.PERMISSION_GRANTED) {
                missing.add(Manifest.permission.BLUETOOTH_SCAN);
            }
            if (context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED) {
                missing.add(Manifest.permission.BLUETOOTH_CONNECT);
            }
        } else {
            if (context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                    != PackageManager.PERMISSION_GRANTED) {
                missing.add(Manifest.permission.ACCESS_FINE_LOCATION);
            }
        }
        if (missing.isEmpty()) {
            return true;
        }
        if (context instanceof Activity) {
            ((Activity) context).requestPermissions(
                missing.toArray(new String[0]), REQUEST_CODE_BLE_PERMISSIONS);
        }
        return false;
    }

    public static final class SensorScanCallback extends ScanCallback {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            nativeOnScanResult(this, result);
        }

        @Override
        public void onBatchScanResults(List<ScanResult> results) {
            for (ScanResult result : results) {
                nativeOnScanResult(this, result);
            }
        }

        @Override
        public void onScanFailed(int errorCode) {
            nativeOnScanFailed(this, errorCode);
        }
    }

    public static final class GattCallbacks extends BluetoothGattCallback {
        private final String address;
        private boolean discoveryRetried;

        public GattCallbacks(String address) {
            this.address = address;
        }

        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            boolean connected = status == BluetoothGatt.GATT_SUCCESS
                && newState == BluetoothProfile.STATE_CONNECTED;
            if (connected) {
                gatt.requestMtu(247);
                gatt.discoverServices();
                nativeOnConnectionStateChanged(this, gatt, address, 1);
            } else {
                nativeOnConnectionStateChanged(this, gatt, address, 0);
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS || gatt.getServices().isEmpty()) {
                if (!discoveryRetried) {
                    discoveryRetried = true;
                    gatt.discoverServices();
                }
                return;
            }
            List<ServiceInfo> services = new ArrayList<>();
            for (BluetoothGattService service : gatt.getServices()) {
                List<CharacteristicInfo> chars = new ArrayList<>();
                for (BluetoothGattCharacteristic ch : service.getCharacteristics()) {
                    List<DescriptorInfo> descs = new ArrayList<>();
                    for (BluetoothGattDescriptor desc : ch.getDescriptors()) {
                        descs.add(new DescriptorInfo(desc.getUuid().toString()));
                    }
                    chars.add(new CharacteristicInfo(ch.getUuid().toString(), ch.getProperties(),
                        descs.toArray(new DescriptorInfo[0])));
                }
                services.add(new ServiceInfo(service.getUuid().toString(),
                    chars.toArray(new CharacteristicInfo[0])));
            }
            nativeOnServicesDiscovered(this, address, services.toArray(new ServiceInfo[0]));
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
            nativeOnCharacteristicChanged(this, address, characteristic.getService().getUuid().toString(),
                characteristic.getUuid().toString(), characteristic.getValue());
        }

        @Override
        public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
            nativeOnMtuChanged(this, address, mtu);
            gatt.discoverServices();
        }

        public void onConnectionUpdated(BluetoothGatt gatt, int interval, int latency, int timeout, int status) {
            nativeOnConnectionUpdated(this, address, interval, latency, timeout);
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor descriptor, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) return;
            if (!descriptor.getUuid().toString().equalsIgnoreCase("00002902-0000-1000-8000-00805f9b34fb")) return;
            boolean enabled = Arrays.equals(descriptor.getValue(), BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            nativeOnNotifyStateChanged(this, address, descriptor.getCharacteristic().getUuid().toString(), enabled);
        }
    }
}
