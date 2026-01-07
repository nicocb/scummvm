#ifndef AUDIO_SPEAKEASY_H
#define AUDIO_SPEAKEASY_H

#include "common/scummsys.h"
#include "common/config-manager.h"
#include "common/debug.h"

#ifdef FORBIDDEN_SYMBOL_EXCEPTION_printf
#undef FORBIDDEN_SYMBOL_EXCEPTION_printf
#endif

// Platform-specific headers (non-Android only - Android uses JNI in .cpp)
#if !defined(__ANDROID__)
#if defined(WIN32) || defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__POSIX__) || defined(__linux__) || defined(__APPLE__)
#define SPEAKEREASY_POSIX
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#endif
#endif // !__ANDROID__

namespace Audio {

/**
 * SpeakerEasy Driver - External PC Speaker via Serial/Bluetooth
 * Windows/Linux/macOS: Serial port (COM port or /dev/tty*)
 * Android: Bluetooth SPP via JNI (implementation in speakereasy.cpp)
 */
class SpeakerEasy {
public:
#if defined(__ANDROID__)
    // Android implementation in speakereasy.cpp
    SpeakerEasy(const char *portName);
    ~SpeakerEasy();
    bool isConnected() const;
    void sendNote(uint16 freq, uint16 dur = 0);
#else
    // Desktop implementation inline
    SpeakerEasy(const char *portName) : _fd(-1), _connected(false) {
#if defined(WIN32) || defined(_WIN32)
        _hSerial = INVALID_HANDLE_VALUE;
        _hSerial = CreateFileA(portName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

        if (_hSerial != INVALID_HANDLE_VALUE) {
            DCB dcbSerialParams;
            memset(&dcbSerialParams, 0, sizeof(dcbSerialParams));
            dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

            if (GetCommState(_hSerial, &dcbSerialParams)) {
                dcbSerialParams.BaudRate = CBR_115200;
                dcbSerialParams.ByteSize = 8;
                dcbSerialParams.StopBits = ONESTOPBIT;
                dcbSerialParams.Parity = NOPARITY;
                dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
                dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;

                if (SetCommState(_hSerial, &dcbSerialParams)) {
                    _connected = true;
                }
            }
        }
#elif defined(SPEAKEREASY_POSIX)
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
#endif
    }

    ~SpeakerEasy() {
#if defined(WIN32) || defined(_WIN32)
        if (_connected && _hSerial != INVALID_HANDLE_VALUE) {
            sendNote(0);
            CloseHandle(_hSerial);
        }
#elif defined(SPEAKEREASY_POSIX)
        if (_connected && _fd >= 0) {
            sendNote(0);
            close(_fd);
        }
#endif
    }

    bool isConnected() const { return _connected; }

    void sendNote(uint16 freq, uint16 dur = 0) {
        if (!_connected) return;

        uint8 packet[5];
        packet[0] = 0x06;
        packet[1] = (uint8)(freq & 0xFF);
        packet[2] = (uint8)((freq >> 8) & 0xFF);
        packet[3] = (uint8)(dur & 0xFF);
        packet[4] = (uint8)((dur >> 8) & 0xFF);

#if defined(WIN32) || defined(_WIN32)
        if (_hSerial == INVALID_HANDLE_VALUE) return;
        DWORD bytesWritten;
        WriteFile(_hSerial, packet, 5, &bytesWritten, NULL);
#elif defined(SPEAKEREASY_POSIX)
        if (_fd < 0) return;
        write(_fd, packet, 5);
#endif
    }
#endif // !__ANDROID__

    /**
     * Factory method that creates a SpeakerEasy instance based on ConfMan settings.
     * Returns nullptr if SpeakerEasy is disabled or port is not configured.
     * On Android, 'port' is the Bluetooth device name (e.g., "Speaker Easy").
     */
    static SpeakerEasy *create() {
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
        debug(1, "SpeakerEasy: Connected to %s", port.c_str());
        return se;
    }

private:
#if defined(WIN32) || defined(_WIN32)
    HANDLE _hSerial;
#endif
#if !defined(__ANDROID__)
    int _fd;
#endif
    bool _connected;
};

} // End of namespace Audio

#endif
