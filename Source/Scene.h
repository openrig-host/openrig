#pragma once

#include <JuceHeader.h>
#include <vector>

/**
 * A Scene represents a snapshot of the entire rack's state.
 */
struct SlotState {
  bool bypassed = false;
  float fohLevel = 0.8f;
  float iemLevel = 0.8f;
  bool fohEnabled = true;
  bool iemEnabled = true;
  bool fadersLinked = true;
  float aux1Send = 0.0f;
  float aux2Send = 0.0f;
  float iemOffset = 1.0f;
  int lowNote = 0;
  int highNote = 127;
  int transposeOctaves = 0;
  int transposeSemitones = 0;

  // DSP (EQ / Dynamics)
  bool gateEnabled = false;
  float gateThreshold = -60.0f;
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

  // IR / convolution reverb
  bool irEnabled = false;
  float irMix = 0.3f;
  juce::String irPath;

  // Arpeggiator
  bool arpEnabled = false;
  float arpBpm = 120.0f;
  int arpOctavesUp = 1;
  int arpOctavesDown = 0;
  float arpGate = 0.9f;
  int arpPatternIdx = 0;

  // Harmonizer
  bool harmEnabled = false;
  int harmOctavesUp = 1;
  int harmOctavesDown = 0;
  int harmAfricaMode = 0;
  int harmTargetSlot = -1;

  // Sampler
  bool samplerEnabled = false;
};

struct Scene {
  juce::String name;
  std::vector<SlotState> slotStates;

  // Optional MIDI trigger: -1 means "no assignment" (use index-based fallback)
  int midiProgramChange = -1;  // 0-127
  int midiChannel = 0;         // 0 = any channel

  Scene(const juce::String &n) : name(n) {}
};
