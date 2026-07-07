/*
  ==============================================================================

    OpenRig Engine
    The heart of the Sovereign Live Performance Rig.

  ==============================================================================
*/

#pragma once

#include "ChannelStripProcessor.h"
#include "SimpleArpeggiator.h"
#include "OctaveHarmonizer.h"
#include "SamplerProcessor.h"
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <vector>

/**
 * A RackSlot represents a single channel strip in the OpenRig host.
 * It can hold a VST3 plugin or act as a raw hardware input.
 */
class RackSlot {
public:
  RackSlot(const juce::String &name) : slotName(name) { setDefaultCCs(); }
  ~RackSlot() = default;

  void prepare(double sampleRate) {
    lastSampleRate = sampleRate;
    strip.prepare(sampleRate);
    arpeggiator.prepare(sampleRate);
    harmonizer.prepare(sampleRate);
    sampler.prepare(sampleRate);

    // Pre-allocate scratch buffer for summed instrument rendering (audio-thread safe)
    scratchBuffer.setSize(2, 4096);
  }

  // --- Audio Logic ---
  SimpleArpeggiator& getArpeggiator() { return arpeggiator; }
  OctaveHarmonizer& getHarmonizer() { return harmonizer; }
  SamplerProcessor& getSampler() { return sampler; }

  // Per-plugin-chain-slot settings for instrument stacking (note range, level, enable)
  struct ChainSlotSettings {
    std::atomic<int> lowNote{0};
    std::atomic<int> highNote{127};
    std::atomic<float> level{1.0f};
    std::atomic<bool> enabled{true};
  };

  ChainSlotSettings& getChainSlotSettings(int index) {
    static ChainSlotSettings fallback;
    if (index >= 0 && index < 3)
      return chainSettings[index];
    return fallback;
  }

