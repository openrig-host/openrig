// Windows MMCSS for real-time audio thread priority
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "CCMappingComponent.h"
#include "MainComponent.h"
#include "NoteRangeComponent.h"
#include "RackSlotComponent.h"

namespace {
// Procedurally renders a gear icon for the settings ImageButton (no asset dep).
juce::Image createGearImage(int size, juce::Colour colour) {
  juce::Image img(juce::Image::ARGB, size, size, true);
  juce::Graphics g(img);
  g.setColour(colour);

  const float cx = size * 0.5f;
  const float cy = size * 0.5f;
  const float bodyR = size * 0.34f;
  const float toothR = size * 0.46f;
  const int teeth = 8;

  juce::Path gear;
  for (int i = 0; i < teeth; ++i) {
    float a0 = juce::MathConstants<float>::twoPi * (i + 0.08f) / teeth;
    float a1 = juce::MathConstants<float>::twoPi * (i + 0.42f) / teeth;
    float a2 = juce::MathConstants<float>::twoPi * (i + 0.5f) / teeth;
    float a3 = juce::MathConstants<float>::twoPi * (i + 0.92f) / teeth;
    auto p = [&](float ang, float r) {
      gear.lineTo(cx + r * std::cos(ang), cy + r * std::sin(ang));
    };
    if (i == 0)
      gear.startNewSubPath(cx + bodyR * std::cos(a0), cy + bodyR * std::sin(a0));
    p(a0, bodyR);
    p(a1, toothR);
    p(a2, toothR);
    p(a3, bodyR);
  }
  gear.closeSubPath();
  g.fillPath(gear);

  // Center hole (drawn in panel colour to read as a hole on the dark header)
  g.setColour(juce::Colour(0xff121315));
  g.fillEllipse(cx - size * 0.16f, cy - size * 0.16f, size * 0.32f,
                size * 0.32f);
  return img;
}
} // namespace

// Self-deleting window for plugin editors
// Windows managed by MainComponent
class PluginWindow : public juce::DocumentWindow {
public:
  std::function<void()> onWindowClosed;
  juce::AudioPluginInstance* pluginInstance = nullptr;

