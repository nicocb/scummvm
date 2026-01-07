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

// Allow use of forbidden symbols needed by Android JNI headers
// These must be defined BEFORE any includes
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h
#define FORBIDDEN_SYMBOL_EXCEPTION_printf

#include "audio/softsynth/speakereasy.h"

#if defined(__ANDROID__)
#include "backends/platform/android/jni-android.h"

namespace Audio {

SpeakerEasy::SpeakerEasy(const char *portName) : _connected(false) {
    // Request Bluetooth permission first (required on Android 12+)
    if (!JNI::hasBluetoothPermission()) {
        JNI::requestBluetoothPermission();
        // Permission will be granted asynchronously, connection will fail this time
        // User needs to try again after granting permission
        return;
    }
    _connected = JNI::bluetoothConnect(portName);
}

SpeakerEasy::~SpeakerEasy() {
    if (_connected) {
        sendNote(0);
        JNI::bluetoothDisconnect();
    }
}

bool SpeakerEasy::isConnected() const {
    return JNI::bluetoothIsConnected();
}

void SpeakerEasy::sendNote(uint16 freq, uint16 dur) {
    if (!_connected) return;
    JNI::bluetoothSendNote(freq, dur);
}

} // End of namespace Audio

#endif // __ANDROID__
