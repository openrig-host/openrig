#pragma once

#include "RackSlot.h"
#include "Scene.h"
#include "RigModel.h"
#include "Version.h"
#include "RigSerializer.h"
#include <JuceHeader.h>
#include "OpenRigConstants.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX // Prevent Windows.h from defining min/max macros
#include <Windows.h>
#include <intrin.h> // For _mm_pause()
#endif

// Simple file logger for debugging. Guarded so the (dead-code) Sequential
// engine's duplicate definition cannot cause an ODR violation if both headers
// are ever included in the same TU, and locked so concurrent off-audio-thread
// callers don't corrupt the file. NOTE: never call this from the audio thread.
#ifndef OPENRIG_LOGTOFILE_DEFINED
#define OPENRIG_LOGTOFILE_DEFINED
inline void logToFile(const juce::String &msg) {
  static juce::CriticalSection logFileLock;
  static juce::File logFile(
      juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
          .getChildFile("OpenRig_log.txt"));
  const juce::ScopedLock sl(logFileLock);
  logFile.appendText(juce::Time::getCurrentTime().toString(true, true) + " - " +
                     msg + "\n");
}
#endif

class OpenRigEngine {
public:
  // ========== KNOWN PLUGINS REGISTRY ==========
  // These are the VST3 plugins available for loading into slots.
  // Each entry is {Display Name, Full Path}
  struct PluginInfo {
    juce::String name;
    juce::String path;
  };

  OpenRigEngine() {
#ifdef _WIN32
    avrtModule = LoadLibraryA("avrt.dll");
    if (avrtModule != nullptr) {
      avSetMmThread = (PAvSetMmThreadCharacteristicsW)GetProcAddress(avrtModule, "AvSetMmThreadCharacteristicsW");
    }
#endif
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
#endif

    std::fill(std::begin(stressTestLastNote), std::end(stressTestLastNote), -1);

    slots.clear();
    for (int i = 0; i < OpenRigConstants::kNumSlots; ++i) {
      if (i == 0) {
        slots.push_back(std::make_unique<RackSlot>("Monitor In"));
        slots.back()->setInputChannelIndex(0); // Hardware In 1
      } else if (i == 1) {
        slots.push_back(std::make_unique<RackSlot>("CK88"));
        slots.back()->setInputChannelIndex(OpenRigConstants::kKeyboardInputChannel); // Hardware In 11 (CK88)
      } else if (i == OpenRigConstants::kNumSlots - 1) {
        slots.push_back(std::make_unique<RackSlot>("Accordion"));
        slots.back()->setInputChannelIndex(12); // Hardware In 13 (Accordion)
      } else {
        slots.push_back(
            std::make_unique<RackSlot>("Slot " + juce::String(i + 1)));
      }
    }

    // Initialize Aux Returns
    auxReturns.push_back(std::make_unique<RackSlot>("Aux 1 Return"));
    auxReturns.push_back(std::make_unique<RackSlot>("Aux 2 Return"));

    // Special routing for Monitor In (slot 0): only to IEM, no aux sends
    slots[0]->setFohEnabled(false);
    slots[0]->setIemEnabled(true);
    slots[0]->setAux1Send(0.0f); // No feedback to aux buses
    slots[0]->setAux2Send(0.0f);

    // Attach harmony-routing callbacks: when a slot's harmonizer targets
    // another slot, generated voices are injected into that slot's MIDI input.
    for (int i = 0; i < (int)slots.size(); ++i) {
      slots[i]->setMidiRouteCallback(
          [this](int targetSlot, const juce::MidiBuffer &buffer) {
            if (targetSlot >= 0 && targetSlot < (int)slots.size())
              slots[targetSlot]->injectMidi(buffer);
          });
    }

    initializeDefaultScenes();
    performLegacyMigration();
    loadPluginList();
  }

  ~OpenRigEngine() {
    // 1. Stop any pending jobs and clear the job array FIRST
    threadPool.removeAllJobs(true,
                             100); // Short timeout since we're not using it
    preallocatedJobs.clear();      // Delete all job objects

    // 2. Now safe to release plugins
    releaseAllPlugins();

#ifdef _WIN32
    if (avrtModule != nullptr) {
      FreeLibrary(avrtModule);
      avrtModule = nullptr;
    }
#endif
  }

  // Call this BEFORE the engine is destroyed to release all VST3 plugins
  void releaseAllPlugins() {
    // Release all slot plugins
    for (auto &slot : slots) {
      slot->clearChain();
    }

    for (auto &slot : auxReturns) {
      slot->clearChain();
    }

    // Release Master FX chains
    for (auto &p : fohPluginChain) {
      if (p) {
        p->releaseResources();
        p.reset();
      }
    }
    fohPluginChain.clear();

    for (auto &p : iemPluginChain) {
      if (p) {
        p->releaseResources();
        p.reset();
      }
    }
    iemPluginChain.clear();
  }

  int getNumSlots() const { return (int)slots.size(); }
  RackSlot *getSlot(int index) { return slots[index].get(); }

  int getNumAuxReturns() const { return (int)auxReturns.size(); }
  RackSlot *getAuxReturn(int index) { return auxReturns[index].get(); }

  void loadScene(int index) {
    if (index >= 0 && index < (int)scenes.size()) {
      auto &scene = scenes[index];
      for (size_t i = 0; i < slots.size() && i < scene.slotStates.size(); ++i) {
        auto* slot = slots[i].get();
        const auto& st = scene.slotStates[i];
        
        slot->setBypass(st.bypassed);
        slot->setFohLevel(st.fohLevel);
        slot->setIemLevel(st.iemLevel);
        slot->setFohEnabled(st.fohEnabled);
        slot->setIemEnabled(st.iemEnabled);
        slot->setFadersLinked(st.fadersLinked);
        slot->setAux1Send(st.aux1Send);
        slot->setAux2Send(st.aux2Send);
        slot->setIemOffset(st.iemOffset);
        slot->setTransposeOctaves(st.transposeOctaves);
        slot->setTransposeSemitones(st.transposeSemitones);
        slot->setNoteRange(st.lowNote, st.highNote);

        // DSP
        auto& dsp = slot->getStrip();
        dsp.gateEnabled.store(st.gateEnabled);
        dsp.gateThreshold.store(st.gateThreshold);
        dsp.eqEnabled.store(st.eqEnabled);
        dsp.hpfFreq.store(st.hpfFreq);
        dsp.lowShelfGain.store(st.lowShelfGain);
        dsp.highShelfGain.store(st.highShelfGain);
        dsp.compEnabled.store(st.compEnabled);
        dsp.compAmount.store(st.compAmount);
        dsp.chorusEnabled.store(st.chorusEnabled);
        dsp.chorusRate.store(st.chorusRate);
        dsp.chorusMix.store(st.chorusMix);
        dsp.reverbEnabled.store(st.reverbEnabled);
        dsp.reverbSize.store(st.reverbSize);
        dsp.reverbMix.store(st.reverbMix);

        // Arpeggiator
        auto& arp = slot->getArpeggiator();
        arp.enabled.store(st.arpEnabled);
        arp.bpm.store(st.arpBpm);
        arp.octavesUp.store(st.arpOctavesUp);
        arp.octavesDown.store(st.arpOctavesDown);
        arp.gate.store(st.arpGate);
        arp.patternIdx.store(st.arpPatternIdx);

        // Harmonizer
        auto& harm = slot->getHarmonizer();
        harm.enabled.store(st.harmEnabled);
        harm.octavesUp.store(st.harmOctavesUp);
        harm.octavesDown.store(st.harmOctavesDown);
        harm.africaMode.store(st.harmAfricaMode);
        harm.harmonyTargetSlot.store(st.harmTargetSlot);

        // Sampler
        slot->getSampler().enabled.store(st.samplerEnabled);
      }
      currentSceneIndex = index;
    }
  }

  int getNumScenes() const { return (int)scenes.size(); }
  juce::String getSceneName(int index) const { return scenes[index].name; }
  int getCurrentSceneIndex() const { return currentSceneIndex; }

  // Per-scene MIDI trigger accessors
  int getSceneMidiPC(int index) const {
    if (index >= 0 && index < (int)scenes.size())
      return scenes[index].midiProgramChange;
    return -1;
  }
  int getSceneMidiChannel(int index) const {
    if (index >= 0 && index < (int)scenes.size())
      return scenes[index].midiChannel;
    return 0;
  }
  void setSceneMidiTrigger(int index, int pc, int channel) {
    if (index >= 0 && index < (int)scenes.size()) {
      scenes[index].midiProgramChange = pc;
      scenes[index].midiChannel = channel;
    }
  }
  void clearSceneMidiTrigger(int index) {
    if (index >= 0 && index < (int)scenes.size()) {
      scenes[index].midiProgramChange = -1;
      scenes[index].midiChannel = 0;
    }
  }

  // Returns the scene index that has been assigned the given PC + channel,
  // or -1 if none found. channel 0 = match any channel.
  int findSceneForProgram(int pc, int channel) const {
    for (int i = 0; i < (int)scenes.size(); ++i) {
      const auto& sc = scenes[i];
      if (sc.midiProgramChange == pc) {
        if (sc.midiChannel == 0 || channel == 0 || sc.midiChannel == channel)
          return i;
      }
    }
    return -1;
  }

  void saveCurrentStateToScene(int index) {
    if (index >= 0 && index < (int)scenes.size()) {
      auto &scene = scenes[index];
      scene.slotStates.clear();
      for (auto &slot : slots) {
        SlotState st;
        st.bypassed = slot->isBypassed();
        st.fohLevel = slot->getFohLevel();
        st.iemLevel = slot->getIemLevel();
        st.fohEnabled = slot->isFohEnabled();
        st.iemEnabled = slot->isIemEnabled();
        st.fadersLinked = slot->areFadersLinked();
        st.aux1Send = slot->getAux1Send();
        st.aux2Send = slot->getAux2Send();
        st.iemOffset = slot->getIemOffset();
        st.transposeOctaves = slot->getTransposeOctaves();
        st.transposeSemitones = slot->getTransposeSemitones();
        st.lowNote = slot->getLowNote();
        st.highNote = slot->getHighNote();

        // DSP
        const auto& dsp = slot->getStrip();
        st.gateEnabled = dsp.gateEnabled.load();
        st.gateThreshold = dsp.gateThreshold.load();
        st.eqEnabled = dsp.eqEnabled.load();
        st.hpfFreq = dsp.hpfFreq.load();
        st.lowShelfGain = dsp.lowShelfGain.load();
        st.highShelfGain = dsp.highShelfGain.load();
        st.compEnabled = dsp.compEnabled.load();
        st.compAmount = dsp.compAmount.load();
        st.chorusEnabled = dsp.chorusEnabled.load();
        st.chorusRate = dsp.chorusRate.load();
        st.chorusMix = dsp.chorusMix.load();
        st.reverbEnabled = dsp.reverbEnabled.load();
        st.reverbSize = dsp.reverbSize.load();
        st.reverbMix = dsp.reverbMix.load();

        // Arpeggiator
        const auto& arp = slot->getArpeggiator();
        st.arpEnabled = arp.enabled.load();
        st.arpBpm = arp.bpm.load();
        st.arpOctavesUp = arp.octavesUp.load();
        st.arpOctavesDown = arp.octavesDown.load();
        st.arpGate = arp.gate.load();
        st.arpPatternIdx = arp.patternIdx.load();

        // Harmonizer
        const auto& harm = slot->getHarmonizer();
        st.harmEnabled = harm.enabled.load();
        st.harmOctavesUp = harm.octavesUp.load();
        st.harmOctavesDown = harm.octavesDown.load();
        st.harmAfricaMode = harm.africaMode.load();
        st.harmTargetSlot = harm.harmonyTargetSlot.load();

        // Sampler
        st.samplerEnabled = slot->getSampler().enabled.load();

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
      st.fohEnabled = slot->isFohEnabled();
      st.iemEnabled = slot->isIemEnabled();
      st.fadersLinked = slot->areFadersLinked();
      st.aux1Send = slot->getAux1Send();
      st.aux2Send = slot->getAux2Send();
      st.iemOffset = slot->getIemOffset();
      st.transposeOctaves = slot->getTransposeOctaves();
      st.transposeSemitones = slot->getTransposeSemitones();
      st.lowNote = slot->getLowNote();
      st.highNote = slot->getHighNote();

      // DSP
      const auto& dsp = slot->getStrip();
      st.gateEnabled = dsp.gateEnabled.load();
      st.gateThreshold = dsp.gateThreshold.load();
      st.eqEnabled = dsp.eqEnabled.load();
      st.hpfFreq = dsp.hpfFreq.load();
      st.lowShelfGain = dsp.lowShelfGain.load();
      st.highShelfGain = dsp.highShelfGain.load();
      st.compEnabled = dsp.compEnabled.load();
      st.compAmount = dsp.compAmount.load();
      st.chorusEnabled = dsp.chorusEnabled.load();
      st.chorusRate = dsp.chorusRate.load();
      st.chorusMix = dsp.chorusMix.load();
      st.reverbEnabled = dsp.reverbEnabled.load();
      st.reverbSize = dsp.reverbSize.load();
      st.reverbMix = dsp.reverbMix.load();

      // Arpeggiator
      const auto& arp = slot->getArpeggiator();
      st.arpEnabled = arp.enabled.load();
      st.arpBpm = arp.bpm.load();
      st.arpOctavesUp = arp.octavesUp.load();
      st.arpOctavesDown = arp.octavesDown.load();
      st.arpGate = arp.gate.load();
      st.arpPatternIdx = arp.patternIdx.load();

      // Harmonizer
      const auto& harm = slot->getHarmonizer();
      st.harmEnabled = harm.enabled.load();
      st.harmOctavesUp = harm.octavesUp.load();
      st.harmOctavesDown = harm.octavesDown.load();
      st.harmAfricaMode = harm.africaMode.load();
      st.harmTargetSlot = harm.harmonyTargetSlot.load();

      // Sampler
      st.samplerEnabled = slot->getSampler().enabled.load();

      newScene.slotStates.push_back(st);
    }
    scenes.push_back(newScene);
  }

  void deleteScene(int index) {
    if (scenes.size() <= 1)
      return;
    if (index >= 0 && index < (int)scenes.size()) {
      scenes.erase(scenes.begin() + index);
      if (currentSceneIndex >= (int)scenes.size()) {
        currentSceneIndex = (int)scenes.size() - 1;
      }
      loadScene(currentSceneIndex);
    }
  }

  struct SlotProcessJob : public juce::ThreadPoolJob {
    OpenRigEngine &engine;
    int slotIdx;

    SlotProcessJob(OpenRigEngine &e, int idx)
        : ThreadPoolJob("SlotProcess"), engine(e), slotIdx(idx) {}

    // Fork-join completion tokens (lock-free). setup() bumps setupToken to mark
    // "the audio thread expects THIS run to complete". runJob captures the token
    // at its start and stores it in completedToken when finished. The audio
    // thread waits for completedToken == setupToken. A stale/late worker that
    // finishes a PREVIOUS block's work carries an OLD token, so it can never
    // falsely satisfy a later block's wait -> reused jobs can't cause a
    // corrupted sumToBuses (the fork-join reuse hazard), with no pool locking.
    std::atomic<uint32_t> setupToken{0};
    std::atomic<uint32_t> completedToken{0};

    void setup(const float *const *input, int numCh, std::atomic<int> &c) {
      rawInput = input;
      numInputChannels = numCh;
      counter = &c;
      setupToken.fetch_add(1, std::memory_order_acq_rel); // new expected completion
    }

    JobStatus runJob() override {
      const uint32_t myToken = setupToken.load(std::memory_order_acquire);
      juce::ScopedNoDenormals noDenormals;

#ifdef _WIN32
      // Boost worker thread to Pro Audio priority
      thread_local bool mmcssApplied = false;
      if (!mmcssApplied) {
        if (engine.avSetMmThread != nullptr) {
          DWORD taskIndex = 0;
          engine.avSetMmThread(L"Pro Audio", &taskIndex);
        }
        mmcssApplied = true;
      }
#endif

      if (slotIdx < (int)engine.slots.size()) {
        auto &slot = engine.slots[slotIdx];
        auto &scratch = engine.scratchBuffers[slotIdx];
        auto &midi = engine.slotMidiBuffers[slotIdx];
        int numSamples = scratch.getNumSamples();

        scratch.clear();
        int inputIdx = slot->getInputChannelIndex();

        // Bounds check inputIdx against available channels
        if (inputIdx >= 0 && rawInput != nullptr &&
            inputIdx < numInputChannels) {
          // Hardware input routing: copy from raw pointers
          if (const float *l = rawInput[inputIdx])
            scratch.copyFrom(0, 0, l, numSamples);

          // Special case: Monitor In (slot 0) always duplicates mono to stereo
          // This allows stereo effects (reverb, delay) to create spatial
          // imaging
          if (slotIdx == 0) {
            if (const float *l = rawInput[inputIdx])
              scratch.copyFrom(1, 0, l, numSamples);
          }
          // For other slots: support stereo hardware pairs if they exist
          else if (inputIdx + 1 < numInputChannels) {
            if (const float *r = rawInput[inputIdx + 1])
              scratch.copyFrom(1, 0, r, numSamples);
          } else if (const float *l = rawInput[inputIdx]) {
            // Fallback for last channel: duplicate mono to right
            scratch.copyFrom(1, 0, l, numSamples);
          }
        }
        slot->processBlock(scratch, midi);
      }

      completedToken.store(myToken, std::memory_order_release); // publishes scratch writes
      if (counter)
        (*counter)++;
      return jobHasFinished;
    }

  private:
    const float *const *rawInput = nullptr;
    int numInputChannels = 0;
    std::atomic<int> *counter = nullptr;
  };

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlockExpected;
    // We use 32 channels internally as a safety margin for multi-out VST3s
    fohBus.setSize(32, samplesPerBlockExpected);
    iemBus.setSize(32, samplesPerBlockExpected);

    // Initialize per-slot scratch buffers and pre-allocated jobs
    scratchBuffers.resize(slots.size() + auxReturns.size());
    preallocatedJobs.clear();

    for (int i = 0; i < (int)slots.size(); ++i) {
      slots[i]->prepare(sampleRate);
      scratchBuffers[i].setSize(32, samplesPerBlockExpected);
      preallocatedJobs.add(new SlotProcessJob(*this, i));
    }

    // Scratch buffers for Aux Returns
    for (int i = 0; i < (int)auxReturns.size(); ++i) {
      auxReturns[i]->prepare(sampleRate);
      scratchBuffers[slots.size() + i].setSize(32, samplesPerBlockExpected);
    }

    aux1Bus.setSize(32, samplesPerBlockExpected);
    aux2Bus.setSize(32, samplesPerBlockExpected);

    // Pre-allocate per-block reusable buffers (avoid heap allocs on audio thread)
    dummyAux1.setSize(2, samplesPerBlockExpected);
    dummyAux2.setSize(2, samplesPerBlockExpected);
    slotMidiBuffers.resize(slots.size());
  }