  PluginWindow(const juce::String &name, juce::Component *content, juce::AudioPluginInstance* pi)
      : DocumentWindow(name, juce::Colours::darkgrey,
                       DocumentWindow::closeButton |
                           DocumentWindow::minimiseButton),
        pluginInstance(pi) {
    setContentOwned(content, true);
    setResizable(true, false);
    setUsingNativeTitleBar(true);
    centreWithSize(content->getWidth(), content->getHeight());
    setVisible(true);
    toFront(true);
  }
  void closeButtonPressed() override {
    if (onWindowClosed)
      onWindowClosed();
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

//==============================================================================
//==============================================================================
MainComponent::MainComponent() {
#ifdef _WIN32
  avrtModule = LoadLibraryA("avrt.dll");
  if (avrtModule != nullptr) {
    avSetMmThreadFn = (PAvSetMmThreadCharacteristicsA)GetProcAddress(avrtModule, "AvSetMmThreadCharacteristicsA");
  }
#endif
  // Initialize MIDI collector with default rate
  midiCollector.reset(OpenRigConstants::kDefaultSampleRate);

  // One-time, non-destructive migration of legacy scattered rig files into
  // the consolidated %APPDATA%/OpenRig library layout.
  OpenRig::RigLibrary::migrateLegacyLibraryIfNeeded();
  OpenRig::RigSerializer::ensureLibraryLayout();

  // Async rig transitioner (Pillar B: trustworthy, non-freezing song switches)
  transitioner = std::make_unique<OpenRig::RigTransitioner>(engine);

  // Listen for audio/MIDI device changes
  deviceManager.addChangeListener(this);
  loadAudioSettings();

  // Apply look and feel
  setLookAndFeel(&boutiqueLookAndFeel);
  juce::LookAndFeel::setDefaultLookAndFeel(&boutiqueLookAndFeel);

  // Preload skeuomorphic assets for performance
  AssetLoader::getInstance().preloadAssets();

  // Setup UI components (extracted for readability)
  setupSlotComponents();
  setupHeaderButtons();
  setupSetupButtons();
  setupMasterSection();

  // Collapsible library sidebar
  addAndMakeVisible(toggleLibraryBtn);
  toggleLibraryBtn.setTooltip("Toggle Library Sidebar");
  toggleLibraryBtn.setColour(juce::TextButton::buttonColourId,
                              juce::Colour(0xFF2A2D32));
  toggleLibraryBtn.onClick = [this] {
    libraryVisible = !libraryVisible;
    toggleLibraryBtn.setButtonText(libraryVisible ? "<<" : ">>");
    if (libraryPanel)
      libraryPanel->setVisible(libraryVisible);
    resized();
  };

  libraryPanel = std::make_unique<LibraryPanel>();
  addAndMakeVisible(libraryPanel.get());
  libraryPanel->onSetupDoubleClicked = [this](const juce::File &file) {
    loadRigFromFile(file);
  };
  libraryPanel->onSetDoubleClicked = [this](const juce::File &file) {
    loadSetFile(file);
  };

  transitioner = std::make_unique<OpenRig::RigTransitioner>(engine);

  // Final setup
  setSize(OpenRigConstants::kDefaultWindowWidth,
          OpenRigConstants::kDefaultWindowHeight);
  startTimer(OpenRigConstants::kTimerIntervalMs);

  // Setup MIDI inputs
  auto midiInputs = juce::MidiInput::getAvailableDevices();
  for (auto &input : midiInputs) {
    deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
    deviceManager.addMidiInputDeviceCallback(input.identifier, this);
  }

  // Add audio callback
  deviceManager.addAudioCallback(this);

  // Initialize scene buttons from default scenes
  refreshSceneButtons();

  LOG_INFO("OpenRig initialized successfully");
}

MainComponent::~MainComponent() {
  stopTimer();
  if (transitioner)
    transitioner->stopTransition();
  activePluginWindows.clear();
  deviceManager.removeChangeListener(this);
  saveAudioSettings();
  deviceManager.removeMidiInputDeviceCallback({}, this);
  auto midiInputs = juce::MidiInput::getAvailableDevices();
  for (auto &input : midiInputs)
    deviceManager.removeMidiInputDeviceCallback(input.identifier, this);

  deviceManager.removeAudioCallback(this);
  engine.releaseAllPlugins();
  juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
  setLookAndFeel(nullptr);

#ifdef _WIN32
  if (avrtModule != nullptr) {
    FreeLibrary(avrtModule);
    avrtModule = nullptr;
  }
#endif
}

//==============================================================================
// Setup Helper Methods (extracted from constructor for readability)
//==============================================================================

void MainComponent::setupSlotComponents() {
  // Build input selector data
  std::vector<std::pair<juce::String, int>> inputItems;
  inputItems.push_back({"No Input", -1});

  auto *device = deviceManager.getCurrentAudioDevice();
  if (device != nullptr) {
    auto inputs = device->getInputChannelNames();
    for (int hw = 0; hw < inputs.size(); ++hw) {
      inputItems.push_back(
          {"In " + juce::String(hw + 1) + ": " + inputs[hw], hw});
    }
  }

  for (int i = 0; i < engine.getNumSlots(); ++i) {
    auto *slot = engine.getSlot(i);
    auto *comp = new RackSlotComponent(*slot, i, boutiqueLookAndFeel);

    // Populate Input Selector
    auto &selector = comp->getInputSelector();
    selector.clear();
    for (const auto &item : inputItems) {
      selector.addItem(item.first, item.second + 2);
    }
    selector.setSelectedId(slot->getInputChannelIndex() + 2,
                           juce::dontSendNotification);

    // Specialized modes
    bool isMonitorIn = (i == 0); // First slot is Monitor In
    bool isAccordion = (slot->getName() == "Accordion");
    comp->setSpecialModes(isMonitorIn, isAccordion, false);

    // Plugin selection menu callback
    comp->onShowPluginMenu = [this, i, slot, comp](int chainIndex) {
      juce::PopupMenu menu;
      menu.addSectionHeader("Select Plugin for Chain " +
                            juce::String(chainIndex + 1));
      for (int p = 0; p < engine.getNumAvailablePlugins(); ++p) {
        menu.addItem(p + 1, engine.getAvailablePluginName(p));
      }
      menu.addSeparator();
      menu.addItem(-1, "(No Plugin)");

      menu.showMenuAsync(juce::PopupMenu::Options(), [this, i, slot, comp,
                                                      chainIndex](int result) {
        if (result > 0) {
          loadingOverlay.reset(new LoadingOverlay());
          addAndMakeVisible(loadingOverlay.get());

          engine.loadPluginIntoSlot(
              i, chainIndex, result - 1,
              [this, comp](bool success, const juce::String &err) {
                loadingOverlay.reset();
                if (!success)
                  juce::AlertWindow::showMessageBoxAsync(
                      juce::MessageBoxIconType::WarningIcon,
                      "Plugin Load Failed", err);
                else
                  comp->repaint();
              });
        } else if (result == -1) {
          juce::ScopedLock sl(engine.getCallbackLock());
          slot->setPluginInChain(chainIndex, nullptr);
          comp->repaint();
        }
      });
    };

    // Editor open callback
    comp->onOpenEditor = [this, slot](int chainIndex) {
      auto *plugin = slot->getPluginInstance(chainIndex);
      if (!plugin || !plugin->hasEditor())
        return;
      for (auto *w : activePluginWindows) {
        if (auto *pw = dynamic_cast<PluginWindow*>(w)) {
          if (pw->pluginInstance == plugin) {
            pw->toFront(true);
            return;
          }
        }
      }
      showLoadingOverlay("OPENING EDITOR", "Creating editor... (heavy plugins may take a while)");
      juce::MessageManager::callAsync([this, slot, chainIndex]() {
        auto *plugin = slot->getPluginInstance(chainIndex);
        if (!plugin) {
          hideLoadingOverlay();
          return;
        }
        try {
          auto *editor = plugin->createEditor();
          hideLoadingOverlay();
          if (editor) {
            auto *w = new PluginWindow(slot->getPluginName(chainIndex), editor, plugin);
            w->onWindowClosed = [this, w] {
              if (activePluginWindows.contains(w)) {
                activePluginWindows.removeObject(w, false);
                juce::MessageManager::callAsync([w]() { delete w; });
              }
            };
            activePluginWindows.add(w);
          }
        } catch (...) {
          hideLoadingOverlay();
          juce::AlertWindow::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon, "Editor Error",
              "Plugin editor could not be created.");
        }
      });
    };

    // Customize Channel / Channel Strip Callback
    comp->onShowChannelStrip = [this, i, slot] {
      // Create overlay
      channelStripOverlay.reset(new ChannelStripComponent(*slot, [this] {
        // Close callback
        channelStripOverlay.reset();
        resized(); // Re-layout to remove gap
      }));
      addAndMakeVisible(channelStripOverlay.get());
      channelStripOverlay->centreWithSize(800, 480);
    };

    // Arpeggiator Dialog
    comp->onShowArpeggiator = [this, i, slot] {
      juce::StringArray slotNames;
      for (int s = 0; s < engine.getNumSlots(); ++s)
        slotNames.add(engine.getSlot(s)->getName());
      midiEffectsOverlay.reset(new MidiEffectsComponent(
          slot->getArpeggiator(), slot->getHarmonizer(), slotNames, i, *slot,
          [this] {
            midiEffectsOverlay.reset();
            resized();
          }));
      addAndMakeVisible(midiEffectsOverlay.get());
      midiEffectsOverlay->centreWithSize(620, 410);
    };

    // Sampler Dialog
    comp->onShowSampler = [this, slot] {
      samplerOverlay.reset(new SamplerComponent(*slot, [this] {
        samplerOverlay.reset();
        resized();
      }));
      addAndMakeVisible(samplerOverlay.get());
      samplerOverlay->centreWithSize(720, 600);
    };

    // Note Range Dialog (CallOutBox)
    comp->onShowNoteRangeDialog = [this, slot, comp] {
      auto *nrComp = new NoteRangeComponent(*slot);
      nrComp->setSize(800, 180);
      activeNoteRangeLearner = nrComp;
      juce::CallOutBox::launchAsynchronously(
          std::unique_ptr<juce::Component>(nrComp),
          comp->getNoteRangeButtonBounds(), this);
    };

    // MIDI CC Dialog Callback (Advanced UI with Learn)
    comp->onShowCCDialog = [this, slot, comp] {
      auto *ccComp = new CCMappingComponent(*slot);
      ccComp->setSize(660, 510);
      auto &box = juce::CallOutBox::launchAsynchronously(
          std::unique_ptr<juce::Component>(ccComp),
          comp->getNoteRangeButtonBounds(), this);
      box.setDismissalMouseClicksAreAlwaysConsumed(true);
    };

    // Save Strip to File
    comp->onSaveStrip = [this, i] {
      fileChooser = std::make_unique<juce::FileChooser>(
          "Save Strip As...",
          OpenRigConstants::getAppDirectory().getChildFile("strips"),
          "*.orstrip");
      fileChooser->launchAsync(
          juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
          [this, i](const juce::FileChooser &fc) {
            auto file = fc.getResult();
            if (file == juce::File{})
              return;
            file.getParentDirectory().createDirectory();
            engine.saveStripToFile(i, file);
          });
    };

    // Load Strip from File
    comp->onLoadStrip = [this, i, slot, comp] {
      juce::File selectedFile = libraryPanel ? libraryPanel->getSelectedPresetFile() : juce::File{};
      if (selectedFile.existsAsFile()) {
          bool isSlotEmpty = true;
          for (int chainIdx = 0; chainIdx < 3; ++chainIdx) {
              if (slot->getPluginInstance(chainIdx) != nullptr) {
                  isSlotEmpty = false;
                  break;
              }
          }
          auto loadPreset = [this, i, comp, selectedFile] {
              OpenRig::SongSlot loadedSlot;
              if (OpenRig::RigSerializer::readStripFromFile(selectedFile, loadedSlot)) {
                  engine.loadSlotPreset(i, loadedSlot);
                  comp->repaint();
              }
          };
          if (isSlotEmpty) {
              loadPreset();
          } else {
              juce::AlertWindow::showOkCancelBox(
                  juce::MessageBoxIconType::QuestionIcon,
                  "Overwrite Channel?",
                  "This channel already has plugins loaded. Do you want to overwrite it with the selected preset?",
                  "Overwrite",
                  "Cancel",
                  nullptr,
                  juce::ModalCallbackFunction::create([loadPreset](int result) {
                      if (result == 1) {
                          loadPreset();
                      }
                  })
              );
          }
      } else {
          fileChooser = std::make_unique<juce::FileChooser>(
              "Load Strip...",
              OpenRigConstants::getAppDirectory().getChildFile("strips"),
              "*.orstrip");
          fileChooser->launchAsync(
              juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
              [this, i, comp](const juce::FileChooser &fc) {
                auto file = fc.getResult();
                if (file == juce::File{})
                  return;
                OpenRig::SongSlot loadedSlot;
                if (OpenRig::RigSerializer::readStripFromFile(file, loadedSlot)) {
                    engine.loadSlotPreset(i, loadedSlot);
                    comp->repaint();
                }
              });
      }
    };

    // Quick-load a specific strip file (from right-click submenu)
    comp->onLoadStripFile = [this, i](const juce::File &file) {
      engine.loadStripFromFile(i, file);
    };

    // Drag-and-drop strip preset from the library sidebar
    comp->onLoadPresetFile = [this, i, comp](const juce::File &file) {
      OpenRig::SongSlot loadedSlot;
      if (OpenRig::RigSerializer::readStripFromFile(file, loadedSlot))
        engine.loadSlotPreset(i, loadedSlot);
      comp->repaint();
    };

    rackSlotComponents.add(comp);
    addAndMakeVisible(comp);
  }

  // Setup Aux Returns
  for (int i = 0; i < engine.getNumAuxReturns(); ++i) {
    auto *slot = engine.getAuxReturn(i);
    auto *comp = new RackSlotComponent(*slot, 100 + i, boutiqueLookAndFeel);

    comp->setSpecialModes(false, false, true);

    comp->onShowPluginMenu = [this, i, slot, comp](int chainIndex) {
      juce::PopupMenu menu;
      for (int p = 0; p < engine.getNumAvailablePlugins(); ++p)
        menu.addItem(p + 1, engine.getAvailablePluginName(p));

      menu.showMenuAsync(juce::PopupMenu::Options(),
                         [this, i, slot, comp, chainIndex](int result) {
                           if (result > 0) {
                             loadingOverlay.reset(new LoadingOverlay());
                             addAndMakeVisible(loadingOverlay.get());
                             // Need engine method for aux returns or reuse slot
                             // load with index mapping For now, let's assume
                             // we'll add loadPluginIntoAuxReturn
                             engine.loadPluginIntoSlot(
                                 (100 + i), chainIndex, result - 1,
                                 [this, comp](bool s, const juce::String &e) {
                                   juce::ignoreUnused(s, e);
                                   loadingOverlay.reset();
                                   comp->repaint();
                                 });
                           }
                         });
    };

    // Editor open callback
    comp->onOpenEditor = [this, slot](int chainIndex) {
      auto *plugin = slot->getPluginInstance(chainIndex);
      if (!plugin || !plugin->hasEditor())
        return;
      for (auto *w : activePluginWindows) {
        if (auto *pw = dynamic_cast<PluginWindow*>(w)) {
          if (pw->pluginInstance == plugin) {
            pw->toFront(true);
            return;
          }
        }
      }
      showLoadingOverlay("OPENING EDITOR", "Creating editor... (heavy plugins may take a while)");
      juce::MessageManager::callAsync([this, slot, chainIndex]() {
        auto *plugin = slot->getPluginInstance(chainIndex);
        if (!plugin) {
          hideLoadingOverlay();
          return;
        }
        try {
          auto *editor = plugin->createEditor();
          hideLoadingOverlay();
          if (editor) {
            auto *w = new PluginWindow(slot->getPluginName(chainIndex), editor, plugin);
            w->onWindowClosed = [this, w] {
              if (activePluginWindows.contains(w)) {
                activePluginWindows.removeObject(w, false);
                juce::MessageManager::callAsync([w]() { delete w; });
              }
            };
            activePluginWindows.add(w);
          }
        } catch (...) {
          hideLoadingOverlay();
          juce::AlertWindow::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon, "Editor Error",
              "Plugin editor could not be created.");
        }
      });
    };

    // Note Range Dialog (CallOutBox)
    comp->onShowNoteRangeDialog = [this, slot, comp] {
      auto *nrComp = new NoteRangeComponent(*slot);
      nrComp->setSize(800, 180);
      activeNoteRangeLearner = nrComp;
      juce::CallOutBox::launchAsynchronously(
          std::unique_ptr<juce::Component>(nrComp),
          comp->getNoteRangeButtonBounds(), this);
    };

    auxReturnComponents.add(comp);
    addAndMakeVisible(comp);
  }
}

