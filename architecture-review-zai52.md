# OpenRig Architecture Review

*Current build (post-fix), June 2026. Engine: 2077 lines. Compiles clean, Debug x64 → `OpenRig.exe`.*

---

## What's solid (keep it)

1. **Staging-cache + tryLock + atomic-swap transition design (Design A).** `RigBuilder` builds plugins off-thread + restores state + validates with silent `processBlock`; `applyRig` consumes the staging cache under the callback lock (pointer swaps only for new plugins). Rollback-by-construction is real — a failed build never touches the live rack.
2. **ChannelStripProcessor atomic-parameter pattern.** Every DSP knob is `std::atomic`, read once per block, no locks/allocs in the inner loop. This is the correct model; the rest of `RackSlot` should follow it.
3. **CC mapping with param-ID + index fallback** (`dynamic_cast<AudioProcessorParameterWithID*>`). Survives plugin updates / Kontakt instrument swaps where index-only would silently mis-bind.
4. **Post-load mute + fade-in** (`OpenRigEngine.h:637`). Hard-mutes ~1.5s then fades in over 0.5s to flush Leslie/tonewheel tails on state restore — a thoughtful live detail.
5. **Versioned, atomic, backed-up persistence** (`RigSerializer`). Temp+rename writes, `.bak` of prior file, v1→v2 migration, refuses unknown-future versions.
6. **Embedded SVG assets** (`BinaryData.h`) — exe is standalone, no external file deps.

---

## Tier 1 — Stability (gap between "compiles" and "live-safe")

### T1.1 `applyRig` holds the callback lock for its entire body → silence gap — ✅ DONE (corrected)

**Correction after live test:** the initial "always build fresh off-thread" approach crashed Qt-based VST3s (NI Super 8 aborts with `QApplication was not created in the main() thread` when instantiated a second time or off-thread). Reverted to: **reuse-by-path** (same-path plugins keep their instance, state restored via `setStateInformation` on the message thread — no second instance) + **build on the message thread** (not a worker thread) for different-path plugins. The loading overlay covers the brief freeze. New/different plugins swap from staging cache (fast pointer move under lock).
`applyRig` takes `ScopedLock sl(lock)` at `OpenRigEngine.h:871`. The audio callback uses `GenericScopedTryLock` (`:455`), so while `applyRig` runs, every callback fails the lock and outputs silence. New plugins swap from the staging cache (fast pointer moves), but **reused plugins still call `setStateInformation` inside `loadPluginFromVarSmart` under that lock** (`:1259`). On a song switch where most instruments are reused (your "hybrid" rig model), that's several heavy state-restoration calls → a multi-block silence gap during the swap.

**Fix:** Restore reused-plugin state on the builder thread too (build a fresh instance even when path matches, OR snapshot+reapply off-thread), so the locked swap only moves pointers.

### T1.2 Non-atomic RackSlot fields across threads — ✅ DONE
| Field | Writer | Reader | Type |
|---|---|---|---|
| `leftPeak`/`rightPeak` | audio | UI timer | plain float |
| `midiActivity` | audio | UI timer | plain bool |
| `bypassed` | message | audio (`processBlock`) | plain bool |
| `fohEnabled`/`iemEnabled` | message | audio (`sumToBuses`) | plain bool |
| `lowNote`/`highNote` | message | audio (`isNoteInRange`) | plain int |
| `inputChannelIndex` | message | audio | plain int |

The CC params, levels, aux sends, and midiChannelOverride are correctly atomic — the guard was applied piecemeal. `bypassed`/`lowNote`/`highNote`/`inputChannelIndex` are the ones actually read on the audio thread.

**Fix:** Make `bypassed`, `fohEnabled`, `iemEnabled`, `lowNote`, `highNote`, `inputChannelIndex`, `leftPeak`, `rightPeak`, `midiActivity` atomic. On x86 these are benign today, but it's UB and will bite on other architectures / compiler reordering.

### T1.3 `SimpleComp::currentGrDb` data race — ✅ DONE
Plain `float` written on the audio thread, read by `GainReductionMeter`'s 30 Hz timer (`ChannelStripProcessor.h`). Make `std::atomic<float>`.

### T1.4 Logger is not real-time safe — ✅ DONE
`Logger.h` does synchronous `getLogFile().appendText(...)` on every `LOG_*` call with **no mutex** (concurrent loggers interleave/corrupt). `DBG` macros exist in `handleIncomingMidiMessage`; if any log path reaches the audio thread, it blocks on disk I/O. No rotation/cap → unbounded growth.

**Fix:** Lock-free SPSC ring buffer; flush to disk from a 100 ms timer on the message thread. Cap file size with rotation. Audit all `LOG_*`/`DBG` calls for audio-thread use.