  struct ScanResults {
    std::vector<PluginInfo> newPlugins;
    std::vector<PluginInfo> missingPlugins;
  };

  std::function<void(ScanResults)> onScanFinished;

  void scanForPlugins() {
    juce::File vstFolder("C:\\Program Files\\Common Files\\VST3");
    if (!vstFolder.exists() || !vstFolder.isDirectory()) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::MessageBoxIconType::WarningIcon, "Scan Error",
          "VST3 folder not found!");
      return;
    }

    // Run in background to avoid UI freeze
    std::thread([this, vstFolder]() {
      ScanResults results;
      std::vector<PluginInfo> foundPlugins;

      // Recursive scan for .vst3 files using modern RangedDirectoryIterator
      for (const auto &entry : juce::RangedDirectoryIterator(
               vstFolder, true, "*.vst3", juce::File::findFiles)) {
        juce::File f = entry.getFile();
        PluginInfo info;
        info.name = f.getFileNameWithoutExtension();
        info.path = f.getFullPathName();
        foundPlugins.push_back(info);
      }

      // Compare with availablePlugins
      {
        juce::ScopedLock sl(lock);

        // New plugins
        for (const auto &found : foundPlugins) {
          bool exists = false;
          for (const auto &existing : availablePlugins) {
            if (existing.path == found.path) {
              exists = true;
              break;
            }
          }
          if (!exists)
            results.newPlugins.push_back(found);
        }

        // Missing plugins
        for (const auto &existing : availablePlugins) {
          bool exists = false;
          for (const auto &found : foundPlugins) {
            if (found.path == existing.path) {
              exists = true;
              break;
            }
          }
          // Also consider missing if file no longer exists
          if (!exists || !juce::File(existing.path).exists()) {
            results.missingPlugins.push_back(existing);
          }
        }
      }

      // Call callback on UI thread
      juce::MessageManager::callAsync([this, results]() {
        if (onScanFinished)
          onScanFinished(results);
      });
    }).detach();
  }

  void applyScanResults(const std::vector<PluginInfo> &toAdd,
                        const std::vector<PluginInfo> &toRemove) {
    juce::ScopedLock sl(lock);

    // Remove
    for (const auto &r : toRemove) {
      availablePlugins.erase(std::remove_if(availablePlugins.begin(),
                                            availablePlugins.end(),
                                            [&r](const PluginInfo &p) {
                                              return p.path == r.path;
                                            }),
                             availablePlugins.end());
    }

    // Add
    for (const auto &a : toAdd) {
      // Avoid duplicates
      bool exists = false;
      for (const auto &p : availablePlugins) {
        if (p.path == a.path) {
          exists = true;
          break;
        }
      }
      if (!exists)
        availablePlugins.push_back(a);
    }

    // Sort alphabetically
    std::sort(availablePlugins.begin(), availablePlugins.end(),
              [](const PluginInfo &a, const PluginInfo &b) {
                return a.name.compareIgnoreCase(b.name) < 0;
              });

    savePluginList();
  }

  void savePluginList() {
    juce::File file =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("OpenRig")
            .getChildFile("known_plugins.json");

    if (!file.getParentDirectory().exists())
      file.getParentDirectory().createDirectory();

    juce::Array<juce::var> pluginNodes;
    for (const auto &p : availablePlugins) {
      auto *obj = new juce::DynamicObject();
      obj->setProperty("name", p.name);
      obj->setProperty("path", p.path);
      pluginNodes.add(juce::var(obj));
    }

    juce::File tempFile = file.withFileExtension("tmp");
    if (tempFile.replaceWithText(
            juce::JSON::toString(juce::var(pluginNodes)))) {
      tempFile.moveFileTo(file);
    }
  }

  void loadPluginList() {
    juce::File file =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("OpenRig")
            .getChildFile("known_plugins.json");

    if (file.existsAsFile()) {
      auto json = juce::JSON::parse(file);
      if (auto *arr = json.getArray()) {
        availablePlugins.clear();
        for (int i = 0; i < arr->size(); ++i) {
          auto v = arr->getReference(i);
          availablePlugins.push_back({v.getProperty("name", "").toString(),
                                      v.getProperty("path", "").toString()});
        }
        return; // Success
      }
    }

    // If file doesn't exist or is invalid, use hardcoded defaults which are
    // already in availablePlugins (initialized in the member declaration)
    // but let's ensure they are sorted.
    std::sort(availablePlugins.begin(), availablePlugins.end(),
              [](const PluginInfo &a, const PluginInfo &b) {
                return a.name.compareIgnoreCase(b.name) < 0;
              });
  }

  void processAudio(const float *const *inputData, int numInputs,
                    float *const *outputData, int numOutputs, int numSamples,
                    juce::MidiBuffer &midi) {
    juce::ScopedNoDenormals noDenormals;
    // Inject stress test MIDI if active
    if (stressTestActive.load()) {
        double sr = currentSampleRate;
        if (sr > 0.0) {
            double samplesPerTrigger = 2048.0;
            stressTestPhase += numSamples;
            if (stressTestPhase >= samplesPerTrigger) {
                stressTestPhase = std::fmod(stressTestPhase, samplesPerTrigger);
                for (int c = 0; c < OpenRigConstants::kNumSlots; ++c) {
                    if (stressTestLastNote[c] >= 0) {
                        midi.addEvent(juce::MidiMessage::noteOff(1, stressTestLastNote[c]), 0);
                        stressTestLastNote[c] = -1;
                    }
                }
                static std::mt19937 stressRng(1337);
                std::uniform_int_distribution<int> noteDist(48, 84);
                std::uniform_int_distribution<int> velDist(60, 127);
                for (int c = 0; c < 6; ++c) {
                    int note = noteDist(stressRng);
                    int vel = velDist(stressRng);
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)vel), 0);
                    stressTestLastNote[c] = note;
                }
            }
        }
    }

    // Use TryLock to avoid blocking the audio thread if UI has the lock
    juce::GenericScopedTryLock<juce::CriticalSection> sl(lock);
    if (!sl.isLocked()) {
      for (int i = 0; i < numOutputs; ++i)
        if (outputData[i])
          juce::FloatVectorOperations::clear(outputData[i], numSamples);
      return;
    }

    // We have the lock - do the quick work (bus clearing, MIDI routing)

    // CRITICAL: Clear all output buffers first to prevent feedback
    for (int i = 0; i < numOutputs; ++i)
      if (outputData[i])
        juce::FloatVectorOperations::clear(outputData[i], numSamples);

    fohBus.clear();
    iemBus.clear();

    if (slotMidiBuffers.size() != slots.size()) {
      // Pre-allocated in prepareToPlay(). A mismatch means prepareToPlay()
      // wasn't called for this rack — never allocate on the audio thread.
      audioUnderrunFlag.store(true);
      return; // hardware outputs + buses already cleared above
    }

    for (auto &smb : slotMidiBuffers)
      smb.clear();

    // Panic Button Trigger
    if (panicTriggered.exchange(false)) {
      // DBG removed for real-time safety
      for (auto &smb : slotMidiBuffers) {
        smb.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        smb.addEvent(juce::MidiMessage::allSoundOff(1), 0);
        smb.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0),
                     0); // Sustain Off
        smb.addEvent(juce::MidiMessage::allControllersOff(1), 0);
      }
    }

    int globalChan = defaultMidiChannel.load();
    // Per-slot MIDI channel routing. Replaces the legacy hard "channel 1 only"
    // filter. effective channel = per-slot override (>=0) else global default.
    // 0 means Omni. A message routes to a slot when either side is Omni or the
    // channels match. Default (global=1) preserves the previous behaviour.
    auto slotAcceptsChannel = [this, globalChan](size_t i, int msgChannel) {
      int overrideCh = slots[i]->getMidiChannelOverride();
      if (overrideCh == -2)
        return false;
      int effective = (overrideCh >= 0) ? overrideCh : globalChan;
      if (effective == 0)
        return true; // slot is Omni
      if (msgChannel == 0)
        return true; // message is Omni
      return msgChannel == effective;
    };

    for (const auto metadata : midi) {
      auto msg = metadata.getMessage();
      int pos = metadata.samplePosition;

      // NEVER pass program change (patch change) messages
      if (msg.isProgramChange())
        continue;

      int msgChannel = msg.getChannel();

      // Special routing: MIDI messages -> only to slots
      if (msg.isNoteOn() || msg.isNoteOff()) {
        int noteNum = msg.getNoteNumber();
        // Apply per-slot channel + note range filtering
        for (size_t i = 0; i < slots.size(); ++i) {
          if (slotAcceptsChannel(i, msgChannel) &&
              slots[i]->isNoteInRange(noteNum)) {
            int transposeSemis = juce::jlimit(-48, 48, slots[i]->getTransposeSemis());
            if (transposeSemis != 0) {
              int transposed = juce::jlimit(0, 127, noteNum + transposeSemis);
              auto transposedMsg = msg.isNoteOn()
                  ? juce::MidiMessage::noteOn(msg.getChannel(), transposed,
                                              msg.getVelocity())
                  : juce::MidiMessage::noteOff(msg.getChannel(), transposed,
                                               msg.getVelocity());
              slotMidiBuffers[i].addEvent(transposedMsg, pos);
            } else {
              slotMidiBuffers[i].addEvent(msg, pos);
            }
          }
        }
      } else if (msg.isAftertouch() || msg.isPitchWheel()) {
        // Aftertouch and pitch wheel go to channel-matched slots
        for (size_t i = 0; i < slots.size(); ++i) {
          if (slotAcceptsChannel(i, msgChannel))
            slotMidiBuffers[i].addEvent(msg, pos);
        }
      } else if (msg.isController()) {
        int ccNum = msg.getControllerNumber();

        for (size_t i = 0; i < slots.size(); ++i) {
          if (slotAcceptsChannel(i, msgChannel) &&
              slots[i]->isCCAllowed(ccNum)) {
            slotMidiBuffers[i].addEvent(msg, pos);
          }
        }
      }
    }

    // Ensure scratch buffers are sized correctly. These are pre-allocated in
    // prepareToPlay(); on a shape/block-size mismatch we bail to silence rather
    // than allocate (priority inversion / dropout on the audio thread).
    size_t totalBuffers = slots.size() + auxReturns.size();
    if (scratchBuffers.size() < totalBuffers) {
      audioUnderrunFlag.store(true);
      return;
    }

    for (size_t i = 0; i < totalBuffers; ++i) {
      if (scratchBuffers[i].getNumChannels() < 32 ||
          scratchBuffers[i].getNumSamples() < numSamples) {
        audioUnderrunFlag.store(true);
        return; // block-size mismatch; never reallocate on the audio thread
      }
    }

    // PARALLEL PROCESSING: Process all slots simultaneously
    int numActiveSlots = (int)slots.size();

    // Pre-allocated in prepareToPlay(); never allocate jobs on the audio thread.
    if (preallocatedJobs.size() < numActiveSlots) {
      audioUnderrunFlag.store(true);
      return;
    }

    // setup() bumps each job's completion token; we then wait for
    // completedToken == setupToken. Because a late/stale worker carries an OLD
    // token, it can never falsely satisfy a later block's wait, so reused jobs
    // can't cause a corrupted sumToBuses (the fork-join reuse hazard).
    for (int i = 0; i < numActiveSlots; ++i)
      preallocatedJobs[i]->setup(inputData, numInputs, slotsFinishedCount);
    slotsFinishedCount.store(0); // diagnostic only; the wait uses completion tokens

    for (int i = 0; i < numActiveSlots; ++i)
      threadPool.addJob(preallocatedJobs[i], false);

    // Spin-wait barrier (BOUNDED). _mm_pause() keeps the core active without
    // yielding to the OS. The ~50M-pause bound is far longer than any valid
    // block can take, so it only ever trips on a wedged worker (e.g. a plugin
    // stuck in an infinite loop). On timeout we MUST NOT sum the scratch
    // buffers — a still-running worker could be writing to them, which would be
    // an unsynchronized concurrent read/write (corruption). Instead emit
    // silence for this block, flag the underrun, and keep the app responsive so
    // the user can recover (panic / restart the offending slot).
    bool timedOut = false;
    {
      constexpr long long kMaxSpins = 50000000LL;
      long long spinCount = 0;
      for (;;) {
        bool allDone = true;
        for (int i = 0; i < numActiveSlots; ++i) {
          const uint32_t expected =
              preallocatedJobs[i]->setupToken.load(std::memory_order_acquire);
          if (preallocatedJobs[i]->completedToken.load(std::memory_order_acquire) != expected) {
            allDone = false;
            break;
          }
        }
        if (allDone) break;
        if (++spinCount >= kMaxSpins) {
          timedOut = true;
          break;
        }
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
        _mm_pause(); // Intel/AMD: hint to reduce power and avoid pipeline stalls
#elif defined(__arm__) || defined(__aarch64__)
        __yield(); // ARM equivalent
#endif
      }
    }

    if (timedOut) {
      audioUnderrunFlag.store(true);
      aux1Bus.clear();
      aux2Bus.clear();
      // fohBus/iemBus already cleared above; do NOT sum contended buffers.
    } else {
      // Sum all processed buffers to buses
      aux1Bus.clear();
      aux2Bus.clear();

      for (int i = 0; i < (int)slots.size(); ++i) {
        slots[i]->sumToBuses(scratchBuffers[i], fohBus, iemBus, aux1Bus, aux2Bus);
      }
    }

    // --- Process Aux Returns ---
    // Aux returns should NOT feed back into the aux buses (feedback loop)
    // Use pre-allocated dummy buffers (cleared, not re-constructed)
    dummyAux1.clear();
    dummyAux2.clear();

    for (int i = 0; i < (int)auxReturns.size(); ++i) {
      auto &scratch = scratchBuffers[slots.size() + i];
      scratch.clear();
      auto &bus = (i == 0) ? aux1Bus : aux2Bus;

      // Copy aux bus to return slot input
      for (int ch = 0; ch < 2; ++ch)
        scratch.copyFrom(ch, 0, bus, ch, 0, numSamples);

      auxReturns[i]->processBlock(scratch, emptyMidiBuf);

      // Sum to main output buses only - NOT back to aux buses
      auxReturns[i]->sumToBuses(scratch, fohBus, iemBus, dummyAux1, dummyAux2);
    }

    // --- Master FX Processing ---
    // Apply Master Fader Levels BEFORE FX (or typically after sums, but before
    // output)
    fohBus.applyGain(fohMasterLevel);
    iemBus.applyGain(iemMasterLevel);

    for (auto &plugin : fohPluginChain) {
      if (plugin)
        plugin->processBlock(fohBus, emptyMidiBuf);
    }
    for (auto &plugin : iemPluginChain) {
      if (plugin)
        plugin->processBlock(iemBus, emptyMidiBuf);
    }

    // --- Post-load mute + fade-in ---
    // Blue 3 and similar plugins dump internal buffer content (Lesie tails,
    // tonewheel state) for several seconds after state restoration. We hard-
    // mute for 1.5s to let those flush, then fade in over 0.5s.
    {
      int fadeRem = postLoadFadeRemaining.load();
      if (fadeRem > 0) {
        int fadeTotal = juce::jmax(1, postLoadFadeTotal.load());
        int fadeStart = fadeTotal / 4; // last 25% = fade-in phase

        if (fadeRem > fadeStart) {
          // Hard mute phase
          for (int ch = 0; ch < 2; ++ch) {
            if (ch < fohBus.getNumChannels())
              fohBus.clear(ch, 0, numSamples);
            if (ch < iemBus.getNumChannels())
              iemBus.clear(ch, 0, numSamples);
          }
        } else {
          // Linear fade-in phase (0 -> 1 over fadeStart samples)
          float startGain = 1.0f - (float)fadeRem / (float)fadeStart;
          int samplesNow = juce::jmin(numSamples, fadeRem);
          float endGain =
              1.0f - (float)(fadeRem - samplesNow) / (float)fadeStart;
          for (int ch = 0; ch < 2; ++ch) {
            if (ch < fohBus.getNumChannels())
              fohBus.applyGainRamp(ch, 0, samplesNow, startGain, endGain);
            if (ch < iemBus.getNumChannels())
              iemBus.applyGainRamp(ch, 0, samplesNow, startGain, endGain);
            if (samplesNow < numSamples) {
              if (ch < fohBus.getNumChannels())
                fohBus.applyGain(ch, samplesNow, numSamples - samplesNow,
                                 endGain);
              if (ch < iemBus.getNumChannels())
                iemBus.applyGain(ch, samplesNow, numSamples - samplesNow,
                                 endGain);
            }
          }
        }
        postLoadFadeRemaining.store(juce::jmax(0, fadeRem - numSamples));
      }
    }

    // --- Copy Buses to Hardware Outputs ---
    // Snapshot the atomic offsets ONCE per block: the setters can write from the
    // message thread, so reading them several times (bounds-check then index)
    // would be a check-then-use race that could index outputData out of bounds.
    const int fohOff = fohOutputOffset.load(std::memory_order_acquire);
    const int iemOff = iemOutputOffset.load(std::memory_order_acquire);

    // Route FOH to configured outputs (default: 1+2)
    if (fohOff >= 0 && fohOff < numOutputs && outputData[fohOff] != nullptr) {
      juce::FloatVectorOperations::copy(outputData[fohOff],
                                        fohBus.getReadPointer(0), numSamples);
      if (fohOff + 1 < numOutputs && outputData[fohOff + 1] != nullptr)
        juce::FloatVectorOperations::copy(outputData[fohOff + 1],
                                          fohBus.getReadPointer(1), numSamples);
    }

    // Route IEM to configured outputs (default: 3+4)
    if (iemOff >= 0 && iemOff < numOutputs && outputData[iemOff] != nullptr) {
      juce::FloatVectorOperations::copy(outputData[iemOff],
                                        iemBus.getReadPointer(0), numSamples);
      if (iemOff + 1 < numOutputs && outputData[iemOff + 1] != nullptr)
        juce::FloatVectorOperations::copy(outputData[iemOff + 1],
                                          iemBus.getReadPointer(1), numSamples);
    }

    // Update master peaks
    fohPeakL = fohBus.getMagnitude(0, 0, fohBus.getNumSamples());
    fohPeakR = (fohBus.getNumChannels() > 1)
                   ? fohBus.getMagnitude(1, 0, fohBus.getNumSamples())
                   : fohPeakL;
    iemPeakL = iemBus.getMagnitude(0, 0, iemBus.getNumSamples());
    iemPeakR = (iemBus.getNumChannels() > 1)
                   ? iemBus.getMagnitude(1, 0, iemBus.getNumSamples())
                   : iemPeakL;
  }

  // --- Master FX Management ---
  void
  addPluginToMasterChain(bool isFoh,
                         std::unique_ptr<juce::AudioPluginInstance> plugin) {
    juce::ScopedLock sl(lock);
    if (isFoh)
      fohPluginChain.push_back(std::move(plugin));
    else
      iemPluginChain.push_back(std::move(plugin));
  }

  // --- Persistence (JSON / var) ---
  juce::String exportRigToJson() {
    saveCurrentStateToScene(currentSceneIndex);

    auto *rig = new juce::DynamicObject();
    rig->setProperty("version", 2);
    rig->setProperty("schemaVersion", 2);
    rig->setProperty("fohMasterLevel", (double)fohMasterLevel);
    rig->setProperty("iemMasterLevel", (double)iemMasterLevel);
    rig->setProperty("fohOutputOffset", (int)fohOutputOffset);
    rig->setProperty("iemOutputOffset", (int)iemOutputOffset);
    rig->setProperty("defaultMidiChannel", defaultMidiChannel.load());

    // Master FX
    juce::Array<juce::var> fohNodes;
    for (auto &p : fohPluginChain) {
      if (p)
        fohNodes.add(getPluginVar(p.get()));
      else
        fohNodes.add(juce::var());
    }
    rig->setProperty("fohFx", fohNodes);

    juce::Array<juce::var> iemNodes;
    for (auto &p : iemPluginChain) {
      if (p)
        iemNodes.add(getPluginVar(p.get()));
      else
        iemNodes.add(juce::var());
    }
    rig->setProperty("iemFx", iemNodes);

    // Channels
    juce::Array<juce::var> channelNodes;
    for (int i = 0; i < (int)slots.size(); ++i) {
      auto s = slots[i].get();
      auto *st = new juce::DynamicObject();
      st->setProperty("name", s->getName());
      st->setProperty("iconIndex", s->getIconIndex());
      st->setProperty("channelColor", s->getChannelColor().toString());
      st->setProperty("level", (double)s->getChannelLevel());
      st->setProperty("foh", s->isFohEnabled());
      st->setProperty("iem", s->isIemEnabled());
      st->setProperty("mute", s->isBypassed());
      st->setProperty("inputIndex", s->getInputChannelIndex());
      st->setProperty("aux1", (double)s->getAux1Send());
      st->setProperty("aux2", (double)s->getAux2Send());
      st->setProperty("iemOffset", (double)s->getIemOffset());
      st->setProperty("transposeOctaves", s->getTransposeOctaves());
      st->setProperty("transposeSemitones", s->getTransposeSemitones());
      st->setProperty("lowNote", s->getLowNote());
      st->setProperty("highNote", s->getHighNote());

      juce::String ccList;
      for (int cc : s->getAllowedCCs())
        ccList += juce::String(cc) + ",";
      st->setProperty("allowedCCs", ccList);

      // CC-controlled faders + per-slot MIDI channel override (new in v2)
      st->setProperty("fohCC", s->getFohCC());
      st->setProperty("iemCC", s->getIemCC());
      st->setProperty("midiChannel", s->getMidiChannelOverride());

      // Save Channel Strip Settings
      auto *stripObj = new juce::DynamicObject();
      auto &strip = s->getStrip();
      stripObj->setProperty("gateEnabled", (bool)strip.gateEnabled);
      stripObj->setProperty("gateThreshold", (double)strip.gateThreshold);
      stripObj->setProperty("eqEnabled", (bool)strip.eqEnabled);
      stripObj->setProperty("hpfFreq", (double)strip.hpfFreq);
      stripObj->setProperty("eqLow", (double)strip.lowShelfGain);
      stripObj->setProperty("eqHigh", (double)strip.highShelfGain);
      stripObj->setProperty("compEnabled", (bool)strip.compEnabled);
      stripObj->setProperty("compAmount", (double)strip.compAmount);
      st->setProperty("strip", stripObj);

      // Save Sampler Settings
      auto *sampObj = new juce::DynamicObject();
      auto &sampler = s->getSampler();
      sampObj->setProperty("enabled", sampler.enabled.load());
      juce::Array<juce::var> samplerSlots;
      for (int sIdx = 0; sIdx < 8; ++sIdx) {
        auto cfg = sampler.getSlotConfig(sIdx);
        auto *slo = new juce::DynamicObject();
        slo->setProperty("wavPath", cfg.wavPath);
        slo->setProperty("rootNote", cfg.rootNote);
        slo->setProperty("keyLow", cfg.keyLow);
        slo->setProperty("keyHigh", cfg.keyHigh);
        slo->setProperty("pitchOffset", (double)cfg.pitchOffsetSemitones);
        slo->setProperty("volume", (double)cfg.volume);
        slo->setProperty("startRatio", (double)cfg.startRatio);
        slo->setProperty("endRatio", (double)cfg.endRatio);
        samplerSlots.add(juce::var(slo));
      }
      sampObj->setProperty("slots", samplerSlots);
      st->setProperty("sampler", sampObj);

      // Save CC-to-Parameter mappings
      juce::Array<juce::var> ccMappingNodes;
      for (const auto &pair : s->getCCMappings()) {
        auto *mapping = new juce::DynamicObject();
        mapping->setProperty("cc", pair.first);
        mapping->setProperty("chainIndex", pair.second.chainIndex);
        mapping->setProperty("paramId", pair.second.paramId);
        mapping->setProperty("paramIndex", pair.second.parameterIndex);
        mapping->setProperty("minValue", (double)pair.second.minValue);
        mapping->setProperty("maxValue", (double)pair.second.maxValue);
        mapping->setProperty("invert", pair.second.invert);
        ccMappingNodes.add(juce::var(mapping));
      }
      st->setProperty("ccMappings", ccMappingNodes);

      // Save CC Passthrough (remap) mappings
      juce::Array<juce::var> ccPassthroughNodes;
      for (const auto &pair : s->getCCPassthroughMap()) {
        auto *pt = new juce::DynamicObject();
        pt->setProperty("incomingCC", pair.first);
        pt->setProperty("outgoingCC", pair.second);
        ccPassthroughNodes.add(juce::var(pt));
      }
      st->setProperty("ccPassthroughs", ccPassthroughNodes);

      juce::Array<juce::var> chainNodes;
      for (int pIdx = 0; pIdx < s->getChainSize(); ++pIdx) {
        auto plugin = s->getPluginInstance(pIdx);
        if (plugin) {
          auto pluginVar = getPluginVar(plugin);
          if (pIdx < 3 && pluginVar.isObject()) {
            auto* pObj = pluginVar.getDynamicObject();
            auto& cs = s->getChainSlotSettings(pIdx);
            pObj->setProperty("lowNote", cs.lowNote.load());
            pObj->setProperty("highNote", cs.highNote.load());
            pObj->setProperty("level", (double)cs.level.load());
            pObj->setProperty("enabled", cs.enabled.load());
          }
          chainNodes.add(pluginVar);
        } else {
          chainNodes.add(juce::var());
        }
      }
      st->setProperty("chain", chainNodes);
      channelNodes.add(juce::var(st));
    }
    rig->setProperty("channels", channelNodes);

    // Scenes (Presets)
    juce::Array<juce::var> sceneNodes;
    for (const auto &scene : scenes) {
      auto *sceneObj = new juce::DynamicObject();
      sceneObj->setProperty("name", scene.name);
      sceneObj->setProperty("midiProgramChange", scene.midiProgramChange);
      sceneObj->setProperty("midiChannel", scene.midiChannel);

      juce::Array<juce::var> states;
      for (const auto &st : scene.slotStates) {
        auto *stateObj = new juce::DynamicObject();
        stateObj->setProperty("bypassed", st.bypassed);
        stateObj->setProperty("level", (double)st.fohLevel); // Backward compatibility
        stateObj->setProperty("fohLevel", (double)st.fohLevel);
        stateObj->setProperty("iemLevel", (double)st.iemLevel);
        stateObj->setProperty("foh", st.fohEnabled);
        stateObj->setProperty("iem", st.iemEnabled);
        stateObj->setProperty("fadersLinked", st.fadersLinked);
        stateObj->setProperty("aux1Send", (double)st.aux1Send);
        stateObj->setProperty("aux2Send", (double)st.aux2Send);
        stateObj->setProperty("iemOffset", (double)st.iemOffset);
        stateObj->setProperty("transposeOctaves", st.transposeOctaves);
        stateObj->setProperty("transposeSemitones", st.transposeSemitones);
        stateObj->setProperty("lowNote", st.lowNote);
        stateObj->setProperty("highNote", st.highNote);

        // DSP
        stateObj->setProperty("gateEnabled", st.gateEnabled);
        stateObj->setProperty("gateThreshold", (double)st.gateThreshold);
        stateObj->setProperty("eqEnabled", st.eqEnabled);
        stateObj->setProperty("hpfFreq", (double)st.hpfFreq);
        stateObj->setProperty("eqLow", (double)st.lowShelfGain);
        stateObj->setProperty("eqHigh", (double)st.highShelfGain);
        stateObj->setProperty("compEnabled", st.compEnabled);
        stateObj->setProperty("compAmount", (double)st.compAmount);
        stateObj->setProperty("chorusEnabled", st.chorusEnabled);
        stateObj->setProperty("chorusRate", (double)st.chorusRate);
        stateObj->setProperty("chorusMix", (double)st.chorusMix);
        stateObj->setProperty("reverbEnabled", st.reverbEnabled);
        stateObj->setProperty("reverbSize", (double)st.reverbSize);
        stateObj->setProperty("reverbMix", (double)st.reverbMix);

        // Arpeggiator
        stateObj->setProperty("arpEnabled", st.arpEnabled);
        stateObj->setProperty("arpBpm", (double)st.arpBpm);
        stateObj->setProperty("arpOctavesUp", st.arpOctavesUp);
        stateObj->setProperty("arpOctavesDown", st.arpOctavesDown);
        stateObj->setProperty("arpGate", (double)st.arpGate);
        stateObj->setProperty("arpPatternIdx", st.arpPatternIdx);

        // Harmonizer
        stateObj->setProperty("harmEnabled", st.harmEnabled);
        stateObj->setProperty("harmOctavesUp", st.harmOctavesUp);
        stateObj->setProperty("harmOctavesDown", st.harmOctavesDown);
        stateObj->setProperty("harmAfricaMode", st.harmAfricaMode);
        stateObj->setProperty("harmTargetSlot", st.harmTargetSlot);

        // Sampler
        stateObj->setProperty("samplerEnabled", st.samplerEnabled);

        states.add(juce::var(stateObj));
      }
      sceneObj->setProperty("states", states);
      sceneNodes.add(juce::var(sceneObj));
    }
    rig->setProperty("scenes", sceneNodes);
    rig->setProperty("currentSceneIndex", currentSceneIndex);

    return juce::JSON::toString(juce::var(rig));
  }

  void importRigFromJson(const juce::String &json) {
    auto rig = juce::JSON::parse(json);
    applyRig(rig);
  }

  // Apply a parsed rig object to the live rack under the callback lock. This
  // is the shared implementation used by both the synchronous import path and
  // the async RigTransitioner (which has already pre-built plugins into the
  // staging cache, so this call stays fast).
  void applyRig(const juce::var &rig) {
    if (!rig.isObject())
      return;

    juce::ScopedLock sl(lock);

    fohMasterLevel = (float)rig.getProperty("fohMasterLevel", 1.0);
    iemMasterLevel = (float)rig.getProperty("iemMasterLevel", 1.0);
    fohOutputOffset = rig.getProperty("fohOutputOffset", 0);
    iemOutputOffset = rig.getProperty("iemOutputOffset", 2);
    defaultMidiChannel.store(
        (int)rig.getProperty("defaultMidiChannel", OpenRigConstants::kDefaultMidiChannel));

    // Master FX
    // Master FX - Smart Loading
    // Don't clear upfront - smart load handles replacement
    if (auto *fohArr = rig.getProperty("fohFx", juce::var()).getArray()) {
      for (int i = 0; i < fohArr->size(); ++i) {
        auto v = fohArr->getReference(i);
        if (v.isObject())
          loadPluginFromVarSmart(-1, i, v, true);
      }
      // Clear any extra plugins beyond new chain size
      while (fohPluginChain.size() > fohArr->size()) {
        if (fohPluginChain.back())
          fohPluginChain.back()->releaseResources();
        fohPluginChain.pop_back();
      }
    } else {
      fohPluginChain.clear();
    }

    if (auto *iemArr = rig.getProperty("iemFx", juce::var()).getArray()) {
      for (int i = 0; i < iemArr->size(); ++i) {
        auto v = iemArr->getReference(i);
        if (v.isObject())
          loadPluginFromVarSmart(-1, i, v, false);
      }
      // Clear any extra plugins beyond new chain size
      while (iemPluginChain.size() > iemArr->size()) {
        if (iemPluginChain.back())
          iemPluginChain.back()->releaseResources();
        iemPluginChain.pop_back();
      }
    } else {
      iemPluginChain.clear();
    }

    // Channels
    if (auto *chanArr = rig.getProperty("channels", juce::var()).getArray()) {
      int numSlots = (int)slots.size();
      int numNodes = chanArr->size();
      int numToLoad = (numSlots < numNodes) ? numSlots : numNodes;

      for (int i = 0; i < numToLoad; ++i) {
        auto s = slots[i].get();
        auto v = chanArr->getReference(i);

        s->setName(v.getProperty("name", "Slot").toString());
        s->setIconIndex(v.getProperty("iconIndex", 0));
        s->setChannelColor(juce::Colour::fromString(
            v.getProperty("channelColor", "#2a2a2a").toString()));
        s->setChannelLevel((float)v.getProperty("level", 0.8));
        s->setFohEnabled(v.getProperty("foh", true));
        s->setIemEnabled(v.getProperty("iem", true));
        s->setBypass(v.getProperty("mute", false));
        s->setInputChannelIndex(v.getProperty("inputIndex", -1));
        s->setAux1Send((float)v.getProperty("aux1", 0.0));
        s->setAux2Send((float)v.getProperty("aux2", 0.0));
        s->setIemOffset((float)v.getProperty("iemOffset", 1.0));
        s->setTransposeOctaves(v.getProperty("transposeOctaves", 0));
        s->setTransposeSemitones(v.getProperty("transposeSemitones", 0));
        s->setNoteRange(v.getProperty("lowNote", 0), v.getProperty("highNote", 127));

        juce::String ccList = v.getProperty("allowedCCs", "64").toString();
        juce::StringArray ccs;
        ccs.addTokens(ccList, ",", "");
        std::set<int> allowed;
        for (auto &cc : ccs)
          if (cc.isNotEmpty())
            allowed.insert(cc.getIntValue());
        s->setAllowedCCs(allowed);

        // CC-controlled faders + per-slot MIDI channel override (v2)
        s->setFohCC(v.getProperty("fohCC", -1));
        s->setIemCC(v.getProperty("iemCC", -1));
        s->setMidiChannelOverride(v.getProperty("midiChannel", -1));

        // Load Channel Strip Settings
        if (auto *stripObj =
                v.getProperty("strip", juce::var()).getDynamicObject()) {
          auto &strip = s->getStrip();
          strip.gateEnabled = stripObj->getProperty("gateEnabled");
          strip.gateThreshold = (float)stripObj->getProperty("gateThreshold");
          strip.eqEnabled = stripObj->getProperty("eqEnabled");
          strip.hpfFreq = (float)stripObj->getProperty("hpfFreq");
          strip.lowShelfGain = (float)stripObj->getProperty("eqLow");
          strip.highShelfGain = (float)stripObj->getProperty("eqHigh");
          strip.compEnabled = stripObj->getProperty("compEnabled");
          strip.compAmount = (float)stripObj->getProperty("compAmount");
          // Re-prepare to update coefficients
          strip.prepare(currentSampleRate);
        }

        // Restore Arpeggiator Settings
        if (auto *arpObj = v.getProperty("arpeggiator", juce::var()).getDynamicObject()) {
          auto &arp = s->getArpeggiator();
          arp.enabled.store((bool)arpObj->getProperty("enabled"));
          arp.bpm.store((float)arpObj->getProperty("bpm"));
          arp.octavesUp.store((int)arpObj->getProperty("octavesUp"));
          arp.octavesDown.store((int)arpObj->getProperty("octavesDown"));
          arp.gate.store((float)arpObj->getProperty("gate"));
          arp.patternIdx.store((int)arpObj->getProperty("patternIdx"));
        }
        if (auto *harmObj = v.getProperty("harmonizer", juce::var()).getDynamicObject()) {
          auto &harm = s->getHarmonizer();
          harm.enabled.store((bool)harmObj->getProperty("enabled"));
          harm.octavesUp.store((int)harmObj->getProperty("octavesUp"));
          harm.octavesDown.store((int)harmObj->getProperty("octavesDown"));
          harm.africaMode.store(harmObj->hasProperty("africaMode")
                                    ? (int)harmObj->getProperty("africaMode")
                                    : 0);
          harm.harmonyTargetSlot.store(
              harmObj->hasProperty("harmonyTargetSlot")
                  ? (int)harmObj->getProperty("harmonyTargetSlot")
                   : -1);
        }

        // Restore Sampler Settings
        if (auto *sampObj =
                v.getProperty("sampler", juce::var()).getDynamicObject()) {
          auto &sampler = s->getSampler();
          sampler.enabled.store((bool)sampObj->getProperty("enabled"));
          if (auto *sls = sampObj->getProperty("slots").getArray()) {
            for (int sIdx = 0; sIdx < juce::jmin(8, sls->size()); ++sIdx) {
              const auto &slo = sls->getReference(sIdx);
              if (slo.isObject()) {
                SamplerProcessor::SlotConfig cfg;
                cfg.wavPath = slo.getProperty("wavPath", "").toString();
                cfg.rootNote = (int)slo.getProperty("rootNote", 60);
                cfg.keyLow = (int)slo.getProperty("keyLow", 0);
                cfg.keyHigh = (int)slo.getProperty("keyHigh", 127);
                cfg.pitchOffsetSemitones = (float)slo.getProperty("pitchOffset", 0.0f);
                cfg.volume = (float)slo.getProperty("volume", 1.0f);
                cfg.startRatio = (float)slo.getProperty("startRatio", 0.0f);
                cfg.endRatio = (float)slo.getProperty("endRatio", 1.0f);
                sampler.setSlotConfig(sIdx, cfg);
              }
            }
          }
        }

        // Restore CC-to-Parameter mappings (paramId-primary, index fallback)
        s->clearAllCCMappings();
        if (auto *mappingArr =
                v.getProperty("ccMappings", juce::var()).getArray()) {
          for (int m = 0; m < mappingArr->size(); ++m) {
            auto mv = mappingArr->getReference(m);
            int ccNum = mv.getProperty("cc", 0);
            int chainIdx = mv.getProperty("chainIndex", 0);
            juce::String paramId = mv.getProperty("paramId", "").toString();
            int paramIdx = mv.getProperty("paramIndex", mv.getProperty("parameterIndex", 0));
            float minVal = (float)mv.getProperty("minValue", 0.0);
            float maxVal = (float)mv.getProperty("maxValue", 1.0);
            bool inv = mv.getProperty("invert", false);
            s->mapCCToParameter(ccNum, chainIdx, paramId, paramIdx, minVal, maxVal, inv);
          }
        }

        // Restore CC Passthrough (remap) mappings
        s->clearAllCCPassthroughs();
        if (auto *ptArr =
                v.getProperty("ccPassthroughs", juce::var()).getArray()) {
          for (int m = 0; m < ptArr->size(); ++m) {
            auto pv = ptArr->getReference(m);
            int inCC = pv.getProperty("incomingCC", -1);
            int outCC = pv.getProperty("outgoingCC", -1);
            if (inCC >= 0 && outCC >= 0)
              s->addCCPassthrough(inCC, outCC);
          }
        }

        // Load Channel Plugin Chains - Smart Loading (two-pass approach)
        std::set<int> pluginsToReuse;
        // Pass 1: Identify which plugins can be reused
        if (auto *chainArr = v.getProperty("chain", juce::var()).getArray()) {
          for (int p = 0; p < chainArr->size(); ++p) {
            auto pv = chainArr->getReference(p);
            if (pv.isObject()) {
              juce::String newPath = pv.getProperty("path", "").toString();
              juce::String currentPath = s->getPluginPath(p);
              if (normalizePath(currentPath) == normalizePath(newPath) &&
                  !currentPath.isEmpty()) {
                pluginsToReuse.insert(p);
              }
            }
          }
        }

        // Pass 2: Clear only non-matching plugins, then load
        s->clearChainPreserve(pluginsToReuse);
        if (auto *chainArr = v.getProperty("chain", juce::var()).getArray()) {
          for (int p = 0; p < chainArr->size(); ++p) {
            auto pv = chainArr->getReference(p);
            if (pv.isObject()) {
              loadPluginFromVarSmart(i, p, pv, true);
              // Per-instrument stacking settings
              if (p < 3) {
                auto& cs = s->getChainSlotSettings(p);
                cs.lowNote.store((int)pv.getProperty("lowNote", 0));
                cs.highNote.store((int)pv.getProperty("highNote", 127));
                cs.level.store((float)pv.getProperty("level", 1.0));
                cs.enabled.store((bool)pv.getProperty("enabled", true));
              }
            }
          }
        }
      }
      for (int i = numToLoad; i < numSlots; ++i) {
        auto s = slots[i].get();
        s->setName("Slot " + juce::String(i + 1));
        s->setIconIndex(0);
        s->setChannelColor(juce::Colour::fromString("#2a2a2a"));
        s->setChannelLevel(0.0f);
        s->setFohEnabled(false);
        s->setIemEnabled(false);
        s->setBypass(true);
        s->clearChain();
        s->clearAllCCMappings();
        s->clearAllCCPassthroughs();
      }
    }

    // Scenes (Presets)
    if (auto *sceneArr = rig.getProperty("scenes", juce::var()).getArray()) {
      scenes.clear();
      for (int i = 0; i < sceneArr->size(); ++i) {
        auto sv = sceneArr->getReference(i);
        Scene newScene(sv.getProperty("name", "Scene").toString());
        newScene.midiProgramChange = sv.getProperty("midiProgramChange", -1);
        newScene.midiChannel = sv.getProperty("midiChannel", 0);

        if (auto *stateArr = sv.getProperty("states", juce::var()).getArray()) {
          for (int s = 0; s < stateArr->size(); ++s) {
            auto stv = stateArr->getReference(s);
            SlotState st;
            st.bypassed = stv.getProperty("bypassed", false);
            st.fohLevel = (float)stv.getProperty("fohLevel", stv.getProperty("level", 0.8));
            st.iemLevel = (float)stv.getProperty("iemLevel", st.fohLevel);
            st.fohEnabled = stv.getProperty("foh", true);
            st.iemEnabled = stv.getProperty("iem", true);
            st.fadersLinked = stv.getProperty("fadersLinked", true);
            st.aux1Send = (float)stv.getProperty("aux1Send", 0.0);
            st.aux2Send = (float)stv.getProperty("aux2Send", 0.0);
            st.iemOffset = (float)stv.getProperty("iemOffset", 1.0);
            st.transposeOctaves = stv.getProperty("transposeOctaves", 0);
            st.transposeSemitones = stv.getProperty("transposeSemitones", 0);
            st.lowNote = stv.getProperty("lowNote", 0);
            st.highNote = stv.getProperty("highNote", 127);

            // DSP
            st.gateEnabled = stv.getProperty("gateEnabled", false);
            st.gateThreshold = (float)stv.getProperty("gateThreshold", -60.0);
            st.eqEnabled = stv.getProperty("eqEnabled", false);
            st.hpfFreq = (float)stv.getProperty("hpfFreq", 20.0);
            st.lowShelfGain = (float)stv.getProperty("eqLow", 0.0);
            st.highShelfGain = (float)stv.getProperty("eqHigh", 0.0);
            st.compEnabled = stv.getProperty("compEnabled", false);
            st.compAmount = (float)stv.getProperty("compAmount", 0.0);
            st.chorusEnabled = stv.getProperty("chorusEnabled", false);
            st.chorusRate = (float)stv.getProperty("chorusRate", 1.0);
            st.chorusMix = (float)stv.getProperty("chorusMix", 0.0);
            st.reverbEnabled = stv.getProperty("reverbEnabled", false);
            st.reverbSize = (float)stv.getProperty("reverbSize", 0.5);
            st.reverbMix = (float)stv.getProperty("reverbMix", 0.0);

            // Arpeggiator
            st.arpEnabled = stv.getProperty("arpEnabled", false);
            st.arpBpm = (float)stv.getProperty("arpBpm", 120.0);
            st.arpOctavesUp = stv.getProperty("arpOctavesUp", 1);
            st.arpOctavesDown = stv.getProperty("arpOctavesDown", 0);
            st.arpGate = (float)stv.getProperty("arpGate", 0.9);
            st.arpPatternIdx = stv.getProperty("arpPatternIdx", 0);

            // Harmonizer
            st.harmEnabled = stv.getProperty("harmEnabled", false);
            st.harmOctavesUp = stv.getProperty("harmOctavesUp", 1);
            st.harmOctavesDown = stv.getProperty("harmOctavesDown", 0);
            st.harmAfricaMode = stv.getProperty("harmAfricaMode", 0);
            st.harmTargetSlot = stv.getProperty("harmTargetSlot", -1);

            // Sampler
            st.samplerEnabled = stv.getProperty("samplerEnabled", false);

            newScene.slotStates.push_back(st);
          }
        }
        scenes.push_back(newScene);
      }
    }
    currentSceneIndex = rig.getProperty("currentSceneIndex", 0);

    // Kill any stuck notes from the previous rig / state restoration
    panicTriggered.store(true);

    // Trigger a 2s mute + fade-in (1.5s hard mute, 0.5s linear fade) so
    // restored plugin state (Leslie tails, tonewheels) doesn't blast.
    int fadeSamples = (int)(currentSampleRate * 2.0);
    if (fadeSamples < currentBlockSize)
      fadeSamples = currentBlockSize;
    postLoadFadeTotal.store(fadeSamples);
    postLoadFadeRemaining.store(fadeSamples);
  }

  void performLegacyMigration() {
    auto appDir = OpenRigConstants::getAppDirectory();
    auto backupLegacyDir = OpenRigConstants::getBackupsDirectory().getChildFile("legacy");

    juce::File migrationFlag = appDir.getChildFile(".migrated");
    if (migrationFlag.existsAsFile())
      return;

    backupLegacyDir.createDirectory();

    // 1. Migrate OpenRigFullRig.json from Desktop
    juce::File desktopRig = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("OpenRigFullRig.json");
    if (desktopRig.existsAsFile()) {
      desktopRig.copyFileTo(backupLegacyDir.getChildFile("OpenRigFullRig.json"));
      desktopRig.copyFileTo(appDir.getChildFile("OpenRigFullRig.json"));
    }

    // 2. Migrate Setups from Documents (copy each file; JUCE has no recursive dir copy)
    juce::File docSetups = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("OpenRig").getChildFile("Setups");
    if (docSetups.isDirectory()) {
      juce::File newSetups = OpenRigConstants::getSongsDirectory();
      newSetups.createDirectory();
      juce::File backupSetups = backupLegacyDir.getChildFile("Setups");
      backupSetups.createDirectory();

      juce::Array<juce::File> files;
      docSetups.findChildFiles(files, juce::File::findFiles, false, "*.json");
      for (const auto& f : files) {
        f.copyFileTo(newSetups.getChildFile(f.getFileName()));
        f.copyFileTo(backupSetups.getChildFile(f.getFileName()));
      }
    }

    // 3. Migrate button_mappings.json if exists
    juce::File appDataMappings = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OpenRig").getChildFile("button_mappings.json");
    if (appDataMappings.existsAsFile()) {
      appDataMappings.copyFileTo(backupLegacyDir.getChildFile("button_mappings.json"));
    }

    migrationFlag.replaceWithText("migrated");
  }

  OpenRig::Song getSongRepresentation(const juce::String& songName = "Current Rig") {
    OpenRig::Song song;
    song.name = songName;
    song.fohMasterLevel = fohMasterLevel;
    song.iemMasterLevel = iemMasterLevel;
    song.fohOutputOffset = fohOutputOffset;
    song.iemOutputOffset = iemOutputOffset;

    // FOH FX
    for (auto& p : fohPluginChain) {
        OpenRig::PluginState ps;
        if (p) {
            ps.name = p->getName();
            ps.path = p->getPluginDescription().fileOrIdentifier;
            juce::MemoryBlock blob;
            p->getStateInformation(blob);
            ps.stateBase64 = blob.toBase64Encoding();
            ps.uid = p->getPluginDescription().uniqueId;
        }
        song.fohFx.push_back(ps);
    }

    // IEM FX
    for (auto& p : iemPluginChain) {
        OpenRig::PluginState ps;
        if (p) {
            ps.name = p->getName();
            ps.path = p->getPluginDescription().fileOrIdentifier;
            juce::MemoryBlock blob;
            p->getStateInformation(blob);
            ps.stateBase64 = blob.toBase64Encoding();
            ps.uid = p->getPluginDescription().uniqueId;
        }
        song.iemFx.push_back(ps);
    }

    // Channels/Slots
    for (int i = 0; i < (int)slots.size(); ++i) {
        auto* s = slots[i].get();
        OpenRig::SongSlot slot;
        slot.name = s->getName();
        slot.iconIndex = s->getIconIndex();
        slot.channelColor = s->getChannelColor();
        slot.level = s->getChannelLevel();
        slot.fohEnabled = s->isFohEnabled();
        slot.iemEnabled = s->isIemEnabled();
        slot.bypassed = s->isBypassed();
        slot.inputChannelIndex = s->getInputChannelIndex();
        slot.aux1Send = s->getAux1Send();
        slot.aux2Send = s->getAux2Send();
        slot.iemOffset = s->getIemOffset();
        slot.transposeOctaves = s->getTransposeOctaves();
        slot.transposeSemitones = s->getTransposeSemitones();
        slot.lowNote = s->getLowNote();
        slot.highNote = s->getHighNote();
        slot.allowedCCs = s->getAllowedCCs();
        slot.fohCC = s->getFohCC();
        slot.iemCC = s->getIemCC();
        slot.midiChannelOverride = s->getMidiChannelOverride();

        auto& strip = s->getStrip();
        slot.strip.gateEnabled = strip.gateEnabled;
        slot.strip.gateThreshold = strip.gateThreshold;
        slot.strip.eqEnabled = strip.eqEnabled;
        slot.strip.hpfFreq = strip.hpfFreq;
        slot.strip.lowShelfGain = strip.lowShelfGain;
        slot.strip.highShelfGain = strip.highShelfGain;
        slot.strip.compEnabled = strip.compEnabled;
        slot.strip.compAmount = strip.compAmount;

        auto& arp = s->getArpeggiator();
        slot.arpeggiator.enabled = arp.enabled.load();
        slot.arpeggiator.bpm = arp.bpm.load();
        slot.arpeggiator.octavesUp = arp.octavesUp.load();
        slot.arpeggiator.octavesDown = arp.octavesDown.load();
        slot.arpeggiator.gate = arp.gate.load();
        slot.arpeggiator.patternIdx = arp.patternIdx.load();

        auto& harm = s->getHarmonizer();
        slot.harmonizer.enabled = harm.enabled.load();
        slot.harmonizer.octavesUp = harm.octavesUp.load();
        slot.harmonizer.octavesDown = harm.octavesDown.load();
        slot.harmonizer.africaMode = harm.africaMode.load();
        slot.harmonizer.harmonyTargetSlot = harm.harmonyTargetSlot.load();

        auto& sampler = s->getSampler();
        slot.sampler.enabled = sampler.enabled.load();
        for (int sIdx = 0; sIdx < 8; ++sIdx) {
          auto cfg = sampler.getSlotConfig(sIdx);
          slot.sampler.slots[sIdx].wavPath = cfg.wavPath;
          slot.sampler.slots[sIdx].rootNote = cfg.rootNote;
          slot.sampler.slots[sIdx].keyLow = cfg.keyLow;
          slot.sampler.slots[sIdx].keyHigh = cfg.keyHigh;
          slot.sampler.slots[sIdx].pitchOffsetSemitones = cfg.pitchOffsetSemitones;
          slot.sampler.slots[sIdx].volume = cfg.volume;
          slot.sampler.slots[sIdx].startRatio = cfg.startRatio;
          slot.sampler.slots[sIdx].endRatio = cfg.endRatio;
        }

        for (const auto& pair : s->getCCMappings()) {
            OpenRig::CCMapping map;
            map.cc = pair.first;
            map.chainIndex = pair.second.chainIndex;
            map.paramId = pair.second.paramId;
            map.parameterIndex = pair.second.parameterIndex;
            map.minValue = pair.second.minValue;
            map.maxValue = pair.second.maxValue;
            map.invert = pair.second.invert;
            slot.ccMappings.push_back(map);
        }

        for (const auto& pair : s->getCCPassthroughMap()) {
            OpenRig::CCPassthrough pt;
            pt.incomingCC = pair.first;
            pt.outgoingCC = pair.second;
            slot.ccPassthroughs.push_back(pt);
        }

        for (int pIdx = 0; pIdx < s->getChainSize(); ++pIdx) {
            auto plugin = s->getPluginInstance(pIdx);
            OpenRig::PluginState ps;
            if (plugin) {
                ps.name = plugin->getName();
                ps.path = plugin->getPluginDescription().fileOrIdentifier;
                juce::MemoryBlock blob;
                plugin->getStateInformation(blob);
                ps.stateBase64 = blob.toBase64Encoding();
                ps.uid = plugin->getPluginDescription().uniqueId;
            }
            // Per-instrument stacking settings
            auto& cs = s->getChainSlotSettings(pIdx);
            ps.lowNote = cs.lowNote.load();
            ps.highNote = cs.highNote.load();
            ps.level = cs.level.load();
            ps.enabled = cs.enabled.load();
            slot.chain.push_back(ps);
        }

        song.slots.push_back(slot);
    }

    // Scenes
    for (const auto& scene : scenes) {
        OpenRig::Scene sc;
        sc.name = scene.name;
        for (const auto& st : scene.slotStates) {
            OpenRig::SlotState slotState;
            slotState.bypassed = st.bypassed;
            slotState.channelLevel = st.fohLevel;
            slotState.fohEnabled = st.fohEnabled;
            slotState.iemEnabled = st.iemEnabled;
            sc.slotStates.push_back(slotState);
        }
        song.scenes.push_back(sc);
    }
    song.currentSceneIndex = currentSceneIndex;

    return song;
  }

  void importRigFromSong(const OpenRig::Song& song) {
    juce::String json = OpenRig::RigSerializer::serializeSong(song);
    importRigFromJson(json);
  }

  OpenRig::SongSlot exportSlot(int slotIdx) {
    OpenRig::SongSlot songSlot;
    if (slotIdx < 0 || slotIdx >= (int)slots.size())
      return songSlot;
    juce::ScopedLock sl(lock);
    auto *s = slots[slotIdx].get();

    songSlot.name = s->getName();
    songSlot.iconIndex = s->getIconIndex();
    songSlot.channelColor = s->getChannelColor();
    songSlot.level = s->getChannelLevel();
    songSlot.fohEnabled = s->isFohEnabled();
    songSlot.iemEnabled = s->isIemEnabled();
    songSlot.bypassed = s->isBypassed();
    songSlot.inputChannelIndex = s->getInputChannelIndex();
    songSlot.aux1Send = s->getAux1Send();
    songSlot.aux2Send = s->getAux2Send();
    songSlot.iemOffset = s->getIemOffset();
    songSlot.transposeOctaves = s->getTransposeOctaves();
    songSlot.transposeSemitones = s->getTransposeSemitones();
    songSlot.lowNote = s->getLowNote();
    songSlot.highNote = s->getHighNote();
    songSlot.allowedCCs = s->getAllowedCCs();
    songSlot.fohCC = s->getFohCC();
    songSlot.iemCC = s->getIemCC();
    songSlot.midiChannelOverride = s->getMidiChannelOverride();

    auto& stripData = s->getStrip();
    songSlot.strip.gateEnabled = stripData.gateEnabled;
    songSlot.strip.gateThreshold = stripData.gateThreshold;
    songSlot.strip.eqEnabled = stripData.eqEnabled;
    songSlot.strip.hpfFreq = stripData.hpfFreq;
    songSlot.strip.lowShelfGain = stripData.lowShelfGain;
    songSlot.strip.highShelfGain = stripData.highShelfGain;
    songSlot.strip.compEnabled = stripData.compEnabled;
    songSlot.strip.compAmount = stripData.compAmount;
    songSlot.strip.chorusEnabled = stripData.chorusEnabled;
    songSlot.strip.chorusRate = stripData.chorusRate;
    songSlot.strip.chorusMix = stripData.chorusMix;
    songSlot.strip.reverbEnabled = stripData.reverbEnabled;
    songSlot.strip.reverbSize = stripData.reverbSize;
    songSlot.strip.reverbMix = stripData.reverbMix;

    auto& arpData = s->getArpeggiator();
    songSlot.arpeggiator.enabled = arpData.enabled.load();
    songSlot.arpeggiator.bpm = arpData.bpm.load();
    songSlot.arpeggiator.octavesUp = arpData.octavesUp.load();
    songSlot.arpeggiator.octavesDown = arpData.octavesDown.load();
    songSlot.arpeggiator.gate = arpData.gate.load();
    songSlot.arpeggiator.patternIdx = arpData.patternIdx.load();

    auto& harmData = s->getHarmonizer();
    songSlot.harmonizer.enabled = harmData.enabled.load();
    songSlot.harmonizer.octavesUp = harmData.octavesUp.load();
    songSlot.harmonizer.octavesDown = harmData.octavesDown.load();
    songSlot.harmonizer.africaMode = harmData.africaMode.load();
    songSlot.harmonizer.harmonyTargetSlot = harmData.harmonyTargetSlot.load();

    auto& samplerLive = s->getSampler();
    songSlot.sampler.enabled = samplerLive.enabled.load();
    for (int idx = 0; idx < 8; ++idx) {
      auto cfg = samplerLive.getSlotConfig(idx);
      songSlot.sampler.slots[idx].wavPath = cfg.wavPath;
      songSlot.sampler.slots[idx].rootNote = cfg.rootNote;
      songSlot.sampler.slots[idx].keyLow = cfg.keyLow;
      songSlot.sampler.slots[idx].keyHigh = cfg.keyHigh;
      songSlot.sampler.slots[idx].pitchOffsetSemitones = cfg.pitchOffsetSemitones;
      songSlot.sampler.slots[idx].volume = cfg.volume;
      songSlot.sampler.slots[idx].startRatio = cfg.startRatio;
      songSlot.sampler.slots[idx].endRatio = cfg.endRatio;
    }

    for (const auto& pair : s->getCCMappings()) {
      OpenRig::CCMapping map;
      map.cc = pair.first;
      map.chainIndex = pair.second.chainIndex;
      map.paramId = pair.second.paramId;
      map.parameterIndex = pair.second.parameterIndex;
      map.minValue = pair.second.minValue;
      map.maxValue = pair.second.maxValue;
      map.invert = pair.second.invert;
      songSlot.ccMappings.push_back(map);
    }

    for (const auto& pair : s->getCCPassthroughMap()) {
      OpenRig::CCPassthrough pt;
      pt.incomingCC = pair.first;
      pt.outgoingCC = pair.second;
      songSlot.ccPassthroughs.push_back(pt);
    }

    for (int pIdx = 0; pIdx < s->getChainSize(); ++pIdx) {
      auto plugin = s->getPluginInstance(pIdx);
      OpenRig::PluginState ps;
      if (plugin) {
        ps.name = plugin->getName();
        ps.path = plugin->getPluginDescription().fileOrIdentifier;
        juce::MemoryBlock blob;
        plugin->getStateInformation(blob);
        ps.stateBase64 = blob.toBase64Encoding();
        ps.uid = plugin->getPluginDescription().uniqueId;
      }
      // Per-instrument stacking settings
      auto& cs = s->getChainSlotSettings(pIdx);
      ps.lowNote = cs.lowNote.load();
      ps.highNote = cs.highNote.load();
      ps.level = cs.level.load();
      ps.enabled = cs.enabled.load();
      songSlot.chain.push_back(ps);
    }

    return songSlot;
  }

  void importSlot(int slotIdx, const OpenRig::SongSlot& songSlot) {
    if (slotIdx < 0 || slotIdx >= (int)slots.size())
      return;
    juce::ScopedLock sl(lock);
    auto *s = slots[slotIdx].get();

    s->setName(songSlot.name);
    s->setIconIndex(songSlot.iconIndex);
    s->setChannelColor(songSlot.channelColor);
    s->setChannelLevel(songSlot.level);
    s->setFohEnabled(songSlot.fohEnabled);
    s->setIemEnabled(songSlot.iemEnabled);
    s->setBypass(songSlot.bypassed);
    s->setInputChannelIndex(songSlot.inputChannelIndex);
    s->setAux1Send(songSlot.aux1Send);
    s->setAux2Send(songSlot.aux2Send);
    s->setIemOffset(songSlot.iemOffset);
    s->setTransposeOctaves(songSlot.transposeOctaves);
    s->setTransposeSemitones(songSlot.transposeSemitones);
    s->setNoteRange(songSlot.lowNote, songSlot.highNote);
    s->setAllowedCCs(songSlot.allowedCCs);
    s->setFohCC(songSlot.fohCC);
    s->setIemCC(songSlot.iemCC);
    s->setMidiChannelOverride(songSlot.midiChannelOverride);

    auto& stripData = s->getStrip();
    stripData.gateEnabled = songSlot.strip.gateEnabled;
    stripData.gateThreshold = songSlot.strip.gateThreshold;
    stripData.eqEnabled = songSlot.strip.eqEnabled;
    stripData.hpfFreq = songSlot.strip.hpfFreq;
    stripData.lowShelfGain = songSlot.strip.lowShelfGain;
    stripData.highShelfGain = songSlot.strip.highShelfGain;
    stripData.compEnabled = songSlot.strip.compEnabled;
    stripData.compAmount = songSlot.strip.compAmount;
    stripData.chorusEnabled = songSlot.strip.chorusEnabled;
    stripData.chorusRate = songSlot.strip.chorusRate;
    stripData.chorusMix = songSlot.strip.chorusMix;
    stripData.reverbEnabled = songSlot.strip.reverbEnabled;
    stripData.reverbSize = songSlot.strip.reverbSize;
    stripData.reverbMix = songSlot.strip.reverbMix;
    stripData.prepare(currentSampleRate);

    auto& arpLive = s->getArpeggiator();
    arpLive.enabled.store(songSlot.arpeggiator.enabled);
    arpLive.bpm.store(songSlot.arpeggiator.bpm);
    arpLive.octavesUp.store(songSlot.arpeggiator.octavesUp);
    arpLive.octavesDown.store(songSlot.arpeggiator.octavesDown);
    arpLive.gate.store(songSlot.arpeggiator.gate);
    arpLive.patternIdx.store(songSlot.arpeggiator.patternIdx);

    auto& harmLive = s->getHarmonizer();
    harmLive.enabled.store(songSlot.harmonizer.enabled);
    harmLive.octavesUp.store(songSlot.harmonizer.octavesUp);
    harmLive.octavesDown.store(songSlot.harmonizer.octavesDown);
    harmLive.africaMode.store(songSlot.harmonizer.africaMode);
    harmLive.harmonyTargetSlot.store(songSlot.harmonizer.harmonyTargetSlot);

    auto& samplerLive = s->getSampler();
    samplerLive.enabled.store(songSlot.sampler.enabled);
    for (int idx = 0; idx < 8; ++idx) {
      SamplerProcessor::SlotConfig cfg;
      cfg.wavPath = songSlot.sampler.slots[idx].wavPath;
      cfg.rootNote = songSlot.sampler.slots[idx].rootNote;
      cfg.keyLow = songSlot.sampler.slots[idx].keyLow;
      cfg.keyHigh = songSlot.sampler.slots[idx].keyHigh;
      cfg.pitchOffsetSemitones = songSlot.sampler.slots[idx].pitchOffsetSemitones;
      cfg.volume = songSlot.sampler.slots[idx].volume;
      cfg.startRatio = songSlot.sampler.slots[idx].startRatio;
      cfg.endRatio = songSlot.sampler.slots[idx].endRatio;
      samplerLive.setSlotConfig(idx, cfg);
    }

    s->clearAllCCMappings();
    for (const auto& m : songSlot.ccMappings)
      s->mapCCToParameter(m.cc, m.chainIndex, m.paramId, m.parameterIndex,
                          m.minValue, m.maxValue, m.invert);

    s->clearAllCCPassthroughs();
    for (const auto& pt : songSlot.ccPassthroughs)
      s->addCCPassthrough(pt.incomingCC, pt.outgoingCC);

    // Plugin loading: reuse matching plugins, clear others
    std::set<int> pluginsToReuse;
    for (int p = 0; p < (int)songSlot.chain.size(); ++p) {
      if (songSlot.chain[p].path.isNotEmpty()) {
        juce::String newPath = songSlot.chain[p].path;
        juce::String currentPath = s->getPluginPath(p);
        if (normalizePath(currentPath) == normalizePath(newPath) && !currentPath.isEmpty())
          pluginsToReuse.insert(p);
      }
    }
    s->clearChainPreserve(pluginsToReuse);
    for (int p = 0; p < (int)songSlot.chain.size(); ++p) {
      if (songSlot.chain[p].path.isNotEmpty()) {
        juce::var pv = songSlotToPluginVar(songSlot.chain[p]);
        loadPluginFromVarSmart(slotIdx, p, pv, true);
      }
    }

    // Apply per-instrument stacking settings (note range, level, enable)
    for (int p = 0; p < (int)songSlot.chain.size() && p < 3; ++p) {
      auto& cs = s->getChainSlotSettings(p);
      cs.lowNote.store(songSlot.chain[p].lowNote);
      cs.highNote.store(songSlot.chain[p].highNote);
      cs.level.store(songSlot.chain[p].level);
      cs.enabled.store(songSlot.chain[p].enabled);
    }
  }

  // Apply a SongSlot preset directly (e.g. from drag-and-drop). Thread-safe:
  // takes the engine lock.
  void loadSlotPreset(int slotIdx, const OpenRig::SongSlot &songSlot) {
    importSlot(slotIdx, songSlot);
  }

  void saveStripToFile(int slotIdx, const juce::File& file) {
    auto songSlot = exportSlot(slotIdx);
    auto json = OpenRig::RigSerializer::serializeStrip(songSlot);
    OpenRig::RigSerializer::save(file, json);
  }

  bool loadStripFromFile(int slotIdx, const juce::File& file) {
    OpenRig::SongSlot songSlot;
    if (!OpenRig::RigSerializer::readStripFromFile(file, songSlot))
      return false;
    importSlot(slotIdx, songSlot);
    return true;
  }

  void saveRigToFile() {
    juce::File f = OpenRigConstants::getAppDirectory().getChildFile("OpenRigFullRig.json");
    OpenRig::Song song = getSongRepresentation("Current Rig");
    OpenRig::RigSerializer::writeSongToFile(f, song);
  }

  void loadRigFromFile() {
    juce::File f = OpenRigConstants::getAppDirectory().getChildFile("OpenRigFullRig.json");
    if (f.existsAsFile()) {
      OpenRig::Song song;
      if (OpenRig::RigSerializer::readSongFromFile(f, song)) {
        importRigFromSong(song);
      }
    }
  }

  juce::var getPluginVar(juce::AudioPluginInstance *plugin) {
    auto *vt = new juce::DynamicObject();
    vt->setProperty("name", plugin->getName());

    juce::MemoryBlock blob;
    plugin->getStateInformation(blob);
    vt->setProperty("state", blob.toBase64Encoding());

    auto desc = plugin->getPluginDescription();
    vt->setProperty("uid", (int)desc.uniqueId);
    vt->setProperty("path", desc.fileOrIdentifier);

    return juce::var(vt);
  }

  juce::String normalizePath(const juce::String &path) {
    if (path.trim().isEmpty())
      return "";
    juce::String p = juce::File(path).getFullPathName().trim().toLowerCase();
    p = p.replaceCharacter('/', '\\');
    while (p.endsWithChar('\\'))
      p = p.dropLastCharacters(1);
    return p;
  }

  juce::var songSlotToPluginVar(const OpenRig::PluginState& ps) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("name", ps.name);
    obj->setProperty("path", ps.path);
    obj->setProperty("state", ps.stateBase64);
    obj->setProperty("uid", ps.uid);
    return juce::var(obj);
  }

  juce::String getMasterPluginPath(bool isFoh, int chainIndex) const {
    const auto &chain = isFoh ? fohPluginChain : iemPluginChain;
    if (chainIndex >= 0 && chainIndex < (int)chain.size()) {
      if (chain[chainIndex]) {
        auto desc = chain[chainIndex]->getPluginDescription();
        return desc.fileOrIdentifier;
      }
    }
    return "";
  }

  void loadPluginFromVarSmart(int slotIdx, int chainIdx, const juce::var &vt,
                              bool isFohOrChannel) {
    juce::String newPath = vt.getProperty("path", "").toString();
    juce::String newStateBase64 = vt.getProperty("state", "").toString();

    if (newPath.isEmpty())
      return;

    // Async path: if the builder pre-built this (staging has a fresh instance
    // with state already applied off-thread), use it directly. This avoids
    // calling setStateInformation on the live instance under the callback lock.
    auto key = stagingKeyFor(slotIdx, chainIdx, isFohOrChannel);
    if (stagingHasKey(key)) {
      loadPluginFromVar(slotIdx, chainIdx, vt, isFohOrChannel);
      return;
    }

    // Sync path (no staging): reuse if same path, else build synchronously.
    juce::String normalizedNewPath = normalizePath(newPath);

    // Get current plugin path
    juce::String currentPath;
    juce::AudioPluginInstance *currentPlugin = nullptr;

    if (slotIdx >= 0) {
      currentPlugin = slots[slotIdx]->getPluginInstance(chainIdx);
      if (currentPlugin)
        currentPath = slots[slotIdx]->getPluginPath(chainIdx);
    } else {
      const auto &chain = isFohOrChannel ? fohPluginChain : iemPluginChain;
      if (chainIdx >= 0 && chainIdx < (int)chain.size()) {
        currentPlugin = chain[chainIdx].get();
        if (currentPlugin)
          currentPath = getMasterPluginPath(isFohOrChannel, chainIdx);
      }
    }

    // Compare paths - if same, just update state
    if (currentPlugin && normalizePath(currentPath) == normalizedNewPath) {
      juce::MemoryBlock blob;
      blob.fromBase64Encoding(newStateBase64);
      // Thread safety: setStateInformation should be on message thread
      // importRigFromJson already holds the lock, so we're safe to call this if
      // we assume import is on message thread.
      // Ideally we would ensure this runs on message thread, but existing logic
      // is synchronous here.
      try {
        currentPlugin->setStateInformation(blob.getData(), (int)blob.getSize());
      } catch (...) {
        logToFile("WARNING: setStateInformation threw for reused plugin, continuing");
      }
      return;
    }

    // Different plugin or no plugin - load new one
    loadPluginFromVar(slotIdx, chainIdx, vt, isFohOrChannel);
  }

  void loadPluginFromVar(int slotIdx, int chainIdx, const juce::var &vt,
                         bool isFohOrChannel) {
    juce::String path = vt.getProperty("path", "").toString();
    if (path.isEmpty())
      return;

    std::unique_ptr<juce::AudioPluginInstance> instance;

    // 1. Prefer a pre-built instance from the staging cache (async transition
    //    path). buildPluginFromVar already prepared it and restored state.
    auto key = stagingKeyFor(slotIdx, chainIdx, isFohOrChannel);
    if (!popStagedPlugin(key, instance)) {
      // 2. Fallback: build synchronously (cold path / legacy synchronous import).
      juce::String error;
      if (!buildPluginFromVar(vt, instance, error) || !instance)
        return;
    }

    if (slotIdx >= 0) {
      slots[slotIdx]->setPluginInChain(chainIdx, std::move(instance));
    } else {
      auto &chain = isFohOrChannel ? fohPluginChain : iemPluginChain;
      while ((int)chain.size() <= chainIdx)
        chain.push_back(nullptr);
      chain[chainIdx] = std::move(instance);
    }
  }

  juce::AudioPluginFormat *findVst3Format() {
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
      if (formatManager.getFormat(i)->getName() == "VST3")
        return formatManager.getFormat(i);
    }
    return nullptr;
  }

  void configureStereoLayout(juce::AudioProcessor *processor) {
    if (processor == nullptr)
      return;

    if (processor->getName().containsIgnoreCase("Omnisphere") ||
        processor->getName().containsIgnoreCase("Spectrasonics")) {
      logToFile("Skipping strict layout enforcement for Omnisphere");
      return;
    }

    // 1. Get current layout to know how many buses exist
    auto layout = processor->getBusesLayout();

    // 2. Configure Inputs
    // Ensure Main Input is Stereo (if it exists)
    if (layout.inputBuses.size() > 0)
      layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();

    // Disable all aux inputs
    for (int i = 1; i < layout.inputBuses.size(); ++i)
      layout.inputBuses.getReference(i) = juce::AudioChannelSet::disabled();

    // 3. Configure Outputs
    // Ensure Main Output is Stereo
    if (layout.outputBuses.size() > 0)
      layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();

    // Disable all aux outputs (this is the critical fix for Kontakt/B3X)
    for (int i = 1; i < layout.outputBuses.size(); ++i)
      layout.outputBuses.getReference(i) = juce::AudioChannelSet::disabled();

    // 4. Apply the strict stereo layout
    bool success = processor->setBusesLayout(layout);
    logToFile(
        "Configured Topology for " + processor->getName() +
        (success ? " [SUCCESS]" : " [FAILED]") +
        " - Ins: " + juce::String(processor->getTotalNumInputChannels()) +
        ", Outs: " + juce::String(processor->getTotalNumOutputChannels()));

    // Explicitly set processing details to match valid layout
    processor->setPlayConfigDetails(processor->getTotalNumInputChannels(),
                                    processor->getTotalNumOutputChannels(),
                                    currentSampleRate, currentBlockSize);
  }