  void processBlock(juce::AudioBuffer<float> &slotBuffer,
                    juce::MidiBuffer &midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    if (bypassed.load()) {
      leftPeak.store(0.0f);
      rightPeak.store(0.0f);
      return;
    }

    auto startTime = juce::Time::getHighResolutionTicks();

    // 1. Merge injected MIDI from harmony routers (thread-safe)
    {
      juce::SpinLock::ScopedLockType sl(injectedMidiLock);
      if (!injectedMidi.isEmpty()) {
        midiMessages.addEvents(injectedMidi, 0, -1, 0);
        injectedMidi.clear();
      }
    }

    // Apply CC to parameter/level mappings FIRST, before plugins consume the
    // messages
    applyCCMappings(midiMessages);

    // Remap passthrough CCs so the plugin sees the outgoing CC number
    applyCCPassthrough(midiMessages);

    // Arpeggiator (before plugin chain)
    arpeggiator.processBlock(midiMessages, slotBuffer.getNumSamples());

    // Sampler (before plugin chain)
    sampler.processBlock(slotBuffer, midiMessages);

    // Harmonizer with redirection support: generated harmony notes go to harmonyBuffer
    juce::MidiBuffer harmonyBuffer;
    harmonizer.processBlock(midiMessages, slotBuffer.getNumSamples(),
                            &harmonyBuffer);

    int target = harmonizer.harmonyTargetSlot.load();
    if (target >= 0 && !harmonyBuffer.isEmpty() && midiRouteCallback)
      midiRouteCallback(target, harmonyBuffer);

    // 1. Process VST Chain — instruments are summed in parallel, effects run in series.
    for (int i = 0; i < (int)pluginChain.size(); ++i) {
      auto &plugin = pluginChain[i];
      if (plugin && (i >= 3 || chainSettings[i].enabled.load())) {
        try {
          if (plugin->getPluginDescription().isInstrument) {
            // Match scratch to the current block without reallocating (no clear on realloc)
            scratchBuffer.setSize(slotBuffer.getNumChannels(), slotBuffer.getNumSamples(), false, false, true);
            scratchBuffer.clear();

            // Clone + note-range-filter MIDI so this plugin can't starve the next
            juce::MidiBuffer filteredMidi;
            int low = 0;
            int high = 127;
            float gain = 1.0f;

            if (i < 3) {
              low = chainSettings[i].lowNote.load();
              high = chainSettings[i].highNote.load();
              gain = chainSettings[i].level.load();
            }

            for (const auto metadata : midiMessages) {
              auto msg = metadata.getMessage();
              if (msg.isNoteOnOrOff()) {
                int note = msg.getNoteNumber();
                if (note >= low && note <= high)
                  filteredMidi.addEvent(msg, metadata.samplePosition);
              } else {
                // Pass pitch bend, CCs, program change through to all instruments
                filteredMidi.addEvent(msg, metadata.samplePosition);
              }
            }

            plugin->processBlock(scratchBuffer, filteredMidi);

            // Per-instrument level gain
            scratchBuffer.applyGain(gain);

            // Sum into the slot buffer
            for (int ch = 0; ch < slotBuffer.getNumChannels(); ++ch) {
              if (ch < scratchBuffer.getNumChannels())
                slotBuffer.addFrom(ch, 0, scratchBuffer, ch, 0, slotBuffer.getNumSamples());
            }
          } else {
            // Audio effect: process sequentially in-place
            plugin->processBlock(slotBuffer, midiMessages);
          }
        } catch (...) {
          // Swallow plugin exceptions — one plugin must not crash the audio thread
        }
      }
    }

    // 2. Process Channel Strip (Gate -> EQ -> Comp)
    strip.processBlock(slotBuffer);

    // Capture peaks for the meters
    leftPeak.store(slotBuffer.getMagnitude(0, 0, slotBuffer.getNumSamples()));
    if (slotBuffer.getNumChannels() > 1)
      rightPeak.store(slotBuffer.getMagnitude(1, 0, slotBuffer.getNumSamples()));
    else
      rightPeak.store(leftPeak.load());

    // MIDI flash logic
    if (!midiMessages.isEmpty())
      midiActivity.store(true);

    // Calculate DSP CPU percentage (ratio of process time to block time)
    auto elapsed = juce::Time::getHighResolutionTicks() - startTime;
    double elapsedSecs =
        (double)elapsed / juce::Time::getHighResolutionTicksPerSecond();
    double totalBlockSecs = (double)slotBuffer.getNumSamples() / lastSampleRate;
    float currentLoad =
        (totalBlockSecs > 0.0) ? (float)(elapsedSecs / totalBlockSecs) : 0.0f;

    // Smooth the load with a one-pole low-pass filter
    cpuUsage.store(0.92f * cpuUsage.load() + 0.08f * currentLoad);
  }

  // --- Incoming MIDI injection (harmony routing target) ---
  void injectMidi(const juce::MidiBuffer &buffer) {
    juce::SpinLock::ScopedLockType sl(injectedMidiLock);
    injectedMidi.addEvents(buffer, 0, -1, 0);
  }

  void setMidiRouteCallback(
      std::function<void(int, const juce::MidiBuffer &)> cb) {
    midiRouteCallback = std::move(cb);
  }

  // --- Metering ---
  float getLeftPeak() const { return leftPeak.load(); }
  float getRightPeak() const { return rightPeak.load(); }
  float getCpuUsage() const { return cpuUsage.load(); }
  bool getAndClearMidiActivity() {
    return midiActivity.exchange(false);
  }

