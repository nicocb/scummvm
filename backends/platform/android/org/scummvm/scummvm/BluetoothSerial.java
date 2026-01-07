package org.scummvm.scummvm;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.util.Log;

import java.io.IOException;
import java.io.OutputStream;
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
            if (name != null && name.contains(deviceName)) {
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

    /**
     * Send raw bytes to the Bluetooth device.
     * @return number of bytes written, or -1 on error
     */
    public int write(byte[] data) {
        if (!_connected || _outputStream == null) {
            return -1;
        }

        try {
            _outputStream.write(data);
            return data.length;
        } catch (IOException e) {
            Log.e(LOG_TAG, "Write failed: " + e.getMessage());
            return -1;
        }
    }

    /**
     * Send a SpeakerEasy note command.
     * Protocol: CMD_STREAM_NOTE (0x06) + Freq(2) + Dur(2)
     * @return true if send succeeded
     */
    public boolean sendNote(int freq, int duration) {
        if (!_connected) {
            return false;
        }

        byte[] packet = new byte[5];
        packet[0] = 0x06;  // CMD_STREAM_NOTE
        packet[1] = (byte) (freq & 0xFF);
        packet[2] = (byte) ((freq >> 8) & 0xFF);
        packet[3] = (byte) (duration & 0xFF);
        packet[4] = (byte) ((duration >> 8) & 0xFF);

        return write(packet) == 5;
    }

    public boolean isConnected() {
        return _connected;
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
