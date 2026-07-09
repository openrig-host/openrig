#pragma once
#include <JuceHeader.h>
#include "ThemeManager.h"

/**
 * A simple full-screen overlay to block interaction and show feedback
 * while plugins are loading or scenes are changing.
 */
class LoadingOverlay : public juce::Component, public juce::Timer {
public:
  LoadingOverlay() {
    setAlwaysOnTop(true);
    startTimer(30);
  }

  void setTitle(const juce::String &t) { title = t; }
  void setMessage(const juce::String &m) { message = m; }

  void paint(juce::Graphics &g) override {
    // Semi-transparent black wash
    g.fillAll(juce::Colours::black.withAlpha(0.7f));

    auto bounds = getLocalBounds().toFloat();
    auto center = bounds.getCentre();

    // Pulse effect
    float pulse =
        (float)(std::sin(juce::Time::getMillisecondCounterHiRes() * 0.005) +
                1.0) *
        0.5f;
    float alpha = 0.6f + (pulse * 0.4f);

    // Rounded box in center
    float w = 300, h = 120;
    juce::Rectangle<float> box(center.x - w / 2, center.y - h / 2, w, h);

    // Themed background
    g.setColour(ThemeManager::get(Theme::Role::accentDim));
    g.fillRoundedRectangle(box, 12.0f);

    // Border
    g.setColour(ThemeManager::get(Theme::Role::knobThumb).withAlpha(0.4f));
    g.drawRoundedRectangle(box, 12.0f, 2.0f);

    // Main text - use Font object explicitly
    auto mainFont = juce::Font(juce::FontOptions(28.0f));
    g.setColour(juce::Colours::white);
    g.setFont(mainFont);
    auto textArea = box.reduced(20);
    g.drawText(title.isEmpty() ? "LOADING..." : title, textArea.removeFromTop(50),
               juce::Justification::centred, false);

    // Subtitle
    auto subFont = juce::Font(juce::FontOptions(14.0f));
    g.setColour(juce::Colours::white.withAlpha(alpha));
    g.setFont(subFont);
    g.drawText(message.isEmpty() ? "Preparing your rig..." : message, textArea,
               juce::Justification::centred, false);
  }

  void timerCallback() override { repaint(); }

  // Intercept all mouse events to prevent clicking through
  void mouseDown(const juce::MouseEvent &) override {}
  void mouseUp(const juce::MouseEvent &) override {}
  void mouseDrag(const juce::MouseEvent &) override {}
  void mouseMove(const juce::MouseEvent &) override {}

private:
  juce::String title;
  juce::String message;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoadingOverlay)
};
