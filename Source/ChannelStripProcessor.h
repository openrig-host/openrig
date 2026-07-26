#pragma once

#include "OpenRigConstants.h"
#include "JuceHeader.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cmath>

// ==============================================================================
// OpenRig Channel Strip DSP
// Dependency-free implementation of Gate, EQ, and Compressor
// ==============================================================================

namespace OpenRigDSP {

// Simple math constants
constexpr float PI = 3.14159265359f;

// ==============================================================================
// SIMPLE GATE
// ==============================================================================
class SimpleGate {
public:
  void prepare(double sampleRate) {
    sr = sampleRate;
    attackCoeff = 1.0f - std::exp(-1.0f / (0.001f * (float)sr));  // 1ms
    releaseCoeff = 1.0f - std::exp(-1.0f / (0.2f * (float)sr));   // 200ms
  }

  void setThreshold(float db) { threshold = db; }
  void setEnabled(bool enabled) { isActive = enabled; }

  float process(float input) {
    if (!isActive)
      return input;

    float inputAbs = std::abs(input);
    float inputDb =
        (inputAbs > 0.000001f) ? 20.0f * std::log10(inputAbs) : -100.0f;

    // Fast Attack (1ms), Moderate Release (200ms)
    float targetGain = (inputDb > threshold) ? 1.0f : 0.0f;
    float coeff = (targetGain < currentGain) ? releaseCoeff : attackCoeff;

    currentGain += (targetGain - currentGain) * coeff;

    return input * currentGain;
  }

private:
  double sr = 44100.0;
  bool isActive = false;
  float threshold = -60.0f;
  float currentGain = 0.0f;

  float attackCoeff = 0.05f;
  float releaseCoeff = 0.0002f;
};

// ==============================================================================
// SIMPLE EQ (Biquad)
// ==============================================================================
class Biquad {
public:
  // Coefficients are normalized by a0 on the way in so process() avoids a
  // per-sample division (and the divide-by-zero / inf risk if a0 == 0). A
  // degenerate a0 configures a safe passthrough instead of corrupting the chain.
  void setCoefficients(float newA0, float newA1, float newA2, float newB0,
                       float newB1, float newB2) {
    if (std::abs(newA0) < 1.0e-12f) {
      a1 = a2 = b1 = b2 = 0.0f;
      b0 = 1.0f;
      return;
    }
    const float inv = 1.0f / newA0;
    a1 = newA1 * inv;
    a2 = newA2 * inv;
    b0 = newB0 * inv;
    b1 = newB1 * inv;
    b2 = newB2 * inv;
  }

  void reset() { z1 = z2 = y1 = y2 = 0.0f; }

  float process(float x) {
    float out = b0 * x + b1 * z1 + b2 * z2 - a1 * y1 - a2 * y2;
    z2 = z1;
    z1 = x;
    y2 = y1;
    y1 = (std::abs(out) < 1.0e-15f) ? 0.0f : out;
    return y1;
  }

private:
  float a1 = 0.0f, a2 = 0.0f;
  float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
  float z1 = 0.0f, z2 = 0.0f; // Inputs
  float y1 = 0.0f, y2 = 0.0f; // Outputs
};

class SimpleEQ {
public:
  static constexpr float kBandFreqs[10] = {
    31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
  };

  void prepare(double sampleRate) {
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (int i = 0; i < 10; ++i) {
      bands[i].reset();
      updateBand(i, bandGains[i]);
    }
  }

  void setEnabled(bool enabled) { isActive = enabled; }

  void setBandGain(int index, float gainDb) {
    if (index >= 0 && index < 10) {
      if (std::abs(bandGains[index] - gainDb) > 0.01f) {
        bandGains[index] = gainDb;
        updateBand(index, gainDb);
      }
    }
  }

  void setVolGain(float db) {
    volGainLinear = std::pow(10.0f, db / 20.0f);
  }

