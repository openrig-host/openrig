#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <algorithm>

class OctaveHarmonizer {
public:
    OctaveHarmonizer() {
        std::fill(std::begin(refCount), std::end(refCount), 0);
    }

    void prepare(double /*sr*/) {}

    void reset() {
        // Schedules a flush on the next audio callback. Safe to call from any thread.
        needsFlush.store(true);
    }

    void processBlock(juce::MidiBuffer& midi, int /*numSamples*/,
                      juce::MidiBuffer* harmonyOutput = nullptr) {
        juce::MidiBuffer output;
        juce::MidiBuffer targetOutput; // holds generated voices routed to another strip

        // Generated harmony voices go to destinationBuffer. When a route target
        // is active they are diverted to targetOutput (forwarded to another
        // slot); otherwise they stay local in output.
        bool hasRouteTarget = (harmonyTargetSlot.load() >= 0 && harmonyOutput != nullptr);
        juce::MidiBuffer& destinationBuffer = hasRouteTarget ? targetOutput : output;

        // --- Flush any stuck generated notes (mode change, enable toggle, panic) ---
        if (needsFlush.exchange(false)) {
            for (int note = 0; note < 128; ++note) {
                if (refCount[note] > 0) {
                    output.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                    targetOutput.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                }
            }
            std::fill(std::begin(refCount), std::end(refCount), 0);
        }

        if (!enabled.load()) {
            // Pass all incoming MIDI through unchanged (note-offs reach the synth)
            for (const auto meta : midi)
                output.addEvent(meta.getMessage(), meta.samplePosition);
            if (harmonyOutput != nullptr)
                harmonyOutput->addEvents(targetOutput, 0, -1, 0);
            midi.swapWith(output);
            return;
        }

        int up = octavesUp.load();
        int down = octavesDown.load();
        int africa = africaMode.load();

        auto addLocalNote = [&](int genNote, int vel, int pos) {
            if (genNote >= 0 && genNote < 128) {
                if (refCount[genNote] == 0) {
                    output.addEvent(juce::MidiMessage::noteOn(1, genNote, (juce::uint8)vel), pos);
                }
                refCount[genNote]++;
            }
        };

        auto addHarmonyNote = [&](int genNote, int vel, int pos) {
            if (genNote >= 0 && genNote < 128) {
                if (refCount[genNote] == 0) {
                    destinationBuffer.addEvent(juce::MidiMessage::noteOn(1, genNote, (juce::uint8)vel), pos);
                }
                refCount[genNote]++;
            }
        };

        auto releaseLocalNote = [&](int genNote, int pos) {
            if (genNote >= 0 && genNote < 128) {
                if (refCount[genNote] > 0) {
                    refCount[genNote]--;
                    if (refCount[genNote] == 0) {
                        output.addEvent(juce::MidiMessage::noteOff(1, genNote), pos);
                    }
                }
            }
        };

        auto releaseHarmonyNote = [&](int genNote, int pos) {
            if (genNote >= 0 && genNote < 128) {
                if (refCount[genNote] > 0) {
                    refCount[genNote]--;
                    if (refCount[genNote] == 0) {
                        destinationBuffer.addEvent(juce::MidiMessage::noteOff(1, genNote), pos);
                    }
                }
            }
        };

        for (const auto meta : midi) {
            auto msg = meta.getMessage();
            int pos = meta.samplePosition;

            if (msg.isNoteOn() && msg.getVelocity() > 0) {
                int note = msg.getNoteNumber();
                int vel = msg.getVelocity();

                if (note < 0 || note >= 128) {
                    output.addEvent(msg, pos);
                    continue;
                }

                if (africa != 3) {
                    // Pass original note locally
                    addLocalNote(note, vel, pos);
                }

                if (africa == 1) {
                    addHarmonyNote(note - 5, vel, pos);
                } else if (africa == 2) {
                    addHarmonyNote(note - 5, vel, pos);
                } else if (africa == 3) {
                    int transposed = note - 1;
                    bool isPart1 = (note >= 48);

                    if (isPart1) {
                        addLocalNote(transposed, vel, pos);
                        addHarmonyNote(transposed - 5, vel, pos);
                    } else {
                        addLocalNote(transposed + 36, vel, pos);
                        addHarmonyNote(transposed - 5 + 36, vel, pos);
                    }
                } else {
                    for (int oct = 1; oct <= up; ++oct) {
                        addHarmonyNote(note + oct * 12, vel, pos);
                    }
                    for (int oct = 1; oct <= down; ++oct) {
                        addHarmonyNote(note - oct * 12, vel, pos);
                    }
                }
            } else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0)) {
                int note = msg.getNoteNumber();

                if (note < 0 || note >= 128) {
                    output.addEvent(msg, pos);
                    continue;
                }

                if (africa != 3) {
                    releaseLocalNote(note, pos);
                }

                if (africa == 1) {
                    releaseHarmonyNote(note - 5, pos);
                } else if (africa == 2) {
                    releaseHarmonyNote(note - 5, pos);
                } else if (africa == 3) {
                    int transposed = note - 1;
                    bool isPart1 = (note >= 48);

                    if (isPart1) {
                        releaseLocalNote(transposed, pos);
                        releaseHarmonyNote(transposed - 5, pos);
                    } else {
                        releaseLocalNote(transposed + 36, pos);
                        releaseHarmonyNote(transposed - 5 + 36, pos);
                    }
                } else {
                    for (int oct = 1; oct <= up; ++oct) {
                        releaseHarmonyNote(note + oct * 12, pos);
                    }
                    for (int oct = 1; oct <= down; ++oct) {
                        releaseHarmonyNote(note - oct * 12, pos);
                    }
                }
            } else {
                // Pass through all other MIDI (CC, pitch bend, etc.) — always local
                output.addEvent(msg, pos);
            }
        }

        if (harmonyOutput != nullptr)
            harmonyOutput->addEvents(targetOutput, 0, -1, 0);
        midi.swapWith(output);
    }

    // Parameters — atomic, safe to write from message thread
    std::atomic<bool> enabled{false};
    std::atomic<int>  octavesUp{1};
    std::atomic<int>  octavesDown{0};
    std::atomic<int>  africaMode{0};
    std::atomic<int>  harmonyTargetSlot{-1}; // -1 = local VST, >=0 = route voices to slot

    // Set this from the message thread whenever you change mode, enable, or want a panic.
    // The next processBlock() call will flush all generated notes cleanly.
    std::atomic<bool> needsFlush{false};

private:
    int refCount[128];               // note -> active reference count
};
