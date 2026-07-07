#pragma once

#include <JuceHeader.h>
#include <vector>

/**
 * A Scene represents a snapshot of the entire rack's state.
 */
struct SlotState {
  bool bypassed = false;
  float fohLevel = 0.8f;
  float iemLevel = 1.0f;
};

struct Scene {
  juce::String name;
  std::vector<SlotState> slotStates;

  Scene(const juce::String &n) : name(n) {}
};
