#include "LibraryPanel.h"
#include "SetupMidiTriggers.h"
#include "SetlistManager.h"
#include "ThemeManager.h"

LibraryList::LibraryList(Kind k) : kind(k) {
  setModel(this);
  setRowHeight(24);
  setMultipleSelectionEnabled(false);
  loadFavorites();
}

void LibraryList::loadFavorites() {
  favorites.clear();
  auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OpenRig");
  auto favFile = appData.getChildFile("library_favorites.txt");
  if (favFile.existsAsFile()) {
    juce::StringArray lines;
    favFile.readLines(lines);
    for (const auto &l : lines) {
      if (l.trim().isNotEmpty())
        favorites.insert(l.trim());
    }
  }
}

void LibraryList::saveFavorites() {
  auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OpenRig");
  if (!appData.exists()) appData.createDirectory();
  auto favFile = appData.getChildFile("library_favorites.txt");
  juce::String text;
  for (const auto &path : favorites)
    text += path + "\n";
  favFile.replaceWithText(text);
}

void LibraryList::toggleFavorite(const juce::File &f) {
  juce::String path = f.getFullPathName();
  if (favorites.count(path) > 0)
    favorites.erase(path);
  else
    favorites.insert(path);
  saveFavorites();
  refresh();
}

juce::Colour LibraryList::detectBadgeColour(const juce::String &name, Kind k) {
  juce::String n = name.toLowerCase();
  if (n.contains("piano") || n.contains("wurl") || n.contains("rhodes") || n.contains("grand") || n.contains("steinway") || n.contains("keys"))
    return juce::Colour(0xFF00B0FF); // EP / Piano (Blue)
  if (n.contains("organ") || n.contains("b3") || n.contains("leslie"))
    return juce::Colour(0xFFFFB300); // Organ (Amber)
  if (n.contains("brass") || n.contains("horn") || n.contains("sax"))
    return juce::Colour(0xFFFF6D00); // Brass / Horns (Orange)
  if (n.contains("synth") || n.contains("lead") || n.contains("pad") || n.contains("juno") || n.contains("arp") || n.contains("square"))
    return juce::Colour(0xFFE91E63); // Synth / Lead (Magenta)
  if (n.contains("sample") || n.contains("perc") || n.contains("marimba") || n.contains("drum") || n.contains("handclap"))
    return juce::Colour(0xFF00E676); // Percussion / Sample (Green)
  if (k == Kind::Setups)
    return juce::Colour(0xFF00E5FF); // Songs / Setups (Cyan)
  return juce::Colour(0xFF8E8E93);   // Default Grey
}

void LibraryList::setDirectory(const juce::File &dir, const juce::String &wildcard) {
  directory = dir;
  wc = wildcard;
  refresh();
}

void LibraryList::setFilterQuery(const juce::String &query) {
  filterQuery = query.trim().toLowerCase();
  refresh();
}

