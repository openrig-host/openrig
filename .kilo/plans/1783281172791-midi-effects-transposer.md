# Plan: Add a Transposer to MIDI Effects (±48 semitones)

## Goal
Expose the existing per-slot MIDI transpose inside the **MIDI Effects** panel as a single **−48..+48 semitone** control, and extend its range to ±48. Move the transpose UI out of the NoteRange dialog so MIDI Effects is its sole home.

## Context (already verified in code)
A per-slot transpose **already exists** — this task reuses it as the single source of truth; no new DSP stage.
- **Data**: `RackSlot.h:255-263` — `transposeOctaves` (clamped **−3..+3**) + `transposeSemitones` (clamped **−11..+11**). Max today ≈ ±47.
- **Applied**: `OpenRigEngine.h:594-602`, in the engine's per-slot MIDI routing, **after** the note-range check (`isNoteInRange`) and **before** the slot's arp/harmonizer/plugin. So the entire layer (arp + harmony output) shifts. **This position is unchanged and intentional** for the "move sound layers up/down" use case.
- **Persisted**: already saved/loaded as `transposeOctaves`/`transposeSemitones` (`OpenRigEngine.h:885-886` save, `:1052-1053` load) and mirrored in `RigModel.h:54-55`. **No schema change needed.**
- **MIDI Effects panel**: `MidiEffectsComponent.h` (header-only, 620×360, two columns: ARP | OCTAVER). Constructed at `MainComponent.cpp:314-315` with `(arp, harm, slotNames, ownSlotIndex, onClose)`.
- **NoteRange UI to remove**: `NoteRangeComponent.h:37-127` (oct/semi buttons, labels, `updateOctaveLabel`/`updateSemiLabel`) and layout `:99-112`.

## Decisions (confirmed with user)
1. **Reuse** the existing per-slot transpose (single source of truth). No second transpose stage.
2. **UI = one −48..+48 semitone control** (slider), internally decomposed into the existing octaves+semitones.
3. **Move transpose out of NoteRange**; MIDI Effects becomes its sole home.

## Implementation tasks

### 1. Widen transpose range + add total clamp — `RackSlot.h`
- `setTransposeOctaves` (`:256-258`): change clamp from `juce::jlimit(-3, 3, oct)` → `juce::jlimit(-4, 4, oct)`.
- Leave `setTransposeSemitones` clamp at `juce::jlimit(-11, 11, semis)` (the ±48 decomposition never exceeds it).
- Add a helper for the combined value used by the UI:
  ```cpp
  int getTransposeSemis() const { return getTransposeOctaves() * 12 + getTransposeSemitones(); }
  void setTransposeSemis(int semis) {
      semis = juce::jlimit(-48, 48, semis);
      int oct = semis / 12;                 // truncates toward zero (C++ integer div)
      int rem = semis - oct * 12;           // in [-11, 11]
      setTransposeOctaves(oct);
      setTransposeSemitones(rem);
  }
  ```
  (Note: C++ integer division truncates toward zero, so −47/12 = −3 and rem = −11. ✓.)

### 2. Guard the total in the audio path — `OpenRigEngine.h:594`
- Change `int transposeSemis = slots[i]->getTransposeOctaves() * 12 + slots[i]->getTransposeSemitones();` to use the new helper and clamp defensively:
  ```cpp
  int transposeSemis = juce::jlimit(-48, 48, slots[i]->getTransposeSemis());
  ```
  (Defensive only — the UI is the sole setter and is bounded ±48.)

