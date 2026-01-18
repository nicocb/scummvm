package org.scummvm.scummvm;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.FileDescriptor;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.reflect.Field;
import java.util.Set;
import java.util.UUID;

/**
 * Bluetooth SPP (Serial Port Profile) driver for SpeakerEasy hardware.
 * Connects to a paired Bluetooth device and sends serial data.
 */
public class BluetoothSerial {
    private static final String LOG_TAG = "BluetoothSerial";

    // Standard SPP UUID
    private static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    private BluetoothAdapter _adapter;
    private BluetoothSocket _socket;
    private OutputStream _outputStream;
    private boolean _connected = false;

    public BluetoothSerial() {
        _adapter = BluetoothAdapter.getDefaultAdapter();
    }

    /**
     * Connect to a Bluetooth device by name.
     * The device must already be paired in Android settings.
     * @param deviceName Name of the paired Bluetooth device (e.g., "Speaker Easy")
     * @return true if connection succeeded
     */
    public boolean connect(String deviceName) {
        if (_adapter == null) {
            Log.e(LOG_TAG, "Bluetooth not available on this device");
            return false;
        }

        if (!_adapter.isEnabled()) {
            Log.e(LOG_TAG, "Bluetooth is disabled");
            return false;
        }

        // Find the paired device by name
        Set<BluetoothDevice> pairedDevices = _adapter.getBondedDevices();
        BluetoothDevice targetDevice = null;

        for (BluetoothDevice device : pairedDevices) {
            String name = device.getName();
            Log.d(LOG_TAG, "Found paired device: " + name);
            if (name != null && name.equalsIgnoreCase(deviceName)) {
                targetDevice = device;
                break;
            }
        }

        if (targetDevice == null) {
            Log.e(LOG_TAG, "Device not found: " + deviceName + ". Make sure it's paired in Bluetooth settings.");
            return false;
        }

        return connectToDevice(targetDevice);
    }

    /**
     * Connect to a Bluetooth device by MAC address.
     * @param macAddress MAC address (e.g., "AA:BB:CC:DD:EE:FF")
     * @return true if connection succeeded
     */
    public boolean connectByAddress(String macAddress) {
        if (_adapter == null) {
            Log.e(LOG_TAG, "Bluetooth not available on this device");
            return false;
        }

        if (!_adapter.isEnabled()) {
            Log.e(LOG_TAG, "Bluetooth is disabled");
            return false;
        }

        try {
            BluetoothDevice device = _adapter.getRemoteDevice(macAddress);
            return connectToDevice(device);
        } catch (IllegalArgumentException e) {
            Log.e(LOG_TAG, "Invalid MAC address: " + macAddress);
            return false;
        }
    }

    private boolean connectToDevice(BluetoothDevice device) {
        try {
            Log.d(LOG_TAG, "Connecting to " + device.getName() + " (" + device.getAddress() + ")");

            // Cancel discovery to speed up connection
            _adapter.cancelDiscovery();

            // Create SPP socket
            _socket = device.createRfcommSocketToServiceRecord(SPP_UUID);
            _socket.connect();

            _outputStream = _socket.getOutputStream();
            _connected = true;

            Log.i(LOG_TAG, "Connected to " + device.getName());
            return true;

        } catch (IOException e) {
            Log.e(LOG_TAG, "Connection failed: " + e.getMessage());
            close();
            return false;
        } catch (SecurityException e) {
            Log.e(LOG_TAG, "Bluetooth permission denied: " + e.getMessage());
            return false;
        }
    }

    public boolean isConnected() {
        return _connected;
    }

