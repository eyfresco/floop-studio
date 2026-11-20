// SPDX-License-Identifier: GPL-3.0-only
// Sending to the FM-1 with CoreMIDI's MIDI 1.0 API (MIDISend, packet lists), on macOS.
//
// Why: JUCE 8 sends through CoreMIDI's MIDI 2.0 (UMP) API on macOS 11+, and macOS converts that
// for the FM-1's USB-MIDI 1.0 port. Long SysEx (a sample upload's 300-byte frames) arrived
// damaged that way (the FM-1 refused the slot: data CRC), while the same bytes sent with MIDISend
// - what Chrome uses for the SLOOP web editor - went through. Tested 16 Nov 2026, SLOOP 2.8.5.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <string>

namespace sloop {

class RawMidiOut
{
public:
    // The first CoreMIDI destination whose name matches (nullptr: none, or not macOS).
    static std::unique_ptr<RawMidiOut> open(const std::function<bool(const std::string&)>& nameMatches);
    ~RawMidiOut();

    bool send(const uint8_t* bytes, size_t n);   // one complete MIDI message (SysEx with F0 ... F7)
    bool stillThere() const;

private:
    RawMidiOut() = default;
    friend class VirtualMidiSource;
    uint32_t endpoint = 0;   // MIDIEndpointRef
    int32_t uniqueId = 0;
};

// A MIDI source of our own, "Floop Studio", that FL Studio sees as a controller (its script,
// Scripts/FL/device_SLOOP_Studio.py, turns the FM-1's PLAY / STOP into FL's). One per process.
class VirtualMidiSource
{
public:
    static VirtualMidiSource& get();
    bool available() const { return source != 0; }
    bool send(const uint8_t* bytes, size_t n);

    // The bridge's messages (channel 16): the FM-1 started / stopped while FL was not / was playing.
    static constexpr uint8_t kChannelStatus = 0xBF, kPlayCc = 118, kStopCc = 119;

private:
    VirtualMidiSource();
    uint32_t source = 0;   // MIDIEndpointRef
};

} // namespace sloop
