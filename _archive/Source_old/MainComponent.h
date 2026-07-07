#pragma once

#include "DaveCoreEngine.h"
#include "RackSlotComponent.h"
#include <JuceHeader.h>

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent : public juce::AudioAppComponent,
                      public juce::MidiInputCallback {
public:
  //==============================================================================
  MainComponent();
  ~MainComponent() override;

  //==============================================================================
  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;

  //==============================================================================
  void paint(juce::Graphics &) override;
  void resized() override;

  void refreshPresetButtons();

private:
  //==============================================================================
  DaveCoreEngine engine;

  juce::TextButton scanButton{"SCAN FOR PLUGINS"};
  juce::TextButton saveBtn{"SAVE ACTIVE"};
  juce::TextButton renameBtn{"RENAME ACTIVE"};
  juce::TextButton addPresetBtn{"+"};
  juce::TextButton exitBtn{"EXIT"};
  juce::OwnedArray<RackSlotComponent> rackSlotComponents;
  juce::OwnedArray<juce::TextButton> sceneButtons;

  juce::MidiMessageCollector midiCollector;
  void handleIncomingMidiMessage(juce::MidiInput *,
                                 const juce::MidiMessage &msg) override {
    midiCollector.addMessageToQueue(msg);
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