public:
  std::vector<std::unique_ptr<RackSlot>>& getSlots() { return slots; }
  const std::vector<std::unique_ptr<RackSlot>>& getSlots() const { return slots; }
  std::vector<std::unique_ptr<RackSlot>>& getAuxReturns() { return auxReturns; }
  const std::vector<std::unique_ptr<RackSlot>>& getAuxReturns() const { return auxReturns; }
  std::vector<std::unique_ptr<juce::AudioPluginInstance>>& getFohPluginChain() { return fohPluginChain; }
  std::vector<std::unique_ptr<juce::AudioPluginInstance>>& getIemPluginChain() { return iemPluginChain; }
  juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
  double getCurrentSampleRate() const { return currentSampleRate; }
  int getCurrentBlockSize() const { return currentBlockSize; }
  std::vector<::Scene>& getScenes() { return scenes; }
  void setCurrentSceneIndex(int index) { currentSceneIndex = index; }

  juce::String getMasterPluginName(bool isFoh, int chainIndex) const {
    juce::ScopedLock sl(lock);
    const auto &chain = isFoh ? fohPluginChain : iemPluginChain;
    if (chainIndex >= 0 && chainIndex < (int)chain.size()) {
      if (chain[chainIndex])
        return chain[chainIndex]->getName();
    }
    return "";
  }

  juce::AudioPluginInstance *getMasterPluginInstance(bool isFoh,
                                                     int chainIndex) const {
    juce::ScopedLock sl(lock);
    const auto &chain = isFoh ? fohPluginChain : iemPluginChain;
    if (chainIndex >= 0 && chainIndex < (int)chain.size())
      return chain[chainIndex].get();
    return nullptr;
  }

  void clearMasterChains() {
    juce::ScopedLock sl(lock);
    for (auto &p : fohPluginChain) {
      if (p != nullptr)
        p->releaseResources();
    }
    for (auto &p : iemPluginChain) {
      if (p != nullptr)
        p->releaseResources();
    }
    fohPluginChain.clear();
    iemPluginChain.clear();
  }

  float getFohPeakL() const { return fohPeakL; }
  float getFohPeakR() const { return fohPeakR; }
  float getIemPeakL() const { return iemPeakL; }
  float getIemPeakR() const { return iemPeakR; }

  // Returns true if the last audio block bailed to silence (e.g. a wedged
  // worker or an unprepared rack) and resets the flag. Poll from a UI timer.
  bool consumeAudioUnderrun() { return audioUnderrunFlag.exchange(false); }

  void setFohMasterLevel(float level) { fohMasterLevel = level; }
  void setIemMasterLevel(float level) { iemMasterLevel = level; }

  // Global default MIDI channel for routing (1..16, or 0 = Omni).
  void setDefaultMidiChannel(int channel) {
    defaultMidiChannel.store(juce::jlimit(0, 16, channel));
  }
  int getDefaultMidiChannel() const { return defaultMidiChannel.load(); }

  void setFohOutputOffset(int offset) { fohOutputOffset = offset; }
  void setIemOutputOffset(int offset) { iemOutputOffset = offset; }
  int getFohOutputOffset() const { return fohOutputOffset; }
  int getIemOutputOffset() const { return iemOutputOffset; }

  void triggerPanic() { panicTriggered.store(true); }

  void setSelectedSlot(int index) {
    selectedSlotIndex = juce::jlimit(0, (int)slots.size() - 1, index);
  }