  void setMasterGain(float db) {
    masterGainLinear = std::pow(10.0f, db / 20.0f);
  }

  // Legacy compatibility helpers
  void setHighPass(float freq) { juce::ignoreUnused(freq); }
  void setLowShelf(float db) { setBandGain(1, db); }
  void setHighShelf(float db) { setBandGain(8, db); }

  float process(float input) {
    if (!isActive)
      return input;

    float out = input * volGainLinear;
    for (int i = 0; i < 10; ++i) {
      out = bands[i].process(out);
    }
    return out * masterGainLinear;
  }

private:
  double sr = 44100.0;
  bool isActive = false;
  float bandGains[10]{0.0f};
  float volGainLinear = 1.0f;
  float masterGainLinear = 1.0f;
  Biquad bands[10];

  void updateBand(int idx, float gainDb) {
    float f0 = kBandFreqs[idx];
    if (std::abs(gainDb) < 0.05f) {
      bands[idx].setCoefficients(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
      return;
    }
    float w0 = 2.0f * PI * f0 / (float)sr;
    float cs = std::cos(w0);
    float sn = std::sin(w0);
    float A = std::pow(10.0f, gainDb / 40.0f);
    float alpha = sn / (2.0f * 1.414f);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cs;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cs;
    float a2 = 1.0f - alpha / A;

    bands[idx].setCoefficients(a0, a1, a2, b0, b1, b2);
  }
};

// ==============================================================================
// UPGRADED COMPRESSOR WITH PRESETS & MAKEUP GAIN
// ==============================================================================
enum class CompPreset {
  Manual = 0,
  MP3Leveler,    // Smooth MP3 / Playlist Leveler
  VocalFocus,    // Vocal / Lead Focus
  DrumsPunch,    // Punchy Drums / Beats
  HardLimiter    // Brickwall Peak Control
};

class SimpleComp {
public:
  void prepare(double sampleRate) {
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    updateCoefficients(0.010f, 0.200f);
  }

  void setEnabled(bool enabled) { isActive = enabled; }

  void updateCoefficients(float attackSec, float releaseSec) {
    float att = juce::jmax(0.0005f, attackSec);
    float rel = juce::jmax(0.005f, releaseSec);
    attackCoeff = 1.0f - std::exp(-1.0f / (att * (float)sr));
    releaseCoeff = 1.0f - std::exp(-1.0f / (rel * (float)sr));
  }

  void setPreset(int pIdx, float userMakeupDb = 0.0f) {
    presetIndex = pIdx;
    userMakeupGainDb = userMakeupDb;

    switch (pIdx) {
    case 1: // MP3 / Track Leveler
      thresholdDb = -26.0f;
      ratio = 3.5f;
      presetMakeupDb = 6.0f;
      updateCoefficients(0.015f, 0.280f);
      break;
    case 2: // Vocal / Lead Focus
      thresholdDb = -20.0f;
      ratio = 4.0f;
      presetMakeupDb = 4.0f;
      updateCoefficients(0.005f, 0.180f);
      break;
    case 3: // Punchy Drums / Beats
      thresholdDb = -16.0f;
      ratio = 4.0f;
      presetMakeupDb = 3.0f;
      updateCoefficients(0.030f, 0.100f);
      break;
    case 4: // Brickwall Peak Control / Limiter
      thresholdDb = -4.0f;
      ratio = 12.0f;
      presetMakeupDb = 0.0f;
      updateCoefficients(0.001f, 0.050f);
      break;
    case 0: // Manual (driven by amount knob)
    default:
      thresholdDb = -10.0f - (30.0f * amount);
      ratio = 2.0f + (6.0f * amount);
      presetMakeupDb = 6.0f * amount;
      updateCoefficients(0.010f, 0.200f);
      break;
    }
  }

  void setAmount(float amt) {
    amount = amt;
    if (presetIndex == 0) {
      setPreset(0, userMakeupGainDb);
    }
  }

