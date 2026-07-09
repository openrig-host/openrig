/*
  ==============================================================================

    DaveCore Engine
    The heart of the Sovereign Live Performance Rig.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

/**
 * A RackSlot represents a single channel strip in the DaveCore host.
 * It can hold a VST3 plugin or act as a raw hardware input.
 */
class RackSlot {
public:
  RackSlot(const juce::String &name) : slotName(name) {}
  ~RackSlot() = default;

  // --- Audio Logic ---
  void processBlock(juce::AudioBuffer<float> &slotBuffer,
                    juce::MidiBuffer &midiMessages) {
    if (bypassed)
      return;

    if (pluginInstance) {
      pluginInstance->processBlock(slotBuffer, midiMessages);
    }
  }

  /**
   * Sums the slot's buffer into the global FOH and IEM buses based on gain
   * levels.
   */
  void sumToBuses(const juce::AudioBuffer<float> &slotBuffer,
                  juce::AudioBuffer<float> &fohBus,
                  juce::AudioBuffer<float> &iemBus) {
    if (bypassed)
      return;

    int numSamples = slotBuffer.getNumSamples();

    for (int ch = 0; ch < slotBuffer.getNumChannels(); ++ch) {
      if (ch < fohBus.getNumChannels())
        fohBus.addFromWithRamp(ch, 0, slotBuffer.getReadPointer(ch), numSamples,
                               lastFohLevel, fohLevel);

      if (ch < iemBus.getNumChannels())
        iemBus.addFromWithRamp(ch, 0, slotBuffer.getReadPointer(ch), numSamples,
                               lastIemLevel, iemLevel);
    }

    lastFohLevel = fohLevel;
    lastIemLevel = iemLevel;
  }

  // --- Mix Controls ---
  void setFohLevel(float newLevel) {
    fohLevel = juce::jlimit(0.0f, 1.0f, newLevel);
  }
  void setIemLevel(float newLevel) {
    iemLevel = juce::jlimit(0.0f, 1.0f, newLevel);
  }

  void setBypass(bool shouldBypass) { bypassed = shouldBypass; }
  bool isBypassed() const { return bypassed; }

  float getFohLevel() const { return fohLevel; }
  float getIemLevel() const { return iemLevel; }

  juce::String getName() const { return slotName; }

  // --- Plugin Management ---
  void loadPlugin(std::unique_ptr<juce::AudioPluginInstance> newPlugin) {
    pluginInstance = std::move(newPlugin);
  }

  void setInputActive(bool active) { inputActive = active; }
  bool isInputActive() const { return inputActive; }

private:
  juce::String slotName;
  std::unique_ptr<juce::AudioPluginInstance> pluginInstance;

  float fohLevel = 0.8f;
  float iemLevel = 1.0f;
  float lastFohLevel = 0.8f;
  float lastIemLevel = 1.0f;
  bool bypassed = false;
  bool inputActive = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackSlot)
};