void MainComponent::setupHeaderButtons() {
  // Scan button
  addAndMakeVisible(scanButton);
  scanButton.onClick = [this] {
    scanButton.setButtonText("SCANNING...");
    scanButton.setEnabled(false);
    engine.scanForPlugins();
  };
  engine.onScanFinished = [this](OpenRigEngine::ScanResults results) {
    scanButton.setButtonText("SCAN FOR PLUGINS");
    scanButton.setEnabled(true);
    engine.applyScanResults(results.newPlugins, results.missingPlugins);
    showScanResultsDialog(results);
  };

  // Panic button
  addAndMakeVisible(panicBtn);
  panicBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
  panicBtn.onClick = [this] { engine.triggerPanic(); };

  // Reset Audio button
  addAndMakeVisible(resetAudioBtn);
  resetAudioBtn.setColour(juce::TextButton::buttonColourId,
                          juce::Colours::orange);
  resetAudioBtn.onClick = [this] { resetAudioDevice(); };

  // Exit button
  addAndMakeVisible(exitBtn);
  exitBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
  exitBtn.onClick = [] { juce::JUCEApplication::quit(); };

  // Audio Settings button
  addAndMakeVisible(audioSettingsBtn);
  audioSettingsBtn.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::darkmagenta);
  audioSettingsBtn.onClick = [this] {
    auto *selector = new juce::AudioDeviceSelectorComponent(
        deviceManager, 0, OpenRigConstants::kDefaultInputChannels, 0,
        OpenRigConstants::kDefaultOutputChannels, true, true, true, false);
    selector->setSize(500, 400);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = "Audio & MIDI Settings";
    options.dialogBackgroundColour = juce::Colours::darkgrey;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
  };

  // MIDI Monitor
  addAndMakeVisible(midiMonitorLabel);
  midiMonitorLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
  midiMonitorLabel.setColour(juce::Label::textColourId, juce::Colours::lime);

  // MIDI Monitor toggle button
  addAndMakeVisible(midiMonitorToggle);
  midiMonitorToggle.setColour(juce::TextButton::buttonColourId,
                              juce::Colours::darkgreen);
  midiMonitorToggle.setColour(juce::TextButton::buttonOnColourId,
                              juce::Colour(0xff00aa44));
  midiMonitorToggle.onClick = [this] {
    midiMonitorVisible = !midiMonitorVisible;
    if (midiMonitorVisible) {
      if (!midiMonitorPanel) {
        midiMonitorPanel = std::make_unique<MidiMonitorComponent>();
        addChildComponent(midiMonitorPanel.get());
      }
      midiMonitorPanel->setVisible(true);
      midiMonitorToggle.setToggleState(true, juce::dontSendNotification);
    } else {
      if (midiMonitorPanel)
        midiMonitorPanel->setVisible(false);
      midiMonitorToggle.setToggleState(false, juce::dontSendNotification);
    }
    resized();
  };

  // Header: About Button (Icon)
  addAndMakeVisible(aboutBtn);
  auto iconFile = juce::File(__FILE__)
                      .getSiblingFile("Resources")
                      .getChildFile("OpenRig_Icon.png");
  auto openRigIcon = juce::ImageCache::getFromFile(iconFile);
  aboutBtn.setImages(false, true, true, openRigIcon, 1.0f,
                     juce::Colours::transparentBlack, openRigIcon, 1.0f,
                     juce::Colours::white.withAlpha(0.2f), openRigIcon, 1.0f,
                     juce::Colours::white.withAlpha(0.5f));
  aboutBtn.onClick = [this] { showAboutDialog(); };
  aboutBtn.setTooltip("About OpenRig");

  // Settings gear button — centralizes the settings buttons into one overlay
  addAndMakeVisible(settingsGearBtn);
  auto gearImg = createGearImage(28, juce::Colour(0xFF00E5FF));
  settingsGearBtn.setImages(false, true, true, gearImg, 1.0f,
                            juce::Colours::transparentBlack, gearImg, 1.0f,
                            juce::Colours::white.withAlpha(0.25f), gearImg,
                            1.0f, juce::Colours::white.withAlpha(0.5f));
  settingsGearBtn.onClick = [this] { showConfigOverlay(); };
  settingsGearBtn.setTooltip("Settings");

  // The individual settings buttons are now surfaced through the gear overlay.
  // Their handlers are retained so the overlay can invoke them.
  scanButton.setVisible(false);
  audioSettingsBtn.setVisible(false);
  busRoutingBtn.setVisible(false);
  resetAudioBtn.setVisible(false);

  // Bus Routing button
  addAndMakeVisible(busRoutingBtn);
  busRoutingBtn.setColour(juce::TextButton::buttonColourId,
                          juce::Colours::darkcyan);
  busRoutingBtn.onClick = [this] {
    juce::PopupMenu menu;
    menu.addSectionHeader("FOH Output Channels");
    menu.addItem(1, "Outputs 1+2", true, engine.getFohOutputOffset() == 0);
    menu.addItem(2, "Outputs 3+4", true, engine.getFohOutputOffset() == 2);
    menu.addItem(3, "Outputs 5+6", true, engine.getFohOutputOffset() == 4);
    menu.addItem(4, "Outputs 7+8", true, engine.getFohOutputOffset() == 6);
    menu.addSeparator();
    menu.addSectionHeader("IEM Output Channels");
    menu.addItem(11, "Outputs 1+2", true, engine.getIemOutputOffset() == 0);
    menu.addItem(12, "Outputs 3+4", true, engine.getIemOutputOffset() == 2);
    menu.addItem(13, "Outputs 5+6", true, engine.getIemOutputOffset() == 4);
    menu.addItem(14, "Outputs 7+8", true, engine.getIemOutputOffset() == 6);

    menu.addSeparator();
    menu.addSectionHeader("MIDI Channel (Global Default)");
    int curCh = engine.getDefaultMidiChannel();
    menu.addItem(100, "Omni (all channels)", true, curCh == 0);
    for (int ch = 1; ch <= 16; ++ch)
      menu.addItem(100 + ch, "Channel " + juce::String(ch), true, curCh == ch);

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
      if (result >= 1 && result <= 4) {
        engine.setFohOutputOffset((result - 1) * 2);
      } else if (result >= 11 && result <= 14) {
        engine.setIemOutputOffset((result - 11) * 2);
      } else if (result == 100) {
        engine.setDefaultMidiChannel(0);
      } else if (result >= 101 && result <= 116) {
        engine.setDefaultMidiChannel(result - 100);
      }
    });
  };

  // Save/Load buttons
  addAndMakeVisible(saveBtn);
  saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
  saveBtn.onClick = [this] {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Save Rig Configuration",
        OpenRig::RigLibrary::getSongsDirectory(),
        "*.json");

    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser &fc) {
          auto file = fc.getResult();
          if (file != juce::File()) {
            if (file.getFileExtension() != ".json")
              file = file.withFileExtension(".json");

            juce::String json = engine.exportRigToJson();
            if (OpenRig::RigSerializer::save(file, json)) {
              LOG_INFO("Rig saved to: " + file.getFullPathName());
            } else {
              LOG_ERROR("Failed to save rig to: " + file.getFullPathName());
            }
          }
        });
  };

  addAndMakeVisible(loadBtn);
  loadBtn.setColour(juce::TextButton::buttonColourId,
                    juce::Colours::darkorange);
  loadBtn.onClick = [this] {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load Rig Configuration",
        OpenRig::RigLibrary::getSongsDirectory(),
        "*.json");

    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser](const juce::FileChooser &fc) {
                            auto file = fc.getResult();
                            if (file.existsAsFile())
                              loadRigAsync(file);
                          });
  };

  // CPU/RAM monitors
  addAndMakeVisible(cpuLabel);
  cpuLabel.setFont(juce::FontOptions(12.0f));
  cpuLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
  addAndMakeVisible(ramLabel);
  ramLabel.setFont(juce::FontOptions(12.0f));
  ramLabel.setColour(juce::Label::textColourId, juce::Colours::yellow);

  addAndMakeVisible(setupNameLabel);
  setupNameLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
  setupNameLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00e5ff));
  setupNameLabel.setJustificationType(juce::Justification::centredLeft);
}