  void setMakeupGain(float db) {
    userMakeupGainDb = db;
  }

  float getGainReductionDb() const { return currentGrDb.load(); }

  float process(float input) {
    if (!isActive) {
      currentGrDb.store(0.0f);
      return input;
    }

    float inputAbs = std::abs(input);
    float inputDb = (inputAbs > 0.000001f) ? 20.0f * std::log10(inputAbs) : -100.0f;

    float overDb = inputDb - thresholdDb;
    if (overDb < 0.0f)
      overDb = 0.0f;

    float grDb = overDb * (1.0f - 1.0f / ratio);
    float coeff = (grDb > envelopeDb) ? attackCoeff : releaseCoeff;
    envelopeDb += (grDb - envelopeDb) * coeff;
    if (std::abs(envelopeDb) < 1.0e-9f) envelopeDb = 0.0f;

    currentGrDb.store(envelopeDb);

    float totalGainDb = presetMakeupDb + userMakeupGainDb - envelopeDb;
    float totalGain = juce::Decibels::decibelsToGain(totalGainDb);

    return input * totalGain;
  }

private:
  double sr = 44100.0;
  bool isActive = false;
  int presetIndex = 0;
  float amount = 0.0f;
  float thresholdDb = -10.0f;
  float ratio = 2.0f;
  float presetMakeupDb = 0.0f;
  float userMakeupGainDb = 0.0f;

  float envelopeDb = 0.0f;
  std::atomic<float> currentGrDb{0.0f};

  float attackCoeff = 0.01f;
  float releaseCoeff = 0.001f;
};

// ==============================================================================
// SIMPLE CHORUS
// ==============================================================================
class SimpleChorus {
public:
  SimpleChorus() {
    std::fill(std::begin(buffer), std::end(buffer), 0.0f);
  }

  void prepare(double sampleRate) {
    sr = sampleRate;
    std::fill(std::begin(buffer), std::end(buffer), 0.0f);
    writeIndex = 0;
    lfoPhase = 0.0f;
  }

  void setEnabled(bool enabled) { isActive = enabled; }
  void setRate(float hz) { rate = hz; }
  void setMix(float mx) { mix = mx; }

  float process(float input, float lfoPhaseOffset) {
    if (!isActive || mix <= 0.001f)
      return input;

    // Modulate LFO
    float lfo = std::sin(lfoPhase + lfoPhaseOffset);
    lfoPhase += 2.0f * PI * rate / (float)sr;
    if (lfoPhase >= 2.0f * PI)
      lfoPhase -= 2.0f * PI;

    // Delay ranges between 10ms and 20ms
    float delayMs = 15.0f + 5.0f * lfo;
    float delaySamples = delayMs * 0.001f * (float)sr;

    // Read with linear interpolation
    float readPos = (float)writeIndex - delaySamples;
    while (readPos < 0.0f)
      readPos += (float)bufferSize;

    int idx0 = (int)readPos % bufferSize;
    int idx1 = (idx0 + 1) % bufferSize;
    float frac = readPos - (float)((int)readPos);

    float wet = buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;

    buffer[writeIndex] = input;
    writeIndex = (writeIndex + 1) % bufferSize;

    return input * (1.0f - mix) + wet * mix;
  }

private:
  double sr = 44100.0;
  bool isActive = false;
  float rate = 1.0f;
  float mix = 0.0f;

  static constexpr int bufferSize = 4096;
  float buffer[bufferSize];
  int writeIndex = 0;
  float lfoPhase = 0.0f;
};

// ==============================================================================
// CONVOLUTION REVERB (IR loader)
// Impulse-response reverb / cab sim via juce::dsp::Convolution. loadIR() is
// wait-free-safe from the message thread while processBlock() runs on the
// audio thread (Convolution swaps the IR atomically when ready).
// ==============================================================================
class ConvolutionReverb {
public:
  void prepare(double sampleRate, int maxBlockSize) {
    sr = sampleRate;
    blockSize = juce::jmax(maxBlockSize, 8192);
    wetBuffer.setSize(2, blockSize, false, false, true);
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32) blockSize;
    spec.numChannels = 2;
    convolution.prepare(spec);
  }

