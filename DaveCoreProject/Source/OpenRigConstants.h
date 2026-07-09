#pragma once

// ==============================================================================
// OpenRig Constants
// Central location for all magic numbers and configuration values
// ==============================================================================

namespace OpenRigConstants {

// --- Slot Configuration ---
constexpr int kNumSlots = 12;
constexpr int kNumFxSlotsPerBus = 3;
constexpr int kNumSetupButtons = 10;

// --- Audio Configuration ---
constexpr int kMaxInternalChannels =
    32; // Internal buffer size for multi-out VST3s
constexpr int kDefaultInputChannels = 18; // Tascam US-1800 / Behringer UMC1820
constexpr int kDefaultOutputChannels = 8;
constexpr int kDefaultSampleRate = 44100;
constexpr int kDefaultBufferSize = 512;

// --- Default Hardware Input Mappings (Tascam US-1800) ---
constexpr int kMonitorInputChannel = 0;    // Hardware In 1
constexpr int kKeyboardInputChannel = 10;   // Hardware In 11 (previously RD88, now CK88)
constexpr int kAccordionInputChannel = 12; // Hardware In 13

// --- Output Bus Routing ---
constexpr int kDefaultFohOutputOffset = 0; // Channels 1+2
constexpr int kDefaultIemOutputOffset = 2; // Channels 3+4

// --- UI Configuration ---
constexpr int kDefaultWindowWidth = 1000;
constexpr int kDefaultWindowHeight = 700;
constexpr int kTimerIntervalMs = 50;
constexpr int kMeterSegments = 16;

// --- MIDI Configuration ---
constexpr int kDefaultMidiChannel = 1;
constexpr int kSustainPedalCC = 64;

// --- CK88 (Roland) Default CCs ---
// Pre-configured CC numbers for the CK88's physical knobs/sliders so the
// host can auto-allow them without requiring the user to discover each one.
constexpr int kCk88SlotIndex = 1;
constexpr int kCk88ModWheelCC = 9;     // CK88 mod wheel (non-standard)
constexpr int kCk88SustainCC = 64;     // Sustain pedal
constexpr int kCk88ChorusCC = 93;      // Chorus / delay knob 1
constexpr int kCk88ReverbCC = 92;      // Reverb / delay knob 2
// B3 drawbars (Hammond organ registration)
constexpr int kCk88Drawbar16CC = 77;   // 16' drawbar
constexpr int kCk88Drawbar513CC = 78;  // 5-1/3' drawbar
constexpr int kCk88Drawbar8CC = 79;    // 8' drawbar
constexpr int kCk88Drawbar4CC = 80;    // 4' drawbar
constexpr int kCk88Drawbar223CC = 81;  // 2-2/3' drawbar
constexpr int kCk88Drawbar2CC = 82;    // 2' drawbar
constexpr int kCk88Drawbar135CC = 83;  // 1-3/5' drawbar
constexpr int kCk88Drawbar113CC = 85;  // 1-1/3' drawbar
constexpr int kCk88Drawbar1CC = 86;    // 1' drawbar

// --- Consolidated Paths ---
inline juce::File getAppDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("OpenRig");
}
inline juce::File getSongsDirectory() {
    return getAppDirectory().getChildFile("songs");
}
inline juce::File getSetsDirectory() {
    return getAppDirectory().getChildFile("sets");
}
inline juce::File getBackupsDirectory() {
    return getAppDirectory().getChildFile("backups");
}
inline juce::File getSettingsDirectory() {
    return getAppDirectory().getChildFile("settings");
}

} // namespace OpenRigConstants