  // --- Summing ---
  void sumToBuses(const juce::AudioBuffer<float> &slotBuffer,
                  juce::AudioBuffer<float> &fohBus,
                  juce::AudioBuffer<float> &iemBus,
                  juce::AudioBuffer<float> &aux1,
                  juce::AudioBuffer<float> &aux2) {
    if (bypassed.load())
      return;

    int numSamples = slotBuffer.getNumSamples();
    int channelsToSum =
        (slotBuffer.getNumChannels() < 2) ? slotBuffer.getNumChannels() : 2;

    float fohBase = fohLevel.load();
    float offset = iemOffset.load();
    float iemBase = fohBase * offset; // IEM level is relative to FOH

    float a1Level = aux1SendLevel.load();
    float a2Level = aux2SendLevel.load();

    for (int ch = 0; ch < channelsToSum; ++ch) {
      const float *ptr = slotBuffer.getReadPointer(ch);

      if (fohEnabled.load() && ch < fohBus.getNumChannels())
        fohBus.addFromWithRamp(ch, 0, ptr, numSamples, lastFohLevel, fohBase);

      if (iemEnabled.load() && ch < iemBus.getNumChannels())
        iemBus.addFromWithRamp(ch, 0, ptr, numSamples, lastIemLevel, iemBase);

      // Post-fader Aux sends
      if (ch < aux1.getNumChannels())
        aux1.addFrom(ch, 0, ptr, numSamples, fohBase * a1Level);
      if (ch < aux2.getNumChannels())
        aux2.addFrom(ch, 0, ptr, numSamples, fohBase * a2Level);
    }

    lastFohLevel = fohBase;
    lastIemLevel = iemBase;
  }

  // --- Mix Controls ---
  void setChannelLevel(float newLevel) {
    // Legacy: sets both FOH and IEM to same level
    float clamped = juce::jlimit(0.0f, 1.0f, newLevel);
    fohLevel.store(clamped);
    iemLevel.store(clamped);
  }
  float getChannelLevel() const { return fohLevel.load(); }

  // Separate FOH/IEM level controls
  void setFohLevel(float level) {
    fohLevel.store(juce::jlimit(0.0f, 1.0f, level));
  }
  float getFohLevel() const { return fohLevel.load(); }

  void setIemLevel(float level) {
    iemLevel.store(juce::jlimit(0.0f, 1.0f, level));
  }
  float getIemLevel() const { return iemLevel.load(); }

  // Fader linking (chain)
  void setFadersLinked(bool linked) { fadersLinked.store(linked); }
  bool areFadersLinked() const { return fadersLinked.load(); }

  void setFohEnabled(bool enabled) { fohEnabled.store(enabled); }
  bool isFohEnabled() const { return fohEnabled.load(); }

  void setIemEnabled(bool enabled) { iemEnabled.store(enabled); }
  bool isIemEnabled() const { return iemEnabled.load(); }

  void setInputActive(bool active) { inputActive.store(active); }
  bool isInputActive() const { return inputActive.load(); }

  // Channel Strip Accessors
  OpenRigDSP::ChannelStripProcessor &getStrip() { return strip; }

  // Aux Sends & IEM Offset
  void setAux1Send(float level) {
    aux1SendLevel.store(juce::jlimit(0.0f, 1.0f, level));
  }
  float getAux1Send() const { return aux1SendLevel.load(); }
  void setAux2Send(float level) {
    aux2SendLevel.store(juce::jlimit(0.0f, 1.0f, level));
  }
  float getAux2Send() const { return aux2SendLevel.load(); }

  void setIemOffset(float offset) {
    iemOffset.store(juce::jlimit(0.0f, 2.0f, offset));
  }
  float getIemOffset() const { return iemOffset.load(); }

  void setBypass(bool shouldBypass) { bypassed.store(shouldBypass); }
  bool isBypassed() const { return bypassed.load(); }

  // CC to Level mapping
  void mapCCToLevel(int ccNum) {
    levelCC = ccNum;
    if (ccNum >= 0 && ccNum < 128)
      allowedCCs[ccNum].store(true);
  }
  void unmapLevelCC() { levelCC = -1; }
  int getLevelCC() const { return levelCC; }

  // Note range filtering (0-127, inclusive)
  void setNoteRange(int low, int high) {
    int lo = juce::jlimit(0, 127, low);
    int hi = juce::jlimit(0, 127, high);
    if (hi < lo) hi = lo;
    lowNote.store(lo);
    highNote.store(hi);
  }
  int getLowNote() const { return lowNote.load(); }
  int getHighNote() const { return highNote.load(); }
  bool isNoteInRange(int noteNum) const {
    return noteNum >= lowNote.load() && noteNum <= highNote.load();
  }

