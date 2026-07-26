#pragma once

#include <JuceHeader.h>
#include "Logger.h"
#include "ChannelStripProcessor.h"
#include <vector>
#include <random>
#include <atomic>

class Mp3PlayerProcessor {
public:
    enum class LoopMode { Off, RepeatTrack, RepeatAll };

    struct TrackInfo {
        juce::File file;
        juce::String title;
        double durationSeconds = 0.0;
    };

    Mp3PlayerProcessor() {
        formatManager.registerBasicFormats();
        formatManager.registerFormat(new juce::FlacAudioFormat(), true);
        formatManager.registerFormat(new juce::OggVorbisAudioFormat(), true);
        bufferingThread.startThread(juce::Thread::Priority::high);
        loadLastFolderFromSettings();
    }

    ~Mp3PlayerProcessor() {
        isShuttingDown.store(true);
        playing.store(false);
        {
            juce::SpinLock::ScopedLockType al(audioLock);
            readerSource.reset();
        }
        bufferingThread.stopThread(2000);
    }

    juce::String lastFolderPath;

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
        auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OpenRig");
        if (!appData.exists()) appData.createDirectory();
        auto settingsFile = appData.getChildFile("mp3_last_folder.txt");
        settingsFile.replaceWithText(lastFolderPath);
    }

    void loadLastFolderFromSettings() {
        auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("OpenRig").getChildFile("mp3_last_folder.txt");
        if (settingsFile.existsAsFile()) {
            lastFolderPath = settingsFile.loadFileAsString().trim();
        }
    }

    OpenRigDSP::SimpleComp levelerL, levelerR;
    std::atomic<bool> levelerEnabled{true};

    void prepare(double sampleRate, int maxBlockSize = 8192) {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        int allocSize = juce::jmax(maxBlockSize, 8192);
        tempBuffer.setSize(2, allocSize);
        tempBuffer.clear();

        levelerL.prepare(currentSampleRate);
        levelerR.prepare(currentSampleRate);
        levelerL.setPreset(1); // MP3 / Track Leveler Preset
        levelerR.setPreset(1);
        levelerL.setEnabled(true);
        levelerR.setEnabled(true);

        juce::SpinLock::ScopedLockType al(audioLock);
        if (readerSource != nullptr) {
            readerSource->prepareToPlay(allocSize, currentSampleRate);
            totalReadLength.store(readerSource->getTotalLength());
            currentReadPos.store(readerSource->getNextReadPosition());
        }
    }

    void setLevelerEnabled(bool enabled) {
        levelerEnabled.store(enabled);
    }
    bool isLevelerEnabled() const { return levelerEnabled.load(); }
    OpenRigDSP::SimpleComp& getLevelerReference() { return levelerL; }

    void processBlock(juce::AudioBuffer<float>& buffer) {
        if (!playing.load()) return;

        juce::SpinLock::ScopedTryLockType lock(audioLock);
        if (!lock.isLocked() || readerSource == nullptr) return;

        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();
        if (tempBuffer.getNumSamples() < numSamples || tempBuffer.getNumChannels() < numChannels) {
            tempBuffer.setSize(numChannels, numSamples, false, false, false);
        }
        tempBuffer.clear();

        juce::AudioSourceChannelInfo info(&tempBuffer, 0, numSamples);
        readerSource->getNextAudioBlock(info);

        float currentGain = gain.load();
        tempBuffer.applyGain(currentGain);

        if (levelerEnabled.load()) {
            levelerL.setEnabled(true);
            levelerR.setEnabled(true);
            float* L = tempBuffer.getWritePointer(0);
            float* R = tempBuffer.getNumChannels() > 1 ? tempBuffer.getWritePointer(1) : nullptr;
            for (int i = 0; i < numSamples; ++i) {
                L[i] = levelerL.process(L[i]);
                if (R) R[i] = levelerR.process(R[i]);
            }
        }

        for (int ch = 0; ch < numChannels; ++ch) {
            buffer.addFrom(ch, 0, tempBuffer, ch, 0, numSamples);
        }

        juce::int64 currentPos = readerSource->getNextReadPosition();
        juce::int64 totalLen = readerSource->getTotalLength();
        currentReadPos.store(currentPos);
        totalReadLength.store(totalLen);

        if (currentPos >= totalLen && totalLen > 0) {
            playing.store(false);
            juce::MessageManager::callAsync([this] {
                onTrackFinished();
            });
        }
    }

    // Playlist Control
    void addFile(const juce::File& file) {
        if (!file.existsAsFile()) return;
        setLastFolder(file);
        juce::String ext = file.getFileExtension().toLowerCase();
        if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg" || ext == ".aiff" || ext == ".m4a" || ext == ".aac") {
            TrackInfo info;
            info.file = file;
            info.title = file.getFileNameWithoutExtension();

            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            if (reader != nullptr) {
                info.durationSeconds = (double)reader->lengthInSamples / reader->sampleRate;
            }

            juce::ScopedLock sl(playlistLock);
            playlist.push_back(info);
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
                {
                    juce::SpinLock::ScopedLockType al(audioLock);
                    readerSource.reset();
                    currentReadPos.store(0);
                    totalReadLength.store(0);
                }
            } else if (currentTrackIndex > index) {
                currentTrackIndex--;
            }
        }
    }

    void clearPlaylist() {
        stop();
        juce::ScopedLock sl(playlistLock);
        playlist.clear();
        currentTrackIndex = -1;
        {
            juce::SpinLock::ScopedLockType al(audioLock);
            readerSource.reset();
            currentReadPos.store(0);
            totalReadLength.store(0);
        }
    }

    const std::vector<TrackInfo>& getPlaylist() const {
        return playlist;
    }

    int getCurrentTrackIndex() const {
        return currentTrackIndex;
    }

    void playTrack(int index) {
        if (isShuttingDown.load()) return;
        juce::ScopedLock sl(playlistLock);
        if (index < 0 || index >= (int)playlist.size()) return;

        currentTrackIndex = index;
        auto file = playlist[index].file;

        playing.store(false);

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader != nullptr) {
            auto rawSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
            auto bufferedSource = std::make_unique<juce::BufferingAudioSource>(rawSource.release(), bufferingThread, true, 65536);
            bufferedSource->prepareToPlay(512, currentSampleRate);
            bufferedSource->setNextReadPosition(0);

            {
                juce::SpinLock::ScopedLockType al(audioLock);
                readerSource = std::move(bufferedSource);
                currentReadPos.store(0);
                totalReadLength.store(readerSource->getTotalLength());
            }
            playing.store(true);
        } else {
            juce::SpinLock::ScopedLockType al(audioLock);
            readerSource.reset();
            currentReadPos.store(0);
            totalReadLength.store(0);
        }
    }

    void play() {
        if (playing.load()) return;

        juce::ScopedLock sl(playlistLock);
        if (playlist.empty()) return;

        if (currentTrackIndex < 0 || currentTrackIndex >= (int)playlist.size()) {
            currentTrackIndex = 0;
        }

        {
            juce::SpinLock::ScopedLockType al(audioLock);
            if (readerSource != nullptr) {
                if (readerSource->getNextReadPosition() >= readerSource->getTotalLength()) {
                    readerSource->setNextReadPosition(0);
                    currentReadPos.store(0);
                }
                playing.store(true);
                return;
            }
        }

        playTrack(currentTrackIndex);
    }

    void pause() {
        playing.store(false);
    }

    void stop() {
        playing.store(false);
        juce::SpinLock::ScopedLockType al(audioLock);
        if (readerSource != nullptr) {
            readerSource->setNextReadPosition(0);
            currentReadPos.store(0);
        }
    }

    void nextTrack() {
        juce::ScopedLock sl(playlistLock);
        if (playlist.empty()) return;

        if (shuffleEnabled && playlist.size() > 1) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, (int)playlist.size() - 1);
            int nextIdx = dis(gen);
            if (nextIdx == currentTrackIndex) nextIdx = (nextIdx + 1) % playlist.size();
            playTrack(nextIdx);
            return;
        }

        int nextIdx = currentTrackIndex + 1;
        if (nextIdx >= (int)playlist.size()) {
            if (loopMode == LoopMode::RepeatAll) {
                nextIdx = 0;
            } else {
                stop();
                return;
            }
        }
        playTrack(nextIdx);
    }

    void prevTrack() {
        juce::ScopedLock sl(playlistLock);
        if (playlist.empty()) return;

        if (getPosition() > 3.0) {
            setPosition(0.0);
            return;
        }

        int prevIdx = currentTrackIndex - 1;
        if (prevIdx < 0) {
            prevIdx = (int)playlist.size() - 1;
        }
        playTrack(prevIdx);
    }

    bool isPlaying() const {
        return playing.load();
    }

    void setPosition(double seconds) {
        juce::SpinLock::ScopedLockType al(audioLock);
        if (readerSource != nullptr && currentSampleRate > 0.0) {
            juce::int64 samplePos = (juce::int64)(seconds * currentSampleRate);
            samplePos = juce::jlimit((juce::int64)0, readerSource->getTotalLength(), samplePos);
            readerSource->setNextReadPosition(samplePos);
            currentReadPos.store(samplePos);
        }
    }

    double getPosition() const {
        double sr = currentSampleRate;
        if (sr > 0.0) {
            return (double)currentReadPos.load() / sr;
        }
        return 0.0;
    }

    double getLength() const {
        double sr = currentSampleRate;
        if (sr > 0.0) {
            return (double)totalReadLength.load() / sr;
        }
        return 0.0;
    }

    void setGain(float g) {
        gain.store(juce::jlimit(0.0f, 2.0f, g));
    }

    float getGain() const {
        return gain.load();
    }

    void setLoopMode(LoopMode mode) { loopMode = mode; }
    LoopMode getLoopMode() const { return loopMode; }

    void setShuffleEnabled(bool s) { shuffleEnabled = s; }
    bool isShuffleEnabled() const { return shuffleEnabled; }

    void savePlaylist(const juce::File& file) const {
        juce::ScopedLock sl(playlistLock);
        if (file.getFileExtension().equalsIgnoreCase(".m3u")) {
            juce::String text = "#EXTM3U\n";
            for (const auto& t : playlist) {
                text += "#EXTINF:" + juce::String((int)t.durationSeconds) + "," + t.title + "\n";
                text += t.file.getFullPathName() + "\n";
            }
            file.replaceWithText(text);
        } else {
            auto* obj = new juce::DynamicObject();
            juce::Array<juce::var> arr;
            for (const auto& t : playlist) {
                auto* to = new juce::DynamicObject();
                to->setProperty("path", t.file.getFullPathName());
                to->setProperty("title", t.title);
                to->setProperty("duration", t.durationSeconds);
                arr.add(juce::var(to));
            }
            obj->setProperty("tracks", arr);
            juce::var data(obj);
            file.replaceWithText(juce::JSON::toString(data));
        }
    }

    void loadPlaylist(const juce::File& file) {
        if (!file.existsAsFile()) return;
        clearPlaylist();

        if (file.getFileExtension().equalsIgnoreCase(".m3u")) {
            juce::StringArray lines;
            file.readLines(lines);
            for (const auto& line : lines) {
                auto trimmed = line.trim();
                if (trimmed.isNotEmpty() && !trimmed.startsWith("#")) {
                    juce::File f(trimmed);
                    if (!juce::File::isAbsolutePath(trimmed)) {
                        f = file.getParentDirectory().getChildFile(trimmed);
                    }
                    addFile(f);
                }
            }
        } else {
            auto parsed = juce::JSON::parse(file);
            if (auto* obj = parsed.getDynamicObject()) {
                if (auto* arr = obj->getProperty("tracks").getArray()) {
                    for (const auto& v : *arr) {
                        if (auto* to = v.getDynamicObject()) {
                            juce::File f(to->getProperty("path").toString());
                            addFile(f);
                        }
                    }
                }
            }
        }
    }

private:
    void onTrackFinished() {
        if (isShuttingDown.load()) return;
        if (loopMode == LoopMode::RepeatTrack && currentTrackIndex >= 0) {
            playTrack(currentTrackIndex);
        } else {
            nextTrack();
        }
    }

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::PositionableAudioSource> readerSource;
    juce::TimeSliceThread bufferingThread{"Mp3BufferingThread"};
    juce::AudioBuffer<float> tempBuffer;
    juce::SpinLock audioLock;

    std::atomic<bool> playing{false};
    std::atomic<bool> isShuttingDown{false};
    std::atomic<float> gain{1.0f};
    std::atomic<juce::int64> currentReadPos{0};
    std::atomic<juce::int64> totalReadLength{0};

    double currentSampleRate = 44100.0;
    int currentTrackIndex = -1;
    LoopMode loopMode = LoopMode::Off;
    bool shuffleEnabled = false;

    mutable juce::CriticalSection playlistLock;
    std::vector<TrackInfo> playlist;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Mp3PlayerProcessor)
};
