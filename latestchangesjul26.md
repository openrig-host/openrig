# OpenRig Changes Summary — July 26, 2026

This document details all code modifications, DSP optimizations, architectural improvements, and bug fixes implemented in **OpenRig** on July 26, 2026. Prepared for system audit by **GLM 5.2**.

---

## 1. Mixer Subgroups & Signal Routing
* **Subgroup Routing Support**: Implemented output target selection for channel strips (`outputTarget` property in `RackSlot` and `RackSlotComponent`).
* **Inline Subgroup Summing**: Slots can now route audio directly into downstream slots acting as subgroups (`sumToSubgroup`) before final summing into FOH/IEM buses.
* **Layout & Persistence**: Added `outputSelector` combo box to `RackSlotComponent` top UI area and included `outputTarget` in JSON rig serialization/deserialization.

---

## 2. DSP Scheduling & Underrun Detection Optimization
* **Sequential Threshold Tuning**: Raised the sequential inline processing threshold to 3 active plugins (`slotsWithPlugins <= 3`), bypassing thread-pool overhead, context switches, and spin-lock timeouts for smaller setups.
* **Underrun Detector Calibration**: Relaxed the audio underrun detection multiplier to `2.2x` to eliminate false-positive XRUN warnings triggered by minor OS scheduling jitter.
* **Real-Time Thread Deferral**: Offloaded all disk I/O and UI label string operations out of the real-time audio callback onto the main GUI thread (`OpenRigLog::logToFile` deferral).

---

## 3. Threading, VST Plugin Loading & COM Concurrency
* **Off-Thread Plugin Builder**: Moved VST3 plugin instantiation, stereo topology configuration, `prepareToPlay`, and `setStateInformation` off the message thread onto background worker threads inside `RigBuilder.h`.
* **Isolated Thread 25s Timeout**: Wrapped individual plugin instantiations in dedicated `std::thread` instances with a 25-second watchdog timer to gracefully handle hanging 3rd-party VST3 DLLs.
* **COM Apartment Model Fix (MTA)**: Switched background builder thread COM initialization from `COINIT_APARTMENTTHREADED` (STA) to `COINIT_MULTITHREADED` (MTA) in `RigBuilder.h`. This eliminates cross-apartment COM deadlocks when VST3 plugins (e.g. Kontakt 8) invoke internal COM helpers during state restoration without a Win32 message pump.

---

## 4. Telemetry & Memory Optimization
* **RAM Telemetry Throttling**: Reduced `GetProcessMemoryInfo` and system memory queries to `0.5 Hz` (once every 2 seconds) inside telemetry loops to prevent Windows kernel page-table lock contention.

---

## 5. Crash Diagnostics & Shutdown Safety
* **Enhanced Callstack Capture**: Updated the centralized crash handler (`crashHandler` in `Logger.h`) to capture C++ backtraces even for `terminate()` and `abort()` calls with `null` `EXCEPTION_POINTERS`.
* **Reentrancy Protection on Editor Close**: Ensured VST editor windows are closed asynchronously on destruction to prevent reentrant message loop callbacks during plugin teardown.
* **Instant Process Termination**: Ensured `TerminateProcess()` is called immediately following crash log writing to prevent zombie background threads from holding Windows audio/MIDI driver handles open.

---

## 6. UI Layout & Controls
* **Mixer Fader Learn Buttons**: Added visible bounds and layout positioning for `fohLearnBtn` and `iemLearnBtn` next to FOH and IEM faders on channel strips.

---

## Commit Log (July 26, 2026)

