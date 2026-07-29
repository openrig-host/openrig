#include "SceneButtonComponent.h"
#include "RigLibrary.h"

namespace OpenRig {

SceneButtonComponent::SceneButtonComponent(int index, const juce::String& name)
    : juce::Button("SceneButton_" + juce::String(index)), sceneIndex(index), defaultName(name) {
    setTooltip("Scene / Quick Preset " + juce::String(index + 1) + " (Drag & Drop setup .json file here)");
}

void SceneButtonComponent::setAssignedFile(const juce::File& file) {
    assignedFile = file;
    repaint();
}

void SceneButtonComponent::setMidiTriggerInfo(int pc, int channel) {
    midiPC = pc;
    midiChannel = channel;
    repaint();
}

void SceneButtonComponent::setActive(bool active) {
    if (isActive != active) {
        isActive = active;
        repaint();
    }
}

bool SceneButtonComponent::isInterestedInDragSource(const SourceDetails& dragSourceDetails) {
    juce::String desc = dragSourceDetails.description.toString();
    if (desc.isEmpty()) return false;
    juce::File f(desc);
    return f.getFileExtension().equalsIgnoreCase(".json") || desc.containsIgnoreCase(".json");
}

void SceneButtonComponent::itemDragEnter(const SourceDetails&) {
    isDragHover = true;
    repaint();
}

void SceneButtonComponent::itemDragExit(const SourceDetails&) {
    isDragHover = false;
    repaint();
}

void SceneButtonComponent::itemDropped(const SourceDetails& dragSourceDetails) {
    isDragHover = false;
    juce::String path = dragSourceDetails.description.toString();
    juce::File file(path);
    if (!file.existsAsFile()) {
        file = RigLibrary::getSongsDirectory().getChildFile(path);
    }

    if (file.existsAsFile() && onFileDropped) {
        onFileDropped(sceneIndex, file);
    }
    repaint();
}

void SceneButtonComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onRightClicked) {
            onRightClicked(sceneIndex, e);
        }
        return;
    }
    
    if (onClicked) {
        onClicked(sceneIndex);
    }
    
    juce::Button::mouseDown(e);
}

void SceneButtonComponent::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    auto bounds = getLocalBounds().toFloat();

    juce::Colour bgColour = ThemeManager::get(Theme::Role::panelAlt);
    juce::Colour borderColour = juce::Colour(0xFF33383F);

    bool hasSetupFile = assignedFile.existsAsFile();

    if (isActive) {
        bgColour = hasSetupFile ? juce::Colour(0xFF005A9E) : ThemeManager::get(Theme::Role::accent);
        borderColour = ThemeManager::get(Theme::Role::accent);
    } else if (hasSetupFile) {
        bgColour = juce::Colour(0xFF1E2638);
        borderColour = juce::Colour(0xFF2A3C5A);
    } else if (midiPC >= 0) {
        bgColour = ThemeManager::get(Theme::Role::iem).darker(0.4f);
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
    g.fillRoundedRectangle(bounds.reduced(1.0f), 4.0f);

    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, isDragHover || isActive ? 2.5f : 1.2f);

    // Text Display
    juce::String title;
    if (hasSetupFile) {
        title = assignedFile.getFileNameWithoutExtension();
    } else {
        title = defaultName;
    }

    g.setFont(juce::FontOptions(isActive ? 14.0f : 12.5f, juce::Font::bold));
    g.setColour(isActive ? juce::Colours::white : (hasSetupFile ? juce::Colour(0xFF00E5FF) : juce::Colours::white.withAlpha(0.85f)));
    
    auto titleArea = bounds.reduced(4.0f, 2.0f);
    g.drawText(title, titleArea, juce::Justification::centred, true);

    // Indicator badge for assigned Setup or MIDI trigger
    if (hasSetupFile) {
        g.setColour(juce::Colour(0xFF00E5FF));
        g.fillEllipse(bounds.getRight() - 8.0f, bounds.getY() + 4.0f, 4.0f, 4.0f);
    } else if (midiPC >= 0) {
        g.setColour(juce::Colours::orange);
        g.fillEllipse(bounds.getRight() - 8.0f, bounds.getY() + 4.0f, 4.0f, 4.0f);
    }
}

} // namespace OpenRig
