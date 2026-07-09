# Sampler / Sample-Player Compile Fix Plan

## Context
New sample-player feature (`SamplerComponent.h`, `SamplerProcessor.h`, `WaveformSpliceEditor.h`, wired via `RackSlot.h`) was added. A previous attempt (Gemini) tried to clear the build errors. This plan verifies and completes that work.

**JUCE version in use: 8.0.12** (confirmed at `C:\JUCE\modules\juce_gui_basics\juce_gui_basics.h:47`).

## Root cause of remaining errors
In JUCE 8, `juce::Slider` exposes value/text formatting as **public member fields**, not setters, and they are named:
- `textFromValueFunction` — `std::function<String(double)>` (value → text)  (`juce_Slider.h:629`)
- `valueFromTextFunction` — `std::function<double(const String&)>` (text → value) (`juce_Slider.h:626`)

Gemini renamed the original (nonexistent) `setValueToTextFunction(...)` calls to `valueToTextFunction = ...`, which is **also not a member**. The correct name is `textFromValueFunction`.

## Verified findings

| File:Line | Current (broken) | Status |
|---|---|---|
| `SamplerComponent.h:50` | `pitchSlider.valueToTextFunction = ...` | WRONG member name |
| `SamplerComponent.h:68` | `rootSlider.valueToTextFunction = noteToText;` | WRONG member name |
| `SamplerComponent.h:77` | `rangeSlider.valueToTextFunction = noteToText;` | WRONG member name |
| `WaveformSpliceEditor.h:133` | `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(...)` | Already correct ✓ |
| `WaveformSpliceEditor.h:57,59` | `juce::Rectangle<float>(0, 0, ...)` / `(..., 0, ...)` | C4244 warnings only (int→float) |

Lambda signatures already match `std::function<String(double)>` — no signature changes needed, only the member name.

## Implementation steps

### Step 1 — Fix the three slider member names (REQUIRED, blocks build)
File: `DaveCoreProject/Source/SamplerComponent.h`

1. Line 50: `pitchSlider.valueToTextFunction` → `pitchSlider.textFromValueFunction`
2. Line 68: `rootSlider.valueToTextFunction` → `rootSlider.textFromValueFunction`
3. Line 77: `rangeSlider.valueToTextFunction` → `rangeSlider.textFromValueFunction`

(Use a targeted replace of the token `valueToTextFunction` → `textFromValueFunction`; all 3 occurrences are these lines. Safe to `replaceAll` within this file.)

### Step 2 — Clear narrowing warnings (RECOMMENDED, non-blocking)
File: `DaveCoreProject/Source/WaveformSpliceEditor.h`
1. Line 57: `juce::Rectangle<float>(0, 0, startX, getHeight())` → change the two `0` int literals to `0.0f`.
2. Line 59: `juce::Rectangle<float>(endX, 0, getWidth() - endX, getHeight())` → change the `0` literal to `0.0f`.

## Validation
1. Rebuild the `DaveCoreProject` target (MSVC / Visual Studio). Expect `Rebuild All: 1 succeeded`.
2. Specifically confirm the following errors are GONE:
   - `C2039: 'valueToTextFunction': is not a member of 'juce::Slider'` (3× in SamplerComponent.h)
   - `C2143` / `C4430` / `C4183` in `WaveformSpliceEditor.h:133-134`
3. After Step 2, confirm C4244 warnings at `WaveformSpliceEditor.h:57,59` are gone.
4. Smoke test at runtime: open the Sample Playback panel, drag a WAV onto a pad, verify the Pitch/Root text boxes show formatted values (e.g. `+5 st`, `C4`) — this proves `textFromValueFunction` is actually wired.

## Out of scope (flagged risks — DO NOT address in this pass unless asked)
- **`SamplerProcessor.h:228-260` `reloadSound()` indexing bug (runtime, not compile):**
  `synth.removeSound(slotIdx)` and `synth.addSound(newSound)` key off the **slot index**, but `juce::Synthesiser` indexes its sound array by **insertion order**. If pads are loaded out of order (e.g. load pad 3 before pad 0), `removeSound(slotIdx)` removes the wrong sound or goes out of range, and key-range/turn-off behavior can desync. Consider tracking each sound's synth index, or using `synth.clearSounds()` + rebuild, or removing by sound pointer. Flagged for a follow-up; not part of this compile-fix task.
- Serialization of `SamplerProcessor::SlotConfig` (8 pads) into `RigSerializer` / rig save-load was not audited here. Verify pads persist across save/load if that is expected.