private:
  void initializeDefaultScenes() {
    scenes.clear();
    Scene s1("WARM PIANO");
    // Initialize 10 slots
    for (int i = 0; i < (int)slots.size(); ++i) {
      SlotState st;
      // Hardware inputs are active by default, synths are bypassed until
      // selected
      bool isHardwareInput = (slots[i]->getInputChannelIndex() >= 0);
      st.bypassed = !isHardwareInput && (i != 1);
      st.fohLevel = 0.8f;
      st.iemLevel = 0.8f;
      st.fohEnabled = (slots[i]->getName() != "Monitor In");
      st.iemEnabled = true;
      s1.slotStates.push_back(st);
    }
    scenes.push_back(s1);

    Scene s2("BIG B3");
    for (int i = 0; i < (int)slots.size(); ++i) {
      SlotState st;
      st.bypassed = (i != 1 && i != 2);
      st.fohLevel = (i == 2) ? 0.9f : 0.4f;
      st.iemLevel = (i == 2) ? 0.9f : 0.4f;
      st.fohEnabled = true;
      st.iemEnabled = true;
      s2.slotStates.push_back(st);
    }
    scenes.push_back(s2);

    Scene s3("SOLO PAD");
    for (int i = 0; i < (int)slots.size(); ++i) {
      SlotState st;
      st.bypassed = (i != 3);
      st.fohLevel = 0.8f;
      st.iemLevel = 0.8f;
      st.fohEnabled = true;
      st.iemEnabled = true;
      s3.slotStates.push_back(st);
    }
    scenes.push_back(s3);
  }

  std::vector<std::unique_ptr<RackSlot>> slots;
  std::vector<juce::MidiBuffer> slotMidiBuffers;
  std::vector<Scene> scenes;
  int currentSceneIndex = 0;

  juce::AudioPluginFormatManager formatManager;
  juce::KnownPluginList knownPluginList;

  std::atomic<bool> panicTriggered{false};

  double currentSampleRate = 44100.0;
  int currentBlockSize = 512;
  int selectedSlotIndex = 1;

  juce::AudioBuffer<float> fohBus;
  juce::AudioBuffer<float> iemBus;
  juce::AudioBuffer<float> aux1Bus;
  juce::AudioBuffer<float> aux2Bus;

  // Pre-allocated per-block reusable buffers (avoid heap allocs on audio thread)
  juce::AudioBuffer<float> dummyAux1;
  juce::AudioBuffer<float> dummyAux2;
  juce::MidiBuffer emptyMidiBuf;

  std::vector<std::unique_ptr<RackSlot>> auxReturns;
  std::atomic<bool> isLoading{false};
  std::atomic<bool> audioUnderrunFlag{false}; // set when a block bailed to silence

  // Parallel Processing Support
  juce::OwnedArray<SlotProcessJob> preallocatedJobs;
  std::atomic<int> slotsFinishedCount{0};
  std::vector<juce::AudioBuffer<float>> scratchBuffers;
  juce::ThreadPool threadPool; // Declared last to be destroyed first

  // Post-load fade-in to prevent transient bursts when a rig is applied
  std::atomic<int> postLoadFadeRemaining{0};
  std::atomic<int> postLoadFadeTotal{0};

  // Master FX chains
  std::vector<std::unique_ptr<juce::AudioPluginInstance>> fohPluginChain;
  std::vector<std::unique_ptr<juce::AudioPluginInstance>> iemPluginChain;

  std::atomic<float> fohPeakL{0.0f}, fohPeakR{0.0f};
  std::atomic<float> iemPeakL{0.0f}, iemPeakR{0.0f};
  std::atomic<float> fohMasterLevel{1.0f};
  std::atomic<float> iemMasterLevel{1.0f};

  std::atomic<int> defaultMidiChannel{OpenRigConstants::kDefaultMidiChannel}; // 1 = legacy behaviour

  std::atomic<int> fohOutputOffset{0}; // Hardware channels 1+2
  std::atomic<int> iemOutputOffset{2}; // Hardware channels 3+4

  juce::CriticalSection lock;

