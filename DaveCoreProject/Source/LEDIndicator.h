#pragma once
#include <JuceHeader.h>

class LEDIndicator : public juce::Component, private juce::Timer {
public:
  LEDIndicator(const juce::Colour &colour = juce::Colours::lime)
      : ledColour(colour) {
    // No timer needed by default unless animating
  }

  ~LEDIndicator() override { stopTimer(); }

  void setState(bool isOn) {
    if (isOn) {
      currentIntensity = 1.0f;
      startTimer(50); // Start decay timer
    } else {
      // Immediate off? Or let decay handle it if we just pulse it?
      // Usually for MIDI we pulse it.
      // If we want static ON/OFF:
      // currentIntensity = 0.0f;
    }
    repaint();
  }

  // Trigger a flash that decays
  void flash() {
    currentIntensity = 1.0f;
    if (!isTimerRunning())
      startTimer(30);
    repaint();
  }

  void setIntensity(float intensity) {
    currentIntensity = juce::jlimit(0.0f, 1.0f, intensity);
    repaint();
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat().reduced(1);

    // Draw LED Bezel/Off state
    g.setColour(ledColour.withBrightness(0.2f).withAlpha(0.8f));
    g.fillEllipse(bounds);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawEllipse(bounds, 1.0f);

    // active part
    if (currentIntensity > 0.01f) {
      // Inner bright core
      g.setColour(ledColour.withAlpha(currentIntensity));
      g.fillEllipse(bounds.reduced(1.5f));

      // Glow center
      g.setColour(juce::Colours::white.withAlpha(currentIntensity * 0.8f));
      g.fillEllipse(bounds.reduced(3.0f));

      // Outer glow
      g.setColour(ledColour.withAlpha(currentIntensity * 0.4f));
      // g.fillEllipse(bounds.expanded(2)); // Don't draw outside bounds if
      // possible, or adjust bounds
    }
  }

private:
  void timerCallback() override {
    currentIntensity *= 0.85f; // Decay
    if (currentIntensity < 0.01f) {
      currentIntensity = 0.0f;
      stopTimer();
    }
    repaint();
  }

  juce::Colour ledColour;
  float currentIntensity = 0.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LEDIndicator)
};
