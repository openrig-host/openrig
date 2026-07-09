#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "RackSlot.h"
#include "BoutiqueLookAndFeel.h"
#include "ThemeManager.h"
#include "IMidiNoteLearner.h"

/**
 * A component that displays a piano keyboard and allows selecting a MIDI note
 * range using two draggable handles. Includes a "Learn" mode to capture range
 * from incoming MIDI notes.
 */
class NoteRangeComponent : public juce::Component, public juce::Timer, public IMidiNoteLearner {
public:
  NoteRangeComponent(RackSlot &s) : slot(s) {
    lowNote = slot.getLowNote();
    highNote = slot.getHighNote();

    // Start in learn mode automatically on open
    learning = true;
    learnedLow = -1;
    learnedHigh = -1;
    startTimerHz(30); // Flash effect for visual feedback

    // Done Button (replaces Start/Stop)
    learnButton.setButtonText("DONE");
    learnButton.setColour(juce::TextButton::buttonColourId,
                          ThemeManager::get(Theme::Role::ok));
    learnButton.onClick = [this] {
      if (auto* co = findParentComponentOfClass<juce::CallOutBox>()) {
        co->dismiss();
      }
    };
    addAndMakeVisible(learnButton);

    // Reset Button
    resetButton.setButtonText("RESET");
    resetButton.setColour(juce::TextButton::buttonColourId,
                          ThemeManager::get(Theme::Role::danger));
    resetButton.onClick = [this] {
      learnedLow = -1;
      learnedHigh = -1;
      lowNote = 0;
      highNote = 127;
      slot.setNoteRange(lowNote, highNote);
      repaint();
    };
    addAndMakeVisible(resetButton);
  }

  ~NoteRangeComponent() override { stopTimer(); }

  void resized() override {
    auto r = getLocalBounds();
    auto buttonArea = r.removeFromBottom(30).reduced(10, 2);
    learnButton.setBounds(buttonArea.removeFromLeft(80));
    buttonArea.removeFromLeft(10);
    resetButton.setBounds(buttonArea.removeFromLeft(80));
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();

    auto* laf = dynamic_cast<BoutiqueLookAndFeel*>(&getLookAndFeel());
    bool useModern = (laf != nullptr && laf->useModernStyle);

    // Draw background
    g.fillAll(ThemeManager::get(Theme::Role::panel));

    // Draw Border
    g.setColour(ThemeManager::get(Theme::Role::border));
    g.drawRect(bounds, 1.0f);

    // Keyboard area (above the buttons)
    auto keyboardArea = bounds.reduced(10.0f, 5.0f);
    keyboardArea.removeFromBottom(35.0f);
    keyboardArea.removeFromTop(25.0f);

    // Draw Piano Keyboard
    drawKeyboard(g, keyboardArea);

    // Draw Overlay for active range
    drawRangeOverlay(g, keyboardArea);

    // Labels
    g.setColour(ThemeManager::get(Theme::Role::text));
    g.setFont(14.0f);
    g.drawText("Low: " + midiNoteToName(lowNote), 12, 5, 100, 20,
               juce::Justification::left);
    g.drawText("High: " + midiNoteToName(highNote), getWidth() - 112, 5, 100,
               20, juce::Justification::right);

    // Title / Learn Mode indicator
    g.setColour(ThemeManager::get(Theme::Role::foh));
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText("PLAY NOTES OR DRAG KEYBOARD!", 0, 5, getWidth(), 20,
               juce::Justification::centred);
  }

  void mouseDown(const juce::MouseEvent &e) override {
    learnedLow = -1;
    learnedHigh = -1;
    updateNoteFromMouse(e.position);
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    learnedLow = -1;
    learnedHigh = -1;
    updateNoteFromMouse(e.position);
  }

