#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "RigLibrary.h"
#include "RigSerializer.h"
#include "BoutiqueLookAndFeel.h"
#include "OpenRigConstants.h"

class StripRowComponent : public juce::Component {
public:
    StripRowComponent(juce::String name, bool initiallyChecked, int defaultCC, std::function<void(bool, int)> onChange)
        : onStateChange(onChange) {
        
        checkbox.setToggleState(initiallyChecked, juce::dontSendNotification);
        checkbox.onClick = [this] {
            updateEnabledStates();
            triggerCallback();
        };
        addAndMakeVisible(checkbox);

        nameLabel.setText(name, juce::dontSendNotification);
        nameLabel.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(nameLabel);

        ccLabel.setText("CC:", juce::dontSendNotification);
        ccLabel.setFont(juce::FontOptions(11.0f));
        addAndMakeVisible(ccLabel);

        ccInput.setText(juce::String(defaultCC));
        ccInput.setInputRestrictions(3, "0123456789");
        ccInput.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF22252A));
        ccInput.onTextChange = [this] { triggerCallback(); };
        addAndMakeVisible(ccInput);

        updateEnabledStates();
    }

    void resized() override {
        auto r = getLocalBounds();
        checkbox.setBounds(r.removeFromLeft(24).reduced(2));
        r.removeFromLeft(4);
        
        auto rightSide = r.removeFromRight(80);
        ccInput.setBounds(rightSide.removeFromRight(40).reduced(2));
        ccLabel.setBounds(rightSide.reduced(2));

        nameLabel.setBounds(r.reduced(2));
    }

    void setCheckedAndCC(bool checked, int cc) {
        checkbox.setToggleState(checked, juce::dontSendNotification);
        ccInput.setText(juce::String(cc), juce::dontSendNotification);
        updateEnabledStates();
    }

private:
    juce::ToggleButton checkbox;
    juce::Label nameLabel;
    juce::Label ccLabel;
    juce::TextEditor ccInput;
    std::function<void(bool, int)> onStateChange;

    void updateEnabledStates() {
        bool active = checkbox.getToggleState();
        ccLabel.setEnabled(active);
        ccInput.setEnabled(active);
    }

    void triggerCallback() {
        if (onStateChange) {
            onStateChange(checkbox.getToggleState(), ccInput.getText().getIntValue());
        }
    }
};

class SetupBuilderOverlay : public juce::Component, public juce::ListBoxModel {
public:
    struct Actions {
        std::function<void(const juce::File&)> onBuildComplete;
        std::function<void()> onClose;
    };

    SetupBuilderOverlay(Actions a) : actions(std::move(a)) {
        titleLabel.setText("SETUP BUILDER", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF00E5FF));
        titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(titleLabel);

        closeBtn.setButtonText("X");
        closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
        closeBtn.onClick = [this] { if (actions.onClose) actions.onClose(); };
        addAndMakeVisible(closeBtn);

        // Load strips list
        stripsDir = OpenRig::RigLibrary::getPresetsDirectory();
        stripFiles = stripsDir.findChildFiles(juce::File::findFiles, false, "*.orstrip");
        selectedStates.resize(stripFiles.size(), false);
        ccValues.resize(stripFiles.size(), 7); // Default to CC 7 (Volume)