void LibraryList::refresh() {
  directory.createDirectory();
  allFiles.clear();
  items.clear();

  if (directory.isDirectory()) {
    juce::Array<juce::File> found;
    // Recursive search to support subfolders (categories / song folders)
    directory.findChildFiles(found, juce::File::findFiles, true, wc);
    if (kind == Kind::Setups) {
      juce::Array<juce::File> more;
      directory.findChildFiles(more, juce::File::findFiles, true, "*.orsong");
      for (auto &f : more)
        found.addIfNotAlreadyThere(f);
    }
    for (auto &f : found)
      allFiles.push_back(f);

    std::sort(allFiles.begin(), allFiles.end(),
              [](const juce::File &a, const juce::File &b) {
                return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
              });

    std::vector<Item> favItems;
    std::map<juce::String, std::vector<Item>> categoryMap;

    for (const auto &f : allFiles) {
      juce::String fileName = f.getFileNameWithoutExtension();
      juce::String fullPath = f.getFullPathName();
      juce::String relPath = f.getRelativePathFrom(directory);

      // Search filter check
      if (filterQuery.isNotEmpty()) {
        if (!fileName.toLowerCase().contains(filterQuery) && !relPath.toLowerCase().contains(filterQuery))
          continue;
      }

      bool isFav = (favorites.count(fullPath) > 0);
      juce::Colour badge = detectBadgeColour(fileName, kind);

      juce::String catName = "GENERAL";
      if (f.getParentDirectory() != directory) {
        catName = f.getParentDirectory().getFileName().toUpperCase();
      }

      Item it;
      it.file = f;
      it.displayName = fileName;
      it.category = catName;
      it.isFolderHeader = false;
      it.isFavorite = isFav;
      it.badgeColour = badge;

      if (isFav) {
        favItems.push_back(it);
      } else {
        categoryMap[catName].push_back(it);
      }
    }

    // 1. Favorites at the top
    if (!favItems.empty()) {
      Item header;
      header.displayName = "⭐ FAVORITES";
      header.isFolderHeader = true;
      items.push_back(header);
      for (const auto &it : favItems)
        items.push_back(it);
    }

    // 2. Folder Subcategories & Root files
    for (auto &kv : categoryMap) {
      if (kv.first != "GENERAL") {
        Item header;
        header.displayName = "📁 " + kv.first;
        header.isFolderHeader = true;
        items.push_back(header);
      }
      for (const auto &it : kv.second)
        items.push_back(it);
    }
  }

  updateContent();
  repaint();
}

int LibraryList::getNumRows() { return (int)items.size(); }

void LibraryList::paintListBoxItem(int row, juce::Graphics &g, int width,
                                  int height, bool isSelected) {
  if (row < 0 || row >= (int)items.size())
    return;

  const auto &item = items[row];

  // Folder Category Header Row
  if (item.isFolderHeader) {
    g.fillAll(juce::Colour(0xFF1E2228));
    g.setColour(juce::Colour(0xFF00E5FF));
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.drawText(item.displayName, 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    g.setColour(juce::Colour(0xFF2A2E35));
    g.drawHorizontalLine(height - 1, 0.0f, (float)width);
    return;
  }

  if (isSelected)
    g.fillAll(juce::Colour(0xFF00E5FF).withAlpha(0.25f));
  else if (row % 2 == 1)
    g.fillAll(juce::Colours::white.withAlpha(0.02f));

  // Color Category Badge Pill
  int badgeX = 6;
  int badgeY = 5;
  g.setColour(item.badgeColour.withAlpha(0.85f));
  g.fillRoundedRectangle((float)badgeX, (float)badgeY, 5.0f, (float)(height - 10), 2.0f);

  // Star Icon if Favorite
  int textX = 16;
  if (item.isFavorite) {
    g.setColour(juce::Colours::yellow);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("*", textX, 0, 10, height, juce::Justification::centred, true);
    textX += 12;
  }

  juce::File f = item.file;
  juce::String name = item.displayName;

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

  g.setFont(juce::FontOptions(13.0f));
  g.drawText(name, textX, 0, width - textX - 8, height, juce::Justification::centredLeft, true);
}

void LibraryList::selectedRowsChanged(int) {}

void LibraryList::listBoxItemClicked(int row, const juce::MouseEvent &e) {
  selectedRow = row;
  if (row < 0 || row >= (int)items.size())
    return;

  const auto &item = items[row];
  if (item.isFolderHeader)
    return;

  if (e.mods.isPopupMenu()) {
    juce::PopupMenu menu;
    bool isFav = item.isFavorite;
    menu.addItem(10, isFav ? "Unpin Favorite" : "Pin to Favorites");
    menu.addSeparator();

    if (kind == Kind::Setups) {
      bool isCurrentlyLearning = (learningFile == item.file);
      if (isCurrentlyLearning) {
        menu.addItem(3, "Cancel Learn");
      } else {
        menu.addItem(1, "Learn MIDI Trigger");
      }
      menu.addItem(2, "Clear MIDI Trigger");
      menu.addSeparator();
      menu.addItem(5, "Add to Setlist");
      menu.addSeparator();
    }

    menu.addItem(4, "Delete Item");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, row](int result) {
      if (row < 0 || row >= (int)items.size()) return;
      const auto &targetItem = items[row];
      auto file = targetItem.file;

      if (result == 10) {
        toggleFavorite(file);
      } else if (result == 1) {
        learningFile = file;
        repaint();
      } else if (result == 2) {
        SetupMidiTriggers::getInstance().clearTrigger(file.getFileName());
        repaint();
      } else if (result == 3) {
        learningFile = juce::File{};
        repaint();
      } else if (result == 4) {
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, "Delete Item",
            "Are you sure you want to permanently delete '" + file.getFileNameWithoutExtension() + "'?",
            "Delete", "Cancel", nullptr,
            juce::ModalCallbackFunction::create([this, file](int confirmResult) {
              if (confirmResult == 1) {
                file.deleteFile();
                refresh();
              }
            }));
      } else if (result == 5) {
        OpenRig::SetlistManager::getInstance().addSetup(file);
      }
    });
  }
}

