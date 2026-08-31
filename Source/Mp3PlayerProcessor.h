#pragma once

#include <JuceHeader.h>
#include "Logger.h"
#include "ChannelStripProcessor.h"
#include "BpmDetector.h"
#include "TimeStretcher.h"
#include "Database/LibraryDatabase.h"
#include <vector>
#include <map>
#include <random>
#include <atomic>
#include <cmath>

class Mp3PlayerProcessor {
public:
    enum class LoopMode { Off, RepeatTrack, RepeatAll };

    enum class SyncMode {
        CleanVinyl = 0, // High-order resampler (0 artifacts, pristine audio, vinyl/CDJ style)
        KeyLock = 1     // Time-stretch with pitch lock (WSOLA)
    };

    struct TrackInfo {
        juce::File file;
        juce::String title;
        double durationSeconds = 0.0;
        float bpm = 0.0f;
        double firstBeatSeconds = 0.0;
        double cueInSeconds = 0.0;
        double cueOutSeconds = 0.0;

        double getEffectiveStart() const { return juce::jmax(0.0, cueInSeconds); }
        double getEffectiveEnd() const {
            return (cueOutSeconds > cueInSeconds && cueOutSeconds <= durationSeconds) ? cueOutSeconds : durationSeconds;
        }
        double getEffectiveDuration() const {
            double s = getEffectiveStart();
            double e = getEffectiveEnd();
            return (e > s) ? (e - s) : (durationSeconds > s ? (durationSeconds - s) : durationSeconds);
        }
        bool hasCues() const {
            return cueInSeconds > 0.0 || (cueOutSeconds > 0.0 && cueOutSeconds < durationSeconds);
        }
    };

    struct PlaylistBank {
        int index = 0;
        juce::String name;
        juce::String folderPath;
        std::vector<TrackInfo> tracks;
    };

    std::function<void()> onPlayStarted;
    std::function<void()> onTrackChanged;
    std::function<void()> onPlaylistChanged;
    std::function<void()> onBanksChanged;

    Mp3PlayerProcessor() {
        formatManager.registerBasicFormats();
        formatManager.registerFormat(new juce::FlacAudioFormat(), true);
        formatManager.registerFormat(new juce::OggVorbisAudioFormat(), true);
        
        bufferingThreadA.startThread(juce::Thread::Priority::high);
        bufferingThreadB.startThread(juce::Thread::Priority::high);

        loadBpmCache();
        initializeDefaultBanks();
        loadBanksFromSettings();
        loadLastFolderFromSettings();
    }

    ~Mp3PlayerProcessor() {
        isShuttingDown.store(true);
        playing.store(false);
        
        stopDeckInternal(0);
        stopDeckInternal(1);

        bufferingThreadA.stopThread(1000);
        bufferingThreadB.stopThread(1000);
    }

    // --- Deck State Struct ---
    struct Deck {
        int deckId = 0; // 0 = Deck A, 1 = Deck B
        std::unique_ptr<juce::AudioFormatReaderSource> rawSource;
        std::unique_ptr<juce::BufferingAudioSource> bufferedSource;
        std::unique_ptr<juce::ResamplingAudioSource> resamplingSource;
        juce::MemoryBlock trackMemory;
        TrackInfo trackInfo;

        TimeStretcher timeStretcher;

        std::atomic<bool> isLoaded{false};
        std::atomic<bool> isPlaying{false};
        std::atomic<float> gain{0.0f};
        std::atomic<juce::int64> currentSamplePos{0};
        std::atomic<juce::int64> totalSampleLength{0};
        std::atomic<float> tempoRatio{1.0f};

        double sourceSampleRate = 44100.0;
        double currentHostSampleRate = 44100.0;
        juce::AudioBuffer<float> rawBuffer;
        juce::AudioBuffer<float> buffer;

        void prepare(double hostSampleRate, int maxBlockSize) {
            currentHostSampleRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
            int allocSize = juce::jmax(maxBlockSize * 2, 32768);
            rawBuffer.setSize(2, allocSize, false, false, true);
            rawBuffer.clear();
            buffer.setSize(2, allocSize, false, false, true);
            buffer.clear();
            timeStretcher.prepare(currentHostSampleRate, allocSize);
            if (resamplingSource != nullptr) {
                resamplingSource->prepareToPlay(allocSize, currentHostSampleRate);
            }
        }

        void reset() {
            isPlaying.store(false);
            isLoaded.store(false);
            gain.store(0.0f);
            currentSamplePos.store(0);
            totalSampleLength.store(0);
            tempoRatio.store(1.0f);
            sourceSampleRate = 44100.0;
            timeStretcher.reset();
            timeStretcher.setTempoRatio(1.0f);

            if (resamplingSource != nullptr) {
                resamplingSource.reset();
            }
            if (bufferedSource != nullptr) {
                bufferedSource.reset();
            }
            if (rawSource != nullptr) {
                rawSource.reset();
            }
            trackMemory.setSize(0);
            trackInfo = TrackInfo();
        }

        void setTempoRatio(float ratio) {
            tempoRatio.store(juce::jlimit(0.80f, 1.25f, ratio));
            timeStretcher.setTempoRatio(ratio);
        }

        float getTempoRatio() const {
            return tempoRatio.load();
        }

        double getBaseSampleRateRatio() const {
            if (sourceSampleRate > 0.0 && currentHostSampleRate > 0.0) {
                return sourceSampleRate / currentHostSampleRate;
            }
            return 1.0;
        }

        // High-Quality Sample-Rate Compensated Continuous Resampler & Key-Lock Renderer
        void renderBlock(int numSamples, bool useKeyLock) {
            if (!isPlaying.load() || resamplingSource == nullptr || bufferedSource == nullptr) {
                buffer.clear();
                return;
            }

            double baseRatio = getBaseSampleRateRatio();
            float userRatio = tempoRatio.load();

            if (useKeyLock && std::abs(userRatio - 1.0f) > 0.002f) {
                // Key-Lock Mode: ResamplingAudioSource converts to host DAC rate with zero block artifacts
                resamplingSource->setResamplingRatio(baseRatio);
                rawBuffer.clear();
                juce::AudioSourceChannelInfo info(&rawBuffer, 0, numSamples);
                resamplingSource->getNextAudioBlock(info);
                currentSamplePos.store(bufferedSource->getNextReadPosition());
                totalSampleLength.store(bufferedSource->getTotalLength());

                buffer.clear();
                timeStretcher.process(rawBuffer, buffer, numSamples);
            } else {
                // Clean Vinyl Mode (or KeyLock at 1.0x tempo): ResamplingAudioSource combines SR conversion + vinyl tempo ratio seamlessly
                double totalRatio = baseRatio * (double)userRatio;
                resamplingSource->setResamplingRatio(totalRatio);

                buffer.clear();
                juce::AudioSourceChannelInfo info(&buffer, 0, numSamples);
                resamplingSource->getNextAudioBlock(info);
                currentSamplePos.store(bufferedSource->getNextReadPosition());
                totalSampleLength.store(bufferedSource->getTotalLength());
            }
        }
    };

    // --- Audio Preparation & Processing ---
    void prepare(double sampleRate, int maxBlockSize = 8192) {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        int allocSize = juce::jmax(maxBlockSize, 16384);

        deckA.prepare(currentSampleRate, allocSize);
        deckB.prepare(currentSampleRate, allocSize);
        mixBuffer.setSize(2, allocSize, false, false, true);
        mixBuffer.clear();

        levelerL.prepare(currentSampleRate);
        levelerR.prepare(currentSampleRate);
        levelerL.setPreset(1); // MP3 Track Leveler Preset
        levelerR.setPreset(1);
        levelerL.setEnabled(true);
        levelerR.setEnabled(true);
    }