void MainComponent::setupSetupButtons() {
  addAndMakeVisible(saveSetBtn);
  addAndMakeVisible(loadSetBtn);
  
  saveSetBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
  loadSetBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkorange);
  
  saveSetBtn.onClick = [this] { saveSetToFile(); };
  loadSetBtn.onClick = [this] { loadSetFromFile(); };

  // Scene management buttons
  addAndMakeVisible(addSceneBtn);
  addAndMakeVisible(saveSceneBtn);
  addAndMakeVisible(renameSceneBtn);

  addSceneBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::teal);
  saveSceneBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
  renameSceneBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkslategrey);

  addSceneBtn.onClick = [this] {
    engine.createNewScene("NEW PRESET");
    refreshSceneButtons();
  };

  saveSceneBtn.onClick = [this] {
    engine.saveCurrentStateToScene(engine.getCurrentSceneIndex());
  };

  renameSceneBtn.onClick = [this] {
    int idx = engine.getCurrentSceneIndex();
    if (idx < 0 || idx >= engine.getNumScenes())
      return;
    auto *alert = new juce::AlertWindow("Rename Scene", "Enter new name:",
                                        juce::AlertWindow::QuestionIcon);
    alert->addTextEditor("name", engine.getSceneName(idx));
    alert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    alert->enterModalState(true,
        juce::ModalCallbackFunction::create([this, idx, alert](int result) {
          if (result == 1) {
            juce::String newName = alert->getTextEditorContents("name");
            if (newName.isNotEmpty()) {
              engine.renameScene(idx, newName);
              refreshSceneButtons();
            }
          }
          delete alert;
        }));
  };

  loadButtonMappings();
}

