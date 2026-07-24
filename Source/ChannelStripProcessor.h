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
    y1 = out;
    return out;
  }

private:
  float a1 = 0.0f, a2 = 0.0f;
  float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
  float z1 = 0.0f, z2 = 0.0f; // Inputs
  float y1 = 0.0f, y2 = 0.0f; // Outputs
};

class SimpleEQ {
public:
  void prepare(double sampleRate) {
    sr = sampleRate;
    updateCoefficients();
  }

  void setEnabled(bool enabled) { isActive = enabled; }
  void setHighPass(float freq) {
    if (std::abs(hpfFreq - freq) > 0.001f) {
      hpfFreq = freq;
      updateCoefficients();
    }
  }
  void setLowShelf(float db) {
    if (std::abs(lowShelfGain - db) > 0.001f) {
      lowShelfGain = db;
      updateCoefficients();
    }
  }
  void setHighShelf(float db) {
    if (std::abs(highShelfGain - db) > 0.001f) {
      highShelfGain = db;
      updateCoefficients();
    }
  }

  float process(float input) {
    if (!isActive)
      return input;
    float out = hpf.process(input);
    out = lowShelf.process(out);
    out = highShelf.process(out);
    return out;
  }

private:
  double sr = 44100.0;
  bool isActive = false;
  float hpfFreq = 20.0f;
  float lowShelfGain = 0.0f;
  float highShelfGain = 0.0f;

  Biquad hpf, lowShelf, highShelf;

  void updateCoefficients() {
    float w0, sn, cs, alpha, A;

    // HPF (Butterworth Q=0.707)
    w0 = 2.0f * PI * hpfFreq / (float)sr;
    cs = std::cos(w0);
    alpha = std::sin(w0) / (2.0f * 0.707f);
    float fa0 = 1.0f + alpha;
    hpf.setCoefficients(fa0, -2.0f * cs, 1.0f - alpha, (1.0f + cs) / 2.0f,
                        -(1.0f + cs), (1.0f + cs) / 2.0f);

    // Low Shelf (200Hz)
    w0 = 2.0f * PI * 200.0f / (float)sr;
    cs = std::cos(w0);
    sn = std::sin(w0);
    A = std::pow(10.0f, lowShelfGain / 40.0f);
    alpha =
        sn / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / 0.707f - 1.0f) + 2.0f);
    float ls_b0 =
        A * ((A + 1.0f) - (A - 1.0f) * cs + 2.0f * std::sqrt(A) * alpha);
    float ls_b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
    float ls_b2 =
        A * ((A + 1.0f) - (A - 1.0f) * cs - 2.0f * std::sqrt(A) * alpha);
    float ls_a0 = (A + 1.0f) + (A - 1.0f) * cs + 2.0f * std::sqrt(A) * alpha;
    float ls_a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
    float ls_a2 = (A + 1.0f) + (A - 1.0f) * cs - 2.0f * std::sqrt(A) * alpha;
    lowShelf.setCoefficients(ls_a0, ls_a1, ls_a2, ls_b0, ls_b1, ls_b2);

    // High Shelf (5000Hz)
    w0 = 2.0f * PI * 5000.0f / (float)sr;
    cs = std::cos(w0);
    sn = std::sin(w0);
    A = std::pow(10.0f, highShelfGain / 40.0f);
    alpha =
        sn / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / 0.707f - 1.0f) + 2.0f);
    float hs_b0 =
        A * ((A + 1.0f) + (A - 1.0f) * cs + 2.0f * std::sqrt(A) * alpha);
    float hs_b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
    float hs_b2 =
        A * ((A + 1.0f) + (A - 1.0f) * cs - 2.0f * std::sqrt(A) * alpha);
    float hs_a0 = (A + 1.0f) - (A - 1.0f) * cs + 2.0f * std::sqrt(A) * alpha;
    float hs_a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
    float hs_a2 = (A + 1.0f) - (A - 1.0f) * cs - 2.0f * std::sqrt(A) * alpha;
    highShelf.setCoefficients(hs_a0, hs_a1, hs_a2, hs_b0, hs_b1, hs_b2);
  }
};

// ==============================================================================
// SIMPLE COMPRESSOR
// ==============================================================================
class SimpleComp {
public:
  void prepare(double sampleRate) {
    sr = sampleRate;
    attackCoeff = 1.0f - std::exp(-1.0f / (0.01f * (float)sr));   // 10ms
    releaseCoeff = 1.0f - std::exp(-1.0f / (0.2f * (float)sr));    // 200ms
  }

  void setEnabled(bool enabled) { isActive = enabled; }
  void setAmount(float amt) { // 0.0 to 1.0
    amount = amt;
    thresholdDb = -10.0f - (30.0f * amount);
    ratio = 2.0f + (6.0f * amount);
    makeupGainDb = 10.0f * amount;
  }

  float getGainReductionDb() const { return currentGrDb.load(); }

  float process(float input) {
    if (!isActive) {
      currentGrDb.store(0.0f);
      return input;
    }

    float inputAbs = std::abs(input);
    float inputDb =
        (inputAbs > 0.000001f) ? 20.0f * std::log10(inputAbs) : -100.0f;

    float overDb = inputDb - thresholdDb;
    if (overDb < 0.0f)
      overDb = 0.0f;

    float grDb = overDb * (1.0f - 1.0f / ratio);
    float coeff = (grDb > envelopeDb) ? attackCoeff : releaseCoeff;
    envelopeDb += (grDb - envelopeDb) * coeff;

    currentGrDb.store(envelopeDb); // Store for UI

    float totalGainDb = makeupGainDb - envelopeDb;
    float totalGain = std::pow(10.0f, totalGainDb / 20.0f);

    return input * totalGain;
  }

private:
  double sr = 44100.0;
  bool isActive = false;
  float amount = 0.0f;
  float thresholdDb = -10.0f;
  float ratio = 2.0f;
  float makeupGainDb = 0.0f;

  float envelopeDb = 0.0f;
  std::atomic<float> currentGrDb{0.0f};

  // 10ms Attack, 200ms Release
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
    juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
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

  std::atomic<bool> compEnabled{false};
  std::atomic<float> compAmount{0.0f};

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
    // Read atomics once for loop consistency
    bool gEnabled = gateEnabled.load();
    float gThresh = gateThreshold.load();
    bool eEnabled = eqEnabled.load();
    float hpfF = hpfFreq.load();
    float lsG = lowShelfGain.load();
    float hsG = highShelfGain.load();
    bool cEnabled = compEnabled.load();
    float cAmt = compAmount.load();
    
    bool choEnabled = chorusEnabled.load();
    float choRate = chorusRate.load();
    float choMix = chorusMix.load();

    gateL.setEnabled(gEnabled);
    gateL.setThreshold(gThresh);
    gateR.setEnabled(gEnabled);
    gateR.setThreshold(gThresh);

    eqL.setEnabled(eEnabled);
    eqL.setHighPass(hpfF);
    eqL.setLowShelf(lsG);
    eqL.setHighShelf(hsG);
    eqR.setEnabled(eEnabled);
    eqR.setHighPass(hpfF);
    eqR.setLowShelf(lsG);
    eqR.setHighShelf(hsG);

    compL.setEnabled(cEnabled);
    compL.setAmount(cAmt);
    compR.setEnabled(cEnabled);
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
