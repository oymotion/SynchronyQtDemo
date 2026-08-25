package com.oymotion.sensor.ble;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.UUID;

/**
 * Java-side bridge for the SensorBLE Android backend.
 *
 * <p>An Android application must create a single instance of this class and call
 * {@link #setBridge()} before using any SensorSDK BLE operations. The C++ layer
 * invokes Bluetooth operations on this object, and this object reports BLE events
 * back to the C++ layer through the {@code nativeOnXxx} methods.
 *
 * <p>Required permissions depend on the target Android version:
 * <ul>
 *   <li>Android 11 and below: {@code BLUETOOTH}, {@code BLUETOOTH_ADMIN},
 *       {@code ACCESS_FINE_LOCATION} (runtime).</li>
 *   <li>Android 12+: {@code BLUETOOTH_SCAN}, {@code BLUETOOTH_CONNECT},
 *       optionally {@code BLUETOOTH_ADVERTISE}.</li>
 * </ul>
 */
public class SensorBleBridge {

    static {
        System.loadLibrary("sensor");
    }

    // Native callbacks implemented in C++. These must match the signatures
    // expected by the SensorBLE Android backend.
    public static native void nativeOnScanResult(String address, String name, int rssi, byte[] advData);
    public static native void nativeOnScanFailed(int errorCode);
    public static native void nativeOnConnectionStateChanged(String address, int state);
    public static native void nativeOnServicesDiscovered(String address, ServiceInfo[] services);
    public static native void nativeOnCharacteristicChanged(String address, String serviceUuid,
                                                            String charUuid, byte[] data);
    public static native void nativeOnMtuChanged(String address, int mtu);
    public static native void nativeOnConnectionUpdated(String address, int interval, int latency, int timeout);
    public static native void nativeOnNotifyStateChanged(String address, String charUuid, boolean enabled);

    // Called from C++ to bind this Java object as the bridge.
    private native void nativeSetBridge();

    private final Context context;
    private final Context applicationContext;
    private final BluetoothAdapter bluetoothAdapter;

    // Dedicated scan thread (InternalScaner parity): with SCAN_MODE_LOW_LATENCY
    // every advertisement is one JNI call; on the main thread that flood made
    // list clicks/refresh laggy while scanning. Scan operations and their
    // callbacks run here instead.
    private final HandlerThread scanThread;
    private final Handler scanHandler;

    private BluetoothLeScanner scanner;
    private ScanCallback scanCallback;

    private final Map<String, BluetoothGatt> connectedGatts = new ConcurrentHashMap<>();
    private final Map<String, GattCallbacks> gattCallbacks = new ConcurrentHashMap<>();

    // One serial thread per connected device, keyed by MAC: connect /
    // disconnect / writeCommand / readCharacteristic / setNotify and all
    // BluetoothGattCallback events (the Handler overload of connectGatt)
    // run on it, keeping GATT traffic and JNI callbacks off the main thread
    // and serialized per device.
    private final Map<String, Handler> deviceHandlers = new ConcurrentHashMap<>();

    public SensorBleBridge(Context context) {
        this.context = context;
        this.applicationContext = context.getApplicationContext();
        BluetoothManager manager =
            (BluetoothManager) applicationContext.getSystemService(Context.BLUETOOTH_SERVICE);
        this.bluetoothAdapter = manager != null ? manager.getAdapter() : null;
        this.scanThread = new HandlerThread("SensorBleScan");
        this.scanThread.start();
        this.scanHandler = new Handler(scanThread.getLooper());
    }

    private Handler handlerFor(String address) {
        return deviceHandlers.computeIfAbsent(address, addr -> {
            HandlerThread thread = new HandlerThread("SensorBleGatt-" + addr);
            thread.start();
            return new Handler(thread.getLooper());
        });
    }

    private void releaseHandler(String address) {
        Handler handler = deviceHandlers.remove(address);
        if (handler != null) {
            handler.getLooper().quitSafely();
        }
    }

    /**
     * Registers this bridge instance with the native layer. Must be called once
     * before any SensorSDK BLE operation.
     */
    public void setBridge() {
        nativeSetBridge();
    }

    private static final int REQUEST_CODE_BLE_PERMISSIONS = 0xB1E;

    /**
     * Requests the runtime permissions BLE scanning/connecting needs on this
     * platform (same permission set as the SensorSDKAndroid demo's
     * MainActivity#getPermission): Android 12+ needs BLUETOOTH_SCAN and
     * BLUETOOTH_CONNECT, Android 11 and below needs ACCESS_FINE_LOCATION.
     * Shows the system permission dialog when the original context is an
     * Activity; otherwise stays a silent no-op.
     *
     * @return true when every required permission is already granted.
     */
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

