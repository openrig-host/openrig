#pragma once

#include "AssetLoader.h"
#include "BoutiqueLookAndFeel.h"
#include "ChannelStripComponent.h"
#include "ConfigModalOverlay.h"
#include "MidiEffectsComponent.h"
#include "SamplerComponent.h"
#include "SetupMidiTriggers.h"
#include "LoadingOverlay.h"
#include "SetupBuilderOverlay.h"
#include "Logger.h"
#include "LibraryPanel.h"
#include "MidiLearnBus.h"
#include "MidiMonitorComponent.h"
#include "NoteRangeComponent.h"
#include "IMidiNoteLearner.h"
#include "OpenRigConstants.h"
#include "OpenRigEngine.h"
#include "RackSlotComponent.h"
#include "RigLibrary.h"
#include "RigSerializer.h"
#include "RigTransitioner.h"
#include <JuceHeader.h>
#include <functional>

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent : public juce::Component,
                      public juce::MidiInputCallback,
                      public juce::AudioIODeviceCallback,
                      public juce::ChangeListener,
                      public juce::Timer {
public:
  //==============================================================================
  MainComponent();
  ~MainComponent() override;

  void timerCallback() override;

  // --- AudioIODeviceCallback ---
  void audioDeviceIOCallbackWithContext(
      const float *const *inputChannelData, int numInputChannels,
      float *const *outputChannelData, int numOutputChannels, int numSamples,
      const juce::AudioIODeviceCallbackContext &context) override;
  void audioDeviceAboutToStart(juce::AudioIODevice *device) override;
  void audioDeviceStopped() override;

  //==============================================================================
  void paint(juce::Graphics &) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &e) override;

  void updateSetupButtonLabels();

  void saveAudioSettings();
  void loadAudioSettings();
  void resetAudioDevice();
  void showAboutDialog();
  void showConfigOverlay();
  void setActiveMidiNoteLearner(juce::Component* learner) { activeMidiNoteLearner = learner; }

private:
  //==============================================================================
  juce::AudioDeviceManager deviceManager;
  OpenRigEngine engine;
  BoutiqueLookAndFeel boutiqueLookAndFeel;
  juce::TooltipWindow tooltipWindow{this};

  juce::ImageButton aboutBtn;
  juce::ImageButton settingsGearBtn;
  juce::TextButton scanButton{"SCAN FOR PLUGINS"};
  juce::TextButton audioSettingsBtn{"AUDIO SETTINGS"};
  juce::TextButton busRoutingBtn{"BUS ROUTING"};
  juce::TextButton saveBtn{"SAVE RIG"};
  juce::TextButton loadBtn{"LOAD RIG"};
  juce::TextButton prevSetlistBtn{"<<"};
  juce::TextButton nextSetlistBtn{">>"};
  juce::TextButton panicBtn{"PANIC"};
  juce::TextButton resetAudioBtn{"RESET AUDIO"};
  juce::TextButton exitBtn{"EXIT"};
  juce::OwnedArray<RackSlotComponent> rackSlotComponents;
  juce::OwnedArray<RackSlotComponent> auxReturnComponents;

  // Overlays
  std::unique_ptr<ChannelStripComponent> channelStripOverlay;
  std::unique_ptr<MidiEffectsComponent> midiEffectsOverlay;
  std::unique_ptr<SamplerComponent> samplerOverlay;
  std::unique_ptr<LoadingOverlay> loadingOverlay;
  std::unique_ptr<ConfigModalOverlay> configOverlay;
  std::unique_ptr<LibraryPanel> libraryPanel;
  std::unique_ptr<SetupBuilderOverlay> setupBuilderOverlay;
  
  int currentSetupIndex = 0;

  // Async rig transitions (Pillar B); CC-learn uses the MidiLearnBus singleton
  std::unique_ptr<OpenRig::RigTransitioner> transitioner;

  // Setup buttons - each links to a JSON rig file
  static constexpr int numSetupButtons = OpenRigConstants::kNumSetupButtons;
  juce::TextButton setupButtons[numSetupButtons];
  juce::String setupFilePaths[numSetupButtons];
  juce::TextButton saveSetBtn{"SAVE SET"};
  juce::TextButton loadSetBtn{"LOAD SET"};
  juce::TextButton setupBuilderBtn{"BUILD SETUP"};

  // Scene (Preset) management buttons
  juce::TextButton addSceneBtn{"+ SCENE"};
  juce::TextButton saveSceneBtn{"SAVE SCENE"};
  juce::TextButton renameSceneBtn{"RENAME"};
  juce::TextButton deleteSceneBtn{"DELETE"};
  juce::OwnedArray<juce::TextButton> sceneButtons;
  void refreshSceneButtons();
  void showSetupBuilderOverlay();

  // Collapsible library sidebar
  juce::TextButton toggleLibraryBtn{"<<"};
  bool libraryVisible = true;

  void loadSetupFromButton(int buttonIndex);
  void assignJsonToButton(int buttonIndex);
  void loadRigFile(int index);
  void loadRigFromFile(const juce::File &file, int targetSetlistIndex = -1);
  void loadSetFile(const juce::File &file);
  void saveButtonMappings();
  void loadButtonMappings();
  void saveSetToFile();
  void loadSetFromFile();
  void applySetlistFromFile(const juce::File &file);
  void showScanResultsDialog(const OpenRigEngine::ScanResults &results);
  void showMasterPluginMenu(bool isFoh, int chainIndex);
  void openMasterPluginEditor(bool isFoh, int chainIndex);
  void updatePreloadStatus();

  // --- Setup Helpers (extracted from constructor) ---
  void setupSlotComponents();
  void setupHeaderButtons();
  void setupSetupButtons();
  void setupMasterSection();

  // --- Loading overlay helpers ---
  void showLoadingOverlay(const juce::String &title, const juce::String &message);
  void setLoadingMessage(const juce::String &message);
  void hideLoadingOverlay();

  // Begin an async (non-freezing) rig load from a file.
  void loadRigAsync(const juce::File &file, int buttonIndexForHighlight = -1, int targetSetlistIndex = -1);

  // Windows managed by MainComponent to prevent shutdown crashes
  juce::OwnedArray<juce::DocumentWindow> activePluginWindows;

  // FileChooser must outlive async callbacks
  std::unique_ptr<juce::FileChooser> fileChooser;

  // Master bus sliders
  juce::Slider masterFohSlider;
  juce::Slider masterIemSlider;
  juce::Label fohLabel{"fohLabel", "FOH"};
  juce::Label iemLabel{"iemLabel", "DAVE EARS"};
  juce::TextButton fohFxBtns[3], fohEditGuiBtns[3];
  juce::TextButton iemFxBtns[3], iemEditGuiBtns[3];
  float masterFohL = 0.0f, masterFohR = 0.0f;
  float masterIemL = 0.0f, masterIemR = 0.0f;

  // CPU/RAM monitors
  juce::Label cpuLabel{"cpuLabel", "CPU: 0%"};
  juce::Label ramLabel{"ramLabel", "RAM: 0%"};
  juce::Label setupNameLabel{"setupNameLabel", "No rig loaded"};
  juce::Label preloadStatusLabel{"preloadStatusLabel", ""};
  juce::Rectangle<int> headerBounds;
  juce::Rectangle<int> masterColumnBounds;
  juce::ComboBox schemeSelector;

  juce::MidiMessageCollector midiCollector;
  bool mmcssRegistered = false; // Windows MMCSS audio thread priority flag
