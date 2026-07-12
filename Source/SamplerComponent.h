#pragma once

#include <JuceHeader.h>
#include "RackSlot.h"
#include "BoutiqueLookAndFeel.h"
#include "WaveformSpliceEditor.h"
#include "ThemeManager.h"

class SamplerSlotRow : public juce::Component,
                       public juce::FileDragAndDropTarget {
public:
    SamplerSlotRow(RackSlot& slot, int slotIndex)
        : slotRef(slot), idx(slotIndex)
    {
        padLabel.setText("PAD " + juce::String(idx + 1), juce::dontSendNotification);
        padLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        padLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
        addAndMakeVisible(padLabel);

        // MIDI Learn Button (L)
        learnBtn.setButtonText("L");
        learnBtn.setTooltip("MIDI Learn Root & Range");
        learnBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::panel));
        learnBtn.onClick = [this] {
            isLearning = !isLearning;
            setLearningState(isLearning);
            if (onLearnToggled) onLearnToggled(isLearning);
        };
        addAndMakeVisible(learnBtn);

        dropButton.setButtonText("[ Drag & Drop WAV here or Click ]");
        dropButton.onClick = [this] { 
            if (onRowSelected) onRowSelected();
            chooseFile(); 
        };
        dropButton.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::panel).darker(0.3f));
        addAndMakeVisible(dropButton);

        volSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        volSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 15);
        volSlider.setRange(0.0, 1.5, 0.01);
        volSlider.setTooltip("Volume");
        volSlider.onValueChange = [this] { updateConfig(); };
        addAndMakeVisible(volSlider);

        pitchSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        pitchSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 15);
        pitchSlider.setRange(-24, 24, 1);
        pitchSlider.setTooltip("Pitch Offset (semitones)");
        pitchSlider.onValueChange = [this] { updateConfig(); };
        pitchSlider.textFromValueFunction = [](double v) {
            return (v > 0 ? "+" : "") + juce::String((int)v) + " st";
        };
        addAndMakeVisible(pitchSlider);

        rootSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        rootSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 15);
        rootSlider.setRange(0, 127, 1);
        rootSlider.setTooltip("Root Note");
        rootSlider.onValueChange = [this] { updateConfig(); };
        rootSlider.textFromValueFunction = [this](double v) { return noteToText(v); };
        rootSlider.valueFromTextFunction = [this](const juce::String& t) {
            double n = noteFromText(t);
            return n >= 0 ? n : rootSlider.getValue();
        };
        rootSlider.setTextBoxIsEditable(true);
        addAndMakeVisible(rootSlider);

        // Two-value horizontal slider for Key Range. The built-in text box
        // can't edit per-thumb values, so dedicated editors drive lo/hi.
        rangeSlider.setSliderStyle(juce::Slider::TwoValueHorizontal);
        rangeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        rangeSlider.setRange(0, 127, 1);
        rangeSlider.setTooltip("Key Range (Low - High)");
        rangeSlider.onValueChange = [this] {
            updateConfig();
            rangeLoEditor.setText(noteToText(rangeSlider.getMinValue()), juce::dontSendNotification);
            rangeHiEditor.setText(noteToText(rangeSlider.getMaxValue()), juce::dontSendNotification);
        };
        addAndMakeVisible(rangeSlider);

        rangeLoEditor.setFont(juce::FontOptions(12.0f));
        rangeLoEditor.setJustification(juce::Justification::centred);
        rangeLoEditor.setInputRestrictions(4);
        rangeLoEditor.onReturnKey = [this] { commitRangeEditor(true); };
        rangeLoEditor.onFocusLost = [this] { commitRangeEditor(true); };
        addAndMakeVisible(rangeLoEditor);

        rangeHiEditor.setFont(juce::FontOptions(12.0f));
        rangeHiEditor.setJustification(juce::Justification::centred);
        rangeHiEditor.setInputRestrictions(4);
        rangeHiEditor.onReturnKey = [this] { commitRangeEditor(false); };
        rangeHiEditor.onFocusLost = [this] { commitRangeEditor(false); };
        addAndMakeVisible(rangeHiEditor);

        clearBtn.setButtonText("X");
        clearBtn.setTooltip("Clear sample");
        clearBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker(0.3f));
        clearBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        clearBtn.onClick = [this] {
            auto cfg = slotRef.getSampler().getSlotConfig(idx);
            cfg.wavPath = "";
            slotRef.getSampler().setSlotConfig(idx, cfg);
            updateUI();
        };
        addAndMakeVisible(clearBtn);

        updateUI();
    }

    void paint(juce::Graphics& g) override {
        if (isSelected) {
            g.setColour(juce::Colours::cyan.withAlpha(0.12f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
            g.setColour(juce::Colours::cyan.withAlpha(0.35f));
            g.drawRoundedRectangle(getLocalBounds().toFloat(), 4.0f, 1.0f);
        }
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(2);
        padLabel.setBounds(bounds.removeFromLeft(40).reduced(2));
        learnBtn.setBounds(bounds.removeFromLeft(20).reduced(2));
        clearBtn.setBounds(bounds.removeFromRight(25).reduced(2));
        
        int w = bounds.getWidth();
        dropButton.setBounds(bounds.removeFromLeft((int)(w * 0.35f)).reduced(2));
        volSlider.setBounds(bounds.removeFromLeft((int)(w * 0.12f)).reduced(2));
        pitchSlider.setBounds(bounds.removeFromLeft((int)(w * 0.12f)).reduced(2));
        rootSlider.setBounds(bounds.removeFromLeft((int)(w * 0.12f)).reduced(2));
        rangeLoEditor.setBounds(bounds.removeFromLeft(42).reduced(2));
        rangeHiEditor.setBounds(bounds.removeFromLeft(42).reduced(2));
        rangeSlider.setBounds(bounds.reduced(2));
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override {
        if (files.size() > 0) {
            juce::File f(files[0]);
            return f.getFileExtension().equalsIgnoreCase(".wav");
        }
        return false;
    }

    void filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/) override {
        if (files.size() > 0) {
            loadWavFile(juce::File(files[0]));
        }
    }

    void mouseDown(const juce::MouseEvent& /*e*/) override {
        if (onRowSelected) onRowSelected();
    }

    void setSelected(bool s) {
        if (isSelected != s) {
            isSelected = s;
            repaint();
        }
    }

    void setLearningState(bool l) {
        isLearning = l;
        if (isLearning) {
            learnBtn.setButtonText("L");
            learnBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
        } else {
            learnBtn.setButtonText("L");
            learnBtn.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::panel));
        }
    }

    void updateUI() {
        auto cfg = slotRef.getSampler().getSlotConfig(idx);
        if (cfg.wavPath.isNotEmpty()) {
            juce::File f(cfg.wavPath);
            dropButton.setButtonText(f.getFileName());
            dropButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green.darker(0.5f));
        } else {
            dropButton.setButtonText("[ Drag & Drop WAV here ]");
            dropButton.setColour(juce::TextButton::buttonColourId, ThemeManager::get(Theme::Role::panel).darker(0.3f));
        }

        volSlider.setValue(cfg.volume, juce::dontSendNotification);
        pitchSlider.setValue(cfg.pitchOffsetSemitones, juce::dontSendNotification);
        rootSlider.setValue(cfg.rootNote, juce::dontSendNotification);
        rangeSlider.setMinAndMaxValues(cfg.keyLow, cfg.keyHigh, juce::dontSendNotification);
        rangeLoEditor.setText(noteToText(cfg.keyLow), juce::dontSendNotification);
        rangeHiEditor.setText(noteToText(cfg.keyHigh), juce::dontSendNotification);
    }

    std::function<void()> onRowSelected;
    std::function<void(bool)> onLearnToggled;