        listBox.setModel(this);
        listBox.setRowHeight(24);
        listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xFF101216));
        listBox.setOutlineThickness(1);
        listBox.setColour(juce::ListBox::outlineColourId, juce::Colour(0xFF2A2D32));
        addAndMakeVisible(listBox);

        // Setup Name Input
        nameLabel.setText("Setup Name:", juce::dontSendNotification);
        nameLabel.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(nameLabel);

        nameEditor.setText("My Custom Rig");
        nameEditor.setInputRestrictions(30);
        nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF101216));
        nameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF2A2D32));
        addAndMakeVisible(nameEditor);

        // Options
        assignMidiChannelsBtn.setButtonText("Pre-assign Consecutive MIDI Channels");
        assignMidiChannelsBtn.setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(assignMidiChannelsBtn);

        buildBtn.setButtonText("BUILD & LOAD SETUP");
        buildBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF00E5FF));
        buildBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        buildBtn.onClick = [this] { buildSetup(); };
        addAndMakeVisible(buildBtn);

        setSize(400, 480);
    }

    int getNumRows() override {
        return stripFiles.size();
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override {
        juce::ignoreUnused(rowIsSelected, rowNumber, width, height);
        g.fillAll(juce::Colour(0xFF101216));
    }

    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate) override {
        juce::ignoreUnused(isRowSelected);
        auto* rowComp = dynamic_cast<StripRowComponent*>(existingComponentToUpdate);
        if (rowComp == nullptr) {
            rowComp = new StripRowComponent(
                stripFiles[rowNumber].getFileNameWithoutExtension(),
                selectedStates[rowNumber],
                ccValues[rowNumber],
                [this, rowNumber](bool checked, int cc) {
                    selectedStates[rowNumber] = checked;
                    ccValues[rowNumber] = cc;
                }
            );
        } else {
            rowComp->setCheckedAndCC(selectedStates[rowNumber], ccValues[rowNumber]);
        }

        return rowComp;
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xE6121315));
        g.setColour(juce::Colour(0xFF00E5FF));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 8.0f, 1.5f);

        if (stripFiles.isEmpty()) {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.setFont(14.0f);
            g.drawText("No saved strips found in:\n" + stripsDir.getFullPathName(),
                       listBox.getBounds(), juce::Justification::centred, true);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(16);
        auto top = area.removeFromTop(30);
        titleLabel.setBounds(top.removeFromLeft(top.getWidth() - 40));
        closeBtn.setBounds(top.removeFromRight(30));

        area.removeFromTop(10);
        listBox.setBounds(area.removeFromTop(200));

        area.removeFromTop(15);
        auto nameRow = area.removeFromTop(28);
        nameLabel.setBounds(nameRow.removeFromLeft(90));
        nameEditor.setBounds(nameRow);

        area.removeFromTop(10);
        assignMidiChannelsBtn.setBounds(area.removeFromTop(24));

        area.removeFromTop(15);
        buildBtn.setBounds(area.removeFromTop(36).reduced(2, 0));
    }

