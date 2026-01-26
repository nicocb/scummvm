/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define FORBIDDEN_SYMBOL_EXCEPTION_time_h
#define FORBIDDEN_SYMBOL_EXCEPTION_printf
#define FORBIDDEN_SYMBOL_EXCEPTION_unistd_h

#include "audio/softsynth/speakereasy.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/textconsole.h"
#include "common/system.h"

namespace Audio {

// Protocol commands
static const uint8 CMD_STREAM_NOTE = 0x06;        // param = duration
static const uint8 CMD_STREAM_NOTE_DELTA = 0x07;  // param = delta ms since last note (Android only)

// Throttling constants
static const uint16 MIN_FREQ_CHANGE = 5;  // Ignore freq changes < 5 Hz
static const uint16 MAX_DELTA = 2000;       // Max delta before forcing a packet (limits glitch duration if packet lost)

// Build a 5-byte packet
static void buildPacket(uint8 *packet, uint8 cmd, uint16 freq, uint16 param) {
    packet[0] = cmd;
    packet[1] = (uint8)(freq & 0xFF);
    packet[2] = (uint8)((freq >> 8) & 0xFF);
    packet[3] = (uint8)(param & 0xFF);
    packet[4] = (uint8)((param >> 8) & 0xFF);
}


void SpeakerEasy::sendNote(uint16 freq, uint16 delta) {
    if (!isConnected()) {
        return;
    }

    // Accumulate delta time
    _accumulatedDelta += delta;

    // Skip if frequency change is too small (unless going from/to silence)
    // This also handles freq == _lastSentFreq since diff=0 is within range
    int16 freqDiff = (int16)freq - (int16)_lastSentFreq;
    if (freqDiff > -MIN_FREQ_CHANGE && freqDiff < MIN_FREQ_CHANGE) {
        // Even if freq hasn't changed much, send a packet if delta is too large
        // This limits glitch duration if a packet is lost over Bluetooth
        if (_accumulatedDelta < MAX_DELTA) {
            return;
        }
    }

    // Frequency changed significantly (or to/from silence) - send with accumulated delta
    writePacket(freq, _accumulatedDelta);
    _lastSentFreq = freq;
    _accumulatedDelta = 0;
}

SpeakerEasy *SpeakerEasy::create() {
    if (!ConfMan.getBool("speakereasy_enable"))
        return nullptr;
    Common::String port = ConfMan.get("speakereasy_port");
    if (port.empty()) {
        warning("SpeakerEasy: No port/device configured");
        return nullptr;
    }
    SpeakerEasy *se = new SpeakerEasy(port.c_str());
    if (!se->isConnected()) {
        warning("SpeakerEasy: Failed to connect to %s", port.c_str());
        delete se;
        return nullptr;
    }
    debug(1, "SpeakerEasy: Successfully connected to %s", port.c_str());
    return se;
}

bool SpeakerEasy::isConnected() {
    if(!_connected) {
        uint32 current = g_system->getMillis();
        if(current - _lastConnectionAttempt > 5000) {
            debug(1, "SpeakerEasy: Attempting to reconnect...");
            _lastConnectionAttempt = current;
            // Try to reconnect
            connect();
        }
    }
    return _connected;
}

} // End of namespace Audio

// ============================================================================
// Platform-specific implementations
// ============================================================================

#if defined(__ANDROID__)
// ----------------------------------------------------------------------------
// Android Implementation - Bluetooth SPP via JNI
// ----------------------------------------------------------------------------
#include "backends/platform/android/jni-android.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <android/log.h>

namespace Audio {

SpeakerEasy::SpeakerEasy(const char *portName) : _connected(false), _lastSentFreq(0), _accumulatedDelta(0), _lastConnectionAttempt(0), _fd(-1) {
    _portName = portName;
    connect();
}

void SpeakerEasy::connect() {
    if (!JNI::hasBluetoothPermission()) {
        JNI::requestBluetoothPermission();
        return;
    }
    _connected = JNI::bluetoothConnect(_portName);
    if (_connected) {
        _fd = JNI::getBluetoothSocketFd();
    }
}

void SpeakerEasy::handleDisconnect() {
    _connected = false;
    JNI::bluetoothDisconnect();
}

SpeakerEasy::~SpeakerEasy() {
    if (_connected) {
        sendNote(0);
        JNI::bluetoothDisconnect();
    }
}



void SpeakerEasy::writePacket(uint16 freq, uint16 delta) {
    uint8 packet[5];
    buildPacket(packet, CMD_STREAM_NOTE_DELTA, freq, delta);

    ssize_t written = write(_fd, packet, 5);
    if (written != 5) {
        warning("SpeakerEasy: write returned %zd (errno %d)", written, errno);
        handleDisconnect();
    }
}

} // End of namespace Audio

#elif defined(WIN32)
// ----------------------------------------------------------------------------
// Windows Implementation - Serial port via Win32 API
// ----------------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Audio {

SpeakerEasy::SpeakerEasy(const char *portName) : _connected(false), _lastSentFreq(0), _accumulatedDelta(0), _lastConnectionAttempt(0), _hSerial(INVALID_HANDLE_VALUE) {
    _portName = portName;
    connect();
}