private:
    void chooseFile() {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select a WAV file...",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.wav"
        );
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.existsAsFile()) {
                    loadWavFile(file);
                }
            }
        );
    }

    void loadWavFile(const juce::File& file) {
        auto cfg = slotRef.getSampler().getSlotConfig(idx);
        cfg.wavPath = file.getFullPathName();
        slotRef.getSampler().setSlotConfig(idx, cfg);
        updateUI();
        if (onRowSelected) onRowSelected(); // refresh thumbnail editor
    }

    void updateConfig() {
        auto cfg = slotRef.getSampler().getSlotConfig(idx);
        cfg.volume = (float)volSlider.getValue();
        cfg.pitchOffsetSemitones = (float)pitchSlider.getValue();
        cfg.rootNote = (int)rootSlider.getValue();
        cfg.keyLow = (int)rangeSlider.getMinValue();
        cfg.keyHigh = (int)rangeSlider.getMaxValue();
        slotRef.getSampler().setSlotConfig(idx, cfg);
    }

    juce::String noteToText(double v) const {
        static const char* names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "B"};
        int note = juce::jlimit(0, 127, (int)v);
        int octave = note / 12 - 1;
        return juce::String(names[note % 12]) + juce::String(octave);
    }

    // Inverse of noteToText. Returns -1.0 if unparseable. Accepts sharps (#),
    // flats (b or U+266D), optional sign, and a required integer octave
    // (MIDI 0 == C-1, 60 == C4). Round-trips with noteToText().
    double noteFromText(const juce::String& input) const {
        static const int baseSemitone[] = {9, 11, 0, 2, 4, 5, 7}; // A..G
        juce::String s = input.trim();
        if (s.isEmpty())
            return -1.0;

        juce::juce_wchar ch0 = juce::CharacterFunctions::toLowerCase((juce::juce_wchar)s[0]);
        if (ch0 < 'a' || ch0 > 'g')
            return -1.0;
        int base = baseSemitone[ch0 - 'a'];

        int i = 1;
        int delta = 0;
        while (i < s.length()) {
            juce::juce_wchar c = (juce::juce_wchar)s[i];
            if (c == '#') {
                delta += 1;
                ++i;
            } else if (c == 'b' || c == 0x266D) {
                delta -= 1;
                ++i;
            } else {
                break;
            }
        }

        int sign = 1;
        if (i < s.length() && (s[i] == '+' || s[i] == '-')) {
            sign = (s[i] == '-') ? -1 : 1;
            ++i;
        }

        int octave = 0;
        int numDigits = 0;
        while (i < s.length() && juce::CharacterFunctions::isDigit((juce::juce_wchar)s[i])) {
            octave = octave * 10 + (s[i] - '0');
            ++i;
            ++numDigits;
        }

        if (numDigits == 0)
            return -1.0;
        if (i != s.length())
            return -1.0;

        octave *= sign;
        int note = (octave + 1) * 12 + base + delta;
        if (note < 0 || note > 127)
            return -1.0;
        return (double)note;
    }

    void commitRangeEditor(bool isLow) {
        auto& ed = isLow ? rangeLoEditor : rangeHiEditor;
        int curLow = (int)rangeSlider.getMinValue();
        int curHigh = (int)rangeSlider.getMaxValue();
        double parsed = noteFromText(ed.getText());
        if (parsed < 0.0) {
            ed.setText(noteToText(isLow ? curLow : curHigh), juce::dontSendNotification);
            return;
        }
        int v = (int)parsed;
        int lo = isLow ? v : curLow;
        int hi = isLow ? curHigh : v;
        if (lo > hi) {
            if (isLow) hi = lo;
            else lo = hi;
        }
        rangeSlider.setMinAndMaxValues(lo, hi, juce::dontSendNotification);
        updateConfig();
        rangeLoEditor.setText(noteToText((int)rangeSlider.getMinValue()), juce::dontSendNotification);
        rangeHiEditor.setText(noteToText((int)rangeSlider.getMaxValue()), juce::dontSendNotification);
    }

    RackSlot& slotRef;
    int idx;

    juce::Label padLabel;
    juce::TextButton learnBtn;
    juce::TextButton dropButton;
    juce::Slider volSlider;
    juce::Slider pitchSlider;
    juce::Slider rootSlider;
    juce::Slider rangeSlider;
    juce::TextEditor rangeLoEditor;
    juce::TextEditor rangeHiEditor;
    juce::TextButton clearBtn;

    bool isSelected = false;
    bool isLearning = false;

    std::unique_ptr<juce::FileChooser> fileChooser;
};

