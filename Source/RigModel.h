#pragma once

#include <JuceHeader.h>
#include <vector>
#include <set>

namespace OpenRig {

struct PluginState {
    juce::String name;
    juce::String path;
    juce::String stateBase64;
    int uid = 0;

    // Per-instrument stacking properties
    int lowNote = 0;
    int highNote = 127;
    float level = 1.0f;
    bool enabled = true;
};

struct CCMapping {
    int cc = -1;
    int chainIndex = -1;
    juce::String paramId;
    int parameterIndex = -1;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool invert = false;
};

struct CCPassthrough {
    int incomingCC = -1;
    int outgoingCC = -1;
};

struct ChannelStripSettings {
    bool gateEnabled = false;
    float gateThreshold = -40.0f;
    bool eqEnabled = false;
    float hpfFreq = 20.0f;
    float lowShelfGain = 0.0f;
    float highShelfGain = 0.0f;
    bool compEnabled = false;
    float compAmount = 0.0f;
    bool chorusEnabled = false;
    float chorusRate = 1.0f;
    float chorusMix = 0.0f;
    bool reverbEnabled = false;
    float reverbSize = 0.5f;
    float reverbMix = 0.0f;
};

struct SamplerSlotSettings {
    juce::String wavPath;
    int rootNote = 60;
    int keyLow = 0;
    int keyHigh = 127;
    float pitchOffsetSemitones = 0.0f;
    float volume = 1.0f;
    float startRatio = 0.0f;
    float endRatio = 1.0f;
};

struct SamplerSettings {
    bool enabled = false;
    SamplerSlotSettings slots[8];
};

struct SongSlot {
    juce::String name;
    int iconIndex = 0;
    juce::Colour channelColor{0xff2a2a2a};
    float level = 0.8f;
    bool fohEnabled = true;
    bool iemEnabled = true;
    bool bypassed = false;
    int inputChannelIndex = -1;
    float aux1Send = 0.0f;
    float aux2Send = 0.0f;
    float iemOffset = 1.0f;
    int transposeOctaves = 0;
    int transposeSemitones = 0;
    int lowNote = 0;
    int highNote = 127;
    std::set<int> allowedCCs;
    int fohCC = -1;
    int iemCC = -1;
    int midiChannelOverride = -1; // -1 means use global default

    ChannelStripSettings strip;
    std::vector<CCMapping> ccMappings;
    std::vector<CCPassthrough> ccPassthroughs;

struct ArpParams {
    bool enabled = false;
    float bpm = 127.0f;
    int octavesUp = 2;
    int octavesDown = 0;
    float gate = 0.75f;
    int patternIdx = 3; // 0=Up, 1=Down, 2=UpDown, 3=Random
};

struct HarmParams {
    bool enabled = false;
    int octavesUp = 1;
    int octavesDown = 0;
    int africaMode = 0;
    int harmonyTargetSlot = -1; // -1 = local VST, >=0 = route generated notes to this slot
};

    ArpParams arpeggiator;
    HarmParams harmonizer;
    SamplerSettings sampler;

    std::vector<PluginState> chain;
};

struct SlotState {
    bool bypassed = false;
    float channelLevel = 0.8f;
    bool fohEnabled = true;
    bool iemEnabled = true;
};

struct Scene {
    juce::String name;
    std::vector<SlotState> slotStates;
};

struct Song {
    juce::String name;
    float fohMasterLevel = 1.0f;
    float iemMasterLevel = 1.0f;
    int fohOutputOffset = 0;
    int iemOutputOffset = 2;
    std::vector<PluginState> fohFx;
    std::vector<PluginState> iemFx;
    std::vector<SongSlot> slots;
    std::vector<Scene> scenes;
    int currentSceneIndex = 0;
};

struct Set {
    juce::String name;
    std::vector<juce::String> songNames; // Or we can store paths/references
    std::vector<Song> songs;
};

} // namespace OpenRig