void LibraryList::listBoxItemDoubleClicked(int row, const juce::MouseEvent &) {
  if (row < 0 || row >= (int)items.size())
    return;
  const auto &item = items[row];
  if (item.isFolderHeader)
    return;

  if (kind != Kind::Strips && onDoubleClicked)
    onDoubleClicked(item.file);
}

juce::var LibraryList::getDragSourceDescription(const juce::SparseSet<int>& selectedRows) {
  if (selectedRows.size() > 0) {
    int row = selectedRows[0];
    if (row >= 0 && row < (int)items.size() && !items[row].isFolderHeader)
      return items[row].file.getFullPathName();
  }
  return {};
}

juce::File LibraryList::getSelectedFile() const {
  if (selectedRow >= 0 && selectedRow < (int)items.size() && !items[selectedRow].isFolderHeader)
    return items[selectedRow].file;
  return {};
}

LibraryPanel::LibraryPanel() {
  addAndMakeVisible(searchEditor);
  searchEditor.setTextToShowWhenEmpty("Filter library...", juce::Colours::grey);
  searchEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF1B1E22));
  searchEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF33383F));
  searchEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
  searchEditor.setFont(juce::FontOptions(13.0f));
  searchEditor.onTextChange = [this] {
    juce::String q = searchEditor.getText();
    if (setsList) setsList->setFilterQuery(q);
    if (setupsList) setupsList->setFilterQuery(q);
    if (stripsList) stripsList->setFilterQuery(q);
  };

  tabs = std::make_unique<juce::TabbedComponent>(
      juce::TabbedButtonBar::TabsAtLeft);
  tabs->setTabBarDepth(28);

  setsList = std::make_unique<LibraryList>(LibraryList::Kind::Sets);
  setsList->setDirectory(OpenRig::RigLibrary::getSetsDirectory(), "*.orset");
  setsList->onDoubleClicked = [this](const juce::File &f) {
    if (onSetDoubleClicked)
      onSetDoubleClicked(f);
  };
  tabs->addTab("Btn Maps", juce::Colour(0xFF1D2023), setsList.get(), false);

  setupsList = std::make_unique<LibraryList>(LibraryList::Kind::Setups);
  setupsList->setDirectory(OpenRig::RigLibrary::getSongsDirectory(), "*.json");
  setupsList->onDoubleClicked = [this](const juce::File &f) {
    if (onSetupDoubleClicked)
      onSetupDoubleClicked(f);
  };
  tabs->addTab("Songs", juce::Colour(0xFF1D2023), setupsList.get(), false);

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

void LibraryPanel::resized() {
  searchEditor.setBounds(4, 4, getWidth() - 8, 24);
  tabs->setBounds(0, 32, getWidth(), getHeight() - 32);
}

void LibraryPanel::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xFF16181B));
  g.setColour(juce::Colour(0xFF2A2D32));
  g.drawLine((float)getWidth() - 0.5f, 0.0f,
             (float)getWidth() - 0.5f, (float)getHeight(), 0.5f);
}
