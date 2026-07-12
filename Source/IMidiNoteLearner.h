#pragma once

#include <JuceHeader.h>

/**
 * Abstract mix-in for any Component that can receive MIDI note-on events
 * while in "learn" mode.  MainComponent stores a SafePointer<Component>
 * and dynamic_casts to this interface when routing incoming MIDI.
 */
class IMidiNoteLearner {
public:
    virtual ~IMidiNoteLearner() = default;
    virtual void handleMidiNote(int noteNumber) = 0;
    virtual bool isLearning() const = 0;
};