| Hash | Author | Message |
| --- | --- | --- |
| `f7564c7` | dwaugh | Fix mixer fader learn layout: set bounds for fohLearnBtn and iemLearnBtn so they are visible |
| `8a8bb2e` | dwaugh | Implement mixer subgroups: support output routing dropdown, left-to-right inline buffer summing, and JSON setup persistence |
| `e261752` | dwaugh | Enhance crash handler diagnostics: capture C++ callstack even for terminate/abort exceptions with null exceptionInfo |
| `715bb4c` | dwaugh | Optimize DSP scheduling: increase sequential execution threshold to 3 active plugins, and relax underrun detector multiplier to 2.2x to filter false-positive scheduling jitter |
| `4d83399` | dwaugh | Fix reentrancy crash on VST editor destruction: close plugin windows asynchronously and ensure instant process termination in crash handler |
| `72938f4` | dwaugh | Throttle RAM telemetry queries: reduce GetProcessMemoryInfo call frequency to 0.5Hz to prevent kernel page-table lock contention |
| `7b57e8f` | dwaugh | Optimize underrun logger: Defer all file I/O and UI label queries from real-time audio thread to main GUI thread |
| `9ecfe9d` | dwaugh | Terminate process on shutdown to eliminate zombie background threads locking audio/MIDI drivers |
| `12442f7` | dwaugh | Include Win32/COM headers in RigBuilder.h to resolve CoInitializeEx compilation errors |
| `2dd8592` | dwaugh | Fix COM initialization on background plugin load threads & optimize MP3 load path |
| `cd0598d` | dwaugh | Optimize DSP thread scheduling: sequential inline processing for <= 1 active VST setups to eliminate thread-pool context switch latency, cache thrashing, and premature spin timeouts |
| `98eb0c0` | dwaugh | Enhance plugin loader robustness: wrap off-thread instantiations in isolated background threads with 25s timeout to bypass stuck DLL lockups |
| `904e560` | dwaugh | Eliminate VST load hangs and UI freezes: load plugins off-thread by default, with automatic message-thread retry fallback for GUI/Qt-dependent VSTs |
| `84cd18e` | dwaugh | Fix audio clicks and setup loading: revert paramsDirty, remove NaN per-sample scan from audio thread, restore callAsync+180s timeout for plugin builds |
| `627c63d` | dwaugh | Fix plugin loading hang: replace callAsync+wait deadlock with callFunctionOnMessageThread for guaranteed immediate execution |
| *Uncommitted* | dwaugh | Fix COM background thread deadlock in `RigBuilder.h` by switching `CoInitializeEx` from `COINIT_APARTMENTTHREADED` to `COINIT_MULTITHREADED` |

---

