#pragma once

#include <JuceHeader.h>

class CustomSamplerSound : public juce::SynthesiserSound {
public:
    CustomSamplerSound(const juce::String& /*name*/,
                       juce::AudioFormatReader& reader,
                       const juce::BigInteger& midiNotes,
                       int midiNoteForNormalPitch,
                       double attackTimeSecs,
                       double releaseTimeSecs,
                       double maxSampleLengthSeconds)
        : sourceSampleRate(reader.sampleRate),
          length(juce::jmin((int)reader.lengthInSamples, (int)(maxSampleLengthSeconds * sourceSampleRate)))
    {
        if (length > 0) {
            data.reset(new juce::AudioBuffer<float>(juce::jmin(2, (int)reader.numChannels), length));
            reader.read(data.get(), 0, length, 0, true, true);
        }
        
        midiNotesRange = midiNotes;
        normalPitchMidiNote = midiNoteForNormalPitch;
        params.attack = attackTimeSecs;
        params.release = releaseTimeSecs;
    }
    
    bool appliesToNote(int midiNoteNumber) override {
        return midiNotesRange[midiNoteNumber];
    }
    
    bool appliesToChannel(int /*midiChannel*/) override {
        return true;
    }
    
    std::atomic<float> volume{1.0f};
    std::atomic<float> pitchOffsetSemitones{0.0f};
    std::atomic<int> rootNote{60};
    std::atomic<float> startRatio{0.0f};
    std::atomic<float> endRatio{1.0f};
    
    double sourceSampleRate = 0.0;
    int length = 0;
    std::unique_ptr<juce::AudioBuffer<float>> data;
    juce::BigInteger midiNotesRange;
    int normalPitchMidiNote = 60;
    
    struct EnvelopeParams {
        double attack = 0.005;
        double release = 0.1;
    } params;
};

class CustomSamplerVoice : public juce::SynthesiserVoice {
public:
    CustomSamplerVoice() {}
    
    bool canPlaySound(juce::SynthesiserSound* sound) override {
        return dynamic_cast<CustomSamplerSound*>(sound) != nullptr;
    }
    
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int /*currentPitchWheelPosition*/) override {
        if (auto* s = dynamic_cast<CustomSamplerSound*>(sound)) {
            double sr = getSampleRate();
            if (sr <= 0.0) sr = 44100.0;

            double pitchOffset = s->pitchOffsetSemitones.load();
            double noteOffset = midiNoteNumber - s->rootNote.load() + pitchOffset;
            pitchRatio = std::pow(2.0, noteOffset / 12.0) * (s->sourceSampleRate / sr);
            
            sourceSamplePosition = s->startRatio.load() * s->length;
            lgain = velocity * s->volume.load();
            rgain = lgain;
            
            envelopeLevel = 1.0f;
            envelopeReleaseCoeff = 1.0f - (float)std::exp(-1.0f / (std::max(0.001, s->params.release) * sr));
            isReleasing = false;
        }
    }
    
    void stopNote(float /*velocity*/, bool allowTailOff) override {
        if (allowTailOff) {
            isReleasing = true;
        } else {
            clearCurrentNote();
        }
    }
    
    void pitchWheelMoved(int /*newPitchWheelValue*/) override {}
    void controllerMoved(int /*controllerNumber*/, int /*newControllerValue*/) override {}
    
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override {
        auto* sound = dynamic_cast<CustomSamplerSound*>(getCurrentlyPlayingSound().get());
        if (sound == nullptr || sound->data == nullptr)
            return;
            
        auto& data = *sound->data;
        const int numChannels = data.getNumChannels();
        
        while (--numSamples >= 0) {
            auto pos = (int)sourceSamplePosition;
            int endSample = (int)(sound->endRatio.load() * sound->length);
            if (pos >= endSample || pos >= sound->length) {
                clearCurrentNote();
                break;
            }
            
            auto alpha = (float)(sourceSamplePosition - pos);
            auto nextPos = pos + 1 < sound->length ? pos + 1 : pos;
            
            float outL = 0.0f;
            float outR = 0.0f;
            
            if (numChannels == 1) {
                float s0 = data.getSample(0, pos);
                float s1 = data.getSample(0, nextPos);
                outL = s0 + alpha * (s1 - s0);
                outR = outL;
            } else {
                float s0_L = data.getSample(0, pos);
                float s1_L = data.getSample(0, nextPos);
                outL = s0_L + alpha * (s1_L - s0_L);
                
                float s0_R = data.getSample(1, pos);
                float s1_R = data.getSample(1, nextPos);
                outR = s0_R + alpha * (s1_R - s0_R);
            }
            
            float currentGain = lgain * envelopeLevel;
            outL *= currentGain;
            outR *= currentGain;
            
            if (isReleasing) {
                envelopeLevel -= envelopeReleaseCoeff;
                if (envelopeLevel <= 0.0f) {
                    clearCurrentNote();
                    break;
                }
            }
            
            if (outputBuffer.getNumChannels() > 1) {
                outputBuffer.addSample(0, startSample, outL);
                outputBuffer.addSample(1, startSample, outR);
            } else {
                outputBuffer.addSample(0, startSample, (outL + outR) * 0.5f);
            }
            
            sourceSamplePosition += pitchRatio;
            startSample++;
        }
    }
    