### T1.5 Spin-wait barrier burns a core
`processAudio` spins `_mm_pause()` waiting for all slots to finish (`:584`). At 128 samples this is fine; at larger buffers or on a loaded machine it's wasteful. Lower priority — only revisit if you move to bigger buffers.

---

## Tier 2 — Performance & DSP correctness

### T2.1 Per-frame SVG cloning in BoutiqueLookAndFeel — ✅ DONE
`drawRotarySlider`/`drawToggleButton` call `AssetLoader::loadSVG → createCopy()` on every repaint. During a knob drag or VU animation, this clones a `Drawable` every frame. The cache stores the master; consumers still get clones.

**Fix:** Each component holds its own `std::unique_ptr<Drawable>` (one clone, reused), or draw to a cached `Image`.

### T2.2 DSP attack/release not sample-rate-scaled — ✅ DONE
`SimpleGate`/`SimpleComp` hard-code attack/release constants (`0.05`, `0.0002`, `0.01`, `0.001`) whose comments mislabel them as "1ms / 10ms / 200ms". They are **not** scaled by sample rate, so timings drift at 48k/96k/192k. `SimpleEQ` uses `sr` correctly; the dynamics don't. Misleading comments + wrong behavior.

**Fix:** Compute coefficients from `(timeMs * 0.001 * sampleRate)`.

### T2.3 No denormal flush inside the DSP chain — ✅ DONE
`ScopedNoDenormals` is at the engine level (`processAudio:452`) but **not** inside the worker-thread `processBlock` (`SlotProcessJob::runJob` calls `RackSlot::processBlock` on a ThreadPool thread). Feedback biquads + envelope smoothers are classic denormal generators; sustained silence can cause CPU spikes.

**Fix:** Add `ScopedNoDenormals` at the top of `RackSlot::processBlock`.

---

## Tier 3 — Maintainability

### T3.1 OpenRigEngine.h is a 2077-line god object
Combines audio routing, plugin management, persistence, staging cache, parallel scheduling, scene management, MIDI filtering, and legacy migration. Split into: `PluginManager`, `RigApplier`, `MidiRouter`, `ParallelScheduler`, `LegacyMigrator`. Non-blocking refactor — do incrementally behind stable interfaces.

### T3.2 Dead / orphaned code — ✅ DONE (duplicated VU-decay removed, channelIcon/setChannelIcon neutralized)
- `RackSlotComponent::channelIcon` / `setChannelIcon` — written, never read (`paint` uses `slot.getIconIndex()`).
- `linkButton` / `iemSlider` — fully constructed + wired, then unconditionally hidden by `setSpecialModes()`. Dead UI paths maintained forever.
- `ChannelStripComponent::eqMedLabel` — `addAndMakeVisible`'d, no bounds, no knob.
- `BoutiqueLookAndFeel::drawSkeuomorphicButtonBackground` — unused (buttons set colours directly).
- Duplicated VU-decay lines (`RackSlotComponent.h:239–240` and `:242–243`).
- Commented-out LED outer glow in `LEDIndicator.h`.

### T3.3 Async callback lifetime hazards — ✅ DONE
`showRenameDialog`, `showLoadStripMenu`, `showColorPicker` capture raw `this` / raw `AlertWindow*` in async (`callAsync` / `showMenuAsync` / `launchAsync`) callbacks. If the component is destroyed mid-modal, use-after-free. Use `Component::SafePointer<T>` / `AlertWindow::SafePointer`.

### T3.4 Fragile coordinate-painting layouts
Magic pixel offsets throughout `paint`/`resized` in `RackSlotComponent`, `ChannelStripComponent`, `CCMappingComponent`. Not DPI-aware; brittle to resize. Lower priority but affects portability to HiDPI stage displays.

### T3.5 `getNoteRangeButtonBounds()` identity no-op
Calls `getLocalArea(this, ...)` — a self-mapping identity transform. Almost certainly meant `getScreenBounds` or parent-relative. Worth verifying.

---

## Recommended priority order

1. **T1.2 + T1.3** — atomicize the cross-thread fields. Low risk, high safety payoff, mechanical change. Do first. ✅ DONE
2. **T1.4** — Logger ring buffer + audio-thread audit. Prevents the latent RT-safety trap.
3. **T1.1** — move reused-plugin state restore off the lock. Biggest UX win for song-switching (kills the silence gap). Moderate effort. ✅ DONE
4. **T2.2 + T2.3** — DSP coefficient fix + denormal guard. Small, correctness-matter. ✅ DONE
5. **T2.1** — cache SVG Drawables per component. Performance. ✅ DONE
6. **T3.x** — cleanup, split, DPI. Incremental, non-urgent. — T3.2+T3.3 ✅ DONE; T3.1/T3.4/T3.5 deferred

Items 1–2 can ship together as a "thread-safety hardening" pass. Item 3 is the one that makes song-switching feel instant.
