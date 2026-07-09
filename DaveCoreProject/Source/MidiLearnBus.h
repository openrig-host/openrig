#pragma once

#include <JuceHeader.h>
#include <functional>
#include <atomic>

namespace OpenRig {

class MidiLearnBus {
public:
    // Superset of all learn-target metadata used across the codebase.
    struct Target {
        // For display in the MIDI monitor banner
        juce::String label;
        juce::String targetId;

        // Metadata used by CCMappingComponent for fader vs param distinction
        bool isFader = false;
        int slotIndex = -1;
        bool isFohFader = false;
        int chainIndex = -1;
        juce::String paramId;
        int parameterIndex = -1;

        // Optional per-target callback (alternative to Component-owner pattern)
        std::function<void(int cc)> onBind;
    };

    struct Capture {
        int ccNum = -1;
        int ccValue = -1;
        juce::int64 timestamp = 0;
        juce::String label;
    };

    static MidiLearnBus& getInstance() {
        static MidiLearnBus instance;
        return instance;
    }

    // Arm with a Target + Component owner (for SafePointer lifetime tracking).
    void arm(Target target, juce::Component* owner) {
        juce::SpinLock::ScopedLockType lock(learnLock);
        currentTarget = std::move(target);
        currentOwner = owner;
        legacyCallback = nullptr;
        isArmedFlag.store(true);
    }

    // Arm with a Target + callback (older pattern used by some call sites).
    void arm(Target target, std::function<void(int cc, int channel)> cb) {
        juce::SpinLock::ScopedLockType lock(learnLock);
        currentTarget = std::move(target);
        currentOwner = nullptr;
        legacyCallback = std::move(cb);
        isArmedFlag.store(true);
    }

    void disarm() {
        juce::SpinLock::ScopedLockType lock(learnLock);
        disarmInternal();
    }

    void disarmIfOwner(juce::Component* owner) {
        juce::SpinLock::ScopedLockType lock(learnLock);
        if (currentOwner == owner)
            disarmInternal();
    }

    bool isArmed() const {
        return isArmedFlag.load();
    }

    juce::String armedLabel() const {
        juce::SpinLock::ScopedLockType lock(learnLock);
        if (!isArmedFlag.load())
            return {};
        return currentTarget.label;
    }

    Capture lastCapture() const {
        juce::SpinLock::ScopedLockType lock(learnLock);
        return lastCap;
    }

    bool handleMidiCC(int ccNum, int ccVal, int channel) {
        if (!isArmedFlag.load())
            return false;

        juce::SpinLock::ScopedLockType lock(learnLock);
        if (!isArmedFlag.load())
            return false;

        lastCap = { ccNum, ccVal, juce::Time::getCurrentTime().toMilliseconds(),
                    currentTarget.label };
        auto bindCopy = currentTarget.onBind;
        auto legacyCopy = legacyCallback;
        disarmInternal();

        if (bindCopy) {
            juce::MessageManager::callAsync([bindCopy, ccNum]() {
                bindCopy(ccNum);
            });
        } else if (legacyCopy) {
            juce::MessageManager::callAsync([legacyCopy, ccNum, channel]() {
                legacyCopy(ccNum, channel);
            });
        }
        return true;
    }

private:
    MidiLearnBus() = default;
    ~MidiLearnBus() = default;

    void disarmInternal() {
        isArmedFlag.store(false);
        currentTarget = {};
        currentOwner = nullptr;
        legacyCallback = nullptr;
    }

    mutable juce::SpinLock learnLock;
    Target currentTarget;
    juce::Component* currentOwner = nullptr;
    std::function<void(int cc, int channel)> legacyCallback;
    std::atomic<bool> isArmedFlag{false};
    Capture lastCap;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnBus)
};

// Backward-compatible alias
using LearnTarget = MidiLearnBus::Target;

} // namespace OpenRig