    void processBlock(juce::AudioBuffer<float>& outBuffer) {
        if (!playing.load() || isShuttingDown.load()) return;

        int numSamples = outBuffer.getNumSamples();
        int numChannels = outBuffer.getNumChannels();
        int capSamples = mixBuffer.getNumSamples();
        if (capSamples < numSamples) numSamples = capSamples;

        mixBuffer.clear();

        juce::SpinLock::ScopedTryLockType lock(audioLock);
        if (!lock.isLocked()) return;

        // Process active crossfade progression
        float xfadeDuration = crossfadeDurationSec.load();
        float xfadeStep = (xfadeDuration > 0.05f && currentSampleRate > 0.0) 
            ? (float)(1.0 / (xfadeDuration * currentSampleRate)) : 1.0f;

        bool useKeyLock = (syncMode.load() == SyncMode::KeyLock);

        // Render Deck A
        bool deckAPlaying = deckA.isPlaying.load() && deckA.bufferedSource != nullptr;
        if (deckAPlaying) {
            deckA.renderBlock(numSamples, useKeyLock);
        }

        // Render Deck B
        bool deckBPlaying = deckB.isPlaying.load() && deckB.bufferedSource != nullptr;
        if (deckBPlaying) {
            deckB.renderBlock(numSamples, useKeyLock);
        }

        // Sample-by-sample Equal Power Crossfade Mixing
        float* mixL = mixBuffer.getWritePointer(0);
        float* mixR = mixBuffer.getNumChannels() > 1 ? mixBuffer.getWritePointer(1) : nullptr;

        const float* deckAL = deckA.buffer.getReadPointer(0);
        const float* deckAR = deckA.buffer.getNumChannels() > 1 ? deckA.buffer.getReadPointer(1) : nullptr;

        const float* deckBL = deckB.buffer.getReadPointer(0);
        const float* deckBR = deckB.buffer.getNumChannels() > 1 ? deckB.buffer.getReadPointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i) {
            if (isCrossfading.load()) {
                float curProg = crossfadeProgress.load();
                int targetDeck = activeDeckIndex.load();
                if (targetDeck == 1) {
                    // Fading from A to B
                    curProg = juce::jmin(1.0f, curProg + xfadeStep);
                    crossfadeProgress.store(curProg);
                    if (curProg >= 1.0f) {
                        isCrossfading.store(false);
                        deckA.isPlaying.store(false);
                        deckA.gain.store(0.0f);
                    }
                } else {
                    // Fading from B to A
                    curProg = juce::jmax(0.0f, curProg - xfadeStep);
                    crossfadeProgress.store(curProg);
                    if (curProg <= 0.0f) {
                        isCrossfading.store(false);
                        deckB.isPlaying.store(false);
                        deckB.gain.store(0.0f);
                    }
                }
            }

            // Calculate Equal-Power (3dB Sine/Cosine) gains
            float prog = crossfadeProgress.load(); // 0 = Deck A fully active, 1 = Deck B fully active
            if (manualCrossfadePos.load() >= 0.0f) {
                prog = manualCrossfadePos.load();
            }

            float gainA = std::cos(prog * 1.57079632679f); // cos(prog * pi/2)
            float gainB = std::sin(prog * 1.57079632679f); // sin(prog * pi/2)

            if (!deckA.isPlaying.load()) gainA = 0.0f;
            if (!deckB.isPlaying.load()) gainB = 0.0f;

            deckA.gain.store(gainA);
            deckB.gain.store(gainB);

            float sL = 0.0f;
            float sR = 0.0f;

            if (deckAPlaying) {
                sL += deckAL[i] * gainA;
                if (deckAR != nullptr) sR += deckAR[i] * gainA;
                else sR += deckAL[i] * gainA;
            }

            if (deckBPlaying) {
                sL += deckBL[i] * gainB;
                if (deckBR != nullptr) sR += deckBR[i] * gainB;
                else sR += deckBL[i] * gainB;
            }

            mixL[i] = sL;
            if (mixR != nullptr) mixR[i] = sR;
        }

        // Apply Leveler Dynamics (Auto-Normalization)
        if (levelerEnabled.load()) {
            float* chL = mixBuffer.getWritePointer(0);
            float* chR = mixBuffer.getNumChannels() > 1 ? mixBuffer.getWritePointer(1) : nullptr;
            for (int i = 0; i < numSamples; ++i) {
                chL[i] = levelerL.process(chL[i]);
                if (chR != nullptr) chR[i] = levelerR.process(chR[i]);
            }
        }

        // Master Gain Scaling
        float g = gain.load();
        mixBuffer.applyGain(0, numSamples, g);

        // Sum directly into target outBuffer
        for (int ch = 0; ch < numChannels; ++ch) {
            int srcCh = juce::jmin(ch, mixBuffer.getNumChannels() - 1);
            outBuffer.addFrom(ch, 0, mixBuffer, srcCh, 0, numSamples);
        }

