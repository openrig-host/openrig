#pragma once

#include "AssetLoader.h"
#include "BoutiqueLookAndFeel.h"
#include "ChannelStripComponent.h"
#include "ConfigModalOverlay.h"
#include "MidiEffectsComponent.h"
#include "SamplerComponent.h"
#include "SetupMidiTriggers.h"
#include "LoadingOverlay.h"
#include "Logger.h"
#include "LibraryPanel.h"
#include "MidiLearnBus.h"
#include "MidiMonitorComponent.h"
#include "NoteRangeComponent.h"
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

private:
  //==============================================================================
  juce::AudioDeviceManager deviceManager;
  OpenRigEngine engine;
  BoutiqueLookAndFeel boutiqueLookAndFeel;

  juce::ImageButton aboutBtn;
  juce::ImageButton settingsGearBtn;
  juce::TextButton scanButton{"SCAN FOR PLUGINS"};
  juce::TextButton audioSettingsBtn{"AUDIO SETTINGS"};
  juce::TextButton busRoutingBtn{"BUS ROUTING"};
  juce::TextButton saveBtn{"SAVE RIG"};
  juce::TextButton loadBtn{"LOAD RIG"};
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
  
  int currentSetupIndex = 0;

  // Async rig transitions (Pillar B); CC-learn uses the MidiLearnBus singleton
  std::unique_ptr<OpenRig::RigTransitioner> transitioner;

  // Setup buttons - each links to a JSON rig file
  static constexpr int numSetupButtons = OpenRigConstants::kNumSetupButtons;
  juce::TextButton setupButtons[numSetupButtons];
  juce::String setupFilePaths[numSetupButtons];
  juce::TextButton saveSetBtn{"SAVE SET"};
  juce::TextButton loadSetBtn{"LOAD SET"};

  // Collapsible library sidebar
  juce::TextButton toggleLibraryBtn{"<<"};
  bool libraryVisible = true;

  void loadSetupFromButton(int buttonIndex);
  void assignJsonToButton(int buttonIndex);
  void loadRigFile(int index);
  void loadRigFromFile(const juce::File &file);
  void loadSetFile(const juce::File &file);
  void saveButtonMappings();
  void loadButtonMappings();
  void saveSetToFile();
  void loadSetFromFile();
  void applySetlistFromFile(const juce::File &file);
  void showScanResultsDialog(const OpenRigEngine::ScanResults &results);
  void showMasterPluginMenu(bool isFoh, int chainIndex);
  void openMasterPluginEditor(bool isFoh, int chainIndex);

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
  void loadRigAsync(const juce::File &file, int buttonIndexForHighlight = -1);

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

    // Route note-on to any active NoteRangeComponent learning. The learner is
    // captured by value as a SafePointer (weak ref) so the async no-ops if the
    // CallOutBox was dismissed before the message thread runs the callback.
    if (msg.isNoteOn() && activeNoteRangeLearner != nullptr) {
      int noteNum = msg.getNoteNumber();
      juce::MessageManager::callAsync([learner = activeNoteRangeLearner, noteNum]() {
        if (learner)
          learner->handleMidiNote(noteNum);
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

  // Active NoteRangeComponent in learn mode. Held as a SafePointer (weak ref)
  // because the CallOutBox owns/deletes it and may dismiss it at any time; the
  // MIDI-thread callback must never dereference a raw owning pointer.
  juce::Component::SafePointer<NoteRangeComponent> activeNoteRangeLearner;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
