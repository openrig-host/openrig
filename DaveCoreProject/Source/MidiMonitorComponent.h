#pragma once

#include <JuceHeader.h>
#include "BoutiqueLookAndFeel.h"
#include "MidiLearnBus.h"
#include "OpenRigConstants.h"

class MidiMonitorComponent : public juce::Component, public juce::Timer {
public:
  struct MidiEvent {
    juce::String text;
    juce::Colour colour;
    juce::int64 timestamp;
    bool isNoteOn;
    int noteNumber;
    int velocity;
    int ccNumber;
    int ccValue;
  };

  MidiMonitorComponent() {
    startTimer(30);
    setInterceptsMouseClicks(false, false);
  }

  ~MidiMonitorComponent() override { stopTimer(); }

  void addMidiMessage(const juce::MidiMessage &msg) {
    juce::int64 now = juce::Time::getCurrentTime().toMilliseconds();

    if (msg.isNoteOn()) {
      MidiEvent ev;
      ev.text = "Note On  " +
               juce::MidiMessage::getMidiNoteName(msg.getNoteNumber(), true, true,
                                                   4) +
               "  vel:" + juce::String(msg.getVelocity());
      ev.colour = juce::Colour(0xff00cc66);
      ev.timestamp = now;
      ev.isNoteOn = true;
      ev.noteNumber = msg.getNoteNumber();
      ev.velocity = msg.getVelocity();
      pushEvent(ev);
    } else if (msg.isNoteOff()) {
      MidiEvent ev;
      ev.text = "Note Off " +
               juce::MidiMessage::getMidiNoteName(msg.getNoteNumber(), true, true,
                                                   4);
      ev.colour = juce::Colour(0xff006633);
      ev.timestamp = now;
      ev.isNoteOn = false;
      ev.noteNumber = msg.getNoteNumber();
      pushEvent(ev);
    } else if (msg.isController()) {
      MidiEvent ev;
      bool armed = OpenRig::MidiLearnBus::getInstance().isArmed();
      juce::String text = "CC" + juce::String(msg.getControllerNumber()).paddedLeft('0', 3) +
                          " = " + juce::String(msg.getControllerValue()).paddedLeft('0', 3);
      if (armed) {
          text += " [CAPTURED]";
      }
      ev.text = text;
      ev.colour = armed ? juce::Colours::gold : juce::Colour(0xff3399ff);
      ev.timestamp = now;
      ev.isNoteOn = false;
      ev.ccNumber = msg.getControllerNumber();
      ev.ccValue = msg.getControllerValue();
      pushEvent(ev);
    } else if (msg.isPitchWheel()) {
      MidiEvent ev;
      int val = msg.getPitchWheelValue();
      ev.text = "PitchBend " + juce::String(val);
      ev.colour = juce::Colour(0xffff6633);
      ev.timestamp = now;
      ev.isNoteOn = false;
      pushEvent(ev);
    } else if (msg.isAftertouch()) {
      MidiEvent ev;
      ev.text = "Aftertouch ch:" + juce::String(msg.getChannel()) +
                " val:" + juce::String(msg.getAfterTouchValue());
      ev.colour = juce::Colour(0xffcc66ff);
      ev.timestamp = now;
      ev.isNoteOn = false;
      pushEvent(ev);
    }
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colour(0xff0a0a0a));

    int rowH = 18;
    int topOffset = 0;

    // --- CC-learn banner (reserved top row) ---
    bool showBanner = false;
    if (armedLabelNow.isNotEmpty()) {
      g.setColour(juce::Colours::lime);
      g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
      g.drawText("● LEARNING: " + armedLabelNow, 4, 0, getWidth() - 8, rowH,
                 juce::Justification::centredLeft, false);
      showBanner = true;
    } else if (lastCap.ccNum >= 0 &&
               juce::Time::getCurrentTime().toMilliseconds() -
                       lastCap.timestamp <
                   2500) {
      g.setColour(juce::Colours::gold);
      g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
      g.drawText("✔ " + lastCap.label + "  ->  CC" +
                     juce::String(lastCap.ccNum),
                 4, 0, getWidth() - 8, rowH, juce::Justification::centredLeft,
                 false);
      showBanner = true;
    }
    if (showBanner)
      topOffset = rowH;

