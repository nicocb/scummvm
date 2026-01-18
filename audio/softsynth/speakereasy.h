#ifndef AUDIO_SPEAKEASY_H
#define AUDIO_SPEAKEASY_H

#include "common/scummsys.h"

// Platform-specific headers for member variables
#if defined(WIN32) || defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Audio {

/**
 * SpeakerEasy Driver - External PC Speaker via Serial/Bluetooth
 * Windows: Serial port via Win32 API
 * Linux/macOS: Serial port via POSIX termios
 * Android: Bluetooth SPP via JNI
 */
class SpeakerEasy {
public:
    SpeakerEasy(const char *portName);
    ~SpeakerEasy();
    bool isConnected() const;
    void sendNote(uint16 freq, uint16 delta = 0);

    /**
     * Factory method that creates a SpeakerEasy instance based on ConfMan settings.
     * Returns nullptr if SpeakerEasy is disabled or port is not configured.
     * On Android, 'port' is the Bluetooth device name (e.g., "spkr-ez").
     */
    static SpeakerEasy *create();

private:
    void writePacket(uint16 freq, uint16 dur);  // Actually send to hardware

    bool _connected;
    uint16 _lastSentFreq;
    uint32 _accumulatedDelta;  // Accumulated time in ms since last sent note
#if defined(WIN32) || defined(_WIN32)
    HANDLE _hSerial;
    OVERLAPPED _overlapped;
#elif defined(__ANDROID__) || defined(__POSIX__) || defined(__linux__) || defined(__APPLE__)
    int _fd;
#endif
};

} // End of namespace Audio

#endif