        // Auto-DJ Detection (Check if active song is near completion and crossfade to next track)
        int activeIdx = activeDeckIndex.load();
        Deck& curDeck = (activeIdx == 0) ? deckA : deckB;
        if (curDeck.isPlaying.load() && !isCrossfading.load()) {
            double curPos = (currentSampleRate > 0.0) ? ((double)curDeck.currentSamplePos.load() / currentSampleRate) : 0.0;
            double curLen = (currentSampleRate > 0.0) ? ((double)curDeck.totalSampleLength.load() / currentSampleRate) : 0.0;

            double effectiveEnd = curLen;
            if (curDeck.trackInfo.cueOutSeconds > curDeck.trackInfo.cueInSeconds && curDeck.trackInfo.cueOutSeconds < curLen) {
                effectiveEnd = curDeck.trackInfo.cueOutSeconds;
            }

            if (curLen > 5.0) {
                double remaining = effectiveEnd - curPos;
                float xfadeSec = crossfadeDurationSec.load();
                if (autoDjEnabled.load() && remaining <= (double)xfadeSec && remaining > 0.1) {
                    // Trigger seamless Auto-DJ crossfade to next song
                    juce::MessageManager::callAsync([this]() {
                        nextTrack(true);
                    });
                } else if (remaining <= 0.05) {
                    juce::MessageManager::callAsync([this]() {
                        onTrackFinished();
                    });
                }
            }
        }
    }

    // --- Dual-Deck Playback & Crossfading Actions ---
    void play() {
        if (!deckA.isLoaded.load() && !deckB.isLoaded.load()) {
            if (!playlist.empty()) {
                playTrack(0, true);
            }
            return;
        }

        int activeIdx = activeDeckIndex.load();
        Deck& curDeck = (activeIdx == 0) ? deckA : deckB;

        if (curDeck.isLoaded.load()) {
            if (curDeck.bufferedSource != nullptr && curDeck.bufferedSource->getNextReadPosition() >= curDeck.bufferedSource->getTotalLength()) {
                curDeck.bufferedSource->setNextReadPosition(0);
                curDeck.currentSamplePos.store(0);
            }
            curDeck.isPlaying.store(true);
            playing.store(true);
            if (onPlayStarted) onPlayStarted();
            if (onTrackChanged) onTrackChanged();
        }
    }

    void pause() {
        deckA.isPlaying.store(false);
        deckB.isPlaying.store(false);
        playing.store(false);
        if (onTrackChanged) onTrackChanged();
    }

    void stop() {
        stopDeckInternal(0);
        stopDeckInternal(1);
        playing.store(false);
        if (onTrackChanged) onTrackChanged();
    }

    void playTrack(int index, bool startPlaying = true) {
        juce::ScopedLock sl(playlistLock);
        if (index < 0 || index >= (int)playlist.size()) return;

        currentTrackIndex = index;
        const auto& track = playlist[index];

        // Determine which deck to load into
        int activeIdx = activeDeckIndex.load();
        int targetDeckIdx = 0;

        if (!deckA.isLoaded.load() && !deckB.isLoaded.load()) {
            // Initial load into Deck A
            targetDeckIdx = 0;
            loadTrackIntoDeck(0, track.file, track);
            crossfadeProgress.store(0.0f);
            activeDeckIndex.store(0);
            isCrossfading.store(false);
            if (startPlaying) {
                deckA.isPlaying.store(true);
                playing.store(true);
            }
        } else {
            // Load into alternate deck and crossfade
            targetDeckIdx = 1 - activeIdx;
            loadTrackIntoDeck(targetDeckIdx, track.file, track);

            if (startPlaying) {
                Deck& targetDeck = (targetDeckIdx == 0) ? deckA : deckB;
                Deck& outgoingDeck = (targetDeckIdx == 0) ? deckB : deckA;

                targetDeck.isPlaying.store(true);
                playing.store(true);
                activeDeckIndex.store(targetDeckIdx);

                // Auto Beat-Sync if enabled
                if (beatSyncEnabled.load()) {
                    syncDeckToOther(targetDeckIdx);
                }

                float dur = crossfadeDurationSec.load();
                if (dur > 0.05f && outgoingDeck.isPlaying.load()) {
                    // Start smooth crossfade
                    isCrossfading.store(true);
                } else {
                    // Instant cut
                    isCrossfading.store(false);
                    crossfadeProgress.store((targetDeckIdx == 0) ? 0.0f : 1.0f);
                    outgoingDeck.isPlaying.store(false);
                    outgoingDeck.gain.store(0.0f);
                }
            }
        }

        if (onTrackChanged) onTrackChanged();
    }

    void nextTrack(bool autoCrossfade = true) {
        juce::ScopedLock sl(playlistLock);
        if (playlist.empty()) return;

        int nextIdx = currentTrackIndex + 1;
        if (shuffleEnabled && playlist.size() > 1) {
            std::random_device rd;
            std::mt19937 g(rd());
            std::uniform_int_distribution<int> dis(0, (int)playlist.size() - 1);
            int randIdx = dis(g);
            if (randIdx == currentTrackIndex) randIdx = (randIdx + 1) % playlist.size();
            nextIdx = randIdx;
        } else if (nextIdx >= (int)playlist.size()) {
            if (loopMode == LoopMode::RepeatAll) {
                nextIdx = 0;
            } else {
                return;
            }
        }

        playTrack(nextIdx, autoCrossfade);
    }

    void prevTrack() {
        juce::ScopedLock sl(playlistLock);
        if (playlist.empty()) return;

        // If playing for > 3 seconds, restart current track
        if (getPosition() > 3.0) {
            setPosition(0.0);
            return;
        }

        int prevIdx = currentTrackIndex - 1;
        if (prevIdx < 0) {
            prevIdx = (loopMode == LoopMode::RepeatAll) ? ((int)playlist.size() - 1) : 0;
        }

        playTrack(prevIdx, true);
    }

    bool loadTrackIntoDeck(int deckId, const juce::File& file, const TrackInfo& info) {
        if (!file.existsAsFile()) return false;

        Deck& targetDeck = (deckId == 0) ? deckA : deckB;
        auto& bufferThread = (deckId == 0) ? bufferingThreadA : bufferingThreadB;

        // Reset target deck
        {
            juce::SpinLock::ScopedLockType al(audioLock);
            targetDeck.reset();
        }

        // Cache file into memory for instant seek & glitch-free streaming
        juce::FileInputStream fin(file);
        if (fin.openedOk()) {
            targetDeck.trackMemory.setSize((size_t)fin.getTotalLength());
            fin.read(targetDeck.trackMemory.getData(), (size_t)targetDeck.trackMemory.getSize());

            auto memStream = std::make_unique<juce::MemoryInputStream>(targetDeck.trackMemory.getData(), targetDeck.trackMemory.getSize(), false);
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(memStream)));

            if (reader != nullptr) {
                double srcRate = (reader->sampleRate > 0.0) ? reader->sampleRate : currentSampleRate;
                targetDeck.sourceSampleRate = srcRate;
                targetDeck.rawSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
                targetDeck.bufferedSource = std::make_unique<juce::BufferingAudioSource>(targetDeck.rawSource.get(), bufferThread, false, 65536);
                targetDeck.bufferedSource->prepareToPlay(4096, srcRate);

                targetDeck.resamplingSource = std::make_unique<juce::ResamplingAudioSource>(targetDeck.bufferedSource.get(), false, 2);
                targetDeck.resamplingSource->setResamplingRatio(targetDeck.getBaseSampleRateRatio());
                targetDeck.resamplingSource->prepareToPlay(4096, currentSampleRate);

                juce::SpinLock::ScopedLockType al(audioLock);
                targetDeck.trackInfo = info;
                targetDeck.isLoaded.store(true);

                juce::int64 startSample = 0;
                if (info.cueInSeconds > 0.0 && srcRate > 0.0) {
                    startSample = (juce::int64)(info.cueInSeconds * srcRate);
                    startSample = juce::jlimit((juce::int64)0, targetDeck.bufferedSource->getTotalLength(), startSample);
                    targetDeck.bufferedSource->setNextReadPosition(startSample);
                    targetDeck.resamplingSource->flushBuffers();
                }

                targetDeck.currentSamplePos.store(startSample);
                targetDeck.totalSampleLength.store(targetDeck.bufferedSource->getTotalLength());
                targetDeck.setTempoRatio(1.0f);
                return true;
            }
        }

        return false;
    }

    void stopDeckInternal(int deckId) {
        juce::SpinLock::ScopedLockType al(audioLock);
        Deck& deck = (deckId == 0) ? deckA : deckB;
        deck.reset();
    }

    bool loadFileToDeck(int deckId, const juce::File& file, bool autoPlay = false) {
        if (!file.existsAsFile()) return false;
        TrackInfo info;
        info.file = file;
        info.title = file.getFileNameWithoutExtension();

        // Check BPM & Cues from cache
        {
            juce::ScopedLock sl(bpmCacheLock);
            auto it = bpmCache.find(file.getFullPathName());
            if (it != bpmCache.end()) {
                info.bpm = it->second.bpm;
                info.firstBeatSeconds = it->second.firstBeatSeconds;
                info.cueInSeconds = it->second.cueInSeconds;
                info.cueOutSeconds = it->second.cueOutSeconds;
            }
        }

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr) {
            info.durationSeconds = (double)reader->lengthInSamples / reader->sampleRate;
        }

        bool loaded = loadTrackIntoDeck(deckId, file, info);
        if (loaded) {
            Deck& d = (deckId == 0) ? deckA : deckB;
            d.isPlaying.store(autoPlay);
            if (autoPlay) {
                playing.store(true);
                activeDeckIndex.store(deckId);
                crossfadeProgress.store((deckId == 0) ? 0.0f : 1.0f);
            }
            if (info.bpm <= 0.0f) {
                analyzeTrackBpm(-1, file);
            }
            if (onTrackChanged) onTrackChanged();
        }
        return loaded;
    }

    void playDeck(int deckId) {
        Deck& d = (deckId == 0) ? deckA : deckB;
        if (d.isLoaded.load()) {
            if (d.bufferedSource != nullptr && d.bufferedSource->getNextReadPosition() >= d.bufferedSource->getTotalLength()) {
                d.bufferedSource->setNextReadPosition(0);
                if (d.resamplingSource != nullptr) {
                    d.resamplingSource->flushBuffers();
                }
                d.currentSamplePos.store(0);
            }
            d.isPlaying.store(true);
            playing.store(true);
            activeDeckIndex.store(deckId);
            if (onPlayStarted) onPlayStarted();
            if (onTrackChanged) onTrackChanged();
        }
    }

    void pauseDeck(int deckId) {
        Deck& d = (deckId == 0) ? deckA : deckB;
        d.isPlaying.store(false);
        if (!deckA.isPlaying.load() && !deckB.isPlaying.load()) {
            playing.store(false);
        }
        if (onTrackChanged) onTrackChanged();
    }

    void stopDeck(int deckId) {
        stopDeckInternal(deckId);
        if (!deckA.isPlaying.load() && !deckB.isPlaying.load()) {
            playing.store(false);
        }
        if (onTrackChanged) onTrackChanged();
    }

    double getDeckPosition(int deckId) const {
        const Deck& d = (deckId == 0) ? deckA : deckB;
        double rate = (d.sourceSampleRate > 0.0) ? d.sourceSampleRate : currentSampleRate;
        if (rate > 0.0) {
            return (double)d.currentSamplePos.load() / rate;
        }
        return 0.0;
    }

    double getDeckLength(int deckId) const {
        const Deck& d = (deckId == 0) ? deckA : deckB;
        double rate = (d.sourceSampleRate > 0.0) ? d.sourceSampleRate : currentSampleRate;
        if (rate > 0.0) {
            return (double)d.totalSampleLength.load() / rate;
        }
        return 0.0;
    }

    void setDeckPosition(int deckId, double seconds) {
        juce::SpinLock::ScopedLockType al(audioLock);
        Deck& d = (deckId == 0) ? deckA : deckB;
        double rate = (d.sourceSampleRate > 0.0) ? d.sourceSampleRate : currentSampleRate;
        if (d.bufferedSource != nullptr && rate > 0.0) {
            juce::int64 samplePos = (juce::int64)(seconds * rate);
            samplePos = juce::jlimit((juce::int64)0, d.bufferedSource->getTotalLength(), samplePos);
            d.bufferedSource->setNextReadPosition(samplePos);
            if (d.resamplingSource != nullptr) {
                d.resamplingSource->flushBuffers();
            }
            d.currentSamplePos.store(samplePos);
        }
    }

    bool isDeckPlaying(int deckId) const {
        const Deck& d = (deckId == 0) ? deckA : deckB;
        return d.isPlaying.load();
    }

    bool isDeckLoaded(int deckId) const {
        const Deck& d = (deckId == 0) ? deckA : deckB;
        return d.isLoaded.load();
    }

    float getDeckBpm(int deckId) const {
        const Deck& d = (deckId == 0) ? deckA : deckB;
        return d.trackInfo.bpm;
    }

    float getDeckTempoRatio(int deckId) const {
        const Deck& d = (deckId == 0) ? deckA : deckB;
        return d.getTempoRatio();
    }

    void setDeckTempoRatio(int deckId, float ratio) {
        Deck& d = (deckId == 0) ? deckA : deckB;
        d.setTempoRatio(ratio);
        if (onTrackChanged) onTrackChanged();
    }

    void resetDeckTempo(int deckId) {
        Deck& d = (deckId == 0) ? deckA : deckB;
        d.setTempoRatio(1.0f);
        if (onTrackChanged) onTrackChanged();
    }

    void setBeatSyncEnabled(bool enabled) {
        beatSyncEnabled.store(enabled);
    }

    bool isBeatSyncEnabled() const {
        return beatSyncEnabled.load();
    }

    void setSyncMode(SyncMode mode) {
        syncMode.store(mode);
        saveBanksToSettings();
        if (onTrackChanged) onTrackChanged();
    }

    SyncMode getSyncMode() const {
        return syncMode.load();
    }

    void syncDeckToOther(int targetSlaveDeckId) {
        int masterDeckId = 1 - targetSlaveDeckId;
        Deck& master = (masterDeckId == 0) ? deckA : deckB;
        Deck& slave = (targetSlaveDeckId == 0) ? deckA : deckB;

        if (!master.isLoaded.load() || !slave.isLoaded.load()) return;
        float masterBpm = master.trackInfo.bpm;
        float slaveBpm = slave.trackInfo.bpm;

        if (masterBpm >= 40.0f && slaveBpm >= 40.0f) {
            float ratio = masterBpm / slaveBpm;
            slave.setTempoRatio(ratio);

            // If master is playing, quantize / align downbeat
            if (master.isPlaying.load() && currentSampleRate > 0.0) {
                double masterPos = (double)master.currentSamplePos.load() / currentSampleRate;
                double beatPeriodMaster = 60.0 / masterBpm;
                double beatPhase = std::fmod(masterPos - master.trackInfo.firstBeatSeconds, beatPeriodMaster);
                if (beatPhase < 0.0) beatPhase += beatPeriodMaster;

                // Nudge slave so its beat phase matches master
                double slaveBeatPeriod = 60.0 / slaveBpm;
                double slavePos = (double)slave.currentSamplePos.load() / currentSampleRate;
                double currentSlavePhase = std::fmod(slavePos - slave.trackInfo.firstBeatSeconds, slaveBeatPeriod);
                if (currentSlavePhase < 0.0) currentSlavePhase += slaveBeatPeriod;

                double diff = beatPhase - currentSlavePhase;
                if (std::abs(diff) > 0.005) {
                    setDeckPosition(targetSlaveDeckId, juce::jmax(0.0, slavePos + diff));
                }
            }

            if (onTrackChanged) onTrackChanged();
        }
    }

    void triggerCrossfadeToDeck(int targetDeckId, float customDuration = -1.0f) {
        if (targetDeckId != 0 && targetDeckId != 1) return;
        Deck& target = (targetDeckId == 0) ? deckA : deckB;
        if (!target.isLoaded.load()) return;

        // Auto Beat-Sync if enabled
        if (beatSyncEnabled.load()) {
            syncDeckToOther(targetDeckId);
        }

        float dur = (customDuration > 0.0f) ? customDuration : crossfadeDurationSec.load();
        if (dur <= 0.05f) {
            isCrossfading.store(false);
            crossfadeProgress.store((targetDeckId == 0) ? 0.0f : 1.0f);
            activeDeckIndex.store(targetDeckId);
            target.isPlaying.store(true);
            stopDeckInternal(1 - targetDeckId);
            playing.store(true);
        } else {
            crossfadeDurationSec.store(dur);
            isCrossfading.store(true);
            activeDeckIndex.store(targetDeckId);
            target.isPlaying.store(true);
            playing.store(true);
        }
        manualCrossfadePos.store(-1.0f);
        if (onTrackChanged) onTrackChanged();
    }

    // --- Playlist & Bank Management ---
    void addTrack(const juce::File& file, const juce::String& title, double durationSeconds) {
        if (!file.existsAsFile()) return;
        TrackInfo info;
        info.file = file;
        info.title = title.isNotEmpty() ? title : file.getFileNameWithoutExtension();
        info.durationSeconds = durationSeconds;

        {
            juce::ScopedLock sl(bpmCacheLock);
            auto it = bpmCache.find(file.getFullPathName());
            if (it != bpmCache.end()) {
                info.bpm = it->second.bpm;
                info.firstBeatSeconds = it->second.firstBeatSeconds;
            }
        }

        int newIdx = 0;
        {
            juce::ScopedLock sl(playlistLock);
            playlist.push_back(info);
            newIdx = (int)playlist.size() - 1;
        }

        if (info.bpm <= 0.0f) {
            analyzeTrackBpm(newIdx, file);
        }

        if (onPlaylistChanged) onPlaylistChanged();
    }

    void addFile(const juce::File& file) {
        if (!file.existsAsFile()) return;
        setLastFolder(file);
        juce::String ext = file.getFileExtension().toLowerCase();
        if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg" || ext == ".aiff" || ext == ".m4a" || ext == ".aac") {
            TrackInfo info;
            info.file = file;
            info.title = file.getFileNameWithoutExtension();

            {
                juce::ScopedLock sl(bpmCacheLock);
                auto it = bpmCache.find(file.getFullPathName());
                if (it != bpmCache.end()) {
                    info.bpm = it->second.bpm;
                    info.firstBeatSeconds = it->second.firstBeatSeconds;
                }
            }

            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            if (reader != nullptr) {
                info.durationSeconds = (double)reader->lengthInSamples / reader->sampleRate;
            }

            int newIdx = 0;
            {
                juce::ScopedLock sl(playlistLock);
                playlist.push_back(info);
                newIdx = (int)playlist.size() - 1;
            }

            if (info.bpm <= 0.0f) {
                analyzeTrackBpm(newIdx, file);
            }

            if (onPlaylistChanged) onPlaylistChanged();
        }
    }

    void addFolder(const juce::File& folder) {
        if (!folder.isDirectory()) return;
        setLastFolder(folder);
        auto files = folder.findChildFiles(juce::File::findFiles, false);
        for (const auto& f : files) {
            addFile(f);
        }
    }

    void removeTrack(int index) {
        juce::ScopedLock sl(playlistLock);
        if (index >= 0 && index < (int)playlist.size()) {
            playlist.erase(playlist.begin() + index);
            if (currentTrackIndex == index) {
                stop();
                currentTrackIndex = -1;
            } else if (currentTrackIndex > index) {
                currentTrackIndex--;
            }
            if (onPlaylistChanged) onPlaylistChanged();
        }
    }

    void moveTrack(int fromIndex, int toIndex) {
        juce::ScopedLock sl(playlistLock);
        int num = (int)playlist.size();
        if (fromIndex < 0 || fromIndex >= num || toIndex < 0 || toIndex >= num || fromIndex == toIndex) return;

        TrackInfo item = playlist[fromIndex];
        playlist.erase(playlist.begin() + fromIndex);
        playlist.insert(playlist.begin() + toIndex, item);

        if (currentTrackIndex == fromIndex) {
            currentTrackIndex = toIndex;
        } else if (fromIndex < currentTrackIndex && toIndex >= currentTrackIndex) {
            currentTrackIndex--;
        } else if (fromIndex > currentTrackIndex && toIndex <= currentTrackIndex) {
            currentTrackIndex++;
        }

        if (onPlaylistChanged) onPlaylistChanged();
    }

    void clearPlaylist() {
        stop();
        juce::ScopedLock sl(playlistLock);
        playlist.clear();
        currentTrackIndex = -1;
        if (onPlaylistChanged) onPlaylistChanged();
    }

    const std::vector<TrackInfo>& getPlaylist() const { return playlist; }
    int getCurrentTrackIndex() const { return currentTrackIndex; }

    void setTrackBpm(int trackIdx, float bpm) {
        juce::ScopedLock sl(playlistLock);
        if (trackIdx >= 0 && trackIdx < (int)playlist.size()) {
            playlist[trackIdx].bpm = bpm;
            auto path = playlist[trackIdx].file.getFullPathName();
            {
                juce::ScopedLock bl(bpmCacheLock);
                bpmCache[path].bpm = bpm;
                bpmCache[path].firstBeatSeconds = playlist[trackIdx].firstBeatSeconds;
                saveBpmCache();
            }
            Fanfare::LibraryDatabase::getInstance().updateBpm(path, bpm, playlist[trackIdx].firstBeatSeconds);
            if (onPlaylistChanged) onPlaylistChanged();
        }
    }

    void setTrackCueIn(int trackIndex, double cueInSec) {
        juce::ScopedLock sl(playlistLock);
        if (trackIndex >= 0 && trackIndex < (int)playlist.size()) {
            playlist[trackIndex].cueInSeconds = juce::jmax(0.0, cueInSec);
            auto fullPath = playlist[trackIndex].file.getFullPathName();
            {
                juce::ScopedLock bl(bpmCacheLock);
                bpmCache[fullPath].cueInSeconds = playlist[trackIndex].cueInSeconds;
                saveBpmCache();
            }
            Fanfare::LibraryDatabase::getInstance().updateCues(fullPath, playlist[trackIndex].cueInSeconds, playlist[trackIndex].cueOutSeconds);
            if (currentTrackIndex == trackIndex) {
                int activeIdx = activeDeckIndex.load();
                Deck& curDeck = (activeIdx == 0) ? deckA : deckB;
                curDeck.trackInfo.cueInSeconds = playlist[trackIndex].cueInSeconds;
            }
            if (onPlaylistChanged) onPlaylistChanged();
            if (onTrackChanged) onTrackChanged();
        }
    }

    void setTrackCueOut(int trackIndex, double cueOutSec) {
        juce::ScopedLock sl(playlistLock);
        if (trackIndex >= 0 && trackIndex < (int)playlist.size()) {
            playlist[trackIndex].cueOutSeconds = juce::jmax(0.0, cueOutSec);
            auto fullPath = playlist[trackIndex].file.getFullPathName();
            {
                juce::ScopedLock bl(bpmCacheLock);
                bpmCache[fullPath].cueOutSeconds = playlist[trackIndex].cueOutSeconds;
                saveBpmCache();
            }
            Fanfare::LibraryDatabase::getInstance().updateCues(fullPath, playlist[trackIndex].cueInSeconds, playlist[trackIndex].cueOutSeconds);
            if (currentTrackIndex == trackIndex) {
                int activeIdx = activeDeckIndex.load();
                Deck& curDeck = (activeIdx == 0) ? deckA : deckB;
                curDeck.trackInfo.cueOutSeconds = playlist[trackIndex].cueOutSeconds;
            }
            if (onPlaylistChanged) onPlaylistChanged();
            if (onTrackChanged) onTrackChanged();
        }
    }

    void clearTrackCues(int trackIndex) {
        juce::ScopedLock sl(playlistLock);
        if (trackIndex >= 0 && trackIndex < (int)playlist.size()) {
            playlist[trackIndex].cueInSeconds = 0.0;
            playlist[trackIndex].cueOutSeconds = 0.0;
            auto fullPath = playlist[trackIndex].file.getFullPathName();
            {
                juce::ScopedLock bl(bpmCacheLock);
                bpmCache[fullPath].cueInSeconds = 0.0;
                bpmCache[fullPath].cueOutSeconds = 0.0;
                saveBpmCache();
            }
            Fanfare::LibraryDatabase::getInstance().updateCues(fullPath, 0.0, 0.0);
            if (currentTrackIndex == trackIndex) {
                int activeIdx = activeDeckIndex.load();
                Deck& curDeck = (activeIdx == 0) ? deckA : deckB;
                curDeck.trackInfo.cueInSeconds = 0.0;
                curDeck.trackInfo.cueOutSeconds = 0.0;
            }
            if (onPlaylistChanged) onPlaylistChanged();
            if (onTrackChanged) onTrackChanged();
        }
    }

    void setDeckCueIn(int deckId) {
        Deck& d = (deckId == 0) ? deckA : deckB;
        double rate = (d.sourceSampleRate > 0.0) ? d.sourceSampleRate : currentSampleRate;
        if (d.isLoaded.load() && rate > 0.0) {
            double curPos = (double)d.currentSamplePos.load() / rate;
            d.trackInfo.cueInSeconds = curPos;
            juce::String path = d.trackInfo.file.getFullPathName();
            {
                juce::ScopedLock bl(bpmCacheLock);
                bpmCache[path].cueInSeconds = curPos;
                saveBpmCache();
            }
            Fanfare::LibraryDatabase::getInstance().updateCues(path, curPos, d.trackInfo.cueOutSeconds);
            {
                juce::ScopedLock pl(playlistLock);
                for (auto& t : playlist) {
                    if (t.file.getFullPathName() == path) {
                        t.cueInSeconds = curPos;
                    }
                }
            }
            if (onPlaylistChanged) onPlaylistChanged();
            if (onTrackChanged) onTrackChanged();
        }
    }

    void setDeckCueOut(int deckId) {
        Deck& d = (deckId == 0) ? deckA : deckB;
        double rate = (d.sourceSampleRate > 0.0) ? d.sourceSampleRate : currentSampleRate;
        if (d.isLoaded.load() && rate > 0.0) {
            double curPos = (double)d.currentSamplePos.load() / rate;
            d.trackInfo.cueOutSeconds = curPos;
            juce::String path = d.trackInfo.file.getFullPathName();
            {
                juce::ScopedLock bl(bpmCacheLock);
                bpmCache[path].cueOutSeconds = curPos;
                saveBpmCache();
            }
            Fanfare::LibraryDatabase::getInstance().updateCues(path, d.trackInfo.cueInSeconds, curPos);
            {
                juce::ScopedLock pl(playlistLock);
                for (auto& t : playlist) {
                    if (t.file.getFullPathName() == path) {
                        t.cueOutSeconds = curPos;
                    }
                }
            }
            if (onPlaylistChanged) onPlaylistChanged();
            if (onTrackChanged) onTrackChanged();
        }
    }

    void clearDeckCues(int deckId) {
        Deck& d = (deckId == 0) ? deckA : deckB;
        if (d.isLoaded.load()) {
            d.trackInfo.cueInSeconds = 0.0;
            d.trackInfo.cueOutSeconds = 0.0;
            juce::String path = d.trackInfo.file.getFullPathName();
            {
                juce::ScopedLock bl(bpmCacheLock);
                bpmCache[path].cueInSeconds = 0.0;
                bpmCache[path].cueOutSeconds = 0.0;
                saveBpmCache();
            }
            Fanfare::LibraryDatabase::getInstance().updateCues(path, 0.0, 0.0);
            {
                juce::ScopedLock pl(playlistLock);
                for (auto& t : playlist) {
                    if (t.file.getFullPathName() == path) {
                        t.cueInSeconds = 0.0;
                        t.cueOutSeconds = 0.0;
                    }
                }
            }
            if (onPlaylistChanged) onPlaylistChanged();
            if (onTrackChanged) onTrackChanged();
        }
    }

    double getTotalPlaylistEffectiveDuration() const {
        juce::ScopedLock sl(playlistLock);
        double total = 0.0;
        float xfade = crossfadeDurationSec.load();
        for (size_t i = 0; i < playlist.size(); ++i) {
            double dur = playlist[i].getEffectiveDuration();
            if (i > 0) dur = juce::jmax(0.0, dur - (double)xfade);
            total += dur;
        }
        return total;
    }

    // --- Smart Sequence & Playlist Arranging Algorithms ---
    void sortPlaylistByBpm(bool ascending) {
        juce::ScopedLock sl(playlistLock);
        if (playlist.size() <= 1) return;

        juce::String currentPath;
        if (currentTrackIndex >= 0 && currentTrackIndex < (int)playlist.size()) {
            currentPath = playlist[currentTrackIndex].file.getFullPathName();
        }

        std::stable_sort(playlist.begin(), playlist.end(), [ascending](const TrackInfo& a, const TrackInfo& b) {
            float bpmA = (a.bpm > 0.0f) ? a.bpm : (ascending ? 9999.0f : -9999.0f);
            float bpmB = (b.bpm > 0.0f) ? b.bpm : (ascending ? 9999.0f : -9999.0f);
            return ascending ? (bpmA < bpmB) : (bpmA > bpmB);
        });

        // Re-locate currentTrackIndex
        if (currentPath.isNotEmpty()) {
            for (int i = 0; i < (int)playlist.size(); ++i) {
                if (playlist[i].file.getFullPathName() == currentPath) {
                    currentTrackIndex = i;
                    break;
                }
            }
        }

        if (onPlaylistChanged) onPlaylistChanged();
    }

    void sortPlaylistMinimalDelta(int startTrackIndex = 0) {
        juce::ScopedLock sl(playlistLock);
        int n = (int)playlist.size();
        if (n <= 2) return;

        startTrackIndex = juce::jlimit(0, n - 1, startTrackIndex);
        juce::String currentPath;
        if (currentTrackIndex >= 0 && currentTrackIndex < n) {
            currentPath = playlist[currentTrackIndex].file.getFullPathName();
        }

        std::vector<TrackInfo> remaining = playlist;
        std::vector<TrackInfo> ordered;
        ordered.reserve(n);

        ordered.push_back(remaining[startTrackIndex]);
        remaining.erase(remaining.begin() + startTrackIndex);

        auto bpmDist = [](float a, float b) -> float {
            if (a <= 0.0f || b <= 0.0f) return 50.0f;
            float dDirect = std::abs(a - b);
            float dDouble = std::abs(a * 2.0f - b);
            float dHalf = std::abs(a * 0.5f - b);
            return juce::jmin(dDirect, juce::jmin(dDouble, dHalf));
        };

        while (!remaining.empty()) {
            float lastBpm = ordered.back().bpm;
            int bestIdx = 0;
            float bestDist = 99999.0f;

            for (int i = 0; i < (int)remaining.size(); ++i) {
                float dist = bpmDist(lastBpm, remaining[i].bpm);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx = i;
                }
            }

            ordered.push_back(remaining[bestIdx]);
            remaining.erase(remaining.begin() + bestIdx);
        }

        playlist = ordered;

        if (currentPath.isNotEmpty()) {
            for (int i = 0; i < (int)playlist.size(); ++i) {
                if (playlist[i].file.getFullPathName() == currentPath) {
                    currentTrackIndex = i;
                    break;
                }
            }
        }

        if (onPlaylistChanged) onPlaylistChanged();
    }

    void sortPlaylistEnergyWave() {
        juce::ScopedLock sl(playlistLock);
        int n = (int)playlist.size();
        if (n <= 2) return;

        juce::String currentPath;
        if (currentTrackIndex >= 0 && currentTrackIndex < n) {
            currentPath = playlist[currentTrackIndex].file.getFullPathName();
        }

        // Partition into 3 tiers: Warmup (<105 BPM), Mid (105-122 BPM), Peak (>122 BPM)
        std::vector<TrackInfo> warmup, mid, peak, unknown;
        for (const auto& t : playlist) {
            if (t.bpm <= 0.0f) unknown.push_back(t);
            else if (t.bpm < 105.0f) warmup.push_back(t);
            else if (t.bpm <= 122.0f) mid.push_back(t);
            else peak.push_back(t);
        }

        auto bpmAsc = [](const TrackInfo& a, const TrackInfo& b) { return a.bpm < b.bpm; };
        std::stable_sort(warmup.begin(), warmup.end(), bpmAsc);
        std::stable_sort(mid.begin(), mid.end(), bpmAsc);
        std::stable_sort(peak.begin(), peak.end(), bpmAsc);

        // Build Wave 1 & Wave 2
        std::vector<TrackInfo> ordered;
        ordered.reserve(n);

        auto takeFrom = [](std::vector<TrackInfo>& src, size_t count, std::vector<TrackInfo>& dst) {
            size_t take = juce::jmin(count, src.size());
            for (size_t i = 0; i < take; ++i) {
                dst.push_back(src[0]);
                src.erase(src.begin());
            }
        };

        // Wave 1: First half of warmup, first half of mid, first half of peak
        takeFrom(warmup, (warmup.size() + 1) / 2, ordered);
        takeFrom(mid, (mid.size() + 1) / 2, ordered);
        takeFrom(peak, (peak.size() + 1) / 2, ordered);

        // Wave 2: Remaining warmup/mid, then peak crescendo
        takeFrom(warmup, warmup.size(), ordered);
        takeFrom(mid, mid.size(), ordered);
        takeFrom(peak, peak.size(), ordered);

        // Append any unanalyzed tracks
        for (const auto& u : unknown) ordered.push_back(u);

        playlist = ordered;

        if (currentPath.isNotEmpty()) {
            for (int i = 0; i < (int)playlist.size(); ++i) {
                if (playlist[i].file.getFullPathName() == currentPath) {
                    currentTrackIndex = i;
                    break;
                }
            }
        }

        if (onPlaylistChanged) onPlaylistChanged();
    }

    void fitPlaylistToDuration(double targetMinutes) {
        juce::ScopedLock sl(playlistLock);
        if (playlist.empty() || targetMinutes <= 0.0) return;

        double targetSeconds = targetMinutes * 60.0;
        double accum = 0.0;
        float xfade = crossfadeDurationSec.load();

        std::vector<TrackInfo> fitted;
        for (size_t i = 0; i < playlist.size(); ++i) {
            double eff = playlist[i].getEffectiveDuration();
            if (i > 0) eff = juce::jmax(0.0, eff - (double)xfade);
            
            if (accum + eff <= targetSeconds + 45.0 || fitted.empty()) {
                fitted.push_back(playlist[i]);
                accum += eff;
                if (accum >= targetSeconds - 15.0) break;
            }
        }

        if (!fitted.empty()) {
            playlist = fitted;
            if (currentTrackIndex >= (int)playlist.size()) {
                currentTrackIndex = 0;
            }
            if (onPlaylistChanged) onPlaylistChanged();
        }
    }

    // --- Playlist Preset Banks ---
    void initializeDefaultBanks() {
        banks.clear();
        banks.push_back({ 0, "HITS", "", {} });
        banks.push_back({ 1, "OLDIES", "", {} });
        banks.push_back({ 2, "DINNER", "", {} });
        banks.push_back({ 3, "DANCE", "", {} });
        banks.push_back({ 4, "BALLADS", "", {} });
        banks.push_back({ 5, "BREAK", "", {} });
    }

    const std::vector<PlaylistBank>& getBanks() const { return banks; }

    void assignBankFolder(int bankIndex, const juce::File& folder, const juce::String& optionalName = "") {
        if (bankIndex < 0 || bankIndex >= (int)banks.size()) return;
        if (!folder.isDirectory()) return;

        banks[bankIndex].folderPath = folder.getFullPathName();
        if (optionalName.isNotEmpty()) {
            banks[bankIndex].name = optionalName;
        }

        banks[bankIndex].tracks.clear();
        auto files = folder.findChildFiles(juce::File::findFiles, false);
        for (const auto& f : files) {
            juce::String ext = f.getFileExtension().toLowerCase();
            if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg" || ext == ".aiff" || ext == ".m4a" || ext == ".aac") {
                TrackInfo info;
                info.file = f;
                info.title = f.getFileNameWithoutExtension();
                std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(f));
                if (reader != nullptr) {
                    info.durationSeconds = (double)reader->lengthInSamples / reader->sampleRate;
                }
                {
                    juce::ScopedLock sl(bpmCacheLock);
                    auto it = bpmCache.find(f.getFullPathName());
                    if (it != bpmCache.end()) {
                        info.bpm = it->second.bpm;
                        info.firstBeatSeconds = it->second.firstBeatSeconds;
                        info.cueInSeconds = it->second.cueInSeconds;
                        info.cueOutSeconds = it->second.cueOutSeconds;
                    }
                }
                banks[bankIndex].tracks.push_back(info);
            }
        }

        saveBanksToSettings();
        if (onBanksChanged) onBanksChanged();
    }

    void loadBank(int bankIndex, bool startPlaying = true) {
        if (bankIndex < 0 || bankIndex >= (int)banks.size()) return;
        const auto& bank = banks[bankIndex];

        std::vector<TrackInfo> newTracks = bank.tracks;
        if (newTracks.empty() && bank.folderPath.isNotEmpty()) {
            juce::File f(bank.folderPath);
            if (f.isDirectory()) {
                assignBankFolder(bankIndex, f, bank.name);
                newTracks = banks[bankIndex].tracks;
            }
        }

        if (newTracks.empty()) return;

        activeBankIndex = bankIndex;
        bool wasPlaying = playing.load();

        {
            juce::ScopedLock sl(playlistLock);
            playlist = newTracks;
            currentTrackIndex = 0;
        }

        if (onPlaylistChanged) onPlaylistChanged();
        if (onBanksChanged) onBanksChanged();

        // Trigger background BPM detection for unanalyzed tracks in bank
        for (int i = 0; i < (int)playlist.size(); ++i) {
            if (playlist[i].bpm <= 0.0f) {
                analyzeTrackBpm(i, playlist[i].file);
            }
        }

        if (startPlaying || wasPlaying) {
            playTrack(0, true);
        }
    }

    int getActiveBankIndex() const { return activeBankIndex; }

    void saveBanksToSettings() {
        auto playlistsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare").getChildFile("Playlists");
        if (!playlistsDir.exists()) playlistsDir.createDirectory();

        auto file = playlistsDir.getChildFile("playlist_banks.json");
        auto* root = new juce::DynamicObject();
        juce::Array<juce::var> bankArray;

        for (const auto& b : banks) {
            auto* bo = new juce::DynamicObject();
            bo->setProperty("index", b.index);
            bo->setProperty("name", b.name);
            bo->setProperty("folderPath", b.folderPath);
            bankArray.add(juce::var(bo));
        }

        root->setProperty("banks", bankArray);
        root->setProperty("crossfadeSec", (double)crossfadeDurationSec.load());
        root->setProperty("autoDj", autoDjEnabled.load());
        root->setProperty("beatSync", beatSyncEnabled.load());
        root->setProperty("syncMode", (int)syncMode.load());

        file.replaceWithText(juce::JSON::toString(juce::var(root)));
    }

    void loadBanksFromSettings() {
        auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare").getChildFile("Playlists").getChildFile("playlist_banks.json");
        if (!file.existsAsFile()) return;

        auto parsed = juce::JSON::parse(file);
        if (parsed.isObject()) {
            crossfadeDurationSec.store((float)parsed.getProperty("crossfadeSec", 4.0));
            autoDjEnabled.store((bool)parsed.getProperty("autoDj", true));
            beatSyncEnabled.store((bool)parsed.getProperty("beatSync", false));
            syncMode.store((SyncMode)(int)parsed.getProperty("syncMode", 0));

            if (auto* arr = parsed.getProperty("banks", juce::var()).getArray()) {
                for (int i = 0; i < arr->size() && i < (int)banks.size(); ++i) {
                    const auto& v = arr->getReference(i);
                    if (v.isObject()) {
                        banks[i].name = v.getProperty("name", banks[i].name).toString();
                        banks[i].folderPath = v.getProperty("folderPath", "").toString();
                        if (banks[i].folderPath.isNotEmpty()) {
                            juce::File dir(banks[i].folderPath);
                            if (dir.isDirectory()) {
                                assignBankFolder(i, dir, banks[i].name);
                            }
                        }
                    }
                }
            }
        }
    }

    // --- State Accessors & Controls ---
    bool isPlaying() const { return playing.load(); }
    
    double getPosition() const {
        int activeIdx = activeDeckIndex.load();
        const Deck& curDeck = (activeIdx == 0) ? deckA : deckB;
        if (currentSampleRate > 0.0) {
            return (double)curDeck.currentSamplePos.load() / currentSampleRate;
        }
        return 0.0;
    }

    double getLength() const {
        int activeIdx = activeDeckIndex.load();
        const Deck& curDeck = (activeIdx == 0) ? deckA : deckB;
        if (currentSampleRate > 0.0) {
            return (double)curDeck.totalSampleLength.load() / currentSampleRate;
        }
        return 0.0;
    }

    void setPosition(double seconds) {
        juce::SpinLock::ScopedLockType al(audioLock);
        int activeIdx = activeDeckIndex.load();
        Deck& curDeck = (activeIdx == 0) ? deckA : deckB;
        if (curDeck.bufferedSource != nullptr && currentSampleRate > 0.0) {
            juce::int64 samplePos = (juce::int64)(seconds * currentSampleRate);
            samplePos = juce::jlimit((juce::int64)0, curDeck.bufferedSource->getTotalLength(), samplePos);
            curDeck.bufferedSource->setNextReadPosition(samplePos);
            curDeck.currentSamplePos.store(samplePos);
        }
    }

    void setGain(float g) { gain.store(juce::jlimit(0.0f, 2.0f, g)); }
    float getGain() const { return gain.load(); }

    void setCrossfadeDuration(float seconds) { crossfadeDurationSec.store(juce::jlimit(0.0f, 15.0f, seconds)); }
    float getCrossfadeDuration() const { return crossfadeDurationSec.load(); }

    void setAutoDjEnabled(bool enabled) { autoDjEnabled.store(enabled); }
    bool isAutoDjEnabled() const { return autoDjEnabled.load(); }

    void setManualCrossfadePos(float pos) { manualCrossfadePos.store(pos); }
    float getManualCrossfadePos() const { return manualCrossfadePos.load(); }

    float getDeckAGain() const { return deckA.gain.load(); }
    float getDeckBGain() const { return deckB.gain.load(); }
    int getActiveDeckIndex() const { return activeDeckIndex.load(); }
    bool getIsCrossfading() const { return isCrossfading.load(); }
    const TrackInfo& getDeckATrack() const { return deckA.trackInfo; }
    const TrackInfo& getDeckBTrack() const { return deckB.trackInfo; }

    void setLoopMode(LoopMode mode) { loopMode = mode; }
    LoopMode getLoopMode() const { return loopMode; }

    void setShuffleEnabled(bool s) { shuffleEnabled = s; }
    bool isShuffleEnabled() const { return shuffleEnabled; }

    void setLevelerEnabled(bool enabled) { levelerEnabled.store(enabled); }
    bool isLevelerEnabled() const { return levelerEnabled.load(); }
    FanfareDSP::SimpleComp& getLevelerReference() { return levelerL; }

    juce::File getLastFolder() const {
        if (lastFolderPath.isNotEmpty()) {
            juce::File f(lastFolderPath);
            if (f.exists()) return f.isDirectory() ? f : f.getParentDirectory();
        }
        return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    }

    void setLastFolder(const juce::File& f) {
        if (f.exists()) {
            juce::File dir = f.isDirectory() ? f : f.getParentDirectory();
            lastFolderPath = dir.getFullPathName();
            saveLastFolderToSettings();
        }
    }

    void saveLastFolderToSettings() {
        auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare");
        if (!appData.exists()) appData.createDirectory();
        auto settingsFile = appData.getChildFile("mp3_last_folder.txt");
        settingsFile.replaceWithText(lastFolderPath);
    }

    void loadLastFolderFromSettings() {
        auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare").getChildFile("mp3_last_folder.txt");
        if (settingsFile.existsAsFile()) {
            lastFolderPath = settingsFile.loadFileAsString().trim();
        }
    }

    void savePlaylist(const juce::File& targetFile) const {
        juce::ScopedLock sl(playlistLock);
        juce::File file = targetFile;
        if (file.getFileExtension().isEmpty()) {
            file = file.withFileExtension("m3u");
        }

        if (file.getFileExtension().equalsIgnoreCase(".json")) {
            auto* obj = new juce::DynamicObject();
            juce::Array<juce::var> arr;
            for (const auto& t : playlist) {
                auto* to = new juce::DynamicObject();
                to->setProperty("path", t.file.getFullPathName());
                to->setProperty("title", t.title);
                to->setProperty("duration", t.durationSeconds);
                to->setProperty("bpm", (double)t.bpm);
                to->setProperty("cueIn", t.cueInSeconds);
                to->setProperty("cueOut", t.cueOutSeconds);
                arr.add(juce::var(to));
            }
            obj->setProperty("tracks", arr);
            file.replaceWithText(juce::JSON::toString(juce::var(obj)));
        } else {
            // Extended M3U playlist format with Fanfare BPM & cue point tags
            juce::String text = "#EXTM3U\n";
            for (const auto& t : playlist) {
                text += "#EXTINF:" + juce::String((int)t.durationSeconds) + "," + t.title + "\n";
                if (t.bpm > 0.0f || t.hasCues()) {
                    text += "#EXT-FANFARE:bpm=" + juce::String(t.bpm, 1) + 
                            ",cue_in=" + juce::String(t.cueInSeconds, 2) + 
                            ",cue_out=" + juce::String(t.cueOutSeconds, 2) + "\n";
                }
                text += t.file.getFullPathName() + "\n";
            }
            file.replaceWithText(text);
        }
    }

    void loadPlaylist(const juce::File& file) {
        if (!file.existsAsFile()) return;
        clearPlaylist();

        // Check if file content is JSON
        auto text = file.loadFileAsString();
        if (text.trimStart().startsWithChar('{')) {
            auto parsed = juce::JSON::parse(file);
            if (auto* obj = parsed.getDynamicObject()) {
                if (auto* arr = obj->getProperty("tracks").getArray()) {
                    for (const auto& v : *arr) {
                        if (auto* to = v.getDynamicObject()) {
                            juce::File f(to->getProperty("path").toString());
                            if (f.existsAsFile()) {
                                TrackInfo info;
                                info.file = f;
                                info.title = to->getProperty("title").toString();
                                if (info.title.isEmpty()) info.title = f.getFileNameWithoutExtension();
                                info.durationSeconds = (double)to->getProperty("duration");
                                info.bpm = (float)(double)to->getProperty("bpm");
                                info.cueInSeconds = (double)to->getProperty("cueIn");
                                info.cueOutSeconds = (double)to->getProperty("cueOut");
                                {
                                    juce::ScopedLock pl(playlistLock);
                                    playlist.push_back(info);
                                }
                            }
                        }
                    }
                    if (onPlaylistChanged) onPlaylistChanged();
                    return;
                }
            }
        }

        // Otherwise parse as Extended M3U or path list
        juce::StringArray lines;
        file.readLines(lines);
        float nextBpm = 0.0f;
        double nextCueIn = 0.0, nextCueOut = 0.0;

        for (const auto& line : lines) {
            auto trimmed = line.trim();
            if (trimmed.isEmpty()) continue;

            if (trimmed.startsWithIgnoreCase("#EXT-FANFARE:")) {
                auto tagData = trimmed.substring(13);
                juce::StringArray pairs;
                pairs.addTokens(tagData, ",;", "\"");
                for (const auto& p : pairs) {
                    if (p.startsWithIgnoreCase("bpm=")) nextBpm = (float)p.substring(4).getDoubleValue();
                    else if (p.startsWithIgnoreCase("cue_in=")) nextCueIn = p.substring(7).getDoubleValue();
                    else if (p.startsWithIgnoreCase("cue_out=")) nextCueOut = p.substring(8).getDoubleValue();
                }
            } else if (!trimmed.startsWith("#")) {
                juce::File f(trimmed);
                if (!juce::File::isAbsolutePath(trimmed)) {
                    f = file.getParentDirectory().getChildFile(trimmed);
                }
                if (f.existsAsFile()) {
                    TrackInfo info;
                    info.file = f;
                    info.title = f.getFileNameWithoutExtension();
                    info.bpm = nextBpm;
                    info.cueInSeconds = nextCueIn;
                    info.cueOutSeconds = nextCueOut;

                    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(f));
                    if (reader != nullptr) {
                        info.durationSeconds = (double)reader->lengthInSamples / reader->sampleRate;
                    }
                    {
                        juce::ScopedLock pl(playlistLock);
                        playlist.push_back(info);
                    }
                    if (info.bpm <= 0.0f) {
                        analyzeTrackBpm((int)playlist.size() - 1, f);
                    }
                }
                nextBpm = 0.0f;
                nextCueIn = 0.0;
                nextCueOut = 0.0;
            }
        }
        if (onPlaylistChanged) onPlaylistChanged();
    }

    void loadBpmCache() {
        Fanfare::LibraryDatabase::getInstance().open();

        auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare").getChildFile("Playlists").getChildFile("bpm_cache.json");
        if (file.existsAsFile()) {
            Fanfare::LibraryDatabase::getInstance().autoMigrateFromJson(file);
        }

        auto records = Fanfare::LibraryDatabase::getInstance().getAllTracks();
        juce::ScopedLock sl(bpmCacheLock);
        bpmCache.clear();
        for (const auto& r : records) {
            CachedBpm cb;
            cb.bpm = r.bpm;
            cb.firstBeatSeconds = r.firstBeatSeconds;
            cb.cueInSeconds = r.cueInSeconds;
            cb.cueOutSeconds = r.cueOutSeconds;
            bpmCache[r.filePath] = cb;
        }
    }

