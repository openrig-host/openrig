#pragma once

#include <JuceHeader.h>
#include <vector>

/**
 * A Scene represents a snapshot of the entire rack's state.
 */
struct SlotState {
  bool bypassed = false;
  float channelLevel = 0.8f;
  bool fohEnabled = true;
  bool iemEnabled = true;
};

struct Scene {
  juce::String name;
  std::vector<SlotState> slotStates;

  Scene(const juce::String &n) : name(n) {}
};
