// SPDX-License-Identifier: GPL-3.0-only
// The FM-1's user sample slots from Floop Studio: audio files -> a slot (SampleSlot.h), the slots'
// contents (SMP_INFO), the upload (SMP_BEGIN / SMP_WRITE / SMP_END, as sloop-fm1
// tools/fm1_sample_upload.py) and, for a drum kit, KIT = USRn on the drum track.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "FM1Import.h"
#include "SampleSlot.h"

namespace sloop {

// A file -> mono int16 at 22050 Hz, peak-normalised (as the web editor). False + error in Spanish.
bool loadSound(const juce::File&, std::vector<int16_t>& out, juce::String& error);
bool isSoundFile(const juce::String& path);

struct SlotInfo { int zones = 0; juce::String name; int kib = 0; };

namespace smpcmd { enum : uint8_t { BEGIN = 11, WRITE = 12, END = 13, ERASE = 14, INFO = 15, DESC = 5 }; }

// SMP_INFO: what each USR slot holds (zones 0 = empty).
bool readSlots(Fm1Connection&, std::vector<SlotInfo>&, juce::String& error);

// Erases the slot, writes the data in 256-byte blocks, then the header (the FM-1 checks the CRC).
// progress: 0..1. False + error in Spanish.
bool uploadSlot(Fm1Connection&, int slot, const smp::Slot&, const std::function<void(float)>& progress,
                juce::String& error);

// The drum track plays USR(slot+1): KIT (its P_E0) set to that entry of the KIT list (DESC), the
// FM-1's selected track put back as it was.
bool useAsDrumKit(Fm1Connection&, int slot, int pE0, juce::String& error);

} // namespace sloop
