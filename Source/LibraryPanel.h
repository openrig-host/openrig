#pragma once

#include <JuceHeader.h>
#include "RigLibrary.h"
#include <set>

class LibraryList : public juce::ListBox,
                    public juce::ListBoxModel {
public:
  enum class Kind { Sets, Setups, Strips };

  struct Item {
    juce::File file;
    juce::String displayName;
    juce::String category;
    bool isFolderHeader = false;
    bool isFavorite = false;
    juce::Colour badgeColour;
  };

  explicit LibraryList(Kind k);
  void setDirectory(const juce::File &dir, const juce::String &wildcard);
  void setFilterQuery(const juce::String &query);
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
  juce::String filterQuery;
  std::vector<juce::File> allFiles;
  std::vector<Item> items;
  std::set<juce::String> favorites;
  int selectedRow = -1;

  void loadFavorites();
  void saveFavorites();
  void toggleFavorite(const juce::File &f);
  juce::Colour detectBadgeColour(const juce::String &name, Kind k);
};

#include "SetlistPanel.h"

class LibraryPanel : public juce::Component,
                     public juce::DragAndDropContainer {
public:
  LibraryPanel();
  ~LibraryPanel() override;

  void refreshAll();
  void visibilityChanged() override;

  juce::File getSelectedPresetFile() const;
  void resized() override;
  void paint(juce::Graphics &g) override;

  bool isLearning() const;
  void assignLearnedTrigger(struct SetupMidiTrigger t);

  std::function<void(const juce::File &)> onSetupDoubleClicked;
  std::function<void(const juce::File &)> onSetDoubleClicked;

  OpenRig::SetlistPanel* getSetlistPanel() { return setlistPanel.get(); }

private:
  juce::TextEditor searchEditor;
  std::unique_ptr<juce::TabbedComponent> tabs;
  std::unique_ptr<LibraryList> setsList, setupsList, stripsList;
  std::unique_ptr<OpenRig::SetlistPanel> setlistPanel;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryPanel)
};
