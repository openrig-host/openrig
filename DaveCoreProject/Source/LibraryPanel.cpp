#include "LibraryPanel.h"
#include "SetupMidiTriggers.h"

LibraryList::LibraryList(Kind k) : kind(k) {
  setModel(this);
  setRowHeight(22);
  setMultipleSelectionEnabled(false);
}

void LibraryList::setDirectory(const juce::File &dir, const juce::String &wildcard) {
  directory = dir;
  wc = wildcard;
  refresh();
}

void LibraryList::refresh() {
  directory.createDirectory();
  files.clear();
  if (directory.isDirectory()) {
    juce::Array<juce::File> found;
    directory.findChildFiles(found, juce::File::findFiles, false, wc);
    if (kind == Kind::Setups) {
      juce::Array<juce::File> more;
      directory.findChildFiles(more, juce::File::findFiles, false, "*.orsong");
      for (auto &f : more)
        found.addIfNotAlreadyThere(f);
    }
    for (auto &f : found)
      files.push_back(f);
    std::sort(files.begin(), files.end(),
              [](const juce::File &a, const juce::File &b) {
                return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
              });
  }
  updateContent();
  repaint();
}

int LibraryList::getNumRows() { return (int)files.size(); }

void LibraryList::paintListBoxItem(int row, juce::Graphics &g, int width,
                                  int height, bool isSelected) {
  if (row < 0 || row >= (int)files.size())
    return;
  if (isSelected)
    g.fillAll(juce::Colour(0xFF00E5FF).withAlpha(0.25f));
  
  juce::File f = files[row];
  juce::String name = f.getFileNameWithoutExtension();
  
  if (kind == Kind::Setups) {
    if (learningFile == f) {
      name += " [LEARNING...]";
      g.setColour(juce::Colours::orange);
    } else {
      auto trigger = SetupMidiTriggers::getInstance().getTrigger(f.getFileName());
      if (trigger.number != -1) {
        name += " [" + trigger.toString() + "]";
        g.setColour(juce::Colours::cyan);
      } else {
        g.setColour(juce::Colours::white);
      }
    }
  } else {
    g.setColour(juce::Colours::white);
  }
  
  g.drawText(name, 4, 0, width - 8,
             height, juce::Justification::centredLeft, true);
}

void LibraryList::selectedRowsChanged(int) {}

void LibraryList::listBoxItemClicked(int row, const juce::MouseEvent &e) {
  selectedRow = row;
  if (row < 0 || row >= (int)files.size())
    return;
    
  if (kind == Kind::Setups && e.mods.isPopupMenu()) {
    juce::PopupMenu menu;
    bool isCurrentlyLearning = (learningFile == files[row]);
    
    if (isCurrentlyLearning) {
      menu.addItem(3, "Cancel Learn");
    } else {
      menu.addItem(1, "Learn MIDI Trigger");
    }
    menu.addItem(2, "Clear MIDI Trigger");
    menu.addSeparator();
    menu.addItem(4, "Delete Setup");
    
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, row](int result) {
      if (result == 1) {
        learningFile = files[row];
        repaint();
      } else if (result == 2) {
        SetupMidiTriggers::getInstance().clearTrigger(files[row].getFileName());
        repaint();
      } else if (result == 3) {
        learningFile = juce::File{};
        repaint();
      } else if (result == 4) {
        auto file = files[row];
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, "Delete Setup",
            "Are you sure you want to permanently delete '" + file.getFileNameWithoutExtension() + "'?",
            "Delete", "Cancel", nullptr,
            juce::ModalCallbackFunction::create([this, file](int confirmResult) {
              if (confirmResult == 1) {
                file.deleteFile();
                refresh();
              }
            }));
      }
    });
  }
}

void LibraryList::listBoxItemDoubleClicked(int row, const juce::MouseEvent &) {
  if (row < 0 || row >= (int)files.size())
    return;
  const auto &f = files[row];
  if (kind != Kind::Strips && onDoubleClicked)
    onDoubleClicked(f);
}

juce::var LibraryList::getDragSourceDescription(const juce::SparseSet<int>& selectedRows) {
  if (selectedRows.size() > 0) {
    int row = selectedRows[0];
    if (row >= 0 && row < (int)files.size())
      return files[row].getFullPathName();
  }
  return {};
}

juce::File LibraryList::getSelectedFile() const {
  if (selectedRow >= 0 && selectedRow < (int)files.size())
    return files[selectedRow];
  return {};
}

LibraryPanel::LibraryPanel() {
  tabs = std::make_unique<juce::TabbedComponent>(
      juce::TabbedButtonBar::TabsAtLeft);
  tabs->setTabBarDepth(28);

  setsList = std::make_unique<LibraryList>(LibraryList::Kind::Sets);
  setsList->setDirectory(OpenRig::RigLibrary::getSetsDirectory(), "*.orset");
  setsList->onDoubleClicked = [this](const juce::File &f) {
    if (onSetDoubleClicked)
      onSetDoubleClicked(f);
  };
  tabs->addTab("Sets", juce::Colour(0xFF1D2023), setsList.get(), false);

  setupsList = std::make_unique<LibraryList>(LibraryList::Kind::Setups);
  setupsList->setDirectory(OpenRig::RigLibrary::getSongsDirectory(), "*.json");
  setupsList->onDoubleClicked = [this](const juce::File &f) {
    if (onSetupDoubleClicked)
      onSetupDoubleClicked(f);
  };
  tabs->addTab("Setups", juce::Colour(0xFF1D2023), setupsList.get(), false);

  stripsList = std::make_unique<LibraryList>(LibraryList::Kind::Strips);
  stripsList->setDirectory(OpenRig::RigLibrary::getPresetsDirectory(), "*.orstrip");
  tabs->addTab("Strips", juce::Colour(0xFF1D2023), stripsList.get(), false);

  setlistPanel = std::make_unique<OpenRig::SetlistPanel>();
  tabs->addTab("Setlist", juce::Colour(0xFF1D2023), setlistPanel.get(), false);

  addAndMakeVisible(tabs.get());
  setSize(240, 400);
  startTimer(3000);
}

LibraryPanel::~LibraryPanel() { stopTimer(); }

void LibraryPanel::refreshAll() {
  if (setsList)
    setsList->refresh();
  if (setupsList)
    setupsList->refresh();
  if (stripsList)
    stripsList->refresh();
}

void LibraryPanel::visibilityChanged() {
  juce::Component::visibilityChanged();
  refreshAll();
}

void LibraryPanel::timerCallback() { refreshAll(); }

juce::File LibraryPanel::getSelectedPresetFile() const {
  if (tabs->getCurrentTabIndex() == 2 && stripsList != nullptr)
    return stripsList->getSelectedFile();
  return {};
}

bool LibraryPanel::isLearning() const {
  return setupsList && setupsList->learningFile.existsAsFile();
}

void LibraryPanel::assignLearnedTrigger(SetupMidiTrigger t) {
  if (setupsList && setupsList->learningFile.existsAsFile()) {
    auto fileName = setupsList->learningFile.getFileName();
    SetupMidiTriggers::getInstance().setTrigger(fileName, t);
    setupsList->learningFile = juce::File{};
    setupsList->repaint();
  }
}

void LibraryPanel::resized() { tabs->setBounds(getLocalBounds()); }

void LibraryPanel::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xFF16181B));
  g.setColour(juce::Colour(0xFF2A2D32));
  g.drawLine((float)getWidth() - 0.5f, 0.0f,
             (float)getWidth() - 0.5f, (float)getHeight(), 0.5f);
}