## Files Modified
* [z:\davecore\Source\RigBuilder.h](file:///z:/davecore/Source/RigBuilder.h)
* [z:\davecore\Source\OpenRigEngine.h](file:///z:/davecore/Source/OpenRigEngine.h)
* [z:\davecore\Source\RackSlot.h](file:///z:/davecore/Source/RackSlot.h)
* [z:\davecore\Source\RackSlotComponent.h](file:///z:/davecore/Source/RackSlotComponent.h)
* [z:\davecore\Source\RackSlotComponent.cpp](file:///z:/davecore/Source/RackSlotComponent.cpp)
* [z:\davecore\Source\MainComponent.cpp](file:///z:/davecore/Source/MainComponent.cpp)
* [z:\davecore\Source\Logger.h](file:///z:/davecore/Source/Logger.h)

---

# Audit Findings — GLM 5.2 Code Review (July 26, 2026)

Reviewed the full diff of today's commits (`627c63d..f7564c7`) plus the uncommitted
`COINIT_MULTITHREADED` change. Findings are ordered by severity. `file:line` refs are to
the **current working tree**, not the diff.

---

## CRITICAL

### 1. Subgroup routing is functionally broken (audio is destroyed before it reaches the bus)
`OpenRigEngine.h:880-893` — sequential path:
```cpp
for (int i = 0; i < numActiveSlots; ++i) {
    preallocatedJobs[i]->runJob();          // writes scratchBuffers[i]
    int target = slots[i]->getOutputTarget();
    if (target >= 0 ...) slots[i]->sumToSubgroup(scratchBuffers[i], scratchBuffers[target]);
    else                 slots[i]->sumToBuses(...);
}
```
`runJob()` (`OpenRigEngine.h:461-483`) seeds the slot buffer with a hard overwrite:
```cpp
scratch.copyFrom(0, 0, l, numSamples);   // overwrite, NOT addFrom
slot->processBlock(scratch, midi);
```
So when the loop reaches `i == target`, the subgroup bus buffer (`scratchBuffers[target]`) —
which has just received the summed audio of all upstream slots via `sumToSubgroup` — is
**overwritten** by the target slot's own hardware input and reprocessed. All subgrouped
audio is discarded. Subgrouping only "works" by accident when the target slot has
`inputChannelIndex < 0` (no direct hardware input), so `copyFrom` is skipped.
**Fix:** the subgroup target must accumulate, not overwrite — either (a) make the target
slot add its input (`addFrom`) into `scratchBuffers[target]` when it is acting as a
subgroup bus, or (b) route subgroup sums into a dedicated bus buffer that the target slot
processes in place, separate from per-slot hardware-input seeding.

### 2. Subgrouped channels are attenuated twice (double fader gain) and lose IEM balance
`RackSlot.h:379-396` (`sumToSubgroup`) applies the slot's `fohLevel` via
`addFromWithRamp(... lastFohLevel, fohBase)`. The subgroup target then calls
`sumToBuses` (`RackSlot.h:340-377`), which applies `fohLevel` **again** to the entire
subgroup mix. Net: a subgrouped channel is scaled by the source fader × target fader.
Additionally `sumToSubgroup` writes **only** into the (eventual) FOH/IEM path through the
target — the source slot's own `iemOffset` and IEM level are bypassed entirely; the
channel inherits the target's IEM balance. **Fix:** subgroup pre-fader sum should be
unity (or a dedicated subgroup-send level), not `fohLevel`; apply fader gain once at the
target's `sumToBuses`. Preserve per-channel IEM offset via a separate IEM subgroup bus.

---

## HIGH

### 3. Enabling one subgroup disables parallel DSP for the whole engine
`OpenRigEngine.h:880` — `if (slotsWithPlugins <= 3 || hasSubgroups)` forces the sequential
inline path whenever *any* slot has `outputTarget >= 0`. On a large multi-VST rig (e.g. 6
slots, heavy instruments) where a single channel is subgrouped, all parallelism is lost
and every plugin runs on the single audio thread — directly increasing underrun risk at
low buffer sizes, the opposite of the changelog's performance goals. The subgroup feature
and the thread-pool path are currently mutually exclusive. **Fix:** support subgroup
accumulation inside the parallel fork-join (sum subgroup sends in the post-join
`sumToBuses` pass), or document the limitation and cap subgroup use to small rigs.

### 4. Timed-out background plugin build leaks VST instances and races the message-thread retry
`RigBuilder.h:226-229` and `354-357` — on the 25s timeout the code does:
```cpp
logToFile("... timed out (25s) ... Detaching thread...");
t.detach();   // shared_ptr<BuildThreadContext> + partially-built AudioPluginInstance leaked
ok = false;
```
After detach the code falls through to the message-thread retry. Two problems:
- **Leak:** the detached thread still holds `shared_ptr<BuildThreadContext>` containing a
  (possibly half-built) `AudioPluginInstance`. If the plugin never returns, the instance,
  its host resources, and the MTA COM init live until process exit. Each timed-out plugin
  per session adds another zombie → cumulative handle/driver pressure (the exact problem
  the "anti-zombie" shutdown changes were meant to address).
- **Race:** the detached build keeps calling `buildPluginFromVar` (and thus the JUCE
  `AudioPluginFormatManager` / known-plugin-list caches) **concurrently** with the
  message-thread retry of the same call. JUCE does not guarantee these are reentrant.
**Fix:** don't fall through to a concurrent retry while the build thread is alive. Either
cancel/abandon cleanly (accept the single failure), or run the retry only after a
deterministic teardown. At minimum, guard `buildPluginFromVar` against concurrent entry.

### 5. Real-time regression: every channel-strip parameter is re-applied every block
`ChannelStripProcessor.h` — commit `84cd18e` deleted `paramsDirty`/`setParamDirty`, so
`processBlock` now unconditionally performs ~25 atomic loads plus their setters on **every
audio block for every slot**, including `compL.setPreset(...)` / `compR.setPreset(...)`
(`ChannelStripProcessor.h:208-245`) which call `updateCoefficients(...)` — coefficient
recomputation with transcendental math — and `eqL.setBandGain(b, ...)` ×10. At 128-sample
buffers across many slots this is exactly the kind of repeated per-block work the other
commits were removing from the RT path. **Fix:** reintroduce a cheap dirty flag (or
compare cached values) so setters/`updateCoefficients` only run when a parameter actually
changed.

---

## MEDIUM

### 6. Hard-coded message-thread allowlist for a single plugin
`RigBuilder.h:187` and `:315` — `bool requiresMessageThread = newPath.containsIgnoreCase("Super 8");`.
Only "Super 8" is special-cased. The changelog itself cites Kontakt 8 and other Qt/GUI
VSTs as message-thread-dependent; those will now go through the background build, hang for
the full 25s, and only then fall back — a poor experience and brittle allowlist. **Fix:**
drive this from plugin metadata / a user-editable list, or treat all builds as
message-thread by default with the off-thread path as the opt-in for known-safe plugins.

### 7. COM apartment mismatch between build thread (MTA) and message thread (STA)
`RigBuilder.h:209` / `:337` — background build uses `COINIT_MULTITHREADED` while the JUCE
message thread (where editors open and the retry path builds) is STA. A plugin
instantiated in MTA and later driven from STA can incur cross-apartment marshalling and
lifetime quirks; behavior now depends on which thread happened to build the instance. The
MTA switch does fix the documented Kontakt deadlock, but the mismatch is a latent
correctness risk. **Fix:** keep construction and the instance's usable lifetime on the
same apartment, or validate per-vendor and log which thread built each plugin.

### 8. No output protection after removing the NaN/Inf scan
`MainComponent.cpp` (commit `84cd18e`) removed the per-sample NaN/Inf/spike clamp from the
audio callback with no replacement. A misbehaving plugin emitting NaN/Inf or large DC now
passes straight to the interface. (The per-sample scan was correctly too expensive for RT;
the issue is that *nothing* replaced it.) **Fix:** add one cheap block-level
finite/magnitude check on the master bus per block (e.g. `buffer.getMagnitude` +
`std::isfinite` on a peak sample) and mute/sanitize on failure.

---

## LOW

### 9. Subgrouped slots silently drop aux sends
`OpenRigEngine.h:887-893` — a subgrouped slot calls only `sumToSubgroup` and never
`sumToBuses`, so its `aux1`/`aux2` sends are dropped; only the subgroup target contributes
to aux. Likely intended, but undocumented — confirm or wire post-fader aux sends from the
target.

### 10. Glitch-log deferral coalesces rapid underruns
`MainComponent.h` / `MainComponent.cpp` — glitches are stored in single
`std::atomic<double>` slots (`pendingGlitchActualMs/ExpectedMs`) + a bool. Multiple
glitches between timer ticks collapse to the last one, so the written log count diverges
from `audioUnderrunCount`. Low impact, but telemetry is lossy. **Fix:** queue values or at
least increment a coalesced counter.

### 11. Unconditional `TerminateProcess()` on normal shutdown
`Main.cpp:56` — `shutdown()` hard-kills the process after deleting `mainWindow`. Main
destructors do run first, so this is a defensible anti-zombie measure, but it (a) skips
JUCE static/leak-detector teardown and (b) permanently masks real shutdown hangs (e.g.
plugin-unload crashes will never be diagnosed). **Fix:** keep it if needed, but log an
explicit "intentional force-terminate" line first, and ensure all config/audio-settings
writes are flushed before this point.

---

### Quick wins to prioritize
1. Fix #1/#2 — subgroup routing is the headline feature of the day and currently does not
   sum correctly.
2. Fix #5 — restore `paramsDirty`; it's a pure RT-performance regression.
3. Address #4's race before relying on the off-thread build in production.