  // Loads an IR (.wav). Only reloads if the path changed. Message-thread only.
  bool loadIR(const juce::File &f) {
    if (!f.existsAsFile()) return false;
    if (f.getFullPathName() == loadedPath && hasIR) return true;
    convolution.loadImpulseResponse(f,
        juce::dsp::Convolution::Stereo::yes,
        juce::dsp::Convolution::Trim::yes,
        0,
        juce::dsp::Convolution::Normalise::yes);
    loadedPath = f.getFullPathName();
    hasIR = true;
    return true;
  }

  bool hasIrLoaded() const { return hasIR; }
  const juce::String &getLoadedPath() const { return loadedPath; }

  void processBlock(juce::AudioBuffer<float> &buffer, bool enabled, float mix) {
    if (!enabled || !hasIR || mix <= 0.001f)
      return;
    const int n = juce::jmin(buffer.getNumSamples(), wetBuffer.getNumSamples());
    const int chans = buffer.getNumChannels();
    if (n <= 0) return;
    for (int c = 0; c < 2; ++c)
      wetBuffer.copyFrom(c, 0, buffer, juce::jmin(c, chans - 1), 0, n);
    juce::dsp::AudioBlock<float> wetBlock = juce::dsp::AudioBlock<float>(wetBuffer).getSubBlock(0, (size_t)n);
    juce::dsp::ProcessContextReplacing<float> ctx(wetBlock);
    convolution.process(ctx);
    const float w = mix, d = 1.0f - mix;
    for (int c = 0; c < chans; ++c) {
      auto *dry = buffer.getWritePointer(c);
      const float *wet = wetBuffer.getReadPointer(juce::jmin(c, 1));
      for (int i = 0; i < n; ++i)
        dry[i] = dry[i] * d + wet[i] * w;
    }
  }

  void reset() {
    convolution.reset();
    wetBuffer.clear();
  }

private:
  juce::dsp::Convolution convolution;
  juce::AudioBuffer<float> wetBuffer;
  double sr = 44100.0;
  int blockSize = 512;
  bool hasIR = false;
  juce::String loadedPath;
};

// ==============================================================================
// MAIN PROCESSOR WRAPPER
// ==============================================================================
class ChannelStripProcessor {
public:
  ChannelStripProcessor() = default;

  SimpleGate gateL, gateR;
  SimpleEQ eqL, eqR;
  SimpleComp compL, compR;
  SimpleChorus chorusL, chorusR;
  juce::Reverb reverb;
  ConvolutionReverb irReverb;

  // Parameters (Atomic for thread-safety)
  std::atomic<bool> gateEnabled{false};
  std::atomic<float> gateThreshold{-60.0f};

  std::atomic<bool> eqEnabled{false};
  std::atomic<float> hpfFreq{20.0f};
  std::atomic<float> lowShelfGain{0.0f};
  std::atomic<float> highShelfGain{0.0f};
  std::atomic<float> eqVolGain{0.0f};
  std::atomic<float> eqMasterGain{0.0f};
  std::atomic<float> eqBands[10]{};

  std::atomic<bool> compEnabled{false};
  std::atomic<float> compAmount{0.0f};
  std::atomic<float> compMakeupDb{0.0f};
  std::atomic<int> compPreset{0};

  std::atomic<bool> chorusEnabled{false};
  std::atomic<float> chorusRate{1.0f};
  std::atomic<float> chorusMix{0.0f};

  std::atomic<bool> reverbEnabled{false};
  std::atomic<float> reverbSize{0.5f};
  std::atomic<float> reverbMix{0.0f};