    // Called from C++
    @SuppressWarnings("unused")
    public void startScan(List<ScanFilter> filters, ScanSettings settings) {
        if (bluetoothAdapter == null) return;
        scanHandler.post(() -> {
            scanner = bluetoothAdapter.getBluetoothLeScanner();
            if (scanner == null) return;
            scanCallback = new ScanCallback() {
                @Override
                public void onScanResult(int callbackType, ScanResult result) {
                    // Scan results are always delivered on the main thread
                    // (there is no Handler overload of startScan). Forward the
                    // expensive part (field extraction + JNI call) to the scan
                    // thread so a LOW_LATENCY advertisement flood does not
                    // stall the UI (list clicks/refresh while scanning).
                    scanHandler.post(() -> {
                        BluetoothDevice device = result.getDevice();
                        String address = device.getAddress();
                        // No fallback (no BluetoothDevice.getName()): only the
                        // advertised local name counts; the native side drops
                        // nameless scan results.
                        String name = result.getScanRecord() != null ? result.getScanRecord().getDeviceName() : null;
                        if (name == null) name = "";
                        byte[] advData = result.getScanRecord() != null ? result.getScanRecord().getBytes() : new byte[0];
                        nativeOnScanResult(address, name, result.getRssi(), advData != null ? advData : new byte[0]);
                    });
                }

                @Override
                public void onScanFailed(int errorCode) {
                    // Post to the scan thread like onScanResult does. Drop the
                    // scan so the next startScan gets a fresh callback instead
                    // of silently receiving nothing, and report the failure so
                    // the native side can keep serving its last scan results
                    // (e.g. SCAN_FAILED_SCANNING_TOO_FREQUENTLY when Android
                    // throttles scan starts).
                    scanHandler.post(() -> {
                        scanner = null;
                        scanCallback = null;
                        nativeOnScanFailed(errorCode);
                    });
                }
            };
            // The filters (hardware-level service-UUID match on 0000ffd0 /
            // 00001812, OR-ed) and settings (low-latency) are built on the JNI
            // side (JavaBridge.cpp) so this class stays policy-free.
            scanner.startScan(filters, settings, scanCallback);
        });
    }

    // Called from C++
    @SuppressWarnings("unused")
    public void stopScan() {
        scanHandler.post(() -> {
            if (scanner != null && scanCallback != null) {
                scanner.stopScan(scanCallback);
            }
            scanner = null;
            scanCallback = null;
        });
    }

    // Called from C++
    @SuppressWarnings("unused")
    public void connect(String address) {
        if (bluetoothAdapter == null) return;
        BluetoothDevice device = bluetoothAdapter.getRemoteDevice(address);
        if (device == null) return;
        Handler deviceHandler = handlerFor(address);
        deviceHandler.post(() -> {
            GattCallbacks callbacks = new GattCallbacks(address);
            gattCallbacks.put(address, callbacks);
            // The Handler overload delivers every GATT callback for this
            // device on its dedicated thread (minSdk 28 >= API 26).
            BluetoothGatt gatt = device.connectGatt(applicationContext, false, callbacks,
                BluetoothDevice.TRANSPORT_LE, BluetoothDevice.PHY_LE_1M, deviceHandler);
            connectedGatts.put(address, gatt);
        });
    }

    // Called from C++
    @SuppressWarnings("unused")
    public void disconnect(String address) {
        handlerFor(address).post(() -> {
            BluetoothGatt gatt = connectedGatts.get(address);
            if (gatt != null) {
                gatt.disconnect();
            }
        });
    }

