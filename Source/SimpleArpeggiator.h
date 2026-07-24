#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <random>
#include <algorithm>
#include <cmath>

class SimpleArpeggiator {
public:
    enum class Pattern { Up = 0, Down, UpDown, Random };

    SimpleArpeggiator() {
        std::fill(std::begin(heldNotesVelocity), std::end(heldNotesVelocity), 0);
        heldNotesCount = 0;
        arpNotesCount = 0;
    }

    void prepare(double sr) { sampleRate = sr; }

    void reset() {
        std::fill(std::begin(heldNotesVelocity), std::end(heldNotesVelocity), 0);
        heldNotesCount = 0;
        arpNotesCount = 0;
        soundingNote = -1;
        noteOffAt = -1;
        nextStepDbl = 0.0;
        stepIndex = 0;
        direction = 1;
        totalSamples = 0;
    }

    void processBlock(juce::MidiBuffer& midi, int numSamples) {
        if (!enabled.load()) {
            if (soundingNote >= 0) {
                midi.addEvent(juce::MidiMessage::noteOff(1, soundingNote), 0);
                soundingNote = -1;
            }
            if (heldNotesCount > 0) {
                reset();
            }
            return;
        }

        float currentBpm = bpm.load();
        if (currentBpm < 1.0f) currentBpm = 1.0f;

        int up = juce::jlimit(0, 4, octavesUp.load());
        int down = juce::jlimit(0, 4, octavesDown.load());
        float g = gate.load();
        Pattern pat = static_cast<Pattern>(patternIdx.load());

        // 16th-note step length
        double stepLen = sampleRate * 60.0 / currentBpm / 4.0;
        int gateLen = std::max(1, (int)(stepLen * std::clamp(g, 0.01f, 1.0f)));

        // --- 1. Scan input for note events ---
        bool notesChanged = false;
        bool firstNoteAfterSilence = false;
        bool wasEmpty = (heldNotesCount == 0);

        for (const auto meta : midi) {
            auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0) {
                int note = msg.getNoteNumber();
                if (note >= 0 && note < 128) {
                    if (wasEmpty) firstNoteAfterSilence = true;
                    if (heldNotesVelocity[note] == 0) {
                        heldNotesCount++;
                    }
                    heldNotesVelocity[note] = msg.getVelocity();
                    notesChanged = true;
                }
            } else if (msg.isNoteOff()) {
                int note = msg.getNoteNumber();
                if (note >= 0 && note < 128) {
                    if (heldNotesVelocity[note] > 0) {
                        heldNotesVelocity[note] = 0;
                        heldNotesCount = std::max(0, heldNotesCount - 1);
                        notesChanged = true;
                    }
                }
            }
        }

        // --- 2. No notes held: send note-off and bail ---
        if (heldNotesCount == 0) {
            juce::MidiBuffer output;
            if (soundingNote >= 0) {
                output.addEvent(juce::MidiMessage::noteOff(1, soundingNote), 0);
                soundingNote = -1;
                noteOffAt = -1;
            }
            for (const auto meta : midi) {
                auto msg = meta.getMessage();
                if (!msg.isNoteOn() && !msg.isNoteOff())
                    output.addEvent(msg, meta.samplePosition);
            }
            midi.swapWith(output);
            return;
        }

        // --- 3. Rebuild pattern if notes changed ---
        if (notesChanged) {
            int prevSounding = soundingNote;
            buildNoteList(up, down);
            if (arpNotesCount == 0) { midi.clear(); return; }

            if (firstNoteAfterSilence) {
                // Starting fresh from silence — reset to the lowest held note
                // and fire the first step immediately in this block.
                stepIndex = 0;
                direction = 1;
                nextStepDbl = (double)totalSamples;
                soundingNote = -1;
                noteOffAt = -1;
            } else {
                // Adding/releasing notes on a running pattern — keep continuity
                // by anchoring stepIndex to the currently-sounding note so the
                // next step plays the next note in the (possibly new) list.
                if (prevSounding >= 0) {
                    int foundIdx = -1;
                    for (int j = 0; j < arpNotesCount; ++j) {
                        if (arpNotes[j] == prevSounding) {
                            foundIdx = j;
                            break;
                        }
                    }
                    if (foundIdx >= 0) {
                        stepIndex = foundIdx + 1;
                    }
                }
            }

            if (stepIndex >= arpNotesCount)
                stepIndex = 0;
        }

        if (arpNotesCount == 0) return;

        // --- 4. Build output buffer ---
        juce::MidiBuffer output;

