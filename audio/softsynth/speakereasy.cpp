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

// Build a 5-byte packet
static void buildPacket(uint8 *packet, uint8 cmd, uint16 freq, uint16 param) {
    packet[0] = cmd;
    packet[1] = (uint8)(freq & 0xFF);
    packet[2] = (uint8)((freq >> 8) & 0xFF);
    packet[3] = (uint8)(param & 0xFF);
    packet[4] = (uint8)((param >> 8) & 0xFF);
}

void SpeakerEasy::sendNote(uint16 freq, uint16 delta) {
    if (!_connected) return;

    // Accumulate delta time
    _accumulatedDelta += delta;

    // Skip if frequency change is too small (unless going from/to silence)
    // This also handles freq == _lastSentFreq since diff=0 is within range
    int16 freqDiff = (int16)freq - (int16)_lastSentFreq;
    if (freqDiff > -MIN_FREQ_CHANGE && freqDiff < MIN_FREQ_CHANGE) {
        debug(1, "SpeakerEasy: DROP (small change) freq=%u lastFreq=%u diff=%d",
            freq, _lastSentFreq, freqDiff);
        return;
    }

    // Frequency changed significantly (or to/from silence) - send with accumulated delta
    writePacket(freq, (uint16)(_accumulatedDelta > 65535 ? 65535 : _accumulatedDelta));
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

SpeakerEasy::SpeakerEasy(const char *portName) : _connected(false), _lastSentFreq(0), _accumulatedDelta(0), _fd(-1) {
    if (!JNI::hasBluetoothPermission()) {
        JNI::requestBluetoothPermission();
        return;
    }
    _connected = JNI::bluetoothConnect(portName);
    if (_connected) {
        _fd = JNI::getBluetoothSocketFd();
        if (_fd >= 0) {
            // Set socket to non-blocking
            int flags = fcntl(_fd, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(_fd, F_SETFL, flags | O_NONBLOCK);
            }

            // Minimize send buffer to reduce latency
            int sndbuf = 5;
            if (setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
                __android_log_print(ANDROID_LOG_WARN, "SpeakerEasy", "setsockopt SO_SNDBUF failed: %s", strerror(errno));
            } else {
                __android_log_print(ANDROID_LOG_DEBUG, "SpeakerEasy", "Set SO_SNDBUF to %d", sndbuf);
            }
        }
    }
}

SpeakerEasy::~SpeakerEasy() {
    if (_connected) {
        sendNote(0);
        JNI::bluetoothDisconnect();
    }
}

bool SpeakerEasy::isConnected() const {
    return _connected;
}

void SpeakerEasy::writePacket(uint16 freq, uint16 delta) {
    uint8 packet[5];
    buildPacket(packet, CMD_STREAM_NOTE_DELTA, freq, delta);

    // DEBUG REMOVEME
    __android_log_print(ANDROID_LOG_DEBUG, "SpeakerEasy", "writePacket freq=%u delta=%u", freq, delta);

    write(_fd, packet, 5);
}

} // End of namespace Audio

#elif defined(WIN32) || defined(_WIN32)
// ----------------------------------------------------------------------------
// Windows Implementation - Serial port via Win32 API
// ----------------------------------------------------------------------------

namespace Audio {

SpeakerEasy::SpeakerEasy(const char *portName) : _connected(false), _lastSentFreq(0), _accumulatedDelta(0), _hSerial(INVALID_HANDLE_VALUE) {
    memset(&_overlapped, 0, sizeof(_overlapped));

    // Open with FILE_FLAG_OVERLAPPED for async I/O
    _hSerial = CreateFileA(portName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (_hSerial == INVALID_HANDLE_VALUE) {
        warning("SpeakerEasy: CreateFileA failed for %s (error %lu)", portName, GetLastError());
        return;
    }

    DCB dcbSerialParams;
    memset(&dcbSerialParams, 0, sizeof(dcbSerialParams));
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(_hSerial, &dcbSerialParams)) {
        warning("SpeakerEasy: GetCommState failed (error %lu)", GetLastError());
        CloseHandle(_hSerial);
        _hSerial = INVALID_HANDLE_VALUE;
        return;
    }

    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(_hSerial, &dcbSerialParams)) {
        warning("SpeakerEasy: SetCommState failed (error %lu)", GetLastError());
        CloseHandle(_hSerial);
        _hSerial = INVALID_HANDLE_VALUE;
        return;
    }

    _connected = true;
}

SpeakerEasy::~SpeakerEasy() {
    if (_connected) {
        sendNote(0);
        CloseHandle(_hSerial);
    }
}

bool SpeakerEasy::isConnected() const {
    return _connected;
}

void SpeakerEasy::writePacket(uint16 freq, uint16 delta) {
    uint8 packet[5];
    buildPacket(packet, CMD_STREAM_NOTE_DELTA, freq, delta);

    // DEBUG REMOVEME
    debug(1, "SpeakerEasy: writePacket freq=%u delta=%u time=%lu", freq, delta, GetTickCount());

    // Fire & forget async write
    WriteFile(_hSerial, packet, 5, NULL, &_overlapped);
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

SpeakerEasy::SpeakerEasy(const char *portName) : _connected(false), _lastSentFreq(0), _accumulatedDelta(0), _fd(-1) {
    _fd = open(portName, O_WRONLY | O_NOCTTY | O_NDELAY);
    if (_fd >= 0) {
        struct termios tty;
        memset(&tty, 0, sizeof(tty));

        if (tcgetattr(_fd, &tty) == 0) {
            cfsetospeed(&tty, B115200);
            cfsetispeed(&tty, B115200);

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
            }
        }
    }
}

SpeakerEasy::~SpeakerEasy() {
    if (_connected) {
        sendNote(0);
        close(_fd);
    }
}

bool SpeakerEasy::isConnected() const {
    return _connected;
}

void SpeakerEasy::writePacket(uint16 freq, uint16 dur) {
    uint8 packet[5];
    buildPacket(packet, CMD_STREAM_NOTE, freq, dur);

    // DEBUG REMOVEME
    debug(1, "SpeakerEasy: writePacket freq=%u dur=%u", freq, dur);

    write(_fd, packet, 5);
}

} // End of namespace Audio

#else
// ----------------------------------------------------------------------------
// Stub Implementation - No serial support on this platform
// ----------------------------------------------------------------------------

namespace Audio {

SpeakerEasy::SpeakerEasy(const char *portName) : _connected(false), _lastSentFreq(0), _accumulatedDelta(0) {
    warning("SpeakerEasy: Not supported on this platform");
}

SpeakerEasy::~SpeakerEasy() {
}

bool SpeakerEasy::isConnected() const {
    return false;
}

void SpeakerEasy::writePacket(uint16 freq, uint16 dur) {
}

} // End of namespace Audio

#endif