    // Called from C++
    @SuppressWarnings("unused")
    public void writeCommand(String address, String serviceUuid, String charUuid, byte[] data, boolean withResponse) {
        handlerFor(address).post(() -> {
            BluetoothGatt gatt = connectedGatts.get(address);
            if (gatt == null) return;
            BluetoothGattCharacteristic characteristic = findCharacteristic(gatt, serviceUuid, charUuid);
            if (characteristic == null || data == null) return;

            boolean canWriteWithoutResponse =
                (characteristic.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0;
            boolean canWriteWithResponse =
                (characteristic.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE) != 0;

            // Prefer no-response when requested and supported; fall back to with-response
            // if no-response is unavailable. If neither is supported, the write is skipped.
            int writeType;
            if (!withResponse && canWriteWithoutResponse) {
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE;
            } else if (canWriteWithResponse) {
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;
            } else if (canWriteWithoutResponse) {
                writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE;
            } else {
                return;
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                gatt.writeCharacteristic(characteristic, data, writeType);
            } else {
                characteristic.setValue(data);
                characteristic.setWriteType(writeType);
                gatt.writeCharacteristic(characteristic);
            }
        });
    }

    // Called from C++
    @SuppressWarnings("unused")
    public void readCharacteristic(String address, String serviceUuid, String charUuid) {
        handlerFor(address).post(() -> {
            BluetoothGatt gatt = connectedGatts.get(address);
            if (gatt == null) return;
            BluetoothGattCharacteristic characteristic = findCharacteristic(gatt, serviceUuid, charUuid);
            if (characteristic == null) return;
            gatt.readCharacteristic(characteristic);
        });
    }

    // Called from C++
    @SuppressWarnings("unused")
    public void setNotify(String address, String serviceUuid, String charUuid, boolean enable) {
        handlerFor(address).post(() -> {
            BluetoothGatt gatt = connectedGatts.get(address);
            if (gatt == null) return;
            BluetoothGattCharacteristic characteristic = findCharacteristic(gatt, serviceUuid, charUuid);
            if (characteristic == null) return;
            gatt.setCharacteristicNotification(characteristic, enable);
            BluetoothGattDescriptor cccd = characteristic.getDescriptor(
                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb"));
            if (cccd != null) {
                if (enable) {
                    cccd.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                } else {
                    cccd.setValue(BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE);
                }
                gatt.writeDescriptor(cccd);
            }
        });
    }

    // Called from C++
    @SuppressWarnings("unused")
    public boolean isBluetoothEnabled() {
        return bluetoothAdapter != null && bluetoothAdapter.isEnabled();
    }

    private BluetoothGattCharacteristic findCharacteristic(BluetoothGatt gatt, String serviceUuid, String charUuid) {
        BluetoothGattService service = gatt.getService(UUID.fromString(serviceUuid));
        if (service == null) return null;
        return service.getCharacteristic(UUID.fromString(charUuid));
    }

    private class GattCallbacks extends BluetoothGattCallback {
        private final String address;

        GattCallbacks(String address) {
            this.address = address;
        }

        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            boolean connected = newState == BluetoothProfile.STATE_CONNECTED;
            nativeOnConnectionStateChanged(address, connected ? 1 : 0);
            if (connected) {
                // SimpleBLE flow: negotiate a larger MTU and start service
                // discovery right after the ACL comes up. The C++ side only
                // reports "connected" to the SDK once discovery finishes
                // (nativeOnServicesDiscovered), so its services() snapshot
                // always contains the full GATT tree.
                gatt.requestMtu(247);
                gatt.discoverServices();
            } else {
                connectedGatts.remove(address);
                gattCallbacks.remove(address);
                gatt.close();
                // The link is down for good; retire the per-device thread.
                releaseHandler(address);
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
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
            nativeOnServicesDiscovered(address, services.toArray(new ServiceInfo[0]));
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
            nativeOnCharacteristicChanged(address, characteristic.getService().getUuid().toString(),
                characteristic.getUuid().toString(), characteristic.getValue());
        }

        @Override
        public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
            nativeOnMtuChanged(address, mtu);
            // SimpleBLE parity: a discoverServices() issued together with
            // requestMtu() is silently dropped by some Android stacks while
            // the MTU exchange is busy — kick discovery off again once the
            // MTU callback arrives.
            gatt.discoverServices();
        }

        // Hidden Android callback (BluetoothGattCallback.onConnectionUpdated is
        // @hide but has existed in the runtime class since API 26): declared
        // without @Override, the framework still dispatches connection
        // parameter updates to it. The interval arrives in 1.25 ms units and
        // the supervision timeout in 10 ms units; conversion happens natively.
        public void onConnectionUpdated(BluetoothGatt gatt, int interval, int latency, int timeout, int status) {
            nativeOnConnectionUpdated(address, interval, latency, timeout);
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor descriptor, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) return;
            if (!descriptor.getUuid().toString().equalsIgnoreCase("00002902-0000-1000-8000-00805f9b34fb")) return;
            boolean enabled = Arrays.equals(descriptor.getValue(), BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            nativeOnNotifyStateChanged(address, descriptor.getCharacteristic().getUuid().toString(), enabled);
        }
    }
}