        for (const auto meta : midi) {
            auto msg = meta.getMessage();
            if (!msg.isNoteOn() && !msg.isNoteOff())
                output.addEvent(msg, meta.samplePosition);
        }

        int blockStartAbs = totalSamples;
        int blockEndAbs = totalSamples + numSamples;

        // --- 5. Pending note-off from previous block gate ---
        if (noteOffAt >= 0 && noteOffAt < blockEndAbs && soundingNote >= 0) {
            int relPos = juce::jlimit(0, numSamples - 1, noteOffAt - blockStartAbs);
            output.addEvent(juce::MidiMessage::noteOff(1, soundingNote), relPos);
            soundingNote = -1;
            noteOffAt = -1;
        }

        // --- 6. Step boundaries in this block ---
        while ((int)nextStepDbl < blockEndAbs) {
            int relPos = juce::jlimit(0, numSamples - 1, (int)nextStepDbl - blockStartAbs);

            if (soundingNote >= 0) {
                output.addEvent(juce::MidiMessage::noteOff(1, soundingNote), relPos);
                soundingNote = -1;
            }

            if (arpNotesCount > 0) {
                int idx = stepIndex % arpNotesCount;
                int note = arpNotes[idx];
                int vel = arpVelocities[idx];
                output.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)vel), relPos);
                soundingNote = note;
                noteOffAt = (int)nextStepDbl + gateLen;
            }

            advanceStep(pat);
            nextStepDbl += stepLen;
        }

        // --- 7. Check if the last note triggered in the loop needs to be turned off in this block ---
        if (noteOffAt >= 0 && noteOffAt < blockEndAbs && soundingNote >= 0) {
            int relPos = juce::jlimit(0, numSamples - 1, noteOffAt - blockStartAbs);
            output.addEvent(juce::MidiMessage::noteOff(1, soundingNote), relPos);
            soundingNote = -1;
            noteOffAt = -1;
        }

        totalSamples = blockEndAbs;
        midi.swapWith(output);
    }

    // --- Parameters (atomic) ---
    std::atomic<bool> enabled{false};
    std::atomic<float> bpm{127.0f};
    std::atomic<int> octavesUp{0};
    std::atomic<int> octavesDown{0};
    std::atomic<float> gate{0.9f};
    std::atomic<int> patternIdx{(int)Pattern::Random};

private:
    void buildNoteList(int up, int down) {
        arpNotesCount = 0;
        for (int note = 0; note < 128; ++note) {
            if (heldNotesVelocity[note] > 0) {
                int srcVel = heldNotesVelocity[note];
                for (int oct = -down; oct <= up; ++oct) {
                    int n = note + oct * 12;
                    if (n >= 0 && n <= 127) {
                        bool duplicate = false;
                        for (int j = 0; j < arpNotesCount; ++j) {
                            if (arpNotes[j] == n) {
                                duplicate = true;
                                break;
                            }
                        }
                        if (!duplicate && arpNotesCount < 512) {
                            int insertIdx = arpNotesCount;
                            while (insertIdx > 0 && arpNotes[insertIdx - 1] > n) {
                                arpNotes[insertIdx] = arpNotes[insertIdx - 1];
                                arpVelocities[insertIdx] = arpVelocities[insertIdx - 1];
                                insertIdx--;
                            }
                            arpNotes[insertIdx] = n;
                            arpVelocities[insertIdx] = srcVel;
                            arpNotesCount++;
                        }
                    }
                }
            }
        }
    }

    void advanceStep(Pattern pat) {
        int total = arpNotesCount;
        if (total == 0) return;

        switch (pat) {
            case Pattern::Up:
                stepIndex = (stepIndex + 1) % total;
                break;
            case Pattern::Down:
                stepIndex = (stepIndex - 1 + total) % total;
                break;
            case Pattern::UpDown:
                stepIndex += direction;
                if (stepIndex >= total - 1) { stepIndex = total - 1; direction = -1; }
                if (stepIndex <= 0)          { stepIndex = 0;          direction = 1;  }
                break;
            case Pattern::Random:
                stepIndex = (int)(rng() % total);
                break;
        }
    }

    double sampleRate = 44100.0;
    int heldNotesVelocity[128];
    int heldNotesCount = 0;
    int arpNotes[512];
    int arpVelocities[512];
    int arpNotesCount = 0;
    int soundingNote = -1;
    int noteOffAt = -1;
    double nextStepDbl = 0.0;
    int stepIndex = 0;
    int direction = 1;
    int totalSamples = 0;
    std::mt19937 rng{std::random_device{}()};
};