void MainComponent::refreshSceneButtons() {
  sceneButtons.clear();
  int numScenes = engine.getNumScenes();
  for (int i = 0; i < numScenes; ++i) {
    auto *btn = new juce::TextButton(engine.getSceneName(i));
    btn->setClickingTogglesState(true);
    btn->setRadioGroupId(9999, juce::dontSendNotification);
    btn->setColour(juce::TextButton::buttonColourId,
                   juce::Colour(0xFF3A3D42));
    btn->setColour(juce::TextButton::buttonOnColourId,
                   juce::Colour(0xFF00E5FF));
    int sceneIdx = i;
    btn->onClick = [this, sceneIdx] {
      engine.loadScene(sceneIdx);
      for (int j = 0; j < sceneButtons.size(); ++j)
        sceneButtons[j]->setToggleState(j == sceneIdx,
                                        juce::dontSendNotification);
    };
    if (i == engine.getCurrentSceneIndex())
      btn->setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(btn);
    sceneButtons.add(btn);
  }
  resized();
}

void MainComponent::setupMasterSection() {
  // Master FOH slider
  addAndMakeVisible(masterFohSlider);
  masterFohSlider.setComponentID("master_foh");
  masterFohSlider.setSliderStyle(juce::Slider::LinearVertical);
  masterFohSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  masterFohSlider.setRange(0.0, 1.0);
  masterFohSlider.setValue(1.0);
  masterFohSlider.onValueChange = [this] {
    engine.setFohMasterLevel((float)masterFohSlider.getValue());
  };

  // Master IEM slider
  addAndMakeVisible(masterIemSlider);
  masterIemSlider.setComponentID("master_iem");
  masterIemSlider.setSliderStyle(juce::Slider::LinearVertical);
  masterIemSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  masterIemSlider.setRange(0.0, 1.0);
  masterIemSlider.setValue(1.0);
  masterIemSlider.onValueChange = [this] {
    engine.setIemMasterLevel((float)masterIemSlider.getValue());
  };

  // Labels
  addAndMakeVisible(fohLabel);
  addAndMakeVisible(iemLabel);

  // FX Slots for FOH / IEM masters
  for (int i = 0; i < 3; ++i) {
    addAndMakeVisible(fohFxBtns[i]);
    fohFxBtns[i].setButtonText(engine.getMasterPluginName(true, i).isEmpty() ? "[EMPTY]" : engine.getMasterPluginName(true, i));
    fohFxBtns[i].onClick = [this, i] { showMasterPluginMenu(true, i); };

    addAndMakeVisible(fohEditGuiBtns[i]);
    fohEditGuiBtns[i].setButtonText("E");
    fohEditGuiBtns[i].onClick = [this, i] { openMasterPluginEditor(true, i); };

    addAndMakeVisible(iemFxBtns[i]);
    iemFxBtns[i].setButtonText(engine.getMasterPluginName(false, i).isEmpty() ? "[EMPTY]" : engine.getMasterPluginName(false, i));
    iemFxBtns[i].onClick = [this, i] { showMasterPluginMenu(false, i); };

    addAndMakeVisible(iemEditGuiBtns[i]);
    iemEditGuiBtns[i].setButtonText("E");
    iemEditGuiBtns[i].onClick = [this, i] { openMasterPluginEditor(false, i); };

    // Color styles matching routing destinations
    fohFxBtns[i].setColour(juce::TextButton::buttonColourId, juce::Colours::limegreen.darker(0.3f));
    iemFxBtns[i].setColour(juce::TextButton::buttonColourId, juce::Colours::dodgerblue.darker(0.3f));
  }
}

void MainComponent::audioDeviceIOCallbackWithContext(
    const float *const *inputChannelData, int numInputChannels,
    float *const *outputChannelData, int numOutputChannels, int numSamples,
    const juce::AudioIODeviceCallbackContext & /*context*/) {
  // Boost audio thread to Pro Audio priority on first callback (Windows only)
#ifdef _WIN32
  if (!mmcssRegistered) {
    if (avSetMmThreadFn != nullptr) {
      DWORD taskIndex = 0;
      if (HANDLE h = avSetMmThreadFn("Pro Audio", &taskIndex)) {
        mmcssRegistered = true;
        LOG_INFO("Audio thread registered with MMCSS Pro Audio characteristics");
      }
    }
  }
#endif

  juce::MidiBuffer incomingMidi;
  midiCollector.removeNextBlockOfMessages(incomingMidi, numSamples);
  engine.processAudio(inputChannelData, numInputChannels, outputChannelData,
                      numOutputChannels, numSamples, incomingMidi);
}

void MainComponent::saveAudioSettings() {
  auto xml = deviceManager.createStateXml();
  if (xml != nullptr) {
    auto file = OpenRigConstants::getAppDirectory().getChildFile("audio_settings.xml");
    file.getParentDirectory().createDirectory();
    xml->writeTo(file);
  }
}

void MainComponent::loadAudioSettings() {
  auto file = OpenRigConstants::getAppDirectory().getChildFile("audio_settings.xml");
  if (file.existsAsFile()) {
    auto xml = juce::XmlDocument::parse(file);
    if (xml != nullptr)
      deviceManager.initialise(0, 8, xml.get(), true);
  } else {
    deviceManager.initialiseWithDefaultDevices(0, 2);
  }
}

void MainComponent::resetAudioDevice() {
  auto *device = deviceManager.getCurrentAudioDevice();
  if (!device)
    return;
  auto setup = deviceManager.getAudioDeviceSetup();
  deviceManager.closeAudioDevice();
  deviceManager.initialise(setup.inputChannels.countNumberOfSetBits(),
                           setup.outputChannels.countNumberOfSetBits(), nullptr,
                           true, "", &setup);
}

void MainComponent::showAboutDialog() {
  juce::AlertWindow::showMessageBoxAsync(
      juce::MessageBoxIconType::InfoIcon,
      "About OpenRig",
      "OpenRig Live Performance Host\nVersion 1.0.0\n\nOptimized for reliable, low-latency live synth rigs.");
}

void MainComponent::showConfigOverlay() {
  // Safe invoker: only call a button's onClick if it has been wired.
  auto invoke = [](juce::Button &b) {
    if (b.onClick)
      b.onClick();
  };

  ConfigModalOverlay::Actions a;
  a.onAudioSettings = [this, invoke] { invoke(audioSettingsBtn); };
  a.onScanPlugins = [this, invoke] { invoke(scanButton); };
  a.onBusRouting = [this, invoke] { invoke(busRoutingBtn); };
  a.onResetAudio = [this, invoke] { invoke(resetAudioBtn); };
  a.onAbout = [this] { showAboutDialog(); };
  a.onClose = [this] {
    configOverlay.reset();
    resized();
  };
  a.onStressTestToggle = [this] {
    engine.stressTestActive.store(!engine.stressTestActive.load());
  };
  a.isStressTestActive = [this] {
    return engine.stressTestActive.load();
  };
  a.getInfo = [this] {
    juce::String s;
    s << "Scanned plugins: " << engine.getNumAvailablePlugins() << "\n";
    int ch = engine.getDefaultMidiChannel();
    s << "Global MIDI: "
      << (ch == 0 ? juce::String("Omni") : juce::String(ch)) << "\n";
    int foh = engine.getFohOutputOffset();
    int iem = engine.getIemOutputOffset();
    s << "FOH out: " << (foh + 1) << "+" << (foh + 2) << "   "
      << "IEM out: " << (iem + 1) << "+" << (iem + 2);
    return s;
  };

  configOverlay = std::make_unique<ConfigModalOverlay>(std::move(a));
  addAndMakeVisible(configOverlay.get());
  configOverlay->centreWithSize(configOverlay->getWidth(),
                                configOverlay->getHeight());
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
  if (source == &deviceManager) {
    saveAudioSettings();
  }
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice *device) {
  midiCollector.reset(device->getCurrentSampleRate());
  engine.prepareToPlay(device->getCurrentBufferSizeSamples(),
                       device->getCurrentSampleRate());
}

