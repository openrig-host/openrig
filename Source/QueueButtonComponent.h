#pragma once

#include <JuceHeader.h>
#include "ThemeManager.h"

namespace Fanfare {

class QueueButtonComponent : public juce::Button,
                             public juce::DragAndDropTarget {
public:
    QueueButtonComponent(int slotIndex);
    ~QueueButtonComponent() override = default;

    int getSlotIndex() const { return slotIndex; }
    void setAssignedFile(const juce::File& file);
    juce::File getAssignedFile() const { return assignedFile; }

    void setSlotStates(bool active, bool preloaded, bool preloading);

    std::function<void(int slotIdx, const juce::File& file)> onFileDropped;
    std::function<void(int slotIdx)> onClearRequested;
    std::function<void(int slotIdx)> onAssignRequested;

    // --- DragAndDropTarget ---
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;

    // --- Component & Button ---
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    int slotIndex = 0;
    juce::File assignedFile;
    bool isActive = false;
    bool isPreloaded = false;
    bool isPreloading = false;
    bool isDragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QueueButtonComponent)
};

} // namespace Fanfare