class SamplerComponent : public juce::Component {
public:
    SamplerComponent(RackSlot& slot, std::function<void()> onClose)
        : slotRef(slot), closeCallback(onClose),
          spliceEditor([this](float start, float end) {
              if (selectedRowIndex >= 0 && selectedRowIndex < 8) {
                  auto cfg = slotRef.getSampler().getSlotConfig(selectedRowIndex);
                  cfg.startRatio = start;
                  cfg.endRatio = end;
                  slotRef.getSampler().setSlotConfig(selectedRowIndex, cfg);
              }
          })
    {
        titleLabel.setText("SAMPLE PLAYBACK (8 ONE-SHOT PADS)", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
        addAndMakeVisible(titleLabel);

        enableBtn.setButtonText(slotRef.getSampler().enabled.load() ? "SAMPLER: ON" : "SAMPLER: OFF");
        enableBtn.setClickingTogglesState(true);
        enableBtn.setToggleState(slotRef.getSampler().enabled.load(), juce::dontSendNotification);
        enableBtn.setColour(juce::TextButton::buttonOnColourId, ThemeManager::get(Theme::Role::ok));
        enableBtn.onClick = [this] {
            bool on = enableBtn.getToggleState();
            slotRef.getSampler().enabled.store(on);
            enableBtn.setButtonText(on ? "SAMPLER: ON" : "SAMPLER: OFF");
        };
        addAndMakeVisible(enableBtn);

        closeBtn.onClick = [this] { if (closeCallback) closeCallback(); };
        closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::black.withAlpha(0.2f));
        addAndMakeVisible(closeBtn);

        fileHeader.setText("Sample Audio File", juce::dontSendNotification);
        fileHeader.setFont(juce::FontOptions(11.0f));
        fileHeader.setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(fileHeader);

        volHeader.setText("Vol", juce::dontSendNotification);
        volHeader.setFont(juce::FontOptions(11.0f));
        volHeader.setColour(juce::Label::textColourId, juce::Colours::grey);
        volHeader.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(volHeader);

        pitchHeader.setText("Pitch", juce::dontSendNotification);
        pitchHeader.setFont(juce::FontOptions(11.0f));
        pitchHeader.setColour(juce::Label::textColourId, juce::Colours::grey);
        pitchHeader.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(pitchHeader);

        rootHeader.setText("Root", juce::dontSendNotification);
        rootHeader.setFont(juce::FontOptions(11.0f));
        rootHeader.setColour(juce::Label::textColourId, juce::Colours::grey);
        rootHeader.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(rootHeader);

        rangeHeader.setText("Key Map Range", juce::dontSendNotification);
        rangeHeader.setFont(juce::FontOptions(11.0f));
        rangeHeader.setColour(juce::Label::textColourId, juce::Colours::grey);
        rangeHeader.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(rangeHeader);

        learnHeader.setText("Lrn", juce::dontSendNotification);
        learnHeader.setFont(juce::FontOptions(11.0f));
        learnHeader.setColour(juce::Label::textColourId, juce::Colours::grey);
        learnHeader.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(learnHeader);

        for (int i = 0; i < 8; ++i) {
            auto* row = new SamplerSlotRow(slotRef, i);
            row->onRowSelected = [this, i] { selectRow(i); };
            row->onLearnToggled = [this, i](bool learning) {
                if (learning) {
                    learningSlotIndex = i;
                    learnedLow = -1;
                    learnedHigh = -1;
                    for (int r = 0; r < 8; ++r) {
                        if (r != i) rows[r]->setLearningState(false);
                    }
                } else {
                    if (learningSlotIndex == i) learningSlotIndex = -1;
                }
            };
            rows.add(row);
            addAndMakeVisible(row);
        }

        addAndMakeVisible(spliceEditor);

        // Select first row by default
        selectRow(0);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        
        g.fillAll(ThemeManager::get(Theme::Role::panel));
        
        g.setColour(ThemeManager::get(Theme::Role::iem).withAlpha(0.2f));
        g.drawRoundedRectangle(bounds, 8.0f, 2.0f);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(15);
        
        auto topArea = bounds.removeFromTop(35);
        closeBtn.setBounds(topArea.removeFromRight(35).reduced(2));
        enableBtn.setBounds(topArea.removeFromRight(120).reduced(2));
        titleLabel.setBounds(topArea.reduced(2));
        
        bounds.removeFromTop(5);
        
        auto headerArea = bounds.removeFromTop(20);
        headerArea.removeFromLeft(40);
        learnHeader.setBounds(headerArea.removeFromLeft(20));
        headerArea.removeFromLeft(5);
        headerArea.removeFromRight(27);
        
        int w = headerArea.getWidth();
        fileHeader.setBounds(headerArea.removeFromLeft((int)(w * 0.35f)));
        volHeader.setBounds(headerArea.removeFromLeft((int)(w * 0.12f)));
        pitchHeader.setBounds(headerArea.removeFromLeft((int)(w * 0.12f)));
        rootHeader.setBounds(headerArea.removeFromLeft((int)(w * 0.12f)));
        rangeHeader.setBounds(headerArea);

        bounds.removeFromTop(2);
        
        auto editorArea = bounds.removeFromBottom(120);
        spliceEditor.setBounds(editorArea);

        bounds.removeFromBottom(10);
        
        int rowHeight = bounds.getHeight() / 8;
        for (auto* row : rows) {
            row->setBounds(bounds.removeFromTop(rowHeight));
        }
    }