  // Transpose (in octaves, -4..+4). Applied to note-on/off after range check.
  int getTransposeOctaves() const { return transposeOctaves.load(); }
  void setTransposeOctaves(int oct) {
    transposeOctaves.store(juce::jlimit(-4, 4, oct));
  }

  // Transpose (in semitones, -11..+11). Applied to note-on/off after range check.
  int getTransposeSemitones() const { return transposeSemitones.load(); }
  void setTransposeSemitones(int semis) {
    transposeSemitones.store(juce::jlimit(-11, 11, semis));
  }

  // Total transpose as a single -48..+48 value (octaves*12 + semitones).
  int getTransposeSemis() const {
    return getTransposeOctaves() * 12 + getTransposeSemitones();
  }
  void setTransposeSemis(int semis) {
    semis = juce::jlimit(-48, 48, semis);
    int oct = semis / 12;       // truncates toward zero (C++ integer div)
    int rem = semis - oct * 12; // in [-11, 11]
    setTransposeOctaves(oct);
    setTransposeSemitones(rem);
  }

  void setInputChannelIndex(int index) { inputChannelIndex.store(index); }
  int getInputChannelIndex() const { return inputChannelIndex.load(); }

  juce::String getName() const { return slotName; }
  void setName(const juce::String &newName) { slotName = newName; }

  // Channel icon type (for visual identification)
  void setIconIndex(int index) { iconIndex = index; }
  int getIconIndex() const { return iconIndex; }

  // Channel color (for visual identification)
  void setChannelColor(const juce::Colour &color) { channelColor = color; }
  juce::Colour getChannelColor() const { return channelColor; }

  // --- Plugin Management ---
  void setPluginInChain(int chainIndex,
                        std::unique_ptr<juce::AudioPluginInstance> newPlugin) {
    if (chainIndex < 0 || chainIndex >= 3)
      return;

    if (chainIndex < (int)pluginChain.size()) {
      if (pluginChain[chainIndex])
        pluginChain[chainIndex]->releaseResources();
      pluginChain[chainIndex] = std::move(newPlugin);
    } else {
      while ((int)pluginChain.size() < chainIndex)
        pluginChain.push_back(nullptr);
      pluginChain.push_back(std::move(newPlugin));
    }
  }

  void clearChain() {
    for (auto &p : pluginChain) {
      if (p)
        p->releaseResources();
    }
    pluginChain.clear();
  }

  void clearChainPreserve(const std::set<int> &chainIndicesToPreserve) {
    for (int i = 0; i < (int)pluginChain.size(); ++i) {
      if (pluginChain[i]) {
        if (chainIndicesToPreserve.find(i) == chainIndicesToPreserve.end()) {
          pluginChain[i]->releaseResources();
          pluginChain[i] = nullptr;
        }
      }
    }
  }

  bool hasPlugins() const { return !pluginChain.empty(); }

  juce::String getPluginName(int chainIndex) const {
    if (chainIndex >= 0 && chainIndex < (int)pluginChain.size()) {
      if (const auto *p = pluginChain[chainIndex].get())
        return p->getName();
    }
    return "";
  }

  juce::AudioPluginInstance *getPluginInstance(int chainIndex) {
    if (chainIndex >= 0 && chainIndex < (int)pluginChain.size())
      return pluginChain[chainIndex].get();
    return nullptr;
  }

  juce::String getPluginPath(int chainIndex) const {
    if (chainIndex >= 0 && chainIndex < (int)pluginChain.size()) {
      if (const auto *p = pluginChain[chainIndex].get()) {
        auto desc = p->getPluginDescription();
        return desc.fileOrIdentifier;
      }
    }
    return "";
  }

