#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>

// High-Performance Stereo WSOLA (Waveform Similarity Overlap-Add) Real-Time Time-Stretcher
class TimeStretcher {
public:
    TimeStretcher() {
        initWindow(grainSize);
    }

    void prepare(double sampleRate, int maxBlockSize = 8192) {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        
        // Compute grain parameters based on sample rate (~35ms grain window)
        grainSize = juce::jlimit(512, 2048, (int)(currentSampleRate * 0.035));
        // Ensure grainSize is even
        if (grainSize % 2 != 0) grainSize++;
        
        hopOut = grainSize / 2;
        searchRange = grainSize / 4;

        initWindow(grainSize);

        int maxFifo = juce::jmax(maxBlockSize * 4, 32768);
        inputFifo.setSize(2, maxFifo);
        inputFifo.clear();

        outputFifo.setSize(2, maxFifo);
        outputFifo.clear();

        prevGrain.setSize(2, grainSize);
        prevGrain.clear();

        inputReadPos = 0.0;
        fifoWritePos = 0;
        fifoReadPos = 0;
        outWritePos = 0;
        outReadPos = 0;
        samplesInInputFifo = 0;
        samplesInOutputFifo = 0;
        initializedFirstGrain = false;
    }

    void reset() {
        inputFifo.clear();
        outputFifo.clear();
        prevGrain.clear();
        inputReadPos = 0.0;
        fifoWritePos = 0;
        fifoReadPos = 0;
        outWritePos = 0;
        outReadPos = 0;
        samplesInInputFifo = 0;
        samplesInOutputFifo = 0;
        initializedFirstGrain = false;
    }

    void setTempoRatio(float ratio) {
        // Clamp ratio to safe ±20% DJ range (0.80x to 1.25x)
        tempoRatio.store(juce::jlimit(0.80f, 1.25f, ratio));
    }

    float getTempoRatio() const {
        return tempoRatio.load();
    }