void SpeakerEasy::connect() {
    _hSerial = CreateFileA(_portName.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

    if (_hSerial == INVALID_HANDLE_VALUE) {
        warning("SpeakerEasy: CreateFileA failed for %s (error %lu)", _portName.c_str(), GetLastError());
        return;
    }

    DCB dcbSerialParams;
    memset(&dcbSerialParams, 0, sizeof(dcbSerialParams));
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState((HANDLE)_hSerial, &dcbSerialParams)) {
        warning("SpeakerEasy: GetCommState failed (error %lu)", GetLastError());
        CloseHandle((HANDLE)_hSerial);
        _hSerial = INVALID_HANDLE_VALUE;
        return;
    }

    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState((HANDLE)_hSerial, &dcbSerialParams)) {
        warning("SpeakerEasy: SetCommState failed (error %lu)", GetLastError());
        CloseHandle((HANDLE)_hSerial);
        _hSerial = INVALID_HANDLE_VALUE;
        return;
    }

    // Set timeouts to prevent blocking on write
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant = 100;  // 100ms max write timeout

    if (!SetCommTimeouts((HANDLE)_hSerial, &timeouts)) {
        warning("SpeakerEasy: SetCommTimeouts failed (error %lu)", GetLastError());
    }

    _connected = true;
}

void SpeakerEasy::handleDisconnect() {
    _connected = false;
    if (_hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle((HANDLE)_hSerial);
        _hSerial = INVALID_HANDLE_VALUE;
    }
}

SpeakerEasy::~SpeakerEasy() {
    if (_connected) {
        sendNote(0);
        CloseHandle((HANDLE)_hSerial);
    }
}

void SpeakerEasy::writePacket(uint16 freq, uint16 delta) {
    uint8 packet[5];
    buildPacket(packet, CMD_STREAM_NOTE_DELTA, freq, delta);

    DWORD bytesWritten;
    if (!WriteFile((HANDLE)_hSerial, packet, 5, &bytesWritten, NULL) || bytesWritten < 5) {
        DWORD err = GetLastError();
        warning("SpeakerEasy: WriteFile failed (error %lu, wrote %lu bytes)", err, bytesWritten);
        handleDisconnect();
    }
}

} // End of namespace Audio

#elif defined(__POSIX__) || defined(__linux__) || defined(__APPLE__)
// ----------------------------------------------------------------------------
// POSIX Implementation - Serial port via termios
// ----------------------------------------------------------------------------
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

namespace Audio {

SpeakerEasy::SpeakerEasy(const char *portName) : _connected(false), _lastSentFreq(0), _accumulatedDelta(0), _lastConnectionAttempt(0), _fd(-1) {
    _portName = portName;
    connect();
}

void SpeakerEasy::connect() {
    warning("SpeakerEasy: connecting to %s", _portName.c_str());
    _fd = open(_portName.c_str(), O_WRONLY | O_NOCTTY);
    if (_fd >= 0) {
        struct termios tty;
        memset(&tty, 0, sizeof(tty));

        if (tcgetattr(_fd, &tty) == 0) {
            cfsetospeed(&tty, B115200);
            cfsetispeed(&tty, B115200);

            tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

            tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
            tty.c_cflag &= ~(PARENB | PARODD);
            tty.c_cflag &= ~CSTOPB;
            tty.c_cflag &= ~CRTSCTS;
            tty.c_cflag |= (CLOCAL | CREAD);

            tty.c_lflag = 0;
            tty.c_oflag = 0;
            tty.c_iflag &= ~(IXON | IXOFF | IXANY);

            if (tcsetattr(_fd, TCSANOW, &tty) == 0) {
                _connected = true;
                debug(1, "SpeakerEasy: Successfully opened %s", _portName.c_str());
            } else {
                debug(1, "SpeakerEasy: tcsetattr failed for %s (errno %d)", _portName.c_str(), errno);
                close(_fd);
            }
        } else {
            debug(1, "SpeakerEasy: tcgetattr failed for %s (errno %d)", _portName.c_str(), errno);
            close(_fd);
        }
    } else {
        debug(1, "SpeakerEasy: open failed for %s (errno %d)", _portName.c_str(), errno);
    }
    _lastConnectionAttempt = g_system->getMillis();
}

void SpeakerEasy::handleDisconnect() {
    _connected = false;
    close(_fd);
}

SpeakerEasy::~SpeakerEasy() {
    if (_connected) {
        sendNote(0);
        close(_fd);
    }
}


void SpeakerEasy::writePacket(uint16 freq, uint16 dur) {
    uint8 packet[5];
    buildPacket(packet, CMD_STREAM_NOTE, freq, dur);

    ssize_t written = write(_fd, packet, 5);
    
    if (written < 5) {
        warning("Error writing packets : wrote %zd of 5 bytes (errno %d)", written, errno);
        if (written < 0) {
            warning("Device disconnected");
            handleDisconnect();
        }
    }
}

} // End of namespace Audio

#else
// ----------------------------------------------------------------------------
// Stub Implementation - No serial support on this platform
// ----------------------------------------------------------------------------

namespace Audio {

SpeakerEasy::SpeakerEasy(const char *portName) : _connected(false), _lastSentFreq(0), _accumulatedDelta(0), _lastConnectionAttempt(0) {
    warning("SpeakerEasy: Not supported on this platform");
}

SpeakerEasy::~SpeakerEasy() {
}

void SpeakerEasy::writePacket(uint16 freq, uint16 dur) {
}

} // End of namespace Audio

#endif
