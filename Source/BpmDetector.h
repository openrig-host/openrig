#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>

class BpmDetector {
public:
    struct BpmResult {
        bool success = false;
        float bpm = 0.0f;
        double firstBeatSeconds = 0.0;
        float confidence = 0.0f;
    };

    static BpmResult analyzeAudio(juce::AudioFormatReader* reader, double maxSecondsToAnalyze = 60.0) {
        BpmResult result;
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0) {
            return result;
        }

        double origSampleRate = reader->sampleRate;
        juce::int64 numSamplesToRead = (juce::int64)(maxSecondsToAnalyze * origSampleRate);
        numSamplesToRead = juce::jmin(numSamplesToRead, reader->lengthInSamples);

        if (numSamplesToRead < (juce::int64)(origSampleRate * 5.0)) {
            // Need at least 5 seconds of audio
            return result;
        }

        // Target downsample rate for fast autocorrelation (~11,025 Hz)
        int downsampleRatio = juce::jmax(1, (int)(origSampleRate / 11025.0));
        double targetSampleRate = origSampleRate / downsampleRatio;
        int targetLength = (int)(numSamplesToRead / downsampleRatio);

        std::vector<float> downsampledAudio(targetLength, 0.0f);

        // Read in blocks and downsample to mono
        const int blockSize = 16384;
        juce::AudioBuffer<float> tempBuffer((int)reader->numChannels, blockSize);

        juce::int64 samplesReadTotal = 0;
        int writeIdx = 0;

        while (samplesReadTotal < numSamplesToRead && writeIdx < targetLength) {
            int toRead = (int)juce::jmin((juce::int64)blockSize, numSamplesToRead - samplesReadTotal);
            reader->read(&tempBuffer, 0, toRead, samplesReadTotal, true, true);

            for (int i = 0; i < toRead; i += downsampleRatio) {
                if (writeIdx >= targetLength) break;
                float mono = 0.0f;
                for (int ch = 0; ch < tempBuffer.getNumChannels(); ++ch) {
                    mono += tempBuffer.getSample(ch, i);
                }
                mono /= (float)tempBuffer.getNumChannels();
                downsampledAudio[writeIdx++] = mono;
            }
            samplesReadTotal += toRead;
        }

        if (writeIdx < 1000) return result;
        targetLength = writeIdx;

        // 1. Low-Pass Filter (Simple 2nd order Butterworth at ~180 Hz to emphasize kick drum / bassline)
        float cutoff = 180.0f;
        float w0 = 2.0f * 3.14159265f * cutoff / (float)targetSampleRate;
        float alpha = std::sin(w0) / (2.0f * 0.707f);
        float cosw0 = std::cos(w0);

        float b0 = (1.0f - cosw0) / 2.0f;
        float b1 = 1.0f - cosw0;
        float b2 = (1.0f - cosw0) / 2.0f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha;

        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        std::vector<float> filteredAudio(targetLength, 0.0f);

        for (int i = 0; i < targetLength; ++i) {
            float x0 = downsampledAudio[i];
            float y0 = (b0 / a0) * x0 + (b1 / a0) * x1 + (b2 / a0) * x2 - (a1 / a0) * y1 - (a2 / a0) * y2;
            filteredAudio[i] = y0;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
        }

        // 2. Envelope & Transient Energy Follower (Rectification + Differentiation)
        int envDecimate = juce::jmax(1, (int)(targetSampleRate / 200.0)); // 200 Hz envelope rate
        double envRate = targetSampleRate / envDecimate;
        int envLength = targetLength / envDecimate;
        std::vector<float> envelope(envLength, 0.0f);