void MainComponent::audioDeviceStopped() {}

void MainComponent::paint(juce::Graphics &g) {
  // Dark premium look
  g.fillAll(juce::Colour(0xff121212));

  // Draw Master VU Meters
  auto drawMasterMeter = [&](juce::Graphics &g, juce::Rectangle<float> area,
                             float L, float R, juce::Colour low,
                             juce::Colour mid, juce::Colour high) {
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(area, 2.0f);

    auto lRect = area.removeFromLeft(area.getWidth() / 2).reduced(1);
    auto rRect = area.reduced(1);

    auto drawSegmentedBar = [&](juce::Rectangle<float> barArea, float val) {
      const int numSegments = 16;
      const float gap = 1.0f;
      float segmentHeight =
          (barArea.getHeight() - (numSegments - 1) * gap) / (float)numSegments;
      int activeSegments = (int)(val * numSegments);

      for (int i = 0; i < numSegments; ++i) {
        auto seg = barArea.removeFromBottom(segmentHeight);
        barArea.removeFromBottom(gap);

        if (i < activeSegments) {
          float ratio = (float)i / (float)numSegments;
          juce::Colour c = (ratio < 0.6f) ? low : ((ratio < 0.85f) ? mid : high);
          g.setColour(c);
        } else {
          g.setColour(juce::Colours::darkgrey.darker(0.8f));
        }
        g.fillRect(seg);
      }
    };

    drawSegmentedBar(lRect, L);
    drawSegmentedBar(rRect, R);
  };

  auto fohBounds = masterFohSlider.getBounds().toFloat();
  drawMasterMeter(g, fohBounds.withX(fohBounds.getRight() + 2).withWidth(14),
                  masterFohL, masterFohR, juce::Colours::limegreen,
                  juce::Colours::yellow, juce::Colours::red);

  auto iemBounds = masterIemSlider.getBounds().toFloat();
  drawMasterMeter(g, iemBounds.withX(iemBounds.getRight() + 2).withWidth(14),
                  masterIemL, masterIemR, juce::Colours::cyan,
                  juce::Colours::yellow, juce::Colours::red);
}

void MainComponent::resized() {
  auto r = getLocalBounds();

  // Header area - Row 1: buttons
  auto header = r.removeFromTop(40);
  headerBounds = header;
  // Library sidebar toggle (left edge)
  toggleLibraryBtn.setBounds(header.removeFromLeft(30).reduced(3));
  // Add About Button (Icon) first
  aboutBtn.setBounds(header.removeFromLeft(40).reduced(5));

  settingsGearBtn.setBounds(header.removeFromLeft(44).reduced(5));
  midiMonitorLabel.setBounds(header.removeFromLeft(140).reduced(5));
  midiMonitorToggle.setBounds(header.removeFromLeft(75).reduced(5));
  cpuLabel.setBounds(header.removeFromLeft(70).reduced(5));
  ramLabel.setBounds(header.removeFromLeft(70).reduced(5));
  setupNameLabel.setBounds(header.removeFromLeft(320).reduced(5));

  saveBtn.setBounds(header.removeFromRight(100).reduced(5));
  loadBtn.setBounds(header.removeFromRight(100).reduced(5));

  // Panic / Exit in middle-ish (Reset Audio is now in the settings overlay)
  panicBtn.setBounds(header.removeFromRight(80).reduced(5));
  exitBtn.setBounds(header.removeFromRight(60).reduced(5));

  // Header area - Row 2: setup buttons (smaller)
  auto presetRow = r.removeFromTop(30);

  // Save/Load Set buttons on left
  saveSetBtn.setBounds(presetRow.removeFromLeft(70).reduced(2));
  loadSetBtn.setBounds(presetRow.removeFromLeft(70).reduced(2));

  // Setup buttons fill rest
  int btnWidth = presetRow.getWidth() / numSetupButtons;
  for (int i = 0; i < numSetupButtons; ++i) {
    setupButtons[i].setBounds(presetRow.removeFromLeft(btnWidth).reduced(1));
  }

  // Header area - Row 3: scene (preset) buttons
  auto sceneRow = r.removeFromTop(30);
  addSceneBtn.setBounds(sceneRow.removeFromLeft(70).reduced(2));
  saveSceneBtn.setBounds(sceneRow.removeFromLeft(80).reduced(2));
  renameSceneBtn.setBounds(sceneRow.removeFromLeft(60).reduced(2));
  if (!sceneButtons.isEmpty()) {
    int sceneBtnWidth = sceneRow.getWidth() / sceneButtons.size();
    for (int i = 0; i < sceneButtons.size(); ++i) {
      sceneButtons[i]->setBounds(sceneRow.removeFromLeft(sceneBtnWidth).reduced(1));
    }
  }

  r.removeFromTop(10); // spacer

  // MIDI Monitor panel (slides open when toggled)
  if (midiMonitorVisible && midiMonitorPanel) {
    int monitorHeight = 180;
    auto monitorArea = r.removeFromTop(monitorHeight);
    midiMonitorPanel->setBounds(monitorArea);
    r.removeFromTop(4); // gap after monitor
  }

  // Library sidebar (collapsible, left side)
  if (libraryVisible) {
    auto libraryArea = r.removeFromLeft(240);
    if (libraryPanel)
      libraryPanel->setBounds(libraryArea);
  } else {
    if (libraryPanel)
      libraryPanel->setBounds(0, 0, 0, 0);
  }

  // Master sliders area (wider for VU meters)
  auto masterArea = r.removeFromRight(100);
  masterColumnBounds = masterArea;  // snapshot before slicing for timer repaint

  // 1. Labels at the very top
  auto labels = masterArea.removeFromTop(20);
  fohLabel.setBounds(labels.removeFromLeft(50));
  iemLabel.setBounds(labels);

  // 2. FX Slots (3 slots)
  auto fxArea = masterArea.removeFromTop(70);
  auto fohFxArea = fxArea.removeFromLeft(50);
  auto iemFxArea = fxArea;

  for (int i = 0; i < 3; ++i) {
    auto fRow = fohFxArea.removeFromTop(20).reduced(2, 1);
    fohEditGuiBtns[i].setBounds(
        fRow.removeFromRight((int)(fRow.getWidth() * 0.25f)));
    fohFxBtns[i].setBounds(fRow);

    auto iRow = iemFxArea.removeFromTop(20).reduced(2, 1);
    iemEditGuiBtns[i].setBounds(
        iRow.removeFromRight((int)(iRow.getWidth() * 0.25f)));
    iemFxBtns[i].setBounds(iRow);
  }

  // 3. Faders take the rest (with space for meters)
  masterFohSlider.setBounds(masterArea.removeFromLeft(50).reduced(8, 2));
  masterIemSlider.setBounds(masterArea.reduced(8, 2));

  // Channel Strips (horizontal arrangement in center)
  int numMain = rackSlotComponents.size();
  int numReturns = auxReturnComponents.size();
  int totalStrips = numMain + numReturns;

  if (totalStrips > 0) {
    int stripWidth = r.getWidth() / totalStrips;

    // Draw regular slots first (skip monitor in at index 0)
    for (int i = 1; i < rackSlotComponents.size(); ++i)
      rackSlotComponents[i]->setBounds(r.removeFromLeft(stripWidth).reduced(2));

    // Draw Monitor In (index 0) next to aux returns
    if (rackSlotComponents.size() > 0)
      rackSlotComponents[0]->setBounds(r.removeFromLeft(stripWidth).reduced(2));

    // Draw aux returns last
    for (auto *c : auxReturnComponents)
      c->setBounds(r.removeFromLeft(stripWidth).reduced(2));
  }

  if (channelStripOverlay)
    channelStripOverlay->centreWithSize(800, 480);
  if (midiEffectsOverlay)
    midiEffectsOverlay->centreWithSize(620, 410);
  if (samplerOverlay)
    samplerOverlay->centreWithSize(720, 600);
}