    // Process a block of audio.
    // If tempoRatio is ~1.0, bypasses stretching with direct copy.
    void process(const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output, int numSamples) {
        float ratio = tempoRatio.load();

        // 1. Instant Bypass when ratio is 1.0 (0.0% CPU overhead)
        if (std::abs(ratio - 1.0f) < 0.002f) {
            for (int ch = 0; ch < output.getNumChannels(); ++ch) {
                int inCh = juce::jmin(ch, input.getNumChannels() - 1);
                output.copyFrom(ch, 0, input, inCh, 0, numSamples);
            }
            return;
        }

        // 2. Push input samples into input FIFO
        pushInput(input, numSamples);

        // 3. Generate stretched output grains while enough input exists
        float hopIn = (float)hopOut * ratio;
        int neededInputForGrain = grainSize + searchRange;

        while (samplesInInputFifo >= neededInputForGrain && samplesInOutputFifo + hopOut <= outputFifo.getNumSamples()) {
            synthesizeGrain(hopIn);
        }

        // 4. Pop available samples from output FIFO into output buffer
        popOutput(output, numSamples);
    }

private:
    void initWindow(int size) {
        window.resize(size);
        for (int i = 0; i < size; ++i) {
            // Hann window
            window[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * (float)i / (float)(size - 1)));
        }
    }

    void pushInput(const juce::AudioBuffer<float>& input, int numSamples) {
        int fifoSize = inputFifo.getNumSamples();
        int space = fifoSize - samplesInInputFifo;
        int toWrite = juce::jmin(numSamples, space);

        for (int ch = 0; ch < 2; ++ch) {
            int inCh = juce::jmin(ch, input.getNumChannels() - 1);
            int part1 = juce::jmin(toWrite, fifoSize - fifoWritePos);
            int part2 = toWrite - part1;

            inputFifo.copyFrom(ch, fifoWritePos, input, inCh, 0, part1);
            if (part2 > 0) {
                inputFifo.copyFrom(ch, 0, input, inCh, part1, part2);
            }
        }

        fifoWritePos = (fifoWritePos + toWrite) % fifoSize;
        samplesInInputFifo += toWrite;
    }

    void synthesizeGrain(float hopIn) {
        int fifoSize = inputFifo.getNumSamples();
        int outFifoSize = outputFifo.getNumSamples();

        if (!initializedFirstGrain) {
            // Read initial grain directly from input FIFO
            for (int ch = 0; ch < 2; ++ch) {
                for (int i = 0; i < grainSize; ++i) {
                    int readIdx = (fifoReadPos + i) % fifoSize;
                    float s = inputFifo.getSample(ch, readIdx);
                    prevGrain.setSample(ch, i, s);
                }
            }
            initializedFirstGrain = true;
            inputReadPos = 0.0;
        }

        // Find optimal offset around nominal input position using cross-correlation
        int bestOffset = 0;
        float maxCorr = -1e9f;

        const float* prevL = prevGrain.getReadPointer(0);
        const float* prevR = prevGrain.getReadPointer(1);

        for (int offset = -searchRange / 2; offset <= searchRange / 2; ++offset) {
            int basePos = (int)(inputReadPos + (double)hopIn + (double)offset);
            if (basePos < 0) continue;

            float corr = 0.0f;
            // Correlate over first half of grain (overlap region)
            int corrLen = hopOut;
            for (int i = 0; i < corrLen; i += 2) { // Step by 2 for SIMD-like speed
                int idx = (fifoReadPos + basePos + i) % fifoSize;
                float inL = inputFifo.getSample(0, idx);
                float inR = inputFifo.getSample(1, idx);
                corr += (inL * prevL[i + hopOut]) + (inR * prevR[i + hopOut]);
            }

            if (corr > maxCorr) {
                maxCorr = corr;
                bestOffset = offset;
            }
        }

        int actualReadBase = (int)(inputReadPos + (double)hopIn + (double)bestOffset);
        actualReadBase = juce::jmax(0, actualReadBase);

        // Overlap-add into output FIFO
        for (int i = 0; i < grainSize; ++i) {
            int inIdx = (fifoReadPos + actualReadBase + i) % fifoSize;
            int outIdx = (outWritePos + i) % outFifoSize;
            float w = window[i];

            for (int ch = 0; ch < 2; ++ch) {
                float newSample = inputFifo.getSample(ch, inIdx) * w;
                if (i < hopOut) {
                    // Overlap region
                    float existing = outputFifo.getSample(ch, outIdx);
                    outputFifo.setSample(ch, outIdx, existing + newSample);
                } else {
                    // Fresh region
                    outputFifo.setSample(ch, outIdx, newSample);
                }
                prevGrain.setSample(ch, i, inputFifo.getSample(ch, inIdx));
            }
        }

        // Advance positions
        outWritePos = (outWritePos + hopOut) % outFifoSize;
        samplesInOutputFifo += hopOut;

        // Advance input FIFO
        int samplesToDiscard = juce::jmin(samplesInInputFifo, (int)(hopIn + bestOffset));
        samplesToDiscard = juce::jmax(0, samplesToDiscard);

        fifoReadPos = (fifoReadPos + samplesToDiscard) % fifoSize;
        samplesInInputFifo -= samplesToDiscard;
        inputReadPos = 0.0;
    }

    void popOutput(juce::AudioBuffer<float>& output, int numSamples) {
        int toRead = juce::jmin(numSamples, samplesInOutputFifo);
        int outFifoSize = outputFifo.getNumSamples();

        for (int ch = 0; ch < output.getNumChannels(); ++ch) {
            int srcCh = juce::jmin(ch, 1);
            int part1 = juce::jmin(toRead, outFifoSize - outReadPos);
            int part2 = toRead - part1;

            output.copyFrom(ch, 0, outputFifo, srcCh, outReadPos, part1);
            if (part2 > 0) {
                output.copyFrom(ch, part1, outputFifo, srcCh, 0, part2);
            }

            // Zero remaining if underrun
            if (toRead < numSamples) {
                output.clear(ch, toRead, numSamples - toRead);
            }
        }

        outReadPos = (outReadPos + toRead) % outFifoSize;
        samplesInOutputFifo -= toRead;
    }

    double currentSampleRate = 44100.0;
    int grainSize = 1024;
    int hopOut = 512;
    int searchRange = 256;

    std::vector<float> window;
    juce::AudioBuffer<float> inputFifo;
    juce::AudioBuffer<float> outputFifo;
    juce::AudioBuffer<float> prevGrain;

    double inputReadPos = 0.0;
    int fifoWritePos = 0;
    int fifoReadPos = 0;
    int outWritePos = 0;
    int outReadPos = 0;
    int samplesInInputFifo = 0;
    int samplesInOutputFifo = 0;
    bool initializedFirstGrain = false;

    std::atomic<float> tempoRatio{1.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeStretcher)
};
