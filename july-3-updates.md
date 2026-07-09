# OpenRig - July 3 2026 Session Log

All changes made by Kilo (AI agent) this session. For Gemini/Antigravity review.

## 1. Compile Fixes (project did NOT compile at start)

Antigravity left multiple half-finished refactor passes with inconsistent code:

- **RackSlot.h**: duplicate midiChannelOverride member + duplicate getters (hard redefinition errors). Removed duplicates.
- **RigSerializer**: readSongFromFile/writeSongToFile/serializeSong called but never defined. Created RigSerializer.cpp with full var-to-Song conversion. Added to vcxproj.
- **OpenRigEngine.h**: missing RigModel.h/RigSerializer.h includes; juce::File copyTo (doesnt exist) fixed to copyFileTo.
- **RigSerializer.h**: DynamicObject::getProperty called with 2 args (only takes 1).
- **MidiMonitorComponent.h**: referenced MidiLearnBus::Capture which didnt exist. Added Capture struct + armedLabel + lastCapture to MidiLearnBus.h.
- **CCMappingComponent.h**: AudioProcessorParameter::getParameterID() missing in JUCE 8.0.x, fixed with dynamic_cast. selectRowsBasedOnModifierKeys = false removed (virtual function).
- **MainComponent.cpp**: orphaned code block at file scope (syntax error), loadBtn converted to loadRigAsync, duplicate transitioner member removed.
- **RigBuilder.cpp/RigTransitioner.cpp**: emptied orphaned .cpp files (Design B no longer matching Design A headers).
- **MidiLearnBus.h**: redesigned as superset Target struct + singleton.

## 2. Architecture Improvements (Tier 1 - Stability)

**T1.1 - Plugin state restore off the callback lock:**
Builder builds plugins off-thread. loadPluginFromVarSmart checks staging cache first. State applied on builder thread, swap is pointer-only. Reverted to reuse-by-path after Qt crash (Super 8 aborts on second instance). Worker-thread build + exception guards. Message-thread retry removed (caused Kontakt freeze).

**T1.2 - Atomicized 11 cross-thread RackSlot fields:**
bypassed/fohEnabled/iemEnabled/fadersLinked to atomic bool. lowNote/highNote/inputChannelIndex to atomic int. leftPeak/rightPeak to atomic float. midiActivity/inputActive to atomic bool. 44 call sites updated.

**T1.3 - SimpleComp::currentGrDb to atomic float** (was plain float, data race with GainReductionMeter).

**T1.4 - Logger rewrite:**
Old: synchronous file I/O per call, no mutex, no rotation. New: mutex-protected StringArray queue. flushLog() from UI timer. 10MB rotation. Audio-thread DBG removed from applyCCMappings.

**T2.1 - SVG caching in BoutiqueLookAndFeel:**
Per-component Drawable cache (loaded once, reused every repaint). Cleaned up dev commentary.

**T2.2 - DSP coefficient SR-scaling:**
SimpleGate/SimpleComp attack/release computed from sample rate: 1 - exp(-1 / (timeMs * sr)).

**T2.3 - Denormal guard:** ScopedNoDenormals added to RackSlot::processBlock (worker threads).

**T3.2 - Dead code:** Removed duplicated VU-decay lines, neutralized channelIcon/setChannelIcon.

**T3.3 - SafePointer lifetime fixes:** showRenameDialog, showLoadStripMenu, showColorPicker now use SafePointer.

**Channel variable unification:** Removed redundant globalDefaultMidiChannel + defaultMidiChannel duplication. Single atomic defaultMidiChannel.

## 3. Crash / Stability Fixes (from live testing)

**Exception guards:**
- buildPluginFromVar: catch(...) around createPluginInstance + prepareToPlay + setStateInformation. Arturia CException and Kontakt runtime_error caught, not fatal.
- loadPluginFromVarSmart: catch(...) around reused-plugin setStateInformation.
- RackSlot::processBlock: catch(...) around each plugin->processBlock(). One plugin exception wont crash audio thread.

**SEH-protected message loop (Main.cpp):**
Replaced START_JUCE_APPLICATION with custom WinMain. runDispatchLoopSEH() wraps runDispatchLoop() in __try/__except. Kontakt/Super 8 internal window access violations caught; loop restarts. App survives.

**Reuse-by-path restored:**
Always-build-fresh crashed Super 8 (Qt-based NI plugin aborts on second instance). Reverted: same-path plugins keep instance, state restored via setStateInformation.

**Message-thread retry removed:**
Kontakt hung the message thread 30+ seconds during retry. Now failed plugins are logged and skipped. Partial-success apply.

**60-second timeout on applyRig:**
WaitableEvent::wait(60000) prevents permanent hang.

## 4. Asset Loading Fix

- Path resolution: exe-relative (5 levels up to project root) instead of __FILE__.
- SVG loading: XmlDocument::parse + Drawable::createFromSVG instead of createFromImageFile.
- Embedded assets: BinaryData.h with 4 SVGs as raw string literals. Exe is standalone.

## 5. UI Improvements

- Setup name label in header (cyan, shows loaded rig file name).
- Editor creation overlay: showLoadingOverlay before createEditor(), deferred via callAsync, wrapped in try/catch.
- fohCC/iemCC persistence confirmed in exportRigToJson (round-trip complete).

## 6. New Feature: Arpeggiator

**SimpleArpeggiator.h (DSP):** 16th-note subdivision, BPM to 1 decimal. Patterns: Up/Down/UpDown/Random. Octave range 0-4 up/down. Gate 0-100%. Retrigger on first note-on after silence. Defaults: 127 BPM, Random, 2 octaves up, 75% gate.

**ArpeggiatorComponent.h (UI):** Modal overlay with BPM editor, octave sliders, gate slider, pattern radio buttons, enable toggle.

**Integration:** RackSlot.h member + processBlock + prepare + accessor. RackSlotComponent "ARP" button. MainComponent overlay wiring. vcxproj updated.

## 7. Documentation

- README.md (both locations): rewritten. OpenRig, CK88, Song/Scene/Set model, new architecture.
- architecture-review-zai52.md: all items marked DONE.

## Build Status

Final build: clean (OpenRig.exe, Debug x64).

## Remaining Work

- T3.1: OpenRigEngine.h god-object split (2077 lines)
- T3.4: DPI-aware layouts
- Arpeggiator persistence (save/load in rig JSON)
- Out-of-process plugin hosting (SEH wrapper is containment, not isolation)