    void selectRow(int idx) {
        selectedRowIndex = idx;
        for (int i = 0; i < 8; ++i) {
            rows[i]->setSelected(i == idx);
        }
        
        auto cfg = slotRef.getSampler().getSlotConfig(idx);
        if (cfg.wavPath.isNotEmpty()) {
            spliceEditor.setFile(juce::File(cfg.wavPath), cfg.startRatio, cfg.endRatio);
        } else {
            spliceEditor.setFile(juce::File(), 0.0f, 1.0f);
        }
    }

    void handleMidiNote(int noteNum) {
        if (learningSlotIndex >= 0 && learningSlotIndex < 8) {
            auto* row = rows[learningSlotIndex];
            auto cfg = slotRef.getSampler().getSlotConfig(learningSlotIndex);
            
            if (learnedLow < 0) {
                learnedLow = noteNum;
                learnedHigh = noteNum;
                cfg.rootNote = noteNum; // First note sets the root
            } else {
                learnedLow = std::min(learnedLow, noteNum);
                learnedHigh = std::max(learnedHigh, noteNum);
            }
            
            cfg.keyLow = learnedLow;
            cfg.keyHigh = learnedHigh;
            slotRef.getSampler().setSlotConfig(learningSlotIndex, cfg);
            
            row->updateUI();
            
            // Refresh waveform editor if this row is selected
            if (learningSlotIndex == selectedRowIndex) {
                selectRow(selectedRowIndex);
            }
        }
    }

private:
    RackSlot& slotRef;
    std::function<void()> closeCallback;

    juce::Label titleLabel;
    juce::TextButton enableBtn;
    juce::TextButton closeBtn{"X"};

    juce::Label learnHeader, fileHeader, volHeader, pitchHeader, rootHeader, rangeHeader;

    juce::OwnedArray<SamplerSlotRow> rows;
    WaveformSpliceEditor spliceEditor;

    int selectedRowIndex = 0;
    int learningSlotIndex = -1;
    int learnedLow = -1;
    int learnedHigh = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerComponent)
};