  int getChainSize() const { return (int)pluginChain.size(); }

  // --- CC Filter ---
  // By default, only sustain pedal (CC64) passes through
  void setAllowedCCs(const std::set<int> &ccs) {
    for (int i = 0; i < 128; ++i)
      allowedCCs[i].store(false);
    for (int cc : ccs) {
      if (cc >= 0 && cc < 128)
        allowedCCs[cc].store(true);
    }
  }

  std::set<int> getAllowedCCs() const {
    std::set<int> ccs;
    for (int i = 0; i < 128; ++i) {
      if (allowedCCs[i].load())
        ccs.insert(i);
    }
    return ccs;
  }

  void allowCC(int ccNum) {
    if (ccNum >= 0 && ccNum < 128)
      allowedCCs[ccNum].store(true);
  }
  void blockCC(int ccNum) {
    if (ccNum >= 0 && ccNum < 128)
      allowedCCs[ccNum].store(false);
  }
  bool isCCAllowed(int ccNum) const {
    if (ccNum >= 0 && ccNum < 128)
      return allowedCCs[ccNum].load();
    return false;
  }

  void allowAllCCs() {
    for (int i = 0; i < 128; ++i)
      allowedCCs[i].store(true);
  }
  void blockAllCCs() {
    for (int i = 0; i < 128; ++i)
      allowedCCs[i].store(false);
  }
  void setDefaultCCs() {
    blockAllCCs();
    allowedCCs[64].store(true); // Sustain
  }

  // --- CC Passthrough (Remap) ---
  // For plugins with their own internal MIDI learn: remap incoming CC to a
  // different CC number and forward it in the MIDI buffer without binding to
  // any JUCE parameter. The outgoing CC is auto-allowed.
  void addCCPassthrough(int incomingCC, int outgoingCC) {
    if (incomingCC < 0 || incomingCC > 127 || outgoingCC < 0 || outgoingCC > 127)
      return;
    juce::SpinLock::ScopedLockType lock(ccPassthroughLock);
    ccPassthroughMap[incomingCC] = outgoingCC;
    allowedCCs[incomingCC].store(true);
  }

  void removeCCPassthrough(int incomingCC) {
    juce::SpinLock::ScopedLockType lock(ccPassthroughLock);
    ccPassthroughMap.erase(incomingCC);
  }

  void clearAllCCPassthroughs() {
    juce::SpinLock::ScopedLockType lock(ccPassthroughLock);
    ccPassthroughMap.clear();
  }

  const std::map<int, int> &getCCPassthroughMap() const {
    return ccPassthroughMap;
  }

  // --- CC to Parameter Mapping ---
  struct CCMapping {
    int chainIndex;
    int parameterIndex = -1;
    juce::String paramId;
    float minValue = 0.0f; // Parameter range minimum (0-1)
    float maxValue = 1.0f; // Parameter range maximum (0-1)
    bool invert = false;   // Flip CC direction (drawbar behaviour)
  };

  void mapCCToParameter(int ccNum, int chainIndex, const juce::String& paramId, int paramIndex,
                        float minVal = 0.0f, float maxVal = 1.0f, bool inv = false) {
    juce::SpinLock::ScopedLockType lock(ccMappingLock);
    ccParameterMappings[ccNum] = {chainIndex, paramIndex, paramId, minVal, maxVal, inv};
    if (ccNum >= 0 && ccNum < 128) {
      allowedCCs[ccNum].store(true);
    }
  }

  void mapCCToParameter(int ccNum, int chainIndex, int paramIndex,
                        float minVal = 0.0f, float maxVal = 1.0f, bool inv = false) {
    juce::String paramId;
    if (auto *plugin = getPluginInstance(chainIndex)) {
      auto &params = plugin->getParameters();
      if (paramIndex >= 0 && paramIndex < (int)params.size()) {
        if (auto *withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(params[paramIndex]))
          paramId = withId->paramID;
      }
    }
    mapCCToParameter(ccNum, chainIndex, paramId, paramIndex, minVal, maxVal, inv);
  }