### 3. Add TRANSPOSE section to MIDI Effects — `MidiEffectsComponent.h`
- Include `RackSlot.h` (needed for full type).
- Constructor: add a `RackSlot& slot` parameter (before `onClose`). Store as `RackSlot& slotRef;`.
- Add a compact full-width **TRANSPOSE** strip directly under the title (reading order then matches chain order: TRANSPOSE → ARP → OCTAVER). Contents:
  - `juce::Label transposeHeader` ("TRANSPOSE", cyan, bold 14 — matches `arpHeader`/`octHeader` style).
  - `juce::Slider transposeSlider` — **horizontal**, range `(-48, 48, 1)`, centre at 0. `onValueChange` → `slotRef.setTransposeSemis((int)transposeSlider.getValue())`.
  - `juce::Label transposeValue` — bold readout, e.g. `"+12"`, `"-7"`, `"0"`; suffix `" st"`. Updated in `onValueChange`. Optional: show octave+semitone split, e.g. `"+1 oct"`.
  - Initialize slider from `slotRef.getTransposeSemis()`.
  - Optional UX niceties: `transposeSlider.setSliderStyle(...LinearHorizontal)`, double-click-to-zero via `setDoubleClickReturnToDefault(true)` + `setDefaultRange`/`setValue(0)` semantics; and snap-to-multiple-of-12 detents if straightforward.
- `resized()`: carve a full-width row (~46px) from `area` after the title row and before splitting into left/right columns. Place header left, slider filling middle, value label right.
- Increase `setSize(620, 360)` → `setSize(620, ~410)` to fit the new row without crowding.

### 4. Wire the new constructor arg — `MainComponent.cpp:314-315`
- Update both instantiation sites (main slots at ~314 and aux-return slots, if any share this overlay) to pass `*slot` (the `RackSlot&`) as the new parameter. Confirm there is exactly one construction site (grep showed only `:314`).

### 5. Remove transpose from NoteRange — `NoteRangeComponent.h`
- Delete the 4 transpose buttons (`octDownBtn`, `octUpBtn`, `semiDownBtn`, `semiUpBtn`), 2 labels (`octaveLabel`, `semiLabel`), their member declarations, the setup blocks (`:37-87`), `updateOctaveLabel()`/`updateSemiLabel()` (`:115-127`), and the transpose layout in `resized()` (`:99-112`).
- Leave the Learn/Reset buttons and the keyboard range selector intact. The dialog is launched at 800×180 (`MainComponent.cpp:327, 509`); removing the right-side transpose cluster leaves the bottom bar with just Learn/Reset — acceptable, optionally re-center them.

## Edge cases / notes
- **Chain position is unchanged**: transpose stays before arp/harmonizer, so arpeggiated and harmonized notes are shifted too. The `OctaveHarmonizer` Africa-mode thresholds use absolute note numbers (`OctaveHarmonizer.h:114-128`); since transpose already preceded the harmonizer before this change, behavior is preserved — no regression.
- **Real-time safety**: transpose is read on the audio thread via atomic loads (`OpenRigEngine.h:594`) and written from the message thread via atomic stores. Already RT-safe; no locks added.
- **Persistence round-trip**: with octaves now clamped ±4, saved `transposeOctaves` values of ±4 load correctly (no clamp rejection). Values from older rigs (±3) still load unchanged. No v1→v2 migration needed.
- **Note clamping**: transposed notes are already `juce::jlimit(0, 127, …)` (`OpenRigEngine.h:596`); notes that clamp at 0/127 fold silently — existing behavior.

## Validation
1. Build Debug|x64 (MSBuild; redirect `IntDir`/`OutDir` to local disk to avoid NAS `C1083`/`C1041` flakiness). Expect 0 errors/0 warnings.
2. Manual:
   - Open a slot's MIDI Effects; set transpose to +12 → incoming notes shift up an octave into the plugin.
   - Set −7 → confirm −7 semitone shift.
   - Set ±48 (extremes) → confirm shift and no crash; notes near 0/127 fold as expected.
   - Confirm arp/octaver outputs are also transposed (chain order).
   - Save rig → reopen → transpose value restores.
   - Open NoteRange dialog → transpose controls are gone; range selection still works.
3. Verify the slider and any NoteRange-free path agree on the single stored value (open MIDI Effects, set +5, close, reopen → shows +5).

## Out of scope
- A separate/independent second transpose stage (decided against).
- Per-key/note-name presets or a "play in key" auto-detect feature.
- Changing the on-disk schema or adding new serializer fields.
- SIMD/DSP changes (transpose is integer note math, not sample DSP).
