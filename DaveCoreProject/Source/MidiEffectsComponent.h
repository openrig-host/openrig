#pragma once

#include <JuceHeader.h>
#include "SimpleArpeggiator.h"
#include "OctaveHarmonizer.h"
#include "ThemeManager.h"
#include "RackSlot.h"

class MidiEffectsComponent : public juce::Component,
                              public juce::Label::Listener {
public:
    MidiEffectsComponent(SimpleArpeggiator& arp, OctaveHarmonizer& harm,
                         const juce::StringArray& slotNames, int ownSlotIndex,
                         RackSlot& slot,
                         std::function<void()> onClose)
        : arpeggiator(arp), harmonizer(harm), slotRef(slot),
          closeCallback(std::move(onClose))
    {
        titleLabel.setText("MIDI EFFECTS", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
        titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(titleLabel);

        // TRANSPOSE strip (full width, above ARP/OCTAVER columns)
        transposeHeader.setText("TRANSPOSE", juce::dontSendNotification);
        transposeHeader.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        transposeHeader.setColour(juce::Label::textColourId, juce::Colours::cyan);
        transposeHeader.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(transposeHeader);

        transposeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        transposeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        transposeSlider.setRange(-48, 48, 1);
        transposeSlider.setValue(slotRef.getTransposeSemis(), juce::dontSendNotification);
        transposeSlider.onValueChange = [this] {
            int v = (int)transposeSlider.getValue();
            slotRef.setTransposeSemis(v);
            transposeValue.setText((v > 0 ? "+" : "") + juce::String(v) + " st",
                                   juce::dontSendNotification);
        };
        transposeValue.setText(
            [&] {
                int v = slotRef.getTransposeSemis();
                return (v > 0 ? "+" : "") + juce::String(v) + " st";
            }(),
            juce::dontSendNotification);
        transposeValue.setColour(juce::Label::textColourId, juce::Colours::cyan);
        transposeValue.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        transposeValue.setJustificationType(juce::Justification::centredRight);

        // +/- arrows for transpose: ArrowButton has built-in auto-repeat on hold
        transposeDownBtn.onClick = [this] {
            int v = juce::jlimit(-48, 48, slotRef.getTransposeSemis() - 1);
            slotRef.setTransposeSemis(v);
            transposeSlider.setValue(v, juce::dontSendNotification);
            transposeValue.setText((v > 0 ? "+" : "") + juce::String(v) + " st",
                                   juce::dontSendNotification);
        };
        transposeUpBtn.onClick = [this] {
            int v = juce::jlimit(-48, 48, slotRef.getTransposeSemis() + 1);
            slotRef.setTransposeSemis(v);
            transposeSlider.setValue(v, juce::dontSendNotification);
            transposeValue.setText((v > 0 ? "+" : "") + juce::String(v) + " st",
                                   juce::dontSendNotification);
        };
        addAndMakeVisible(transposeSlider);
        addAndMakeVisible(transposeValue);
        addAndMakeVisible(transposeDownBtn);
        addAndMakeVisible(transposeUpBtn);

        arpHeader.setText("ARPEGGIATOR", juce::dontSendNotification);
        arpHeader.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        arpHeader.setColour(juce::Label::textColourId, juce::Colours::orange);
        addAndMakeVisible(arpHeader);

        arpEnableBtn.setButtonText("ON");
        arpEnableBtn.setClickingTogglesState(true);
        arpEnableBtn.setToggleState(arpeggiator.enabled.load(), juce::dontSendNotification);
        arpEnableBtn.onClick = [this] {
            arpeggiator.enabled.store(arpEnableBtn.getToggleState());
            arpEnableBtn.setButtonText(arpEnableBtn.getToggleState() ? "ON" : "OFF");
        };
        addAndMakeVisible(arpEnableBtn);
        setupArpControls();
        setupOctControls();

        // Harmony routing target (links generated voices to another strip)
        harmonyTargetLabel.setText("ROUTE HARMONY TO", juce::dontSendNotification);
        harmonyTargetLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
        harmonyTargetLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        harmonyTargetLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(harmonyTargetLabel);

        harmonyTargetCombo.addItem("None (Local)", 1); // id 1 == target -1
        for (int s = 0; s < slotNames.size(); ++s) {
            if (s == ownSlotIndex)
                continue; // never route a slot to itself
            harmonyTargetCombo.addItem(
                slotNames[s] + "  (Slot " + juce::String(s + 1) + ")", s + 2);
        }
        int desiredId = harmonizer.harmonyTargetSlot.load() + 2;
        harmonyTargetCombo.setSelectedId(
            (harmonyTargetCombo.indexOfItemId(desiredId) >= 0) ? desiredId : 1,
            juce::dontSendNotification);
        harmonyTargetCombo.onChange = [this] {
            harmonizer.harmonyTargetSlot.store(harmonyTargetCombo.getSelectedId() - 2);
            harmonizer.needsFlush.store(true);
        };
        addAndMakeVisible(harmonyTargetCombo);

        closeBtn.setButtonText("X");
        closeBtn.onClick = [this] { if (closeCallback) closeCallback(); };
        addAndMakeVisible(closeBtn);
        setSize(620, 410);
    }

    void labelTextChanged(juce::Label* l) override {
        if (l == &arpBpmEditor) {
            float val = arpBpmEditor.getText().getFloatValue();
            if (val >= 1.0f && val <= 999.0f) arpeggiator.bpm.store(val);
            arpBpmEditor.setText(juce::String(arpeggiator.bpm.load(), 1), juce::dontSendNotification);
        }
    }

private:
    void setupArpControls() {
        arpBpmLabel.setText("BPM", juce::dontSendNotification); addAndMakeVisible(arpBpmLabel);
        arpBpmEditor.setText(juce::String(arpeggiator.bpm.load(), 1), juce::dontSendNotification);
        arpBpmEditor.setEditable(true); arpBpmEditor.addListener(this);
        arpBpmEditor.setJustificationType(juce::Justification::centred);
        arpBpmEditor.setFont(juce::FontOptions(20.0f, juce::Font::bold));

        // +/- arrows for BPM: ArrowButton has built-in auto-repeat on hold
        arpBpmDownBtn.onClick = [this] {
            float v = juce::jlimit(1.0f, 999.0f, arpeggiator.bpm.load() - 1.0f);
            arpeggiator.bpm.store(v);
            arpBpmEditor.setText(juce::String(v, 1), juce::dontSendNotification);
        };
        arpBpmUpBtn.onClick = [this] {
            float v = juce::jlimit(1.0f, 999.0f, arpeggiator.bpm.load() + 1.0f);
            arpeggiator.bpm.store(v);
            arpBpmEditor.setText(juce::String(v, 1), juce::dontSendNotification);
        };
        addAndMakeVisible(arpBpmEditor);
        addAndMakeVisible(arpBpmDownBtn);
        addAndMakeVisible(arpBpmUpBtn);

        arpOctUpLabel.setText("OCT UP", juce::dontSendNotification); addAndMakeVisible(arpOctUpLabel);
        arpOctUpSlider.setRange(0, 4, 1);
        arpOctUpSlider.setValue(arpeggiator.octavesUp.load(), juce::dontSendNotification);
        arpOctUpSlider.onValueChange = [this] { arpeggiator.octavesUp.store((int)arpOctUpSlider.getValue()); };
        addAndMakeVisible(arpOctUpSlider);

        arpOctDownLabel.setText("OCT DN", juce::dontSendNotification); addAndMakeVisible(arpOctDownLabel);
        arpOctDownSlider.setRange(0, 4, 1);
        arpOctDownSlider.setValue(arpeggiator.octavesDown.load(), juce::dontSendNotification);
        arpOctDownSlider.onValueChange = [this] { arpeggiator.octavesDown.store((int)arpOctDownSlider.getValue()); };
        addAndMakeVisible(arpOctDownSlider);

        arpGateLabel.setText("GATE", juce::dontSendNotification); addAndMakeVisible(arpGateLabel);
        arpGateSlider.setRange(0.01, 1.0, 0.01);
        arpGateSlider.setValue(arpeggiator.gate.load(), juce::dontSendNotification);
        arpGateSlider.onValueChange = [this] { arpeggiator.gate.store((float)arpGateSlider.getValue()); };
        addAndMakeVisible(arpGateSlider);

        arpPatternLabel.setText("PATTERN", juce::dontSendNotification); addAndMakeVisible(arpPatternLabel);
        const char* names[] = {"UP", "DOWN", "U/D", "RND"};
        for (int i = 0; i < 4; ++i) {
            auto* btn = new juce::TextButton(names[i]);
            btn->setClickingTogglesState(true); btn->setRadioGroupId(1);
            btn->setToggleState(i == arpeggiator.patternIdx.load(), juce::dontSendNotification);
            int idx = i;
            btn->onClick = [this, idx] { arpeggiator.patternIdx.store(idx); };
            arpPatternBtns.add(btn); addAndMakeVisible(btn);
        }
    }

    void setupOctControls() {
        octHeader.setText("OCTAVER", juce::dontSendNotification);
        octHeader.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        octHeader.setColour(juce::Label::textColourId, juce::Colours::cyan);
        addAndMakeVisible(octHeader);

        octEnableBtn.setButtonText("ON");
        octEnableBtn.setClickingTogglesState(true);
        octEnableBtn.setToggleState(harmonizer.enabled.load(), juce::dontSendNotification);
        octEnableBtn.onClick = [this] {
            harmonizer.enabled.store(octEnableBtn.getToggleState());
            harmonizer.needsFlush.store(true);
            octEnableBtn.setButtonText(octEnableBtn.getToggleState() ? "ON" : "OFF");
        };
        addAndMakeVisible(octEnableBtn);

        octUpLabel.setText("OCTAVES UP", juce::dontSendNotification); addAndMakeVisible(octUpLabel);
        octUpSlider.setRange(0, 3, 1);
        octUpSlider.setValue(harmonizer.octavesUp.load(), juce::dontSendNotification);
        octUpSlider.onValueChange = [this] { harmonizer.octavesUp.store((int)octUpSlider.getValue()); harmonizer.needsFlush.store(true); };
        addAndMakeVisible(octUpSlider);

        octDownLabel.setText("OCTAVES DOWN", juce::dontSendNotification); addAndMakeVisible(octDownLabel);
        octDownSlider.setRange(0, 3, 1);
        octDownSlider.setValue(harmonizer.octavesDown.load(), juce::dontSendNotification);
        octDownSlider.onValueChange = [this] { harmonizer.octavesDown.store((int)octDownSlider.getValue()); harmonizer.needsFlush.store(true); };
        addAndMakeVisible(octDownSlider);

        addAndMakeVisible(africaModeBtn);
        int initialMode = harmonizer.africaMode.load();
        auto getBtnText = [](int mode) -> juce::String {
            if (mode == 1) return "AFRICA: PART 1";
            if (mode == 2) return "AFRICA: PART 2";
            if (mode == 3) return "PLAY IN C SPLIT";
            return "AFRICA HARM: OFF";
        };
        africaModeBtn.setButtonText(getBtnText(initialMode));
        africaModeBtn.setClickingTogglesState(false);
        africaModeBtn.setToggleState(initialMode > 0, juce::dontSendNotification);
        africaModeBtn.onClick = [this, getBtnText] {
            int current = harmonizer.africaMode.load();
            int next = (current + 1) % 4;
            harmonizer.africaMode.store(next); harmonizer.needsFlush.store(true);
            
            africaModeBtn.setButtonText(getBtnText(next));
            africaModeBtn.setToggleState(next > 0, juce::dontSendNotification);
        };

        octInfo.setText("Layers octave notes on top of what you play.\nReference-counted: no stuck notes if you\nplay the same note the software generates.", juce::dontSendNotification);
        octInfo.setColour(juce::Label::textColourId, juce::Colours::grey);
        octInfo.setFont(juce::FontOptions(11.0f));
        addAndMakeVisible(octInfo);
    }

public:
    void paint(juce::Graphics& g) override {
        g.fillAll(ThemeManager::get(Theme::Role::scrim));
        g.setColour(ThemeManager::get(Theme::Role::accent));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 8.0f, 1.5f);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(12);
        auto top = area.removeFromTop(30);
        titleLabel.setBounds(top.removeFromLeft(top.getWidth() - 40));
        closeBtn.setBounds(top.removeFromRight(30));

        area.removeFromTop(6);
        auto transRow = area.removeFromTop(40);
        transposeHeader.setBounds(transRow.removeFromLeft(95));
        transRow.removeFromLeft(6); // spacing
        transposeUpBtn.setBounds(transRow.removeFromRight(24));
        transRow.removeFromRight(2);
        transposeDownBtn.setBounds(transRow.removeFromRight(24));
        transRow.removeFromRight(6);
        transposeValue.setBounds(transRow.removeFromRight(60));
        transposeSlider.setBounds(transRow.reduced(6, 8));
        area.removeFromTop(8);

        auto left = area.removeFromLeft(area.getWidth() / 2).reduced(6);
        auto right = area.reduced(6);

        auto arpRow = left.removeFromTop(28);
        arpHeader.setBounds(arpRow.removeFromLeft(arpRow.getWidth() - 60));
        arpEnableBtn.setBounds(arpRow.removeFromRight(50));
        left.removeFromTop(6);

        auto bpmRow = left.removeFromTop(50);
        arpBpmLabel.setBounds(bpmRow.removeFromLeft(35));
        arpBpmEditor.setBounds(bpmRow.removeFromLeft(100).reduced(2));
        bpmRow.removeFromLeft(4);
        arpBpmDownBtn.setBounds(bpmRow.removeFromLeft(30));
        arpBpmUpBtn.setBounds(bpmRow.removeFromLeft(30).reduced(0, 0));
        left.removeFromTop(6);

        auto knobRow = left.removeFromTop(70);
        int kw = knobRow.getWidth() / 3;
        auto k1 = knobRow.removeFromLeft(kw).reduced(3);
        arpOctUpLabel.setBounds(k1.removeFromTop(16)); arpOctUpSlider.setBounds(k1);
        auto k2 = knobRow.removeFromLeft(kw).reduced(3);
        arpOctDownLabel.setBounds(k2.removeFromTop(16)); arpOctDownSlider.setBounds(k2);
        auto k3 = knobRow.reduced(3);
        arpGateLabel.setBounds(k3.removeFromTop(16)); arpGateSlider.setBounds(k3);
        left.removeFromTop(6);
        arpPatternLabel.setBounds(left.removeFromTop(16));
        auto patRow = left.removeFromTop(28);
        int pw = patRow.getWidth() / 4;
        for (int i = 0; i < arpPatternBtns.size(); ++i)
            arpPatternBtns[i]->setBounds(patRow.removeFromLeft(pw).reduced(2));

        auto octRow = right.removeFromTop(28);
        octHeader.setBounds(octRow.removeFromLeft(octRow.getWidth() - 60));
        octEnableBtn.setBounds(octRow.removeFromRight(50));
        right.removeFromTop(6);
        auto octKnobRow = right.removeFromTop(80);
        int okw = octKnobRow.getWidth() / 2;
        auto ok1 = octKnobRow.removeFromLeft(okw).reduced(6);
        octUpLabel.setBounds(ok1.removeFromTop(18)); octUpSlider.setBounds(ok1);
        auto ok2 = octKnobRow.reduced(6);
        octDownLabel.setBounds(ok2.removeFromTop(18)); octDownSlider.setBounds(ok2);
        right.removeFromTop(4);

        auto harmRow = right.removeFromTop(24);
        africaModeBtn.setBounds(harmRow.reduced(10, 0));
        right.removeFromTop(4);

        harmonyTargetLabel.setBounds(right.removeFromTop(16));
        harmonyTargetCombo.setBounds(right.removeFromTop(24).reduced(2, 0));
        right.removeFromTop(4);

        octInfo.setBounds(right);
    }

private:
    SimpleArpeggiator& arpeggiator;
    OctaveHarmonizer& harmonizer;
    RackSlot& slotRef;
    std::function<void()> closeCallback;

    juce::Label titleLabel;
    juce::TextButton closeBtn{"X"};

    juce::Label transposeHeader, transposeValue;
    juce::Slider transposeSlider;
    juce::ArrowButton transposeDownBtn{juce::translate("Transpose Down"), 0.25f, juce::Colours::cyan};
    juce::ArrowButton transposeUpBtn{juce::translate("Transpose Up"),   0.75f, juce::Colours::cyan};

    juce::Label arpHeader, arpBpmLabel, arpOctUpLabel, arpOctDownLabel, arpGateLabel, arpPatternLabel;
    juce::Label arpBpmEditor;
    juce::ArrowButton arpBpmDownBtn{juce::translate("BPM Down"), 0.25f, juce::Colours::orange};
    juce::ArrowButton arpBpmUpBtn{juce::translate("BPM Up"),   0.75f, juce::Colours::orange};
    juce::TextButton arpEnableBtn{"ON"};
    juce::Slider arpOctUpSlider, arpOctDownSlider, arpGateSlider;
    juce::OwnedArray<juce::TextButton> arpPatternBtns;

    juce::Label octHeader, octUpLabel, octDownLabel, octInfo;
    juce::TextButton octEnableBtn{"ON"};
    juce::Slider octUpSlider, octDownSlider;
    juce::TextButton africaModeBtn{"AFRICA HARM"};
    juce::Label harmonyTargetLabel;
    juce::ComboBox harmonyTargetCombo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiEffectsComponent)
};
