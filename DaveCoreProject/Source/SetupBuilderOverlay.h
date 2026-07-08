#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "RigLibrary.h"
#include "RigSerializer.h"
#include "BoutiqueLookAndFeel.h"

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
        mapVolumeCCBtn.setButtonText("Map CC 7 to Volume Faders");
        mapVolumeCCBtn.setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(mapVolumeCCBtn);

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
        juce::ignoreUnused(rowIsSelected);
        if (rowNumber >= (int)stripFiles.size()) return;

        g.fillAll(juce::Colour(0xFF101216));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(12.0f);
        g.drawText(stripFiles[rowNumber].getFileNameWithoutExtension(), 30, 0, width - 40, height,
                   juce::Justification::centredLeft, true);
    }

    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate) override {
        juce::ignoreUnused(isRowSelected);
        auto* toggle = dynamic_cast<juce::ToggleButton*>(existingComponentToUpdate);
        if (toggle == nullptr) {
            toggle = new juce::ToggleButton();
        }

        toggle->setToggleState(selectedStates[rowNumber], juce::dontSendNotification);
        toggle->onClick = [this, rowNumber, toggle] {
            selectedStates[rowNumber] = toggle->getToggleState();
        };

        return toggle;
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
        mapVolumeCCBtn.setBounds(area.removeFromTop(24));
        assignMidiChannelsBtn.setBounds(area.removeFromTop(24));

        area.removeFromTop(15);
        buildBtn.setBounds(area.removeFromTop(36).reduced(2, 0));
    }

private:
    Actions actions;
    juce::File stripsDir;
    juce::Array<juce::File> stripFiles;
    std::vector<bool> selectedStates;

    juce::Label titleLabel;
    juce::TextButton closeBtn;
    juce::ListBox listBox{ "StripsList", nullptr };

    juce::Label nameLabel;
    juce::TextEditor nameEditor;

    juce::ToggleButton mapVolumeCCBtn;
    juce::ToggleButton assignMidiChannelsBtn;

    juce::TextButton buildBtn;

    void buildSetup() {
        juce::Array<juce::File> chosenStrips;
        for (int i = 0; i < (int)stripFiles.size(); ++i) {
            if (selectedStates[i]) {
                chosenStrips.add(stripFiles[i]);
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
        for (int i = 0; i < 10; ++i) {
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

                    // Map volume CC (e.g. FOH CC to 7)
                    if (mapVolumeCCBtn.getToggleState()) {
                        obj->setProperty("fohCC", 7);
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
        for (int i = 0; i < 10; ++i) {
            auto* stateObj = new juce::DynamicObject();
            stateObj->setProperty("bypassed", true);
            stateObj->setProperty("level", 0.8f);
            stateObj->setProperty("fohEnabled", true);
            stateObj->setProperty("iemEnabled", true);
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