        float prevEnergy = 0.0f;
        for (int i = 0; i < envLength; ++i) {
            float sumSq = 0.0f;
            int startSample = i * envDecimate;
            for (int j = 0; j < envDecimate && (startSample + j) < targetLength; ++j) {
                float s = filteredAudio[startSample + j];
                sumSq += s * s;
            }
            float energy = std::sqrt(sumSq / (float)envDecimate);
            // Half-wave rectified derivative (transient energy spike)
            float diff = energy - prevEnergy;
            envelope[i] = (diff > 0.0f) ? diff : 0.0f;
            prevEnergy = energy;
        }

        // 3. Autocorrelation over 65 to 185 BPM
        float minBpm = 65.0f;
        float maxBpm = 185.0f;
        int minLag = (int)(envRate * 60.0 / maxBpm);
        int maxLag = (int)(envRate * 60.0 / minBpm);

        if (maxLag >= envLength / 2) {
            maxLag = envLength / 2 - 1;
        }
        if (minLag >= maxLag) return result;

        std::vector<float> autocorr(maxLag + 1, 0.0f);
        int numFrames = envLength - maxLag;

        float maxCorr = -1.0f;
        int bestLag = minLag;

        for (int lag = minLag; lag <= maxLag; ++lag) {
            float sum = 0.0f;
            for (int n = 0; n < numFrames; ++n) {
                sum += envelope[n] * envelope[n + lag];
            }
            autocorr[lag] = sum;
            if (sum > maxCorr) {
                maxCorr = sum;
                bestLag = lag;
            }
        }

        if (maxCorr <= 0.00001f) return result;

        // 4. Parabolic / Quadratic Peak Interpolation for Sub-Sample BPM Accuracy
        float delta = 0.0f;
        if (bestLag > minLag && bestLag < maxLag) {
            float y_prev = autocorr[bestLag - 1];
            float y_curr = autocorr[bestLag];
            float y_next = autocorr[bestLag + 1];
            float denom = 2.0f * (2.0f * y_curr - y_prev - y_next);
            if (std::abs(denom) > 0.0001f) {
                delta = (y_next - y_prev) / denom;
            }
        }

        float exactLag = (float)bestLag + delta;
        float rawBpm = (float)(envRate * 60.0 / exactLag);

        // Normalize to standard DJ range (75 - 165 BPM)
        while (rawBpm < 75.0f) rawBpm *= 2.0f;
        while (rawBpm > 165.0f) rawBpm /= 2.0f;

        // 5. Downbeat / Phase Estimation
        float beatPeriodSec = 60.0f / rawBpm;
        int beatSamples = (int)(beatPeriodSec * envRate);
        if (beatSamples > 0) {
            std::vector<float> phaseEnergy(beatSamples, 0.0f);
            for (int i = 0; i < envLength; ++i) {
                int ph = i % beatSamples;
                phaseEnergy[ph] += envelope[i];
            }
            int bestPhase = 0;
            float maxPhaseE = -1.0f;
            for (int ph = 0; ph < beatSamples; ++ph) {
                if (phaseEnergy[ph] > maxPhaseE) {
                    maxPhaseE = phaseEnergy[ph];
                    bestPhase = ph;
                }
            }
            result.firstBeatSeconds = (double)bestPhase / envRate;
        }

        result.bpm = std::round(rawBpm * 10.0f) / 10.0f; // Round to 1 decimal place (e.g. 124.5)
        result.confidence = juce::jlimit(0.0f, 1.0f, maxCorr / (autocorr[minLag] + 0.0001f));
        result.success = (result.bpm >= 60.0f && result.bpm <= 200.0f);

        return result;
    }

    static void detectBpmAsync(juce::AudioFormatManager& formatManager, const juce::File& file, std::function<void(const BpmResult&)> callback) {
        if (!file.existsAsFile()) {
            if (callback) callback(BpmResult());
            return;
        }

        juce::Thread::launch([&formatManager, file, callback]() {
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            BpmResult res = analyzeAudio(reader.get(), 60.0);
            juce::MessageManager::callAsync([callback, res]() {
                if (callback) callback(res);
            });
        });
    }
};
