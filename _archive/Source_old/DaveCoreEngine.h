#pragma once

#include "RackSlot.h"
#include "Scene.h"
#include <JuceHeader.h>
#include <memory>
#include <vector>

class DaveCoreEngine {
public:
  DaveCoreEngine() {
    // Manually adding the formats we need instead of using addDefaultFormats()
    // This bypasses the "deleted function" error caused by the headless module.
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat(new juce::VST3PluginFormat());
#endif
#if JUCE_PLUGINHOST_VST
    formatManager.addFormat(new juce::VSTPluginFormat());
#endif

    slots.push_back(std::make_unique<RackSlot>("RD88 Internal"));
    slots.push_back(std::make_unique<RackSlot>("Kontakt"));
    slots.push_back(std::make_unique<RackSlot>("B3X"));
    slots.push_back(std::make_unique<RackSlot>("Zenology"));
    slots.push_back(std::make_unique<RackSlot>("Juno 106"));
    slots.push_back(std::make_unique<RackSlot>("Accordion In"));
    slots.back()->setInputActive(true);
    slots.push_back(std::make_unique<RackSlot>("Monitor In"));
    slots.back()->setInputActive(true);
    slots.push_back(std::make_unique<RackSlot>("Bus EXCITER-104"));
    slots.back()->setInputActive(true);
    slots.push_back(std::make_unique<RackSlot>("Bus FORCE"));
    slots.back()->setInputActive(true);
    slots.push_back(std::make_unique<RackSlot>("Bus PEAK"));
    slots.back()->setInputActive(true);
    slots.push_back(std::make_unique<RackSlot>("Chorus JUN-6"));
    slots.back()->setInputActive(true);
    slots.push_back(std::make_unique<RackSlot>("Pre TridA"));
    slots.back()->setInputActive(true);
    slots.push_back(std::make_unique<RackSlot>("Pre 1973"));
    slots.back()->setInputActive(true);

    initializeDefaultScenes();
  }

  int getNumSlots() const { return (int)slots.size(); }
  RackSlot *getSlot(int index) { return slots[index].get(); }

  void loadScene(int index) {
    if (index >= 0 && index < (int)scenes.size()) {
      auto &scene = scenes[index];
      for (size_t i = 0; i < slots.size() && i < scene.slotStates.size(); ++i) {
        slots[i]->setBypass(scene.slotStates[i].bypassed);
        slots[i]->setFohLevel(scene.slotStates[i].fohLevel);
        slots[i]->setIemLevel(scene.slotStates[i].iemLevel);
      }
      currentSceneIndex = index;
    }
  }

  int getNumScenes() const { return (int)scenes.size(); }
  juce::String getSceneName(int index) const { return scenes[index].name; }
  int getCurrentSceneIndex() const { return currentSceneIndex; }

  void saveCurrentStateToScene(int index) {
    if (index >= 0 && index < (int)scenes.size()) {
      auto &scene = scenes[index];
      scene.slotStates.clear();
      for (auto &slot : slots) {
        SlotState st;
        st.bypassed = slot->isBypassed();
        st.fohLevel = slot->getFohLevel();
        st.iemLevel = slot->getIemLevel();
        scene.slotStates.push_back(st);
      }
    }
  }

  void renameScene(int index, const juce::String &newName) {
    if (index >= 0 && index < (int)scenes.size()) {
      scenes[index].name = newName;
    }
  }