    int visibleRows = (getHeight() - topOffset) / rowH;

    {
      juce::GenericScopedLock<juce::CriticalSection> sl(lock);
      for (int i = 0; i < visibleRows && i < displayEvents.size(); ++i) {
        auto &ev = displayEvents[i];
        float age = (float)(juce::Time::getCurrentTime().toMilliseconds() -
                            ev.timestamp) /
                    2000.0f;
        float alpha = juce::jmax(0.3f, 1.0f - age);

        int y = topOffset + i * rowH;
        juce::Rectangle<int> row(4, y, getWidth() - 8, rowH - 1);

        // Highlight a just-bound CC.
        bool isCapture = (lastCap.ccNum >= 0 && ev.ccNumber == lastCap.ccNum);

        if (ev.isNoteOn && ev.noteNumber >= 0) {
          int keyWidth = 4;
          float keyPos =
              (float)ev.noteNumber / 127.0f * (float)(getWidth() - 8);
          g.setColour(ev.colour.withAlpha(alpha * 0.15f));
          g.fillRect(row.withX((int)keyPos).withWidth(keyWidth + 1));

          g.setColour(ev.colour.withAlpha(alpha));
          g.drawRoundedRectangle(
              row.withTrimmedRight((int)keyPos + keyWidth + 4).toFloat(), 3.0f, 1.0f);
        } else if (ev.ccNumber >= 0) {
          float barWidth =
              (float)ev.ccValue / 127.0f * (float)(getWidth() - 8);
          juce::Colour barCol = isCapture ? juce::Colours::gold
                                          : ev.colour.withAlpha(alpha * 0.2f);
          g.setColour(barCol);
          g.fillRect(
              row.withTrimmedLeft(60).withWidth((int)barWidth).reduced(0, 2));

          g.setColour(isCapture ? juce::Colours::gold
                                : ev.colour.withAlpha(alpha));
          g.drawText(ev.text, row, juce::Justification::centredLeft, false);
        } else {
          g.setColour(ev.colour.withAlpha(alpha));
          g.drawText(ev.text, row, juce::Justification::centredLeft, false);
        }
      }
    }

    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
  }

  void timerCallback() override {
    bool changed = false;
    {
      juce::GenericScopedLock<juce::CriticalSection> sl(lock);
      if (!pendingEvents.empty()) {
        displayEvents.insert(displayEvents.begin(), pendingEvents.begin(),
                             pendingEvents.end());
        pendingEvents.clear();

        while (displayEvents.size() > 50)
          displayEvents.pop_back();
        changed = true;
      }

      juce::int64 now = juce::Time::getCurrentTime().toMilliseconds();
      while (!displayEvents.empty() &&
             now - displayEvents.back().timestamp > 3000) {
        displayEvents.pop_back();
        changed = true;
      }
    }

    // Poll the learn bus for armed/capture annotations.
    {
      auto &bus = OpenRig::MidiLearnBus::getInstance();
      juce::String newArmed = bus.armedLabel();
      auto newCap = bus.lastCapture();
      if (newArmed != armedLabelNow ||
          (newCap.ccNum != lastCap.ccNum) ||
          (newCap.ccNum >= 0 &&
           juce::Time::getCurrentTime().toMilliseconds() - newCap.timestamp <
               2500)) {
        armedLabelNow = newArmed;
        lastCap = newCap;
        changed = true;
      } else {
        armedLabelNow = newArmed;
        lastCap = newCap;
      }
    }

    if (changed)
      repaint();
  }

  juce::CriticalSection &getLock() { return lock; }

private:
  void pushEvent(const MidiEvent &ev) {
    juce::GenericScopedLock<juce::CriticalSection> sl(lock);
    pendingEvents.push_back(ev);
  }

  juce::CriticalSection lock;
  std::vector<MidiEvent> pendingEvents;
  std::vector<MidiEvent> displayEvents;

  juce::String armedLabelNow;
  OpenRig::MidiLearnBus::Capture lastCap;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMonitorComponent)
};
