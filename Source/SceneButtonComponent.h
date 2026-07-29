#pragma once

#include <JuceHeader.h>
#include "ThemeManager.h"

namespace OpenRig {

class SceneButtonComponent : public juce::Button,
                             public juce::DragAndDropTarget {
public:
    SceneButtonComponent(int sceneIndex, const juce::String& defaultName);
    ~SceneButtonComponent() override = default;

    int getSceneIndex() const { return sceneIndex; }

    void setAssignedFile(const juce::File& file);
    juce::File getAssignedFile() const { return assignedFile; }

    void setMidiTriggerInfo(int pc, int channel);
    int getMidiPC() const { return midiPC; }
    int getMidiChannel() const { return midiChannel; }

    void setActive(bool active);
    bool getActive() const { return isActive; }

    std::function<void(int sceneIdx, const juce::File& file)> onFileDropped;
    std::function<void(int sceneIdx)> onClicked;
    std::function<void(int sceneIdx, const juce::MouseEvent& e)> onRightClicked;

    // --- DragAndDropTarget ---
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;

    // --- Component & Button ---
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    int sceneIndex = 0;
    juce::String defaultName;
    juce::File assignedFile;
    int midiPC = -1;
    int midiChannel = 0;
    bool isActive = false;
    bool isDragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneButtonComponent)
};

} // namespace OpenRig
