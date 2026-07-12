#pragma once

#include "RackSlot.h"
#include "Scene.h"
#include <JuceHeader.h>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

// Simple file logger for debugging. Guarded so this duplicate definition (the
// Sequential engine is currently dead code) cannot cause an ODR violation if
// both engine headers are ever included in the same translation unit.
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
  OpenRigEngine() {
    // Manually adding the formats we need instead of using addDefaultFormats()
    // This bypasses the "deleted function" error caused by the headless module.
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
#endif

    slots.clear();
    for (int i = 0; i < 10; ++i) {
      // Slot 0 is "Monitor In" (IEM only)
      if (i == 0)
        slots.push_back(std::make_unique<RackSlot>("Monitor In"));
      // Slot 9 is "Accordion" (dual sliders)
      else if (i == 9)
        slots.push_back(std::make_unique<RackSlot>("Accordion"));
      else
        slots.push_back(
            std::make_unique<RackSlot>("Slot " + juce::String(i + 1)));
    }

    // Special routing for Monitor In (slot 0): only to IEM
    slots[0]->setFohEnabled(false);
    slots[0]->setIemEnabled(true);

    initializeDefaultScenes();
  }

  ~OpenRigEngine() {
    // 1. Stop any pending jobs and clear the job array FIRST
    threadPool.removeAllJobs(true,
                             100); // Short timeout since we're not using it
    preallocatedJobs.clear();      // Delete all job objects

    // 2. Now safe to release plugins
    releaseAllPlugins();
  }

  // Call this BEFORE the engine is destroyed to release all VST3 plugins
  void releaseAllPlugins() {
    // Release all slot plugins
    for (auto &slot : slots) {
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

  void loadScene(int index) {
    if (index >= 0 && index < (int)scenes.size()) {
      auto &scene = scenes[index];
      for (size_t i = 0; i < slots.size() && i < scene.slotStates.size(); ++i) {
        slots[i]->setBypass(scene.slotStates[i].bypassed);
        slots[i]->setChannelLevel(scene.slotStates[i].channelLevel);
        slots[i]->setFohEnabled(scene.slotStates[i].fohEnabled);
        slots[i]->setIemEnabled(scene.slotStates[i].iemEnabled);
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
        st.channelLevel = slot->getChannelLevel();
        st.fohEnabled = slot->isFohEnabled();
        st.iemEnabled = slot->isIemEnabled();
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
      st.channelLevel = slot->getChannelLevel();
      st.fohEnabled = slot->isFohEnabled();
      st.iemEnabled = slot->isIemEnabled();
      newScene.slotStates.push_back(st);
    }
    scenes.push_back(newScene);
  }

  // Pre-allocated job for parallel processing to avoid heap allocations in
  // audio thread
  struct SlotProcessJob : public juce::ThreadPoolJob {
    OpenRigEngine &engine;
    int slotIdx;
    juce::AudioBuffer<float> *masterInput = nullptr;
    std::atomic<int> *counter = nullptr;

    SlotProcessJob(OpenRigEngine &e, int idx)
        : ThreadPoolJob("SlotProcess"), engine(e), slotIdx(idx) {}

    void setup(juce::AudioBuffer<float> &input, std::atomic<int> &c) {
      masterInput = &input;
      counter = &c;
    }

    JobStatus runJob() override {
      juce::ScopedNoDenormals noDenormals; // CRITICAL: Stop denormal CPU spikes

      if (slotIdx < (int)engine.slots.size()) {
        auto &slot = engine.slots[slotIdx];
        auto &scratch = engine.scratchBuffers[slotIdx];
        auto &midi = engine.slotMidiBuffers[slotIdx];
        int numSamples = masterInput->getNumSamples();

        scratch.clear();
        int inputIdx = slot->getInputChannelIndex();
        if (inputIdx >= 0 && inputIdx < masterInput->getNumChannels()) {
          scratch.copyFrom(0, 0, *masterInput, inputIdx, 0, numSamples);
          if (inputIdx + 1 < masterInput->getNumChannels())
            scratch.copyFrom(1, 0, *masterInput, inputIdx + 1, 0, numSamples);
          else
            scratch.copyFrom(1, 0, *masterInput, inputIdx, 0, numSamples);
        }
        slot->processBlock(scratch, midi);
      }

      if (counter)
        (*counter)++;
      return jobHasFinished;
    }
  };

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlockExpected;
    // We use 32 channels internally as a safety margin for multi-out VST3s
    fohBus.setSize(32, samplesPerBlockExpected);
    iemBus.setSize(32, samplesPerBlockExpected);

    // Initialize per-slot scratch buffers and pre-allocated jobs
    scratchBuffers.resize(slots.size());
    preallocatedJobs.clear();

    for (int i = 0; i < (int)slots.size(); ++i) {
      scratchBuffers[i].setSize(32, samplesPerBlockExpected);
      preallocatedJobs.add(new SlotProcessJob(*this, i));
    }
  }

  // Improved scanner logic: avoids direct VST3 dependency if not enabled
  void scanForPlugins() {
    // TODO: Implement out-of-process plugin scanning for crash protection.
    // For now, show a placeholder message.
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::InfoIcon, "Plugin Scanner",
        "Plugin scanning is not yet fully implemented.\n\n"
        "In a future update, this will safely scan your VST3 folders\n"
        "and let you load plugins into each slot.");
  }

  void processAudio(juce::AudioBuffer<float> &mainBuffer,
                    juce::MidiBuffer &midi) {
    juce::ScopedNoDenormals noDenormals; // Prevent CPU spikes from tiny floats

    // Use TryLock to avoid blocking the audio thread if UI has the lock
    juce::GenericScopedTryLock<juce::CriticalSection> sl(lock);
    if (!sl.isLocked()) {
      // UI thread has the lock - just output silence this block rather than
      // block
      mainBuffer.clear();
      return;
    }

    // We have the lock - do the quick work (bus clearing, MIDI routing)
    fohBus.clear();
    iemBus.clear();

    if (slotMidiBuffers.size() != slots.size())
      slotMidiBuffers.resize(slots.size());

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

    for (const auto metadata : midi) {
      auto msg = metadata.getMessage();
      int pos = metadata.samplePosition;

      // GLOBAL MIDI FILTERS:
      // 1. Only accept MIDI channel 1 (0 in 0-indexed)
      if (msg.getChannel() != 1 && msg.getChannel() != 0)
        continue;

      // 2. NEVER pass program change (patch change) messages
      if (msg.isProgramChange())
        continue;

      // Special routing: MIDI messages -> only to slots
      if (msg.isNoteOn() || msg.isNoteOff()) {
        int noteNum = msg.getNoteNumber();
        // Apply per-slot note range filtering
        for (size_t i = 0; i < slots.size(); ++i) {
          if (slots[i]->isNoteInRange(noteNum)) {
            slotMidiBuffers[i].addEvent(msg, pos);
          }
        }
      } else if (msg.isAftertouch() || msg.isPitchWheel()) {
        // Aftertouch and pitch wheel go to all slots
        for (auto &b : slotMidiBuffers)
          b.addEvent(msg, pos);
      } else if (msg.isController()) {
        int ccNum = msg.getControllerNumber();

        for (size_t i = 0; i < slots.size(); ++i) {
          bool allowed = slots[i]->isCCAllowed(ccNum);
          // DBG("OpenRigEngine: CC" + juce::String(ccNum) + "=" +
          //     juce::String(ccVal) + " slot[" + juce::String((int)i) + "]=\""
          //     + slots[i]->getName() +
          //     "\" allowed=" + juce::String(allowed ? "YES" : "NO"));
          if (allowed) {
            slotMidiBuffers[i].addEvent(msg, pos);
          }
        }
      }
    }

    // Ensure scratch buffers are sized correctly (one per slot)
    if (scratchBuffers.size() != slots.size()) {
      scratchBuffers.resize(slots.size());
    }

    for (size_t i = 0; i < scratchBuffers.size(); ++i) {
      if (scratchBuffers[i].getNumChannels() < 32 ||
          scratchBuffers[i].getNumSamples() < mainBuffer.getNumSamples()) {
        scratchBuffers[i].setSize(32, mainBuffer.getNumSamples());
      }
    }

    // SEQUENTIAL PROCESSING
    // Note: Parallel processing disabled - plugins are not thread-safe
    for (int i = 0; i < (int)slots.size(); ++i) {
      scratchBuffers[i].clear();
      int numSamples = mainBuffer.getNumSamples();

      // --- Hardware Input Routing ---
      int inputIdx = slots[i]->getInputChannelIndex();
      if (inputIdx >= 0 && inputIdx < mainBuffer.getNumChannels()) {
        scratchBuffers[i].copyFrom(0, 0, mainBuffer, inputIdx, 0, numSamples);

        // Special case: Monitor In (slot 0) always duplicates mono to stereo
        // This allows stereo effects (reverb, delay) to create spatial imaging
        if (i == 0) {
          scratchBuffers[i].copyFrom(1, 0, mainBuffer, inputIdx, 0, numSamples);
        }
        // For other slots: support stereo hardware pairs if they exist
        else if (inputIdx + 1 < mainBuffer.getNumChannels()) {
          scratchBuffers[i].copyFrom(1, 0, mainBuffer, inputIdx + 1, 0,
                                     numSamples);
        } else {
          scratchBuffers[i].copyFrom(1, 0, mainBuffer, inputIdx, 0, numSamples);
        }
      }

      // Process plugin chain
      slots[i]->processBlock(scratchBuffers[i], slotMidiBuffers[i]);

      // Sum to buses immediately
      slots[i]->sumToBuses(scratchBuffers[i], fohBus, iemBus);
    }

    // --- Master FX Processing ---
    // Apply Master Fader Levels BEFORE FX (or typically after sums, but before
    // output)
    fohBus.applyGain(fohMasterLevel);
    iemBus.applyGain(iemMasterLevel);

    juce::MidiBuffer emptyMidi;
    for (auto &plugin : fohPluginChain) {
      if (plugin)
        plugin->processBlock(fohBus, emptyMidi);
    }
    for (auto &plugin : iemPluginChain) {
      if (plugin)
        plugin->processBlock(iemBus, emptyMidi);
    }

    // --- Copy Buses to Hardware Outputs ---
    mainBuffer.clear();
    int numOuts = mainBuffer.getNumChannels();
    int numSamples = mainBuffer.getNumSamples();

    // Route FOH to configured outputs (default: 1+2)
    if (fohOutputOffset + 1 < numOuts) {
      mainBuffer.copyFrom(fohOutputOffset, 0, fohBus, 0, 0, numSamples);
      mainBuffer.copyFrom(fohOutputOffset + 1, 0, fohBus, 1, 0, numSamples);
    }

    // Route IEM to configured outputs (default: 3+4)
    if (iemOutputOffset + 1 < numOuts) {
      mainBuffer.copyFrom(iemOutputOffset, 0, iemBus, 0, 0, numSamples);
      mainBuffer.copyFrom(iemOutputOffset + 1, 0, iemBus, 1, 0, numSamples);
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
    auto *rig = new juce::DynamicObject();
    rig->setProperty("version", 1);
    rig->setProperty("fohMasterLevel", (double)fohMasterLevel);
    rig->setProperty("iemMasterLevel", (double)iemMasterLevel);
    rig->setProperty("fohOutputOffset", fohOutputOffset);
    rig->setProperty("iemOutputOffset", iemOutputOffset);

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
      st->setProperty("level", (double)s->getChannelLevel());
      st->setProperty("foh", s->isFohEnabled());
      st->setProperty("iem", s->isIemEnabled());
      st->setProperty("mute", s->isBypassed());
      st->setProperty("inputIndex", s->getInputChannelIndex());

      juce::String ccList;
      for (int cc : s->getAllowedCCs())
        ccList += juce::String(cc) + ",";
      st->setProperty("allowedCCs", ccList);

      // Save CC-to-Parameter mappings
      juce::Array<juce::var> ccMappingNodes;
      for (const auto &pair : s->getCCMappings()) {
        auto *mapping = new juce::DynamicObject();
        mapping->setProperty("cc", pair.first);
        mapping->setProperty("chainIndex", pair.second.chainIndex);
        mapping->setProperty("paramIndex", pair.second.parameterIndex);
        mapping->setProperty("minValue", (double)pair.second.minValue);
        mapping->setProperty("maxValue", (double)pair.second.maxValue);
        ccMappingNodes.add(juce::var(mapping));
      }
      st->setProperty("ccMappings", ccMappingNodes);

      juce::Array<juce::var> chainNodes;
      for (int pIdx = 0; pIdx < s->getChainSize(); ++pIdx) {
        auto plugin = s->getPluginInstance(pIdx);
        if (plugin)
          chainNodes.add(getPluginVar(plugin));
        else
          chainNodes.add(juce::var());
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

      juce::Array<juce::var> states;
      for (const auto &st : scene.slotStates) {
        auto *stateObj = new juce::DynamicObject();
        stateObj->setProperty("bypassed", st.bypassed);
        stateObj->setProperty("level", (double)st.channelLevel);
        stateObj->setProperty("foh", st.fohEnabled);
        stateObj->setProperty("iem", st.iemEnabled);
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
    if (!rig.isObject())
      return;

    juce::ScopedLock sl(lock);

    fohMasterLevel = (float)rig.getProperty("fohMasterLevel", 1.0);
    iemMasterLevel = (float)rig.getProperty("iemMasterLevel", 1.0);
    fohOutputOffset = rig.getProperty("fohOutputOffset", 0);
    iemOutputOffset = rig.getProperty("iemOutputOffset", 2);

    // Master FX
    fohPluginChain.clear();
    if (auto *fohArr = rig.getProperty("fohFx", juce::var()).getArray()) {
      for (int i = 0; i < fohArr->size(); ++i) {
        auto v = fohArr->getReference(i);
        if (v.isObject())
          loadPluginFromVar(-1, i, v, true);
      }
    }

    iemPluginChain.clear();
    if (auto *iemArr = rig.getProperty("iemFx", juce::var()).getArray()) {
      for (int i = 0; i < iemArr->size(); ++i) {
        auto v = iemArr->getReference(i);
        if (v.isObject())
          loadPluginFromVar(-1, i, v, false);
      }
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
        s->setChannelLevel((float)v.getProperty("level", 0.8));
        s->setFohEnabled(v.getProperty("foh", true));
        s->setIemEnabled(v.getProperty("iem", true));
        s->setBypass(v.getProperty("mute", false));
        s->setInputChannelIndex(v.getProperty("inputIndex", -1));

        juce::String ccList = v.getProperty("allowedCCs", "64").toString();
        juce::StringArray ccs;
        ccs.addTokens(ccList, ",", "");
        std::set<int> allowed;
        for (auto &cc : ccs)
          if (cc.isNotEmpty())
            allowed.insert(cc.getIntValue());
        s->setAllowedCCs(allowed);

        // Restore CC-to-Parameter mappings
        s->clearAllCCMappings();
        if (auto *mappingArr =
                v.getProperty("ccMappings", juce::var()).getArray()) {
          for (int m = 0; m < mappingArr->size(); ++m) {
            auto mv = mappingArr->getReference(m);
            int ccNum = mv.getProperty("cc", 0);
            int chainIdx = mv.getProperty("chainIndex", 0);
            int paramIdx = mv.getProperty("paramIndex", 0);
            float minVal = (float)mv.getProperty("minValue", 0.0);
            float maxVal = (float)mv.getProperty("maxValue", 1.0);
            s->mapCCToParameter(ccNum, chainIdx, paramIdx, minVal, maxVal);
          }
        }

        s->clearChain();
        if (auto *chainArr = v.getProperty("chain", juce::var()).getArray()) {
          for (int p = 0; p < chainArr->size(); ++p) {
            auto pv = chainArr->getReference(p);
            if (pv.isObject())
              loadPluginFromVar(i, p, pv, true);
          }
        }
      }
    }

    // Scenes (Presets)
    if (auto *sceneArr = rig.getProperty("scenes", juce::var()).getArray()) {
      scenes.clear();
      for (int i = 0; i < sceneArr->size(); ++i) {
        auto sv = sceneArr->getReference(i);
        Scene newScene(sv.getProperty("name", "Scene").toString());

        if (auto *stateArr = sv.getProperty("states", juce::var()).getArray()) {
          for (int s = 0; s < stateArr->size(); ++s) {
            auto stv = stateArr->getReference(s);
            SlotState st;
            st.bypassed = stv.getProperty("bypassed", false);
            st.channelLevel = (float)stv.getProperty("level", 0.8);
            st.fohEnabled = stv.getProperty("foh", true);
            st.iemEnabled = stv.getProperty("iem", true);
            newScene.slotStates.push_back(st);
          }
        }
        scenes.push_back(newScene);
      }
    }
    currentSceneIndex = rig.getProperty("currentSceneIndex", 0);
  }

  void saveRigToFile() {
    auto json = exportRigToJson();
    juce::File f =
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("OpenRigFullRig.json");
    f.replaceWithText(json);
  }

  void loadRigFromFile() {
    juce::File f =
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("OpenRigFullRig.json");
    if (f.existsAsFile())
      importRigFromJson(f.loadFileAsString());
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

  void loadPluginFromVar(int slotIdx, int chainIdx, const juce::var &vt,
                         bool isFohOrChannel) {
    juce::String path = vt.getProperty("path", "").toString();
    juce::String stateBase64 = vt.getProperty("state", "").toString();
    if (path.isEmpty())
      return;

    juce::AudioPluginFormat *vst3Format = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
      if (formatManager.getFormat(i)->getName() == "VST3") {
        vst3Format = formatManager.getFormat(i);
        break;
      }
    }
    if (!vst3Format)
      return;

    juce::OwnedArray<juce::PluginDescription> descs;
    vst3Format->findAllTypesForFile(descs, path);
    if (descs.isEmpty())
      return;

    juce::String errorMsg;
    auto instance = formatManager.createPluginInstance(
        *descs[0], currentSampleRate, currentBlockSize, errorMsg);
    if (instance) {
      configureStereoLayout(instance.get());
      instance->prepareToPlay(currentSampleRate, currentBlockSize);
      juce::MemoryBlock blob;
      blob.fromBase64Encoding(stateBase64);
      instance->setStateInformation(blob.getData(), (int)blob.getSize());

      if (slotIdx >= 0) {
        slots[slotIdx]->setPluginInChain(chainIdx, std::move(instance));
      } else {
        auto &chain = isFohOrChannel ? fohPluginChain : iemPluginChain;
        while ((int)chain.size() <= chainIdx)
          chain.push_back(nullptr);
        chain[chainIdx] = std::move(instance);
      }
    }
  }

  void configureStereoLayout(juce::AudioProcessor *processor) {
    if (processor == nullptr)
      return;

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
    // Skip strict layout for Omnisphere as it crashes when aux buses are
    // disabled
    if (processor->getName().containsIgnoreCase("Omnisphere")) {
      logToFile("Skipping strict layout enforcement for Omnisphere");
    } else {
      bool success = processor->setBusesLayout(layout);
      logToFile(
          "Configured Topology for " + processor->getName() +
          (success ? " [SUCCESS]" : " [FAILED]") +
          " - Ins: " + juce::String(processor->getTotalNumInputChannels()) +
          ", Outs: " + juce::String(processor->getTotalNumOutputChannels()));
    }

    // Explicitly set processing details to match valid layout
    processor->setPlayConfigDetails(processor->getTotalNumInputChannels(),
                                    processor->getTotalNumOutputChannels(),
                                    currentSampleRate, currentBlockSize);
  }

public:
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
    for (auto &p : fohPluginChain)
      p->releaseResources();
    for (auto &p : iemPluginChain)
      p->releaseResources();
    fohPluginChain.clear();
    iemPluginChain.clear();
  }

  float getFohPeakL() const { return fohPeakL; }
  float getFohPeakR() const { return fohPeakR; }
  float getIemPeakL() const { return iemPeakL; }
  float getIemPeakR() const { return iemPeakR; }

  void setFohMasterLevel(float level) { fohMasterLevel = level; }
  void setIemMasterLevel(float level) { iemMasterLevel = level; }

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
      st.bypassed = (i != 1);
      st.channelLevel = 0.8f;
      st.fohEnabled = (slots[i]->getName() != "Monitor In");
      st.iemEnabled = true;
      s1.slotStates.push_back(st);
    }
    scenes.push_back(s1);

    Scene s2("BIG B3");
    for (int i = 0; i < (int)slots.size(); ++i) {
      SlotState st;
      st.bypassed = (i != 1 && i != 2);
      st.channelLevel = (i == 2) ? 0.9f : 0.4f;
      st.fohEnabled = true;
      st.iemEnabled = true;
      s2.slotStates.push_back(st);
    }
    scenes.push_back(s2);

    Scene s3("SOLO PAD");
    for (int i = 0; i < (int)slots.size(); ++i) {
      SlotState st;
      st.bypassed = (i != 3);
      st.channelLevel = 0.8f;
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

  // Parallel Processing Support
  juce::OwnedArray<SlotProcessJob> preallocatedJobs;
  std::atomic<int> slotsFinishedCount{0};
  std::vector<juce::AudioBuffer<float>> scratchBuffers;
  juce::ThreadPool threadPool; // Declared last to be destroyed first

  // Master FX chains
  std::vector<std::unique_ptr<juce::AudioPluginInstance>> fohPluginChain;
  std::vector<std::unique_ptr<juce::AudioPluginInstance>> iemPluginChain;

  float fohPeakL = 0, fohPeakR = 0;
  float iemPeakL = 0, iemPeakR = 0;
  float fohMasterLevel = 1.0f;
  float iemMasterLevel = 1.0f;

  int fohOutputOffset = 0; // Hardware channels 1+2
  int iemOutputOffset = 2; // Hardware channels 3+4

  juce::CriticalSection lock;

public:
  // ========== KNOWN PLUGINS REGISTRY ==========
  // These are the VST3 plugins available for loading into slots.
  // Each entry is {Display Name, Full Path}
  struct PluginInfo {
    juce::String name;
    juce::String path;
  };

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
      {"Waves (Shell)", "C:\\Program Files\\Common "
                        "Files\\VST3\\WaveShell1-VST3 15.5_x64.vst3"},
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
    if (slotIndex < 0 || slotIndex >= (int)slots.size() || pluginIndex < 0 ||
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
        slots[slotIndex]->setPluginInChain(chainIndex, std::move(instance));
        if (chainIndex == 0)
          slots[slotIndex]->setName(slots[slotIndex]->getPluginName(0));
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

  juce::CriticalSection &getCallbackLock() { return lock; }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenRigEngine)
};