void MainComponent::hideLoadingOverlay() { loadingOverlay.reset(); }

void MainComponent::loadRigAsync(const juce::File &file, int buttonIndexForHighlight) {
  if (!transitioner || transitioner->isBusy()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::InfoIcon, "Transition Busy",
        "A rig transition is currently in progress. Please wait.");
    return;
  }

  showLoadingOverlay("SWITCHING SONG",
                     "Loading " + file.getFileNameWithoutExtension() + "...");

  int highlight = buttonIndexForHighlight;

  // Defer the transition slightly to guarantee that the message thread paints
  // the LoadingOverlay before heavy VST builds block the event loop.
  juce::Timer::callAfterDelay(50, [this, file, highlight]() {
    if (!transitioner)
      return;

    transitioner->transitionToFile(file,
        OpenRig::RigTransitioner::Callbacks{
            [this](juce::String progress) { setLoadingMessage(progress); },
            [this, highlight, fileName = file.getFileNameWithoutExtension()](bool ok, juce::String message, int) {
                hideLoadingOverlay();
                if (ok) {
                    setupNameLabel.setText(fileName, juce::dontSendNotification);
                    for (auto *comp : rackSlotComponents)
                        comp->repaint();
                    repaint();
                    refreshSceneButtons();
                    if (highlight >= 0 && highlight < numSetupButtons) {
                        for (int i = 0; i < numSetupButtons; ++i)
                            setupButtons[i].setToggleState(i == highlight,
                                                           juce::dontSendNotification);
                    }
                    midiMonitorLabel.setText("RIG: " + message,
                                             juce::dontSendNotification);
                } else {
                    // Rollback-by-construction: current rig is untouched.
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon, "Rig Load Failed",
                        message + "\n\nThe current rig is unchanged.");
                    LOG_ERROR("Failed to load rig: " + message + " file: " +
                              fileName);
                }
            }
        });
  });
}

void MainComponent::assignJsonToButton(int buttonIndex) {
  auto setupsDir = OpenRig::RigLibrary::getSongsDirectory();
  setupsDir.createDirectory();

  auto chooser = std::make_shared<juce::FileChooser>("Select JSON Rig File",
                                                     setupsDir, "*.json");

  chooser->launchAsync(
      juce::FileBrowserComponent::openMode |
          juce::FileBrowserComponent::canSelectFiles,
      [this, buttonIndex, chooser](const juce::FileChooser &fc) {
        auto file = fc.getResult();
        if (file.existsAsFile()) {
          setupFilePaths[buttonIndex] = file.getFullPathName();
          saveButtonMappings();
          updateSetupButtonLabels();
        }
      });
}

void MainComponent::saveButtonMappings() {
  auto mappingsFile =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("OpenRig")
          .getChildFile("button_mappings.json");
  mappingsFile.getParentDirectory().createDirectory();

  auto *root = new juce::DynamicObject();
  juce::Array<juce::var> paths;
  for (int i = 0; i < numSetupButtons; ++i) {
    paths.add(setupFilePaths[i]);
  }
  root->setProperty("buttons", paths);

  mappingsFile.replaceWithText(juce::JSON::toString(juce::var(root)));
}

void MainComponent::loadButtonMappings() {
  auto mappingsFile =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("OpenRig")
          .getChildFile("button_mappings.json");

  if (!mappingsFile.existsAsFile())
    return;

  auto json = juce::JSON::parse(mappingsFile.loadFileAsString());
  if (auto *arr = json.getProperty("buttons", juce::var()).getArray()) {
    for (int i = 0; i < std::min((int)arr->size(), numSetupButtons); ++i) {
      setupFilePaths[i] = arr->getReference(i).toString();
    }
  }
}

void MainComponent::timerCallback() {
  double cpu = deviceManager.getCpuUsage() * 100.0;
  cpuLabel.setText("CPU: " + juce::String(cpu, 1) + "%",
                   juce::dontSendNotification);

#if JUCE_WINDOWS
  MEMORYSTATUSEX memInfo;
  memInfo.dwLength = sizeof(MEMORYSTATUSEX);
  if (GlobalMemoryStatusEx(&memInfo)) {
      ramLabel.setText("RAM: " + juce::String(memInfo.dwMemoryLoad) + "%", juce::dontSendNotification);
  } else {
      ramLabel.setText("RAM: --", juce::dontSendNotification);
  }
#else
  ramLabel.setText("RAM: --", juce::dontSendNotification);
#endif

  // Surface any audio underruns (a block bailed to silence: wedged worker or an
  // unprepared rack). consumeAudioUnderrun() atomically clears the flag.
  if (engine.consumeAudioUnderrun())
    OpenRigLog::log(OpenRigLog::Level::Warning,
                    "Audio underrun: a block emitted silence "
                    "(worker stall or rack not prepared).");

  // Sync Master FX buttons
  for (int i = 0; i < 3; ++i) {
    auto fohName = engine.getMasterPluginName(true, i);
    fohFxBtns[i].setButtonText(fohName.isEmpty() ? "FOH " + juce::String(i + 1)
                                                 : fohName);

    auto iemName = engine.getMasterPluginName(false, i);
    iemFxBtns[i].setButtonText(iemName.isEmpty() ? "EARS " + juce::String(i + 1)
                                                 : iemName);
  }

  // Update master meters
  float decay = 0.85f;
  masterFohL = std::max(OpenRigLog::amplitudeToLogScale(engine.getFohPeakL()),
                        masterFohL * decay);
  masterFohR = std::max(OpenRigLog::amplitudeToLogScale(engine.getFohPeakR()),
                        masterFohR * decay);
  masterIemL = std::max(OpenRigLog::amplitudeToLogScale(engine.getIemPeakL()),
                        masterIemL * decay);
  masterIemR = std::max(OpenRigLog::amplitudeToLogScale(engine.getIemPeakR()),
                        masterIemR * decay);

  // Repaint header (labels, FX buttons) AND master column (VU meters).
  repaint(headerBounds);
  if (!masterColumnBounds.isEmpty())
    repaint(masterColumnBounds);
}