#ifdef _WIN32
  HMODULE avrtModule = nullptr;
  typedef HANDLE(WINAPI * PAvSetMmThreadCharacteristicsA)(LPCSTR, LPDWORD);
  PAvSetMmThreadCharacteristicsA avSetMmThreadFn = nullptr;
#endif
  juce::Label midiMonitorLabel{"midiMonitor", "MIDI: --"};
  juce::TextButton midiMonitorToggle{"MIDI MON"};
  std::unique_ptr<MidiMonitorComponent> midiMonitorPanel;
  bool midiMonitorVisible = false;

  void changeListenerCallback(juce::ChangeBroadcaster *source) override;

  void handleIncomingMidiMessage(juce::MidiInput *,
                                 const juce::MidiMessage &msg) override {
    // Skip SysEx messages - they can cause issues and we don't need them
    if (msg.isSysEx())
      return;

    // Intercept for Library MIDI Trigger Learning
    if (libraryPanel != nullptr && libraryPanel->isLearning()) {
      if (msg.isProgramChange()) {
        SetupMidiTrigger t;
        t.isProgramChange = true;
        t.number = msg.getProgramChangeNumber();
        t.channel = msg.getChannel();
        juce::MessageManager::callAsync([this, t]() {
          if (libraryPanel) libraryPanel->assignLearnedTrigger(t);
        });
        return;
      }
      if (msg.isController() && msg.getControllerValue() > 64) {
        SetupMidiTrigger t;
        t.isProgramChange = false;
        t.number = msg.getControllerNumber();
        t.channel = msg.getChannel();
        juce::MessageManager::callAsync([this, t]() {
          if (libraryPanel) libraryPanel->assignLearnedTrigger(t);
        });
        return;
      }
    }

    // Scene MIDI trigger learn — intercept next PC if armed
    if (msg.isProgramChange() && sceneMidiLearnArmed >= 0) {
      int learnedPC = msg.getProgramChangeNumber();
      int learnedCh = msg.getChannel();
      int targetScene = sceneMidiLearnArmed;
      juce::MessageManager::callAsync([this, targetScene, learnedPC, learnedCh]() {
        engine.setSceneMidiTrigger(targetScene, learnedPC, learnedCh);
        sceneMidiLearnArmed = -1;
        if (sceneLearnAlert != nullptr) {
          sceneLearnAlert->exitModalState(0);
          sceneLearnAlert = nullptr;
        }
        refreshSceneButtons();
      });
      return;
    }

    // Direct MIDI Trigger matching for Setups
    if (msg.isProgramChange()) {
      int pgNum = msg.getProgramChangeNumber();
      int channel = msg.getChannel();
      juce::String setupFile = SetupMidiTriggers::getInstance().findSetupForTrigger(true, pgNum, channel);
      if (setupFile.isNotEmpty()) {
        juce::MessageManager::callAsync([this, setupFile]() {
          loadRigFromFile(OpenRig::RigLibrary::getSongsDirectory().getChildFile(setupFile));
        });
        return;
      }

      // Check if any scene has this PC assigned as a trigger
      int sceneByTrigger = engine.findSceneForProgram(pgNum, channel);
      if (sceneByTrigger >= 0) {
        juce::MessageManager::callAsync([this, sceneByTrigger]() {
          engine.saveCurrentStateToScene(engine.getCurrentSceneIndex());
          engine.loadScene(sceneByTrigger);
          refreshSceneButtons();
        });
        return;
      }

      // If it doesn't trigger a setup switch, use the PC number to select the scene index inside the current setup
      juce::MessageManager::callAsync([this, pgNum]() {
        engine.saveCurrentStateToScene(engine.getCurrentSceneIndex());
        if (pgNum >= 0 && pgNum < engine.getNumScenes()) {
          engine.loadScene(pgNum);
          refreshSceneButtons();
        }
      });
      return;
    } else if (msg.isController() && msg.getControllerValue() > 64) {
      int ccNum = msg.getControllerNumber();
      int channel = msg.getChannel();
      juce::String setupFile = SetupMidiTriggers::getInstance().findSetupForTrigger(false, ccNum, channel);
      if (setupFile.isNotEmpty()) {
        juce::MessageManager::callAsync([this, setupFile]() {
          loadRigFromFile(OpenRig::RigLibrary::getSongsDirectory().getChildFile(setupFile));
        });
        return;
      }
    }

    // Debug: Log all MIDI including CC at device input level
    if (msg.isController()) {
      int ccNum = msg.getControllerNumber();
      int ccVal = msg.getControllerValue();
      int channel = msg.getChannel();

      if (OpenRig::MidiLearnBus::getInstance().handleMidiCC(ccNum, ccVal, channel)) {
        return; // Intercepted by learn mode
      }
    }

    if (msg.isNoteOn() && msg.getChannel() == 10) {
      int note = msg.getNoteNumber();
      if (note >= 36 && note < 36 + numSetupButtons) {
        int setupIdx = note - 36;
        juce::MessageManager::callAsync([this, setupIdx]() {
          loadRigFile(setupIdx);
        });
      }
    }

    midiCollector.addMessageToQueue(msg);

    // Route note-on to active sampler overlay for learning
    if (msg.isNoteOn() && samplerOverlay != nullptr) {
      int noteNum = msg.getNoteNumber();
      juce::MessageManager::callAsync([this, noteNum]() {
        if (samplerOverlay)
          samplerOverlay->handleMidiNote(noteNum);
      });
    }

    // Route note-on to any active learner (NoteRangeComponent or per-plugin config).
    // The learner is captured by value as a SafePointer (weak ref) so the async
    // no-ops if the CallOutBox was dismissed before the message thread runs.
    if (msg.isNoteOn() && activeMidiNoteLearner != nullptr) {
      int noteNum = msg.getNoteNumber();
      juce::MessageManager::callAsync([learner = activeMidiNoteLearner, noteNum]() {
        if (learner) {
          if (auto* l = dynamic_cast<IMidiNoteLearner*>(learner.getComponent()))
            l->handleMidiNote(noteNum);
        }
      });
    }

    // Update MIDI monitor on message thread
    if (msg.isNoteOn()) {
      juce::String noteText = "MIDI: " +
                              juce::MidiMessage::getMidiNoteName(
                                  msg.getNoteNumber(), true, true, 4) +
                              " vel:" + juce::String(msg.getVelocity());
      juce::MessageManager::callAsync([this, noteText]() {
        midiMonitorLabel.setText(noteText, juce::dontSendNotification);
      });
    } else if (msg.isController()) {
      juce::String ccText =
          "MIDI: CC" + juce::String(msg.getControllerNumber()) + "=" +
          juce::String(msg.getControllerValue());
      juce::MessageManager::callAsync([this, ccText]() {
        midiMonitorLabel.setText(ccText, juce::dontSendNotification);
      });
    }

    // Feed detailed monitor panel
    if (midiMonitorPanel) {
      midiMonitorPanel->addMidiMessage(msg);
    }
  }

  // Active MIDI note learner. Can be a NoteRangeComponent, PluginStackConfigComp,
  // or any Component implementing IMidiNoteLearner. Held as a generic SafePointer
  // because the CallOutBox owns/deletes the component and may dismiss at any time.
  juce::Component::SafePointer<juce::Component> activeMidiNoteLearner;

  // Scene MIDI trigger learn state
  int sceneMidiLearnArmed = -1;  // scene index being learned, -1 = not armed
  juce::AlertWindow* sceneLearnAlert = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