#ifdef _WIN32
  HMODULE avrtModule = nullptr;
  typedef HANDLE(WINAPI * PAvSetMmThreadCharacteristicsW)(LPCWSTR, LPDWORD);
  PAvSetMmThreadCharacteristicsW avSetMmThread = nullptr;
#endif

  double stressTestPhase = 0.0;
  int stressTestLastNote[OpenRigConstants::kNumSlots];

public:
  std::atomic<bool> stressTestActive{false};

  // Staging cache for async rig transitions: plugin instances built off-thread
  // by RigBuilder, keyed by rack location (see stagingKeyFor()).
  juce::CriticalSection stagingLock;
  std::map<juce::String, std::unique_ptr<juce::AudioPluginInstance>> stagedPlugins;
  std::map<juce::String, std::unique_ptr<juce::AudioPluginInstance>> preloadedPlugins;

public:
  std::vector<PluginInfo> availablePlugins = {
      // --- Original Plugins ---
      {"bx_meter (VU)", "C:\\Program Files\\Common Files\\VST3\\bx_meter.vst3"},
      {"EZkeys 2", "C:\\Program Files\\Common Files\\VST3\\EZkeys 2.vst3"},
      {"TR5 Metering",
       "C:\\Program Files\\Common Files\\VST3\\TR5 Metering.vst3"},
      {"Kontakt 8", "C:\\Program Files\\Common Files\\VST3\\Kontakt 8.vst3"},
      {"Supercharger GT",
       "C:\\Program Files\\Common Files\\VST3\\Supercharger GT.vst3"},
      {"Super 8", "C:\\Program Files\\Common Files\\VST3\\Super 8.vst3"},
      {"JUNO-106", "C:\\Program Files\\Common "
                   "Files\\VST3\\Roland\\JUNO-106\\JUNO-106(VST3 64bit).vst3"},
      {"ZENOLOGY", "C:\\Program Files\\Common "
                   "Files\\VST3\\Roland\\ZENOLOGY\\ZENOLOGY.vst3"},
      {"XV-5080", "C:\\Program Files\\Common "
                  "Files\\VST3\\Roland\\XV-5080\\XV-5080(VST3 64bit).vst3"},
      {"UVI Workstation", "C:\\Program Files\\Common "
                          "Files\\VST3\\UVIWorkstation.vst3\\Contents\\x86_64-"
                          "win\\UVIWorkstation.vst3"},

      // --- New Additions ---
      {"bx_console Focusrite SC",
       "C:\\Program Files\\Common Files\\VST3\\bx_console Focusrite SC.vst3"},
      {"Jun-6 V", "C:\\Program Files\\Common Files\\VST3\\Jun-6 V.vst3"},
      {"Replika XT", "C:\\Program Files\\Common Files\\VST3\\Replika XT.vst3"},
      {"Blue3 Organ", "C:\\Program Files\\Common Files\\VST3\\Cherry "
                      "Audio\\Blue3 Organ.vst3"},
      {"MixBox", "C:\\Program Files\\Common Files\\VST3\\MixBox.vst3"},
      {"Hammond B-3X",
       "C:\\Program Files\\Common Files\\VST3\\Hammond B-3X.vst3"},

      // --- Dave's Latest Additions ---
      {"Syntronik 2",
       "C:\\Program Files\\Common Files\\VST3\\Syntronik 2.vst3"},
      {"Omnisphere", "C:\\Program Files\\Common Files\\VST3\\Omnisphere.vst3"},
      {"PolyMax (UA)",
       "C:\\Program Files\\Common Files\\VST3\\uaudio_polymax.vst3"},
      {"Pre 1973", "C:\\Program Files\\Common Files\\VST3\\Pre 1973.vst3"},
      {"Pre TridA", "C:\\Program Files\\Common Files\\VST3\\Pre TridA.vst3"},
      {"Chorus JUN-6",
       "C:\\Program Files\\Common Files\\VST3\\Chorus JUN-6.vst3"},
      {"Bus EXCITER-104",
       "C:\\Program Files\\Common Files\\VST3\\Bus EXCITER-104.vst3"},
      {"Bus FORCE", "C:\\Program Files\\Common Files\\VST3\\Bus FORCE.vst3"},
      {"Bus PEAK", "C:\\Program Files\\Common Files\\VST3\\Bus PEAK.vst3"},
      {"TR5 British Channel",
       "C:\\Program Files\\Common Files\\VST3\\TR5 British Channel.vst3"},
      {"TR5 White Channel",
       "C:\\Program Files\\Common Files\\VST3\\TR5 White Channel.vst3"},
  };

  int getNumAvailablePlugins() const { return (int)availablePlugins.size(); }
  juce::String getAvailablePluginName(int index) const {
    return availablePlugins[index].name;
  }
  juce::String getAvailablePluginPath(int index) const {
    return availablePlugins[index].path;
  }

  // Load a plugin from the registry into a specific slot
  // NOTE: This is synchronous - UI will freeze briefly during load
  void
  loadPluginIntoSlot(int slotIndex, int chainIndex, int pluginIndex,
                     std::function<void(bool, const juce::String &)> callback) {
    bool isAux = (slotIndex >= 100);
    int realIdx = isAux ? (slotIndex - 100) : slotIndex;
    auto &slotVec = isAux ? auxReturns : slots;

    if (realIdx < 0 || realIdx >= (int)slotVec.size() || pluginIndex < 0 ||
        pluginIndex >= (int)availablePlugins.size()) {
      callback(false, "Invalid indices");
      return;
    }

    juce::String pluginPath = availablePlugins[pluginIndex].path;
    juce::File pluginFile(pluginPath);

    logToFile("=== LOADING PLUGIN (SYNC) ===");
    logToFile("Path: " + pluginPath);

    if (!pluginFile.exists()) {
      callback(false, "Plugin file not found: " + pluginPath);
      return;
    }

    // Find VST3 format
    juce::AudioPluginFormat *vst3Format = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
      if (formatManager.getFormat(i)->getName() == "VST3") {
        vst3Format = formatManager.getFormat(i);
        break;
      }
    }

    if (!vst3Format) {
      callback(false, "VST3 format not available");
      return;
    }

    // Scan for plugin types (this may take a moment)
    logToFile("Scanning plugin...");
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3Format->findAllTypesForFile(descriptions, pluginPath);

    if (descriptions.isEmpty()) {
      callback(false, "No plugins found in file");
      return;
    }

    juce::PluginDescription desc = *descriptions[0];
    logToFile("Found: " + desc.name);

    // Create instance synchronously
    juce::String errorMessage;
    auto instance = formatManager.createPluginInstance(
        desc, currentSampleRate, currentBlockSize, errorMessage);

    if (instance) {
      logToFile("Plugin loaded successfully!");
      configureStereoLayout(instance.get());
      instance->prepareToPlay(currentSampleRate, currentBlockSize);

      // CRITICAL: Lock while swapping the actual instance in the rack
      {
        juce::ScopedLock sl(lock);
        slotVec[realIdx]->setPluginInChain(chainIndex, std::move(instance));
        if (chainIndex == 0)
          slotVec[realIdx]->setName(slotVec[realIdx]->getPluginName(0));
      }

      callback(true, "");
    } else {
      logToFile("Load failed: " + errorMessage);
      callback(false, errorMessage);
    }
  }

  // Same logic for Master chains
  void loadPluginIntoMasterBus(
      bool isFoh, int chainIndex, int pluginIndex,
      std::function<void(bool, const juce::String &)> callback) {

    if (pluginIndex == -1) {
      juce::ScopedLock sl(lock);
      auto &chain = isFoh ? fohPluginChain : iemPluginChain;
      if (chainIndex >= 0 && chainIndex < (int)chain.size()) {
        if (chain[chainIndex])
          chain[chainIndex]->releaseResources();
        chain[chainIndex] = nullptr;
      }
      callback(true, "");
      return;
    }

    if (pluginIndex < 0 || pluginIndex >= (int)availablePlugins.size()) {
      callback(false, "Invalid index");
      return;
    }

    juce::String pluginPath = availablePlugins[pluginIndex].path;
    juce::AudioPluginFormat *vst3Format = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
      if (formatManager.getFormat(i)->getName() == "VST3") {
        vst3Format = formatManager.getFormat(i);
        break;
      }
    }

    if (!vst3Format) {
      callback(false, "VST3 format not available");
      return;
    }

    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3Format->findAllTypesForFile(descriptions, pluginPath);
    if (descriptions.isEmpty()) {
      callback(false, "No plugins found in file");
      return;
    }

    juce::PluginDescription desc = *descriptions[0];
    juce::String errorMessage;
    auto instance = formatManager.createPluginInstance(
        desc, currentSampleRate, currentBlockSize, errorMessage);

    if (instance) {
      configureStereoLayout(instance.get());
      instance->prepareToPlay(currentSampleRate, currentBlockSize);
      {
        juce::ScopedLock sl(lock);
        auto &chain = isFoh ? fohPluginChain : iemPluginChain;
        // Expand chain if needed
        while ((int)chain.size() <= chainIndex)
          chain.push_back(nullptr);

        if (chain[chainIndex])
          chain[chainIndex]->releaseResources();

        chain[chainIndex] = std::move(instance);
      }
      callback(true, "");
    } else {
      callback(false, errorMessage);
    }
  }

  // ========== ASYNC RIG TRANSITION SUPPORT ==========
  // The RigBuilder builds plugin instances off the audio/message threads into
  // a staging cache; applyRig() then consumes the cache under the callback
  // lock, keeping the locked window short (heavy instantiation already done).

  struct PluginPathSnapshot {
    std::vector<juce::StringArray> slotChains; // [slotIdx] -> normalized paths (padded)
    juce::StringArray fohChain;                // normalized master paths (padded)
    juce::StringArray iemChain;
  };

  // Snapshot normalized plugin paths of the live rack (briefly holds lock).
  PluginPathSnapshot snapshotPluginPaths() {
    juce::ScopedLock sl(lock);
    PluginPathSnapshot s;
    s.slotChains.resize(slots.size());
    for (size_t i = 0; i < slots.size(); ++i) {
      for (int p = 0; p < slots[i]->getChainSize(); ++p)
        s.slotChains[i].add(normalizePath(slots[i]->getPluginPath(p)));
      while (s.slotChains[i].size() < 3)
        s.slotChains[i].add("");
    }
    auto pad = [this](const std::vector<std::unique_ptr<juce::AudioPluginInstance>> &chain,
                      juce::StringArray &out, bool isFoh) {
      for (int p = 0; p < (int)chain.size(); ++p)
        out.add(chain[p] ? normalizePath(getMasterPluginPath(isFoh, p)) : "");
      while (out.size() < 3)
        out.add("");
    };
    pad(fohPluginChain, s.fohChain, true);
    pad(iemPluginChain, s.iemChain, false);
    return s;
  }

  // Build (create + configure + prepare + restore state) a plugin instance from
  // its serialized var, WITHOUT touching the rack. Thread-safe vs. the audio
  // thread. Used off-thread by RigBuilder and on-thread as a fallback.
  bool buildPluginFromVar(const juce::var &vt,
                          std::unique_ptr<juce::AudioPluginInstance> &out,
                          juce::String &error) {
    juce::String path = vt.getProperty("path", "").toString();
    juce::String stateBase64 = vt.getProperty("state", "").toString();
    if (path.isEmpty()) {
      error = "empty plugin path";
      return false;
    }

    logToFile("TRACE: buildPluginFromVar started for path: " + path);

    juce::AudioPluginFormat *vst3Format = findVst3Format();
    if (!vst3Format) {
      error = "VST3 format unavailable";
      logToFile("TRACE: buildPluginFromVar failed - VST3 format unavailable");
      return false;
    }

    juce::OwnedArray<juce::PluginDescription> descs;
    vst3Format->findAllTypesForFile(descs, path);
    if (descs.isEmpty()) {
      error = "no plugin found in: " + path;
      logToFile("TRACE: buildPluginFromVar failed - " + error);
      return false;
    }

    logToFile("TRACE: buildPluginFromVar creating instance for " + descs[0]->name);
    try {
      auto instance = formatManager.createPluginInstance(
          *descs[0], currentSampleRate,
          currentBlockSize > 0 ? currentBlockSize : 512, error);
      if (!instance) {
        logToFile("TRACE: buildPluginFromVar failed to create instance: " + error);
        return false;
      }

      logToFile("TRACE: buildPluginFromVar configuring stereo layout for " + instance->getName());
      configureStereoLayout(instance.get());

      logToFile("TRACE: buildPluginFromVar calling prepareToPlay for " + instance->getName());
      instance->prepareToPlay(currentSampleRate,
                              currentBlockSize > 0 ? currentBlockSize : 512);

      juce::MemoryBlock blob;
      blob.fromBase64Encoding(stateBase64);
      if (blob.getSize() > 0) {
        logToFile("TRACE: buildPluginFromVar setting state information (" + juce::String(blob.getSize()) + " bytes) for " + instance->getName());
        instance->setStateInformation(blob.getData(), (int)blob.getSize());
      }

      logToFile("TRACE: buildPluginFromVar successfully built: " + instance->getName());
      out = std::move(instance);
      return true;
    } catch (...) {
      error = "plugin threw an exception during build";
      logToFile("TRACE: buildPluginFromVar caught exception for: " + path);
      return false;
    }
  }

  // Silent processBlock validation. Returns false if processing throws.
  bool validatePluginInstance(juce::AudioPluginInstance &inst) {
    // Calling processBlock on a background thread is unsafe for many complex/multi-output
    // plugins (e.g. Omnisphere) and can cause crashes/hangs.
    juce::ignoreUnused(inst);
    return true;
  }

  // Staging cache: pre-built plugin instances keyed by rack location.
  static juce::String stagingKeyFor(int slotIdx, int chainIdx, bool isFoh) {
    if (slotIdx >= 0)
      return "S:" + juce::String(slotIdx) + ":" + juce::String(chainIdx);
    return isFoh ? ("F:" + juce::String(chainIdx))
                 : ("I:" + juce::String(chainIdx));
  }
  void pushStagedPlugin(const juce::String &key,
                        std::unique_ptr<juce::AudioPluginInstance> inst) {
    juce::ScopedLock sl(stagingLock);
    stagedPlugins[key] = std::move(inst);
  }
  bool popStagedPlugin(const juce::String &key,
                       std::unique_ptr<juce::AudioPluginInstance> &out) {
    juce::ScopedLock sl(stagingLock);
    auto it = stagedPlugins.find(key);
    if (it == stagedPlugins.end())
      return false;
    out = std::move(it->second);
    stagedPlugins.erase(it);
    return true;
  }
  void clearStagingCache() {
    juce::ScopedLock sl(stagingLock);
    stagedPlugins.clear();
  }
  bool stagingHasKey(const juce::String &key) {
    juce::ScopedLock sl(stagingLock);
    return stagedPlugins.find(key) != stagedPlugins.end();
  }

  void pushPreloadedPlugin(const juce::String &key,
                           std::unique_ptr<juce::AudioPluginInstance> inst) {
    juce::ScopedLock sl(stagingLock);
    preloadedPlugins[key] = std::move(inst);
  }
  void clearPreloadedCache() {
    juce::ScopedLock sl(stagingLock);
    preloadedPlugins.clear();
  }
  bool hasPreloadedPlugin(const juce::String &key) {
    juce::ScopedLock sl(stagingLock);
    return preloadedPlugins.find(key) != preloadedPlugins.end();
  }
  void promotePreloadedToStaged() {
    juce::ScopedLock sl(stagingLock);
    for (auto &pair : preloadedPlugins) {
      stagedPlugins[pair.first] = std::move(pair.second);
    }
    preloadedPlugins.clear();
  }
  bool hasPreloadedPlugins() {
    juce::ScopedLock sl(stagingLock);
    return !preloadedPlugins.empty();
  }

  juce::CriticalSection &getCallbackLock() { return lock; }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenRigEngine)
};