  // Called by MainComponent to feed MIDI notes when learning
  void handleMidiNote(int noteNumber) override {
    if (!learning)
      return;

    // First note sets both low and high
    if (learnedLow < 0) {
      learnedLow = noteNumber;
      learnedHigh = noteNumber;
    } else {
      learnedLow = std::min(learnedLow, noteNumber);
      learnedHigh = std::max(learnedHigh, noteNumber);
    }

    lowNote = learnedLow;
    highNote = learnedHigh;
    slot.setNoteRange(lowNote, highNote);
    repaint();
  }

  bool isLearning() const override { return learning; }

private:
  RackSlot &slot;
  int lowNote = 0;
  int highNote = 127;

  juce::TextButton learnButton;
  juce::TextButton resetButton;

  bool learning = false;
  int learnedLow = -1;
  int learnedHigh = -1;

  void timerCallback() override {
    // Flash effect while learning
    repaint();
  }

  void drawKeyboard(juce::Graphics &g, juce::Rectangle<float> area) {
    float noteWidth = area.getWidth() / 128.0f;

    for (int i = 0; i < 128; ++i) {
      bool isBlack = isBlackKey(i);
      juce::Rectangle<float> noteRect(area.getX() + i * noteWidth, area.getY(),
                                      noteWidth, area.getHeight());

      if (!isBlack) {
        g.setColour(juce::Colours::white);
        g.fillRect(noteRect.reduced(0.5f));

        // Draw Octave labels (C-1, C0, etc)
        if (i % 12 == 0) {
          g.setColour(juce::Colours::black.withAlpha(0.5f));
          g.setFont(10.0f);
          g.drawText("C" + juce::String(i / 12 - 1), noteRect,
                     juce::Justification::centredBottom);
        }
      }
    }

    // Draw black keys on top
    for (int i = 0; i < 128; ++i) {
      if (isBlackKey(i)) {
        juce::Rectangle<float> noteRect(area.getX() + i * noteWidth,
                                        area.getY(), noteWidth,
                                        area.getHeight() * 0.6f);
        g.setColour(juce::Colours::black);
        g.fillRect(noteRect);
      }
    }
  }

  void drawRangeOverlay(juce::Graphics &g, juce::Rectangle<float> area) {
    float noteWidth = area.getWidth() / 128.0f;
    float x1 = area.getX() + lowNote * noteWidth;
    float x2 = area.getX() + (highNote + 1) * noteWidth;

    // Dim the inactive areas
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRect(area.withWidth(x1 - area.getX()));
    g.fillRect(area.withX(x2).withWidth(area.getRight() - x2));

    // Highlight the active area
    auto highlightColor = learning ? ThemeManager::get(Theme::Role::foh).withAlpha(0.4f)
                                   : juce::Colours::gold.withAlpha(0.4f);
    g.setColour(highlightColor);
    g.fillRect(
        juce::Rectangle<float>(x1, area.getY(), x2 - x1, area.getHeight()));

    // Draw Handles
    g.setColour(learning ? ThemeManager::get(Theme::Role::foh) : juce::Colours::gold);
    g.drawVerticalLine((int)x1, area.getY() - 5, area.getBottom() + 5);
    g.drawVerticalLine((int)x2, area.getY() - 5, area.getBottom() + 5);
  }

  bool isBlackKey(int note) {
    int n = note % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
  }

  juce::String midiNoteToName(int note) {
    static const char *names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                  "F#", "G",  "G#", "A",  "A#", "B"};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
  }

  void updateNoteFromMouse(juce::Point<float> p) {
    auto area = getLocalBounds().toFloat().reduced(10.0f, 5.0f);
    area.removeFromBottom(35.0f);
    area.removeFromTop(25.0f);

    if (!area.contains(p))
      return;

    float normalizedX = (p.getX() - area.getX()) / area.getWidth();
    int note = juce::jlimit(0, 127, (int)(normalizedX * 128.0f));

    if (std::abs(note - lowNote) < std::abs(note - highNote)) {
      lowNote = note;
    } else {
      highNote = note;
    }

    int actualLow = std::min(lowNote, highNote);
    int actualHigh = std::max(lowNote, highNote);

    slot.setNoteRange(actualLow, actualHigh);
    repaint();
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteRangeComponent)
};
