#pragma once

#include <JuceHeader.h>
#include "RigLibrary.h"

class LibraryList : public juce::ListBox,
                    public juce::ListBoxModel {
public:
  enum class Kind { Sets, Setups, Strips };

  explicit LibraryList(Kind k);
  void setDirectory(const juce::File &dir, const juce::String &wildcard);
  void refresh();

  int getNumRows() override;
  void paintListBoxItem(int row, juce::Graphics &g, int width, int height,
                        bool isSelected) override;

  void selectedRowsChanged(int) override;
  void listBoxItemClicked(int row, const juce::MouseEvent &) override;
  void listBoxItemDoubleClicked(int row, const juce::MouseEvent &) override;

  juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;

  juce::File getSelectedFile() const;

  std::function<void(const juce::File &)> onDoubleClicked;

  Kind kind;
  juce::File learningFile;

private:
  juce::File directory;
  juce::String wc;
  std::vector<juce::File> files;
  int selectedRow = -1;
};

#include "SetlistPanel.h"

class LibraryPanel : public juce::Component,
                     public juce::DragAndDropContainer,
                     private juce::Timer {
public:
  LibraryPanel();
  ~LibraryPanel() override;

  void refreshAll();
  void visibilityChanged() override;
  void timerCallback() override;

  juce::File getSelectedPresetFile() const;
  void resized() override;
  void paint(juce::Graphics &g) override;

  bool isLearning() const;
  void assignLearnedTrigger(struct SetupMidiTrigger t);

  std::function<void(const juce::File &)> onSetupDoubleClicked;
  std::function<void(const juce::File &)> onSetDoubleClicked;

  OpenRig::SetlistPanel* getSetlistPanel() { return setlistPanel.get(); }

private:
  std::unique_ptr<juce::TabbedComponent> tabs;
  std::unique_ptr<LibraryList> setsList, setupsList, stripsList;
  std::unique_ptr<OpenRig::SetlistPanel> setlistPanel;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryPanel)
};