  void createNewScene(const juce::String &name) {
    Scene newScene(name);
    for (auto &slot : slots) {
      SlotState st;
      st.bypassed = slot->isBypassed();
      st.fohLevel = slot->getFohLevel();
      st.iemLevel = slot->getIemLevel();
      newScene.slotStates.push_back(st);
    }
    scenes.push_back(newScene);
  }

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlockExpected;
    fohBus.setSize(2, samplesPerBlockExpected);
    iemBus.setSize(2, samplesPerBlockExpected);
  }

  // Improved scanner logic: avoids direct VST3 dependency if not enabled
  void scanForPlugins() {
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
      auto format = formatManager.getFormat(i);
      juce::FileSearchPath path(format->getDefaultLocationsToSearch());

      // Use the correct JUCE scanner class
      juce::PluginDirectoryScanner scanner(knownPlugins, *format, path, true,
                                           {});
      juce::String name;
      while (scanner.scanNextFile(true, name)) {
        // Scanning...
      }
    }
  }

  void processAudio(juce::AudioBuffer<float> &mainBuffer,
                    juce::MidiBuffer &midi) {
    fohBus.clear();
    iemBus.clear();

    if (slotMidiBuffers.size() != slots.size())
      slotMidiBuffers.resize(slots.size());

    for (auto &smb : slotMidiBuffers)
      smb.clear();

    for (const auto metadata : midi) {
      auto msg = metadata.getMessage();
      int pos = metadata.samplePosition;

      if (msg.getChannel() != 1)
        continue;

      if (msg.isNoteOn() || msg.isNoteOff() || msg.isAftertouch()) {
        for (auto &b : slotMidiBuffers)
          b.addEvent(msg, pos);
      } else if (msg.isController()) {
        int ccNum = msg.getControllerNumber();
        if (ccNum == 64) {
          for (auto &b : slotMidiBuffers)
            b.addEvent(msg, pos);
        } else if (ccNum == 1) {
          if (slotMidiBuffers.size() > 2)
            slotMidiBuffers[2].addEvent(msg, pos);
        } else if (ccNum == 7 || ccNum == 11) {
          if (selectedSlotIndex >= 0 && selectedSlotIndex < (int)slots.size()) {
            slotMidiBuffers[selectedSlotIndex].addEvent(msg, pos);
          }
        }
      }
    }

    for (int i = 0; i < (int)slots.size(); ++i) {
      juce::AudioBuffer<float> slotBuffer(mainBuffer.getNumChannels(),
                                          mainBuffer.getNumSamples());
      if (slots[i]->isInputActive()) {
        slotBuffer.makeCopyOf(mainBuffer);
      } else {
        slotBuffer.clear();
      }
      slots[i]->processBlock(slotBuffer, slotMidiBuffers[i]);
      slots[i]->sumToBuses(slotBuffer, fohBus, iemBus);
    }

    mainBuffer.clear();
    for (int ch = 0; ch < mainBuffer.getNumChannels(); ++ch) {
      if (ch < fohBus.getNumChannels())
        mainBuffer.copyFrom(ch, 0, fohBus, ch, 0, mainBuffer.getNumSamples());
    }
  }

  void setSelectedSlot(int index) {
    selectedSlotIndex = juce::jlimit(0, (int)slots.size() - 1, index);
  }

private:
  void initializeDefaultScenes() {
    scenes.clear();
    Scene s1("WARM PIANO");
    for (int i = 0; i < (int)slots.size(); ++i) {
      SlotState st;
      // Don't bypass input-active slots, only bypass synths that aren't
      // selected
      st.bypassed = !slots[i]->isInputActive() && (i != 1);
      st.fohLevel = 0.8f;
      s1.slotStates.push_back(st);
    }
    scenes.push_back(s1);

    Scene s2("BIG B3");
    for (int i = 0; i < (int)slots.size(); ++i) {
      SlotState st;
      st.bypassed = !slots[i]->isInputActive() && (i != 1 && i != 2);
      st.fohLevel = (i == 2) ? 0.9f : 0.4f;
      s2.slotStates.push_back(st);
    }
    scenes.push_back(s2);

    Scene s3("SOLO PAD");
    for (int i = 0; i < (int)slots.size(); ++i) {
      SlotState st;
      st.bypassed = !slots[i]->isInputActive() && (i != 3);
      s3.slotStates.push_back(st);
    }
    scenes.push_back(s3);
  }

  std::vector<std::unique_ptr<RackSlot>> slots;
  std::vector<juce::MidiBuffer> slotMidiBuffers;
  std::vector<Scene> scenes;
  int currentSceneIndex = 0;

  juce::AudioPluginFormatManager formatManager;
  juce::KnownPluginList knownPlugins;

  double currentSampleRate = 44100.0;
  int currentBlockSize = 512;
  int selectedSlotIndex = 1;

  juce::AudioBuffer<float> fohBus;
  juce::AudioBuffer<float> iemBus;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DaveCoreEngine)
};