private:
    struct CachedBpm {
        float bpm = 0.0f;
        double firstBeatSeconds = 0.0;
        double cueInSeconds = 0.0;
        double cueOutSeconds = 0.0;
    };

    void saveBpmCache() {
        auto playlistsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare").getChildFile("Playlists");
        if (!playlistsDir.exists()) playlistsDir.createDirectory();
        auto file = playlistsDir.getChildFile("bpm_cache.json");

        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        {
            juce::ScopedLock sl(bpmCacheLock);
            for (const auto& pair : bpmCache) {
                juce::DynamicObject::Ptr bo = new juce::DynamicObject();
                bo->setProperty("bpm", (double)pair.second.bpm);
                bo->setProperty("firstBeat", pair.second.firstBeatSeconds);
                bo->setProperty("cueIn", pair.second.cueInSeconds);
                bo->setProperty("cueOut", pair.second.cueOutSeconds);
                root->setProperty(pair.first, juce::var(bo.get()));
            }
        }
        file.replaceWithText(juce::JSON::toString(juce::var(root.get())));
    }

    void analyzeTrackBpm(int trackIndex, const juce::File& file) {
        if (!file.existsAsFile()) return;
        juce::String fullPath = file.getFullPathName();

        // Check SQLite / memory cache first
        {
            juce::ScopedLock sl(bpmCacheLock);
            auto it = bpmCache.find(fullPath);
            if (it != bpmCache.end()) {
                juce::ScopedLock pl(playlistLock);
                if (trackIndex >= 0 && trackIndex < (int)playlist.size()) {
                    playlist[trackIndex].bpm = it->second.bpm;
                    playlist[trackIndex].firstBeatSeconds = it->second.firstBeatSeconds;
                    playlist[trackIndex].cueInSeconds = it->second.cueInSeconds;
                    playlist[trackIndex].cueOutSeconds = it->second.cueOutSeconds;
                    if (onPlaylistChanged) onPlaylistChanged();
                }
                return;
            }
        }

        // Launch background BPM detection
        BpmDetector::detectBpmAsync(formatManager, file, [this, fullPath, trackIndex](const BpmDetector::BpmResult& res) {
            if (res.success && res.bpm > 0.0f) {
                {
                    juce::ScopedLock sl(bpmCacheLock);
                    bpmCache[fullPath].bpm = res.bpm;
                    bpmCache[fullPath].firstBeatSeconds = res.firstBeatSeconds;
                    saveBpmCache();
                }
                Fanfare::LibraryDatabase::getInstance().updateBpm(fullPath, res.bpm, res.firstBeatSeconds);
                {
                    juce::ScopedLock pl(playlistLock);
                    if (trackIndex >= 0 && trackIndex < (int)playlist.size() && playlist[trackIndex].file.getFullPathName() == fullPath) {
                        playlist[trackIndex].bpm = res.bpm;
                        playlist[trackIndex].firstBeatSeconds = res.firstBeatSeconds;
                    }
                }
                // Update active decks if matching file
                if (deckA.isLoaded.load() && deckA.trackInfo.file.getFullPathName() == fullPath) {
                    deckA.trackInfo.bpm = res.bpm;
                    deckA.trackInfo.firstBeatSeconds = res.firstBeatSeconds;
                }
                if (deckB.isLoaded.load() && deckB.trackInfo.file.getFullPathName() == fullPath) {
                    deckB.trackInfo.bpm = res.bpm;
                    deckB.trackInfo.firstBeatSeconds = res.firstBeatSeconds;
                }
                if (onPlaylistChanged) onPlaylistChanged();
                if (onTrackChanged) onTrackChanged();
            }
        });
    }

    void onTrackFinished() {
        if (isShuttingDown.load()) return;
        if (loopMode == LoopMode::RepeatTrack && currentTrackIndex >= 0) {
            playTrack(currentTrackIndex, true);
        } else {
            nextTrack(true);
        }
    }

    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread bufferingThreadA{"Mp3BufferingThreadA"};
    juce::TimeSliceThread bufferingThreadB{"Mp3BufferingThreadB"};

    Deck deckA{0};
    Deck deckB{1};
    juce::AudioBuffer<float> mixBuffer;
    juce::SpinLock audioLock;

    FanfareDSP::SimpleComp levelerL, levelerR;
    std::atomic<bool> levelerEnabled{true};

    std::atomic<bool> playing{false};
    std::atomic<bool> isShuttingDown{false};
    std::atomic<float> gain{1.0f};

    std::atomic<int> activeDeckIndex{0}; // 0 = Deck A, 1 = Deck B
    std::atomic<bool> isCrossfading{false};
    std::atomic<float> crossfadeProgress{0.0f}; // 0.0 (Deck A) to 1.0 (Deck B)
    std::atomic<float> crossfadeDurationSec{4.0f}; // 4 seconds equal-power crossfade
    std::atomic<bool> autoDjEnabled{true};
    std::atomic<bool> beatSyncEnabled{false};
    std::atomic<SyncMode> syncMode{SyncMode::CleanVinyl};
    std::atomic<float> manualCrossfadePos{-1.0f};

    double currentSampleRate = 44100.0;
    int currentTrackIndex = -1;
    LoopMode loopMode = LoopMode::Off;
    bool shuffleEnabled = false;
    juce::String lastFolderPath;

    mutable juce::CriticalSection playlistLock;
    std::vector<TrackInfo> playlist;

    mutable juce::CriticalSection bpmCacheLock;
    std::map<juce::String, CachedBpm> bpmCache;

    std::vector<PlaylistBank> banks;
    int activeBankIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Mp3PlayerProcessor)
};