  void unmapCC(int ccNum) {
    juce::SpinLock::ScopedLockType lock(ccMappingLock);
    ccParameterMappings.erase(ccNum);
  }

  void setCCInvert(int ccNum, bool inv) {
    juce::SpinLock::ScopedLockType lock(ccMappingLock);
    auto it = ccParameterMappings.find(ccNum);
    if (it != ccParameterMappings.end())
      it->second.invert = inv;
  }

  void clearAllCCMappings() {
    juce::SpinLock::ScopedLockType lock(ccMappingLock);
    ccParameterMappings.clear();
  }

  const std::map<int, CCMapping> &getCCMappings() const {
    return ccParameterMappings;
  }

  juce::StringArray getParameterNames(int chainIndex) const {
    juce::StringArray names;
    if (chainIndex >= 0 && chainIndex < (int)pluginChain.size()) {
      if (auto *plugin = pluginChain[chainIndex].get()) {
        for (auto *param : plugin->getParameters())
          names.add(param->getName(64));
      }
    }
    return names;
  }

  // Apply CC passthrough remaps: rewrite CC numbers in the buffer so the
  // plugin receives the remapped CC. Must be called AFTER applyCCMappings
  // (parameter bindings use the original CC number) and BEFORE plugins consume
  // the buffer.
  void applyCCPassthrough(juce::MidiBuffer &midiMessages) {
    juce::SpinLock::ScopedLockType lock(ccPassthroughLock);
    if (ccPassthroughMap.empty())
      return;

    passthroughScratch.clear();
    for (const auto &meta : midiMessages) {
      auto msg = meta.getMessage();
      if (msg.isController()) {
        int ccNum = msg.getControllerNumber();
        auto it = ccPassthroughMap.find(ccNum);
        if (it != ccPassthroughMap.end()) {
          msg = juce::MidiMessage::controllerEvent(
              msg.getChannel(), it->second, msg.getControllerValue());
        }
      }
      passthroughScratch.addEvent(msg, meta.samplePosition);
    }
    midiMessages.swapWith(passthroughScratch);
  }

  // Apply CC values to mapped parameters (call in processBlock)
  void applyCCMappings(const juce::MidiBuffer &midiMessages) {
    juce::SpinLock::ScopedLockType lock(ccMappingLock);
    for (const auto &meta : midiMessages) {
      auto msg = meta.getMessage();
      if (msg.isController()) {
        int ccNum = msg.getControllerNumber();
        int ccVal = msg.getControllerValue();

        // CC routing is working - verbose logging disabled
        // DBG("[" + slotName + "] CC" + juce::String(ccNum) + "=" +
        //     juce::String(ccVal));

        // FOH Level Control
        if (fohCC.load() >= 0 && ccNum == fohCC.load()) {
          fohLevel.store(ccVal / 127.0f);
        }

        // IEM Level Control
        if (iemCC.load() >= 0 && ccNum == iemCC.load()) {
          iemLevel.store(ccVal / 127.0f);
        }

        // Handle CC-to-Parameter mappings
        auto it = ccParameterMappings.find(ccNum);
        if (it != ccParameterMappings.end()) {
          auto &map = it->second;
          if (auto *plugin = getPluginInstance(map.chainIndex)) {
            auto &params = plugin->getParameters();
            juce::AudioProcessorParameter* targetParam = nullptr;

            // 1. Resolve by paramId if not empty
            if (map.paramId.isNotEmpty()) {
              for (auto* param : params) {
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                  if (withId->paramID == map.paramId) {
                    targetParam = param;
                    break;
                  }
                }
              }
            }

            // 2. Fallback to index
            if (targetParam == nullptr && map.parameterIndex >= 0 && map.parameterIndex < (int)params.size()) {
              targetParam = params[map.parameterIndex];
            }

            if (targetParam != nullptr) {
              float normalized = ccVal / 127.0f;
              if (map.invert) {
                normalized = 1.0f - normalized;
              }
              float scaled = map.minValue + normalized * (map.maxValue - map.minValue);
              targetParam->setValueNotifyingHost(scaled);
            }
          }
        }
      }
    }
  }

  // --- Midi Mapping ---
  int getFohCC() const { return fohCC.load(); }
  void setFohCC(int cc) { fohCC.store(cc); }

  int getIemCC() const { return iemCC.load(); }
  void setIemCC(int cc) { iemCC.store(cc); }

  // Per-slot MIDI channel override. -1 means "use the engine's global
  // default channel". 0 means Omni (accepts every channel). 1..16 pins this
  // slot to a single channel.
  int getMidiChannelOverride() const { return midiChannelOverride.load(); }
  void setMidiChannelOverride(int channel) {
    midiChannelOverride.store(juce::jlimit(-2, 16, channel));
  }