private:
    double sourceSamplePosition = 0.0;
    double pitchRatio = 1.0;
    float lgain = 0.0f, rgain = 0.0f;
    float envelopeLevel = 1.0f;
    float envelopeReleaseCoeff = 0.001f;
    bool isReleasing = false;
};

class SamplerProcessor {
public:
    SamplerProcessor() {
        formatManager.registerBasicFormats();
        
        // 16 voices is plenty for 8 simple one-shot pads
        for (int i = 0; i < 16; ++i)
            synth.addVoice(new CustomSamplerVoice());
    }
    
    void prepare(double sampleRate) {
        currentSampleRate = sampleRate;
        synth.setCurrentPlaybackSampleRate(sampleRate);
    }
    
    void reset() {
        synth.allNotesOff(0, false);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
        if (!enabled.load()) {
            if (wasEnabled.exchange(false)) {
                synth.allNotesOff(0, false);
            }
            return;
        }
        wasEnabled.store(true);
            
        synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    }
    
    struct SlotConfig {
        juce::String wavPath;
        int rootNote = 60;
        int keyLow = 0;
        int keyHigh = 127;
        float pitchOffsetSemitones = 0.0f;
        float volume = 1.0f;
        float startRatio = 0.0f;
        float endRatio = 1.0f;
    };
    
    void setSlotConfig(int slotIdx, const SlotConfig& config) {
        if (slotIdx < 0 || slotIdx >= 8) return;
        
        juce::ScopedLock sl(configLock);
        
        bool pathChanged = (configs[slotIdx].wavPath != config.wavPath);
        configs[slotIdx] = config;
        
        if (pathChanged) {
            reloadSound(slotIdx);
        } else {
            // Update existing sound parameters in real-time
            if (auto* sound = sounds[slotIdx].get()) {
                sound->volume.store(config.volume);
                sound->pitchOffsetSemitones.store(config.pitchOffsetSemitones);
                sound->rootNote.store(config.rootNote);
                sound->startRatio.store(config.startRatio);
                sound->endRatio.store(config.endRatio);
                
                // Update key range
                juce::BigInteger keys;
                int low = juce::jlimit(0, 127, juce::jmin(config.keyLow, config.keyHigh));
                int high = juce::jlimit(0, 127, juce::jmax(config.keyLow, config.keyHigh));
                keys.setRange(low, high - low + 1, true);
                sound->midiNotesRange = keys;
            }
        }
    }
    
    SlotConfig getSlotConfig(int slotIdx) const {
        if (slotIdx < 0 || slotIdx >= 8) return {};
        juce::ScopedLock sl(configLock);
        return configs[slotIdx];
    }
    
    std::atomic<bool> enabled{false};

private:
    void reloadSound(int slotIdx) {
        if (sounds[slotIdx] != nullptr) {
            for (int i = synth.getNumSounds() - 1; i >= 0; --i) {
                if (synth.getSound(i) == sounds[slotIdx])
                    synth.removeSound(i);
            }
            sounds[slotIdx] = nullptr;
        }
        
        auto path = configs[slotIdx].wavPath;
        if (path.isNotEmpty()) {
            juce::File file(path);
            if (file.existsAsFile()) {
                std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
                if (reader != nullptr) {
                    juce::BigInteger keys;
                    int low = juce::jlimit(0, 127, juce::jmin(configs[slotIdx].keyLow, configs[slotIdx].keyHigh));
                    int high = juce::jlimit(0, 127, juce::jmax(configs[slotIdx].keyLow, configs[slotIdx].keyHigh));
                    keys.setRange(low, high - low + 1, true);
                    
                    auto* newSound = new CustomSamplerSound(
                        "slot_" + juce::String(slotIdx),
                        *reader,
                        keys,
                        configs[slotIdx].rootNote,
                        0.005,
                        0.1,
                        10.0
                    );
                    newSound->volume.store(configs[slotIdx].volume);
                    newSound->pitchOffsetSemitones.store(configs[slotIdx].pitchOffsetSemitones);
                    newSound->rootNote.store(configs[slotIdx].rootNote);
                    newSound->startRatio.store(configs[slotIdx].startRatio);
                    newSound->endRatio.store(configs[slotIdx].endRatio);
                    
                    sounds[slotIdx] = newSound;
                    synth.addSound(newSound);
                }
            }
        }
    }

    std::atomic<bool> wasEnabled{false};
    juce::Synthesiser synth;
    juce::AudioFormatManager formatManager;
    double currentSampleRate = 44100.0;
    
    mutable juce::CriticalSection configLock;
    SlotConfig configs[8];
    juce::ReferenceCountedObjectPtr<CustomSamplerSound> sounds[8];
};
