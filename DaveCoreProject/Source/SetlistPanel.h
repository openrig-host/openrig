#pragma once

#include <JuceHeader.h>
#include "SetlistManager.h"
#include "BoutiqueLookAndFeel.h"
#include "ThemeManager.h"

namespace OpenRig {

class SetlistPanel : public juce::Component,
                     public juce::ListBoxModel,
                     public juce::DragAndDropTarget,
                     public juce::ChangeListener {
public:
    SetlistPanel() {
        listBox.setModel(this);
        addAndMakeVisible(listBox);

        addBtn.setButtonText("Add Current");
        addBtn.onClick = [this] {
            if (auto* parent = getParentComponent()) {
                // Get active setup file from MainComponent or SetlistManager
                auto active = SetlistManager::getInstance().getActiveFile();
                // Wait, if SetlistManager has no active file yet, we can load currently loaded rig
                // Let's call a callback or look up the loaded rig file.
                if (onAddCurrentRequested)
                    onAddCurrentRequested();
            }
        };
        addAndMakeVisible(addBtn);

        removeBtn.setButtonText("Remove");
        removeBtn.onClick = [this] {
            int sel = listBox.getSelectedRow();
            if (sel >= 0) {
                SetlistManager::getInstance().removeSetup(sel);
            }
        };
        addAndMakeVisible(removeBtn);

        moveUpBtn.setButtonText("Up");
        moveUpBtn.onClick = [this] {
            int sel = listBox.getSelectedRow();
            if (sel > 0) {
                SetlistManager::getInstance().moveSetupUp(sel);
                listBox.selectRow(sel - 1);
            }
        };
        addAndMakeVisible(moveUpBtn);

        moveDownBtn.setButtonText("Down");
        moveDownBtn.onClick = [this] {
            int sel = listBox.getSelectedRow();
            if (sel >= 0 && sel < SetlistManager::getInstance().getSetups().size() - 1) {
                SetlistManager::getInstance().moveSetupDown(sel);
                listBox.selectRow(sel + 1);
            }
        };
        addAndMakeVisible(moveDownBtn);

        saveBtn.setButtonText("Save List");
        saveBtn.onClick = [this] {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Save Setlist",
                RigLibrary::getSongsDirectory(),
                "*.orsetlist");
            fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc) {
                    auto file = fc.getResult();
                    if (file != juce::File{}) {
                        if (file.getFileExtension().isEmpty())
                            file = file.withFileExtension(".orsetlist");
                        SetlistManager::getInstance().saveSetlist(file);
                    }
                });
        };
        addAndMakeVisible(saveBtn);

        loadBtn.setButtonText("Load List");
        loadBtn.onClick = [this] {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Load Setlist",
                RigLibrary::getSongsDirectory(),
                "*.orsetlist");
            fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc) {
                    auto file = fc.getResult();
                    if (file.existsAsFile()) {
                        SetlistManager::getInstance().loadSetlist(file);
                    }
                });
        };
        addAndMakeVisible(loadBtn);

        clearBtn.setButtonText("Clear");
        clearBtn.onClick = [this] {
            SetlistManager::getInstance().clearSetlist();
        };
        addAndMakeVisible(clearBtn);

        SetlistManager::getInstance().addChangeListener(this);
    }

    ~SetlistPanel() override {
        SetlistManager::getInstance().removeChangeListener(this);
    }

    std::function<void()> onAddCurrentRequested;
    std::function<void(const juce::File&)> onLoadSetupRequested;

    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        listBox.updateContent();
        listBox.repaint();
    }

    // --- ListBoxModel implementation ---
    int getNumRows() override {
        return SetlistManager::getInstance().getSetups().size();
    }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool isSelected) override {
        auto setups = SetlistManager::getInstance().getSetups();
        if (row < 0 || row >= setups.size())
            return;

        int activeIdx = SetlistManager::getInstance().getActiveIndex();

        if (row == activeIdx) {
            g.fillAll(ThemeManager::get(Theme::Role::foh).withAlpha(0.2f)); // active song highlight
        } else if (isSelected) {
            g.fillAll(ThemeManager::get(Theme::Role::accent).withAlpha(0.25f));
        }

        g.setColour(ThemeManager::get(Theme::Role::text));
        juce::String name = setups[row].getFileNameWithoutExtension();

        if (row == activeIdx) {
            name = "▶  " + name;
            g.setColour(ThemeManager::get(Theme::Role::foh));
        }

        g.drawText(name, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
        auto setups = SetlistManager::getInstance().getSetups();
        if (row >= 0 && row < setups.size()) {
            SetlistManager::getInstance().setActiveIndex(row);
            if (onLoadSetupRequested)
                onLoadSetupRequested(setups[row]);
        }
    }

    // --- DragAndDropTarget implementation ---
    bool isInterestedInDragSource(const SourceDetails& details) override {
        juce::String desc = details.description.toString();
        return desc.endsWithIgnoreCase(".json");
    }

    void itemDropped(const SourceDetails& details) override {
        juce::File file(details.description.toString());
        if (file.existsAsFile()) {
            SetlistManager::getInstance().addSetup(file);
        }
    }

    void resized() override {
        auto b = getLocalBounds().reduced(4);
        
        auto controlRow = b.removeFromBottom(28);
        int w = controlRow.getWidth() / 4;
        addBtn.setBounds(controlRow.removeFromLeft(w).reduced(2));
        removeBtn.setBounds(controlRow.removeFromLeft(w).reduced(2));
        moveUpBtn.setBounds(controlRow.removeFromLeft(w).reduced(2));
        moveDownBtn.setBounds(controlRow.reduced(2));

        auto fileRow = b.removeFromBottom(28);
        int w2 = fileRow.getWidth() / 3;
        loadBtn.setBounds(fileRow.removeFromLeft(w2).reduced(2));
        saveBtn.setBounds(fileRow.removeFromLeft(w2).reduced(2));
        clearBtn.setBounds(fileRow.reduced(2));

        b.removeFromBottom(4);
        listBox.setBounds(b);
    }

private:
    juce::ListBox listBox;
    juce::TextButton addBtn, removeBtn, moveUpBtn, moveDownBtn;
    juce::TextButton loadBtn, saveBtn, clearBtn;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetlistPanel)
};

} // namespace OpenRig
