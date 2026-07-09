#pragma once

#include <JuceHeader.h>
#include "SimpleArpeggiator.h"
#include "BoutiqueLookAndFeel.h"

class ArpeggiatorComponent : public juce::Component,
                              public juce::Label::Listener {
public:
    ArpeggiatorComponent(SimpleArpeggiator& arp, std::function<void()> onClose)
        : arpeggiator(arp), closeCallback(std::move(onClose))
    {
        // Title
        titleLabel.setText("ARPEGGIATOR", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
        titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(titleLabel);

        // Enable toggle
        enableBtn.setButtonText("ON");
        enableBtn.setClickingTogglesState(true);
        enableBtn.setToggleState(arpeggiator.enabled.load(), juce::dontSendNotification);
        enableBtn.onClick = [this] {
            arpeggiator.enabled.store(enableBtn.getToggleState());
            enableBtn.setButtonText(enableBtn.getToggleState() ? "ON" : "OFF");
        };
        addAndMakeVisible(enableBtn);

        // BPM editor
        bpmLabel.setText("BPM", juce::dontSendNotification);
        bpmLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(bpmLabel);

        bpmEditor.setText(juce::String(arpeggiator.bpm.load(), 1), juce::dontSendNotification);
        bpmEditor.setEditable(true);
        bpmEditor.addListener(this);
        bpmEditor.setJustificationType(juce::Justification::centred);
        bpmEditor.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        bpmEditor.setColour(juce::Label::textColourId, juce::Colours::white);
        bpmEditor.setColour(juce::Label::backgroundColourId, juce::Colours::darkgrey.withAlpha(0.3f));
        addAndMakeVisible(bpmEditor);

        // Octave Up
        octUpLabel.setText("OCT UP", juce::dontSendNotification);
        addAndMakeVisible(octUpLabel);
        octUpSlider.setRange(0, 4, 1);
        octUpSlider.setValue(arpeggiator.octavesUp.load(), juce::dontSendNotification);
        octUpSlider.onValueChange = [this] { arpeggiator.octavesUp.store((int)octUpSlider.getValue()); };
        addAndMakeVisible(octUpSlider);

        // Octave Down
        octDownLabel.setText("OCT DOWN", juce::dontSendNotification);
        addAndMakeVisible(octDownLabel);
        octDownSlider.setRange(0, 4, 1);
        octDownSlider.setValue(arpeggiator.octavesDown.load(), juce::dontSendNotification);
        octDownSlider.onValueChange = [this] { arpeggiator.octavesDown.store((int)octDownSlider.getValue()); };
        addAndMakeVisible(octDownSlider);

        // Gate
        gateLabel.setText("GATE", juce::dontSendNotification);
        addAndMakeVisible(gateLabel);
        gateSlider.setRange(0.01, 1.0, 0.01);
        gateSlider.setValue(arpeggiator.gate.load(), juce::dontSendNotification);
        gateSlider.onValueChange = [this] { arpeggiator.gate.store((float)gateSlider.getValue()); };
        addAndMakeVisible(gateSlider);

        // Pattern buttons
        patternLabel.setText("PATTERN", juce::dontSendNotification);
        addAndMakeVisible(patternLabel);

        const char* names[] = {"UP", "DOWN", "UP/DOWN", "RND"};
        for (int i = 0; i < 4; ++i) {
            auto* btn = new juce::TextButton(names[i]);
            btn->setClickingTogglesState(true);
            btn->setRadioGroupId(1);
            btn->setToggleState(i == arpeggiator.patternIdx.load(), juce::dontSendNotification);
            int idx = i;
            btn->onClick = [this, idx] { arpeggiator.patternIdx.store(idx); };
            patternBtns.add(btn);
            addAndMakeVisible(btn);
        }

        // Close button
        closeBtn.setButtonText("X");
        closeBtn.onClick = [this] { if (closeCallback) closeCallback(); };
        addAndMakeVisible(closeBtn);

        setSize(420, 300);
    }

    void paint(juce::Graphics& g) override {
        auto* laf = dynamic_cast<BoutiqueLookAndFeel*>(&getLookAndFeel());
        bool useModern = (laf != nullptr && laf->useModernStyle);

        if (useModern) {
            // Dark modern glassmorphic background
            g.fillAll(juce::Colour(0xff16181b));
            g.setColour(juce::Colour(0xff2a2d32));
            g.drawRoundedRectangle(getLocalBounds().toFloat(), 8.0f, 1.5f);
        } else {
            g.fillAll(juce::Colours::darkgrey.darker());
        }
    }

    void labelTextChanged(juce::Label* labelThatHasChanged) override {
        if (labelThatHasChanged == &bpmEditor) {
            float val = bpmEditor.getText().getFloatValue();
            if (val >= 1.0f && val <= 999.0f) {
                arpeggiator.bpm.store(val);
            }
            bpmEditor.setText(juce::String(arpeggiator.bpm.load(), 1), juce::dontSendNotification);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        auto top = area.removeFromTop(30);
        titleLabel.setBounds(top.removeFromLeft(top.getWidth() - 40));
        closeBtn.setBounds(top.removeFromRight(30));

        // Enable
        enableBtn.setBounds(area.removeFromTop(36).removeFromLeft(80));
        area.removeFromTop(8);

        // BPM
        auto bpmRow = area.removeFromTop(60);
        bpmLabel.setBounds(bpmRow.removeFromLeft(50));
        bpmEditor.setBounds(bpmRow.removeFromLeft(120).reduced(2));
        area.removeFromTop(8);

        // Oct Up / Oct Down / Gate
        auto knobRow = area.removeFromTop(80);
        auto colW = knobRow.getWidth() / 3;
        auto octUpCol = knobRow.removeFromLeft(colW).reduced(4);
        octUpLabel.setBounds(octUpCol.removeFromTop(18));
        octUpSlider.setBounds(octUpCol);
        auto octDownCol = knobRow.removeFromLeft(colW).reduced(4);
        octDownLabel.setBounds(octDownCol.removeFromTop(18));
        octDownSlider.setBounds(octDownCol);
        auto gateCol = knobRow.reduced(4);
        gateLabel.setBounds(gateCol.removeFromTop(18));
        gateSlider.setBounds(gateCol);
        area.removeFromTop(8);

        // Pattern buttons
        patternLabel.setBounds(area.removeFromTop(18));
        auto btnRow = area.removeFromTop(32);
        int bw = btnRow.getWidth() / 4;
        for (int i = 0; i < patternBtns.size(); ++i)
            patternBtns[i]->setBounds(btnRow.removeFromLeft(bw).reduced(2));
    }

private:
    SimpleArpeggiator& arpeggiator;
    std::function<void()> closeCallback;

    juce::Label titleLabel, bpmLabel, octUpLabel, octDownLabel, gateLabel, patternLabel;
    juce::Label bpmEditor;
    juce::TextButton enableBtn{"ON"}, closeBtn{"X"};
    juce::Slider octUpSlider, octDownSlider, gateSlider;
    juce::OwnedArray<juce::TextButton> patternBtns;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpeggiatorComponent)
};