    /**
     * Get the native file descriptor for the Bluetooth socket.
     * This allows native code to write directly without JNI calls.
     * @return file descriptor number, or -1 if not connected
     */
    public int getSocketFd() {
        if (!_connected || _socket == null) {
            return -1;
        }
        try {
            // Use reflection to get the underlying FileDescriptor
            // BluetoothSocket has mSocket (LocalSocket) which has mFdHandle (ParcelFileDescriptor)
            // or alternatively mSocket has getFileDescriptor() method
            Field socketField = BluetoothSocket.class.getDeclaredField("mSocket");
            socketField.setAccessible(true);
            Object localSocket = socketField.get(_socket);
            if (localSocket == null) {
                Log.e(LOG_TAG, "mSocket is null");
                return -1;
            }

            // Try to get FileDescriptor via getFileDescriptor() method first
            try {
                java.lang.reflect.Method getFdMethod = localSocket.getClass().getMethod("getFileDescriptor");
                FileDescriptor fd = (FileDescriptor) getFdMethod.invoke(localSocket);
                if (fd != null && fd.valid()) {
                    Field descriptorField = FileDescriptor.class.getDeclaredField("descriptor");
                    descriptorField.setAccessible(true);
                    int fdInt = descriptorField.getInt(fd);
                    Log.d(LOG_TAG, "Got fd via getFileDescriptor(): " + fdInt);
                    return fdInt;
                }
            } catch (NoSuchMethodException e) {
                Log.d(LOG_TAG, "getFileDescriptor() not available, trying fields");
            }

            // Try mFdHandle (ParcelFileDescriptor) field
            try {
                Field fdHandleField = localSocket.getClass().getDeclaredField("mFdHandle");
                fdHandleField.setAccessible(true);
                ParcelFileDescriptor pfd = (ParcelFileDescriptor) fdHandleField.get(localSocket);
                if (pfd != null) {
                    int fdInt = pfd.getFd();
                    Log.d(LOG_TAG, "Got fd via mFdHandle: " + fdInt);
                    return fdInt;
                }
            } catch (NoSuchFieldException e) {
                Log.d(LOG_TAG, "mFdHandle not available, trying impl");
            }

            // Try mImpl.mFd path (older Android)
            try {
                Field implField = localSocket.getClass().getDeclaredField("impl");
                implField.setAccessible(true);
                Object impl = implField.get(localSocket);
                if (impl != null) {
                    Field fdField = impl.getClass().getDeclaredField("fd");
                    fdField.setAccessible(true);
                    FileDescriptor fd = (FileDescriptor) fdField.get(impl);
                    if (fd != null && fd.valid()) {
                        Field descriptorField = FileDescriptor.class.getDeclaredField("descriptor");
                        descriptorField.setAccessible(true);
                        int fdInt = descriptorField.getInt(fd);
                        Log.d(LOG_TAG, "Got fd via impl.fd: " + fdInt);
                        return fdInt;
                    }
                }
            } catch (NoSuchFieldException e) {
                Log.d(LOG_TAG, "impl.fd not available");
            }

            Log.e(LOG_TAG, "Could not find fd in LocalSocket");
            return -1;
        } catch (Exception e) {
            Log.e(LOG_TAG, "Failed to get socket fd: " + e.getMessage());
            return -1;
        }
    }

    public void close() {
        _connected = false;

        if (_outputStream != null) {
            try {
                _outputStream.close();
            } catch (IOException ignored) {}
            _outputStream = null;
        }

        if (_socket != null) {
            try {
                _socket.close();
            } catch (IOException ignored) {}
            _socket = null;
        }

        Log.d(LOG_TAG, "Bluetooth connection closed");
    }

    /**
     * Get list of paired Bluetooth devices.
     * @return Array of device names
     */
    public String[] getPairedDevices() {
        if (_adapter == null || !_adapter.isEnabled()) {
            return new String[0];
        }

        try {
            Set<BluetoothDevice> pairedDevices = _adapter.getBondedDevices();
            String[] names = new String[pairedDevices.size()];
            int i = 0;
            for (BluetoothDevice device : pairedDevices) {
                names[i++] = device.getName();
            }
            return names;
        } catch (SecurityException e) {
            Log.e(LOG_TAG, "Bluetooth permission denied");
            return new String[0];
        }
    }
}