private:
  juce::String slotName;
  int iconIndex = 0; // ChannelIcon cast to int for persistence
  juce::Colour channelColor{0xff2a2a2a}; // Default zebra color (even slots)
  std::vector<std::unique_ptr<juce::AudioPluginInstance>> pluginChain;
  ChainSlotSettings chainSettings[3];
  juce::AudioBuffer<float> scratchBuffer;
  std::atomic<int> inputChannelIndex{-1}; // -1 means no hardware input (MIDI only)

  std::atomic<float> fohLevel{0.8f}; // FOH (house) level
  std::atomic<float> iemLevel{0.8f}; // IEM (monitor) level
  float lastFohLevel = 0.8f;
  float lastIemLevel = 0.8f;
  std::atomic<bool> fohEnabled{true};
  std::atomic<bool> iemEnabled{true};
  std::atomic<bool> bypassed{false};
  std::atomic<bool> fadersLinked{true}; // Faders move together by default

  std::atomic<int> fohCC{-1};   // CC for FOH level
  std::atomic<int> iemCC{-1};   // CC for IEM level
  std::atomic<int> levelCC{-1}; // CC for general level control
  std::atomic<int> midiChannelOverride{-1}; // -1 = global default, 0 = Omni

  std::atomic<int> lowNote{0};    // Note range low (inclusive)
  std::atomic<int> highNote{127}; // Note range high (inclusive)
  std::atomic<int> transposeOctaves{0};
  std::atomic<int> transposeSemitones{0};

  // CC filter: only these CC numbers pass through to this slot
  // Using atomic array for thread-safe access (no heap allocation/locks on
  // audio thread)
  std::atomic<bool> allowedCCs[128];

  // CC to parameter mappings
  std::map<int, CCMapping> ccParameterMappings;
  juce::SpinLock ccMappingLock; // Thread-safe access for audio thread

  // CC passthrough remap (incoming CC -> outgoing CC)
  std::map<int, int> ccPassthroughMap;
  juce::SpinLock ccPassthroughLock;
  juce::MidiBuffer passthroughScratch; // pre-allocated to avoid per-block alloc

  std::atomic<float> leftPeak{0.0f};
  std::atomic<float> rightPeak{0.0f};
  std::atomic<bool> midiActivity{false};
  std::atomic<bool> inputActive{false};

  SimpleArpeggiator arpeggiator;
  OctaveHarmonizer harmonizer;
  SamplerProcessor sampler;

  OpenRigDSP::ChannelStripProcessor strip;

  juce::SpinLock injectedMidiLock;
  juce::MidiBuffer injectedMidi;
  std::function<void(int, const juce::MidiBuffer &)> midiRouteCallback;
  std::atomic<float> cpuUsage{0.0f};
  double lastSampleRate = 44100.0;

  std::atomic<float> aux1SendLevel{0.0f};
  std::atomic<float> aux2SendLevel{0.0f};
  std::atomic<float> iemOffset{1.0f}; // 1.0 = same as FOH, >1.0 = louder in IEM

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackSlot)
};