  // IR / convolution reverb
  std::atomic<bool> irEnabled{false};
  std::atomic<float> irMix{0.3f};

  ConvolutionReverb &getIRReverb() { return irReverb; }
  const ConvolutionReverb &getIRReverb() const { return irReverb; }

  void prepare(double sampleRate) {
    gateL.prepare(sampleRate);
    gateR.prepare(sampleRate);
    eqL.prepare(sampleRate);
    eqR.prepare(sampleRate);
    compL.prepare(sampleRate);
    compR.prepare(sampleRate);
    chorusL.prepare(sampleRate);
    chorusR.prepare(sampleRate);
    reverb.setSampleRate(sampleRate);
    irReverb.prepare(sampleRate, 4096);
  }

  void processBlock(juce::AudioBuffer<float> &buffer) {
    bool gEnabled = gateEnabled.load();
    float gThresh = gateThreshold.load();
    bool eEnabled = eqEnabled.load();
    float vGain = eqVolGain.load();
    float mGain = eqMasterGain.load();
    bool cEnabled = compEnabled.load();
    float cAmt = compAmount.load();
    float cMakeup = compMakeupDb.load();
    int cPreset = compPreset.load();
    
    bool choEnabled = chorusEnabled.load();
    float choRate = chorusRate.load();
    float choMix = chorusMix.load();

    gateL.setEnabled(gEnabled);
    gateL.setThreshold(gThresh);
    gateR.setEnabled(gEnabled);
    gateR.setThreshold(gThresh);

    eqL.setEnabled(eEnabled);
    eqL.setVolGain(vGain);
    eqL.setMasterGain(mGain);
    eqR.setEnabled(eEnabled);
    eqR.setVolGain(vGain);
    eqR.setMasterGain(mGain);

    for (int b = 0; b < 10; ++b) {
      float bg = eqBands[b].load();
      eqL.setBandGain(b, bg);
      eqR.setBandGain(b, bg);
    }

    compL.setEnabled(cEnabled);
    compL.setPreset(cPreset, cMakeup);
    compL.setAmount(cAmt);
    compR.setEnabled(cEnabled);
    compR.setPreset(cPreset, cMakeup);
    compR.setAmount(cAmt);

    chorusL.setEnabled(choEnabled);
    chorusL.setRate(choRate);
    chorusL.setMix(choMix);
    chorusR.setEnabled(choEnabled);
    chorusR.setRate(choRate);
    chorusR.setMix(choMix);

    int numSamples = buffer.getNumSamples();
    auto *L = buffer.getWritePointer(0);
    auto *R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
      L[i] = gateL.process(L[i]);
      if (R)
        R[i] = gateR.process(R[i]);

      L[i] = eqL.process(L[i]);
      if (R)
        R[i] = eqR.process(R[i]);

      L[i] = compL.process(L[i]);
      if (R)
        R[i] = compR.process(R[i]);

      L[i] = chorusL.process(L[i], 0.0f);
      if (R)
        R[i] = chorusR.process(R[i], PI * 0.5f);
    }

    if (reverbEnabled.load() && reverbMix.load() > 0.001f) {
      juce::Reverb::Parameters rp;
      rp.roomSize = reverbSize.load();
      rp.wetLevel = reverbMix.load() * 0.5f;
      rp.dryLevel = 1.0f - reverbMix.load() * 0.3f;
      rp.width = 1.0f;
      rp.damping = 0.4f;
      reverb.setParameters(rp);

      if (R) {
        reverb.processStereo(L, R, numSamples);
      } else {
        reverb.processMono(L, numSamples);
      }
    }

    // IR / convolution reverb (impulse-response cab/room sim)
    irReverb.processBlock(buffer, irEnabled.load(), irMix.load());
  }

  float getGainReductionDb() const { return compL.getGainReductionDb(); }
  SimpleComp &getCompReference() { return compL; }

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripProcessor);
};

} // namespace OpenRigDSP
