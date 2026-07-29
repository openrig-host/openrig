#include "QueueButtonComponent.h"
#include "RigLibrary.h"

namespace Fanfare {

QueueButtonComponent::QueueButtonComponent(int index)
    : juce::Button("QueueButton_" + juce::String(index)), slotIndex(index) {
    setTooltip(slotIndex == 0 ? "CORE SETUP (Default stage rig)" : "Stage Queue Slot " + juce::String(slotIndex));
}

void QueueButtonComponent::setAssignedFile(const juce::File& file) {
    assignedFile = file;
    repaint();
}

void QueueButtonComponent::setSlotStates(bool active, bool preloaded, bool preloading) {
    bool changed = (isActive != active || isPreloaded != preloaded || isPreloading != preloading);
    isActive = active;
    isPreloaded = preloaded;
    isPreloading = preloading;
    if (changed) repaint();
}

bool QueueButtonComponent::isInterestedInDragSource(const SourceDetails& dragSourceDetails) {
    juce::String desc = dragSourceDetails.description.toString();
    if (desc.isEmpty()) return false;
    juce::File f(desc);
    return f.getFileExtension().equalsIgnoreCase(".json") || desc.containsIgnoreCase(".json");
}

void QueueButtonComponent::itemDragEnter(const SourceDetails&) {
    isDragHover = true;
    repaint();
}

void QueueButtonComponent::itemDragExit(const SourceDetails&) {
    isDragHover = false;
    repaint();
}

void QueueButtonComponent::itemDropped(const SourceDetails& dragSourceDetails) {
    isDragHover = false;
    juce::String path = dragSourceDetails.description.toString();
    juce::File file(path);
    if (!file.existsAsFile()) {
        file = RigLibrary::getSongsDirectory().getChildFile(path);
    }

    if (file.existsAsFile() && onFileDropped) {
        onFileDropped(slotIndex, file);
    }
    repaint();
}

void QueueButtonComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addSectionHeader(slotIndex == 0 ? "CORE SETUP SLOT" : "QUEUE SLOT " + juce::String(slotIndex));
        menu.addItem(1, "Assign Setup File...");
        if (assignedFile.existsAsFile()) {
            menu.addItem(2, "Clear Slot");
        }
        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
            if (result == 1 && onAssignRequested) {
                onAssignRequested(slotIndex);
            } else if (result == 2 && onClearRequested) {
                onClearRequested(slotIndex);
            }
        });
        return;
    }
    juce::Button::mouseDown(e);
}

void QueueButtonComponent::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    auto bounds = getLocalBounds().toFloat();

    // Background color determination
    juce::Colour bgColour = ThemeManager::get(Theme::Role::panelAlt);
    juce::Colour borderColour = juce::Colour(0xFF33383F);

    bool isCore = (slotIndex == 0);

    if (isActive) {
        bgColour = isCore ? juce::Colour(0xFF005A9E) : ThemeManager::get(Theme::Role::accent).darker(0.3f);
        borderColour = ThemeManager::get(Theme::Role::accent);
    } else if (isPreloaded) {
        bgColour = juce::Colour(0xFF1B3D2F); // Ready green tint
        borderColour = ThemeManager::get(Theme::Role::ok);
    } else if (isCore) {
        bgColour = juce::Colour(0xFF1E2638);
        borderColour = juce::Colour(0xFF2A3C5A);
    }

    if (shouldDrawButtonAsDown) {
        bgColour = bgColour.darker(0.3f);
    } else if (shouldDrawButtonAsHighlighted || isDragHover) {
        bgColour = bgColour.brighter(0.2f);
    }

    if (isDragHover) {
        borderColour = juce::Colour(0xFF00E5FF);
    }

    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.reduced(1.0f), 5.0f);

    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds.reduced(1.0f), 5.0f, isDragHover || isActive ? 2.5f : 1.2f);

    // Slot Category Header Tag
    auto tagArea = bounds.removeFromTop(14.0f).reduced(4.0f, 2.0f);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    if (isCore) {
        g.setColour(juce::Colour(0xFF00E5FF));
        g.drawText("CORE SETUP", tagArea, juce::Justification::centredLeft, true);
    } else {
        g.setColour(juce::Colour(0xFF8E8E93));
        g.drawText("NEXT " + juce::String(slotIndex), tagArea, juce::Justification::centredLeft, true);
    }

    // Preload Status Badge
    if (isPreloaded) {
        g.setColour(ThemeManager::get(Theme::Role::ok));
        g.drawText("READY", tagArea, juce::Justification::centredRight, true);
    } else if (isPreloading) {
        g.setColour(juce::Colours::orange);
        g.drawText("PRELOADING...", tagArea, juce::Justification::centredRight, true);
    } else if (isActive) {
        g.setColour(juce::Colours::white);
        g.drawText("PLAYING", tagArea, juce::Justification::centredRight, true);
    }

    // Main Song Title text
    juce::String title;
    if (assignedFile.existsAsFile()) {
        title = assignedFile.getFileNameWithoutExtension();
    } else if (isCore) {
        title = "CORE SETUP";
    } else {
        title = "[ Drag Song Here ]";
    }

    g.setFont(juce::FontOptions(isActive ? 15.0f : 13.5f, juce::Font::bold));
    g.setColour(assignedFile.existsAsFile() ? juce::Colours::white : juce::Colours::grey.withAlpha(0.6f));
    
    auto titleArea = bounds.reduced(6.0f, 2.0f);
    g.drawText(title, titleArea, juce::Justification::centred, true);
}

} // namespace Fanfare