private:
    Actions actions;
    juce::File stripsDir;
    juce::Array<juce::File> stripFiles;
    std::vector<bool> selectedStates;
    std::vector<int> ccValues;

    juce::Label titleLabel;
    juce::TextButton closeBtn;
    juce::ListBox listBox{ "StripsList", nullptr };

    juce::Label nameLabel;
    juce::TextEditor nameEditor;

    juce::ToggleButton assignMidiChannelsBtn;

    juce::TextButton buildBtn;

    void buildSetup() {
        juce::Array<juce::File> chosenStrips;
        juce::Array<int> chosenCCs;
        for (int i = 0; i < (int)stripFiles.size(); ++i) {
            if (selectedStates[i]) {
                chosenStrips.add(stripFiles[i]);
                chosenCCs.add(ccValues[i]);
            }
        }

        if (chosenStrips.isEmpty()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon, "Build Setup",
                "Please select at least one channel strip preset from the list.");
            return;
        }

        juce::String songName = nameEditor.getText().trim();
        if (songName.isEmpty()) songName = "My Custom Rig";

        // Create the Setup JSON object
        auto* root = new juce::DynamicObject();
        juce::Array<juce::var> channelsArr;

        int midiChannel = 1;
        for (int i = 0; i < OpenRigConstants::kNumSlots; ++i) {
            if (i < chosenStrips.size()) {
                auto stripFile = chosenStrips[i];
                auto stripJson = juce::JSON::parse(stripFile);
                if (auto* obj = stripJson.getDynamicObject()) {
                    // Lock-to-Muted: All loaded slots are initialized as bypassed/muted
                    obj->setProperty("mute", true);

                    // Pre-assign MIDI channel
                    if (assignMidiChannelsBtn.getToggleState()) {
                        obj->setProperty("midiChannel", midiChannel++);
                    }

                    // Map volume CC as specified for this strip
                    int ccNum = chosenCCs[i];
                    if (ccNum >= 0 && ccNum <= 127) {
                        obj->setProperty("fohCC", ccNum);
                        obj->setProperty("iemCC", ccNum);
                    }

                    channelsArr.add(juce::var(obj));
                }
            } else {
                // Empty channel
                auto* emptyObj = new juce::DynamicObject();
                emptyObj->setProperty("name", "[EMPTY]");
                emptyObj->setProperty("mute", true);
                channelsArr.add(juce::var(emptyObj));
            }
        }
        root->setProperty("channels", channelsArr);

        // Add standard master sections
        root->setProperty("fohFx", juce::Array<juce::var>{});
        root->setProperty("iemFx", juce::Array<juce::var>{});
        
        // Add default scenes list
        juce::Array<juce::var> scenesArr;
        auto* defaultScene = new juce::DynamicObject();
        defaultScene->setProperty("name", "INIT PRESET");
        juce::Array<juce::var> statesArr;
        for (int i = 0; i < OpenRigConstants::kNumSlots; ++i) {
            auto* stateObj = new juce::DynamicObject();
            stateObj->setProperty("bypassed", true);
            stateObj->setProperty("level", 0.8f);
            stateObj->setProperty("fohLevel", 0.8f);
            stateObj->setProperty("iemLevel", 0.8f);
            stateObj->setProperty("foh", true);
            stateObj->setProperty("iem", true);
            stateObj->setProperty("fadersLinked", true);
            stateObj->setProperty("aux1Send", 0.0f);
            stateObj->setProperty("aux2Send", 0.0f);
            stateObj->setProperty("iemOffset", 1.0f);
            stateObj->setProperty("transposeOctaves", 0);
            stateObj->setProperty("transposeSemitones", 0);
            stateObj->setProperty("lowNote", 0);
            stateObj->setProperty("highNote", 127);
            stateObj->setProperty("gateEnabled", false);
            stateObj->setProperty("gateThreshold", -60.0f);
            stateObj->setProperty("eqEnabled", false);
            stateObj->setProperty("hpfFreq", 20.0f);
            stateObj->setProperty("eqLow", 0.0f);
            stateObj->setProperty("eqHigh", 0.0f);
            stateObj->setProperty("compEnabled", false);
            stateObj->setProperty("compAmount", 0.0f);
            stateObj->setProperty("chorusEnabled", false);
            stateObj->setProperty("chorusRate", 1.0f);
            stateObj->setProperty("chorusMix", 0.0f);
            stateObj->setProperty("reverbEnabled", false);
            stateObj->setProperty("reverbSize", 0.5f);
            stateObj->setProperty("reverbMix", 0.0f);
            stateObj->setProperty("arpEnabled", false);
            stateObj->setProperty("arpBpm", 120.0f);
            stateObj->setProperty("arpOctavesUp", 1);
            stateObj->setProperty("arpOctavesDown", 0);
            stateObj->setProperty("arpGate", 0.9f);
            stateObj->setProperty("arpPatternIdx", 0);
            stateObj->setProperty("harmEnabled", false);
            stateObj->setProperty("harmOctavesUp", 1);
            stateObj->setProperty("harmOctavesDown", 0);
            stateObj->setProperty("harmAfricaMode", 0);
            stateObj->setProperty("harmTargetSlot", -1);
            stateObj->setProperty("samplerEnabled", false);
            statesArr.add(juce::var(stateObj));
        }
        defaultScene->setProperty("states", statesArr);
        scenesArr.add(juce::var(defaultScene));
        root->setProperty("scenes", scenesArr);
        root->setProperty("currentSceneIndex", 0);

        // Save new setup file to Songs/ folder
        auto targetFile = OpenRig::RigLibrary::getSongsDirectory().getChildFile(songName + ".json");
        targetFile.deleteFile();
        targetFile.getParentDirectory().createDirectory();

        juce::FileOutputStream fos(targetFile);
        if (fos.openedOk()) {
            juce::JSON::writeToStream(fos, juce::var(root));
            fos.flush();
        }

        // Notify parent to load the newly built file
        if (actions.onBuildComplete) {
            actions.onBuildComplete(targetFile);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetupBuilderOverlay)
};