void MainComponent::showMasterPluginMenu(bool isFoh, int chainIndex) {
  juce::PopupMenu menu;
  menu.addSectionHeader("Select Master " + juce::String(isFoh ? "FOH" : "IEM") + " Plugin " + juce::String(chainIndex + 1));
  for (int p = 0; p < engine.getNumAvailablePlugins(); ++p) {
    menu.addItem(p + 1, engine.getAvailablePluginName(p));
  }
  menu.addSeparator();
  menu.addItem(-1, "(No Plugin)");

  menu.showMenuAsync(juce::PopupMenu::Options(), [this, isFoh, chainIndex](int result) {
    if (result > 0) {
      loadingOverlay.reset(new LoadingOverlay());
      addAndMakeVisible(loadingOverlay.get());

      engine.loadPluginIntoMasterBus(isFoh, chainIndex, result - 1,
          [this, isFoh, chainIndex](bool success, const juce::String &err) {
            loadingOverlay.reset();
            if (!success) {
              juce::AlertWindow::showMessageBoxAsync(
                  juce::MessageBoxIconType::WarningIcon,
                  "Plugin Load Failed", err);
            } else {
              auto name = engine.getMasterPluginName(isFoh, chainIndex);
              auto &btn = isFoh ? fohFxBtns[chainIndex] : iemFxBtns[chainIndex];
              btn.setButtonText(name.isEmpty() ? "[EMPTY]" : name);
            }
          });
    } else if (result == -1) {
      engine.loadPluginIntoMasterBus(isFoh, chainIndex, -1, [this, isFoh, chainIndex](bool, const juce::String &) {
        auto &btn = isFoh ? fohFxBtns[chainIndex] : iemFxBtns[chainIndex];
        btn.setButtonText("[EMPTY]");
      });
    }
  });
}

void MainComponent::openMasterPluginEditor(bool isFoh, int chainIndex) {
  auto *plugin = engine.getMasterPluginInstance(isFoh, chainIndex);
  if (plugin && plugin->hasEditor()) {
    juce::String name = (isFoh ? "FOH Master " : "IEM Master ") + juce::String(chainIndex + 1) + ": " + plugin->getName();
    for (auto *w : activePluginWindows) {
      if (auto *pw = dynamic_cast<PluginWindow*>(w)) {
        if (pw->pluginInstance == plugin) {
          pw->toFront(true);
          return;
        }
      }
    }

    auto *editor = plugin->createEditor();
    if (editor) {
      auto *w = new PluginWindow(name, editor, plugin);
      w->onWindowClosed = [this, w] {
        if (activePluginWindows.contains(w)) {
          activePluginWindows.removeObject(w, false);
          juce::MessageManager::callAsync([w]() { delete w; });
        }
      };
      activePluginWindows.add(w);
    }
  }
}

void MainComponent::loadRigFile(int index) {
  if (index >= 0 && index < numSetupButtons) {
    currentSetupIndex = index;
  }
  if (setupFilePaths[index].isEmpty()) {
    assignJsonToButton(index);
    return;
  }

  juce::File file(setupFilePaths[index]);
  loadRigAsync(file, index);
}

void MainComponent::loadRigFromFile(const juce::File &file) {
  if (!file.existsAsFile())
    return;
  loadRigAsync(file, -1);
  setupNameLabel.setText(file.getFileNameWithoutExtension(),
                         juce::dontSendNotification);
}

void MainComponent::showScanResultsDialog(const OpenRigEngine::ScanResults &results) {
  // Display plugin scanner results
  juce::String msg = "Scan complete.\n\n";
  msg += "Plugins found: " + juce::String(results.newPlugins.size()) + "\n";
  msg += "Failed: " + juce::String(results.missingPlugins.size()) + "\n\n";
  
  if (!results.missingPlugins.empty()) {
    msg += "Failed plugins list:\n";
    for (auto &p : results.missingPlugins)
      msg += "- " + p.name + "\n";
  }
  
  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                         "Plugin Scan Results", msg);
}


void MainComponent::saveSetToFile() {
  fileChooser = std::make_unique<juce::FileChooser>(
      "Save Set...",
      OpenRig::RigLibrary::getSetsDirectory(),
      "*.orset");
  fileChooser->launchAsync(
      juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
      [this](const juce::FileChooser &fc) {
          auto file = fc.getResult();
          if (file == juce::File{})
              return;
          if (file.getFileExtension() != ".orset")
              file = file.withFileExtension(".orset");
          
          juce::DynamicObject::Ptr obj = new juce::DynamicObject();
          for (int i = 0; i < numSetupButtons; ++i) {
              if (!setupFilePaths[i].isEmpty())
                  obj->setProperty("setup_" + juce::String(i), setupFilePaths[i]);
          }
          
          file.getParentDirectory().createDirectory();
          juce::FileOutputStream fos(file);
          if (fos.openedOk()) {
              fos.setPosition(0);
              fos.truncate();
              juce::JSON::writeToStream(fos, juce::var(obj.get()));
          }
      });
}

void MainComponent::loadSetFromFile() {
  fileChooser = std::make_unique<juce::FileChooser>(
      "Load Set...",
      OpenRig::RigLibrary::getSetsDirectory(),
      "*.orset");
  fileChooser->launchAsync(
      juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
      [this](const juce::FileChooser &fc) {
          applySetlistFromFile(fc.getResult());
      });
}

// Apply a .orset setlist to the setup buttons (assigns song paths, colors
// them green/grey, persists the button mappings). Shared between the file-
// chooser path and the library sidebar.
void MainComponent::applySetlistFromFile(const juce::File &file) {
  if (!file.existsAsFile())
    return;
  auto json = juce::JSON::parse(file);
  auto *obj = json.getDynamicObject();
  if (!obj)
    return;
  for (int i = 0; i < numSetupButtons; ++i) {
    juce::String key = "setup_" + juce::String(i);
    if (obj->hasProperty(key)) {
      setupFilePaths[i] = obj->getProperty(key).toString();
      if (juce::File(setupFilePaths[i]).existsAsFile())
        setupButtons[i].setColour(juce::TextButton::buttonColourId,
                                 juce::Colours::darkgreen);
      else
        setupButtons[i].setColour(juce::TextButton::buttonColourId,
                                 juce::Colours::darkslategrey);
    } else {
      setupFilePaths[i] = "";
      setupButtons[i].setColour(juce::TextButton::buttonColourId,
                               juce::Colours::darkslategrey);
    }
  }
  saveButtonMappings();
}

void MainComponent::loadSetFile(const juce::File &file) {
  applySetlistFromFile(file);
}

void MainComponent::updateSetupButtonLabels() {
  // Empty
}


// Loading overlay helpers (recovered after MainComponent.cpp truncation)
void MainComponent::showLoadingOverlay(const juce::String &title, const juce::String &message) {
  if (!loadingOverlay)
    loadingOverlay = std::make_unique<LoadingOverlay>();
  loadingOverlay->setTitle(title);
  loadingOverlay->setMessage(message);
  loadingOverlay->setBounds(getLocalBounds());
  addAndMakeVisible(loadingOverlay.get());
  loadingOverlay->toFront(true);
}

void MainComponent::setLoadingMessage(const juce::String &message) {
  if (loadingOverlay)
    loadingOverlay->setMessage(message);
}

void MainComponent::mouseDown(const juce::MouseEvent &e) {
  juce::ignoreUnused(e);
}

