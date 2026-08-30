# Fanfare (OpenRig) — Strategic Architectural Review & Development Roadmap
**Date**: August 30, 2026  
**Target Platform**: Windows 11 / x64  
**Author**: Antigravity (Advanced Agentic AI Assistant)  
**Status**: Active Design & Implementation Blueprint  

---

## Executive Summary

**Fanfare** has evolved into a robust, low-latency live keyboard performance environment, mixer, and stage workstation. Built on JUCE C++17 with real-time Win32 multimedia thread scheduling, it provides:
- High-density VST3 plugin hosting across 11 instrument/audio slots with dual FOH and IEM buses.
- Subgroup routing, inline dynamics (SimpleComp, analog-modeled saturation, parametric EQ), and MIDI harmonizers/arpeggiators.
- A dedicated **Dual-Deck DJ & Break Music Engine** featuring equal-power crossfading, Auto-DJ continuous mixing, persistent preset banks, and background YouTube-to-MP3 (192kbps) conversion with instant title verification.
- Integrated **Gig Notepad** with automatic per-setup persistence and real-time synchronization.
- An embedded zero-install **WebSocket / HTTP Web Server** powering companion wireless tablet remotes.

This document presents an in-depth architectural audit of the codebase, identifies existing bottlenecks, and outlines a prioritized roadmap for future refinement, feature expansion, and long-term stability.

---

## Architectural Audit & Current State Analysis

```
+-------------------------------------------------------------------------------+
|                            FANFARE CORE ARCHITECTURE                          |
+-------------------------------------------------------------------------------+
|                                                                               |
|  +-------------------+    +----------------------+    +--------------------+  |
|  |   MIDI ENGINE     |    |    AUDIO HOST / DSP  |    |  DJ / MP3 ENGINE   |  |
|  | - Note Filters    |    | - 11 Multi-Slots     |    | - Dual Deck A / B  |  |
|  | - Harmonizer      |    | - VST3 Off-Thread    |    | - Equal-Power Fade |  |
|  | - Arpeggiator     |    | - Subgroups & Auxes  |    | - Auto-DJ Engine   |  |
|  | - CC Learn Bus    |    | - FOH / IEM Buses    |    | - yt-dlp 192k Grab |  |
|  +---------+---------+    +----------+-----------+    +---------+----------+  |
|            |                         |                          |             |
|            +-------------------------+--------------------------+             |
|                                      |                                        |
|                          +-----------v------------+                           |
|                          |   FANFARE ENGINE CORE  |                           |
|                          |   (FanfareEngine.h)    |                           |
|                          +-----------+------------+                           |
|                                      |                                        |
|            +-------------------------+--------------------------+             |
|            |                                                    |             |
|  +---------v----------+                               +---------v----------+  |
|  |   JUCE DESKTOP GUI |                               |   EMBEDDED WEB SRV |  |
|  | - Boutique Theme   |                               | - WebSocket Engine |  |
|  | - Dual-Deck View   |                               | - Tablet Companion |  |
|  | - Live Mix Console |                               | - Mobile FOH / IEM |  |
|  | - Gig Notepad      |                               | - QR Auto-Connect  |  |
|  +--------------------+                               +--------------------+  |
|                                                                               |
+-------------------------------------------------------------------------------+
```

### Key Strengths
1. **Low-Latency Real-Time Scheduling**: Uses Windows Multimedia Class Scheduler Service (`AvSetMmThreadCharacteristicsW` on `Pro Audio`) to elevate the audio processing thread priority.
2. **Resilient Off-Thread Plugin Loading**: Background worker threads handle COM apartments (`COINIT_MULTITHREADED`), plugin instantiation, and state restoration with watchdog timeouts, eliminating UI freezes during patch loading.
3. **Stage-Optimized Break Engine**: Seamless integration of break music, DJ crossfading, and background YouTube track downloads allows single-operator wedding and event management with zero dead air.
4. **Dual FOH & IEM Mix Paths**: Dedicated stereo buses allow distinct front-of-house feeds and performer in-ear mixes without requiring external digital consoles.

### Technical Debt & Optimization Targets
1. **Monolithic Core Headers**: `FanfareEngine.h` (~3,330 lines) and `MainComponent.cpp` (~2,370 lines) contain significant coupling between audio graph scheduling, MIDI routing, JSON serialization, and UI state management.
2. **Audio-Path Synchronization**: While real-time critical sections use spinlocks, certain cross-thread state transitions (e.g., scene transitions and slot buffer reallocations) benefit from moving to complete lock-free ring-buffer and atomic snapshot topologies.
3. **Subgroup Dependency Graph**: Current subgroup summing relies on left-to-right slot ordering rather than an explicit topological dependency graph.

---

## Strategic Roadmap & Proposed Enhancements

---

### Pillar 1: DSP Engine & Real-Time Audio Infrastructure

#### 1.1 Lock-Free Audio Callback & Memory Arena Architecture
- **Objective**: Guarantee zero allocations, zero lock contention, and deterministic execution under heavy CPU load.
- **Implementation**:
  - Replace remaining `juce::SpinLock` usages in the audio loop with atomic snapshot pointers (`std::atomic<std::shared_ptr<const GraphConfig>>`) using RCU (Read-Copy-Update) semantics.
  - Implement a pre-allocated Real-Time Scratch Memory Arena for DSP filters and dynamic buffer transfers, completely eliminating OS heap calls.
  - Upgrade audio buffer underrun telemetry to log timing histograms with sub-millisecond precision.

#### 1.2 Topological Audio Graph & Multicore Work Stealing
- **Objective**: Maximize multi-core CPU utilization across complex rigs with deep subgroup routing and parallel instrument processing.
- **Implementation**:
  - Build an explicit Directed Acyclic Graph (DAG) for slot-to-subgroup-to-bus routing.
  - Upgrade the thread-pool dispatcher to use a lock-free work-stealing deque (Chase-Lev algorithm), allowing worker threads to process independent slot branches simultaneously with minimum synchronization latency.

#### 1.3 Per-Plugin Sandboxing & Out-of-Process Isolation (Optional Mode)
- **Objective**: Isolate unstable 3rd-party VST3 plugins so a crash in a single instrument never compromises the host process.
- **Implementation**:
  - Implement an IPC (Inter-Process Communication) proxy host using shared memory audio buffers and Windows named pipes for high-risk legacy plugins.
  - Wrap in-process plugin calls in Structured Exception Handling (`__try` / `__except`) with rapid bypass fallback if a plugin throws an access violation.

#### 1.4 Master Bus Processing Suite
- **Objective**: Deliver a polished, master-grade output directly from the host.
- **Implementation**:
  - **Multiband Compressor**: 3-band crossover with adjustable attack/release and auto-makeup gain.
  - **True-Peak Brickwall Limiter**: 4x oversampled lookahead limiter preventing inter-sample digital clipping on stage feeds.
  - **Loudness & Stereo Correlation Meter**: Real-time LUFS (Integrated, Short-term, Momentary) and phase correlation metering.

---

### Pillar 2: Live Performance Workflows & Rig Ergonomics

```
+-------------------------------------------------------------------------------+
|                       VISUAL 88-KEY SPLIT & LAYER EDITOR                      |
+-------------------------------------------------------------------------------+
|                                                                               |
| [ C1  .................. B2 ] [ C3 .............. B4 ] [ C5 ............. C8 ]|
| +---------------------------+ +----------------------+ +--------------------+ |
| |   ZONE 1: Acoustic Bass   | |  ZONE 2: Grand Piano | |  ZONE 3: Synth Pad | |
| |   Slot 2 (Cyan)           | |  Slot 3 (Green)      | |  Slot 4 (Magenta)  | |
| |   Vel: 1-127 | Transp: 0  | |  Vel: 1-127 | Oct: 0 | |  Vel: 64-127 | +1  | |
| +---------------------------+ +----------------------+ +--------------------+ |
|                                                                               |
+-------------------------------------------------------------------------------+
```

#### 2.1 Visual Keyboard Split & Velocity Layer Map
- **Interactive 88-Key Graphical Editor**: Click and drag to create split zones, set high/low note limits, and assign colors to matching channel strips.
- **Velocity Switching & Crossfading**: Soft velocity layers (e.g., Rhodes below velocity 80, Brass above velocity 80) with smooth transition curves.
- **Per-Zone Transposition & Octave Shift**: Dedicated octave and semitone offsets per zone without altering controller hardware settings.

#### 2.2 Seamless Scene Transitions (Patch Morphing & Spillover)
- **Effect Tail Spillover**: Reverb and delay tails continue decaying naturally when switching scenes, without voice truncation.
- **Gain & Filter Morphing**: Configurable 50ms to 500ms crossfade times between scenes for sound layering transitions.
- **Pre-Warming Inactive Plugins**: Background thread pre-caches plugin samples for upcoming queue songs to eliminate loading lag during live performance.

#### 2.3 Setlist Manager & Dynamic Song Sections
- **Song Structure Sections**: Mark songs into sections (*Intro*, *Verse*, *Chorus*, *Solo*, *Outro*) with dedicated MIDI pedal triggers (e.g., dual footswitch / Behringer FCB1010).
- **Setlist Reordering & Gig Mode**: Drag-and-drop song ordering with total estimated set time and countdown timer.
- **Auto-Sync Notepad Prompter**: Gig notes, chord charts, and cues automatically scroll to the active song section.

#### 2.4 Hardware MIDI Controller Auto-Profiles
- **One-Touch Controller Templates**: Pre-configured MIDI mappings for industry-standard stage keyboards (Yamaha CK88 / CP88, Roland RD-2000 / Fantom, Nord Stage 3/4, Korg Kronos/Nautilus, Arturia KeyLab).
- **Bi-Directional MIDI Feedback**: LED rings, motorized faders, and display labels synchronize bidirectionally via MIDI SysEx / CC feedback.

---

### Pillar 3: DJ, Sampler & Break Music Innovations

```
+-------------------------------------------------------------------------------+
|                       DUAL-DECK DJ & SAMPLER WORKSTATION                      |
+-------------------------------------------------------------------------------+
|                                                                               |
|  [ DECK A: September ]                        [ DECK B: Celebration ]         |
|  BPM: 126.0  | Key: A Maj                     BPM: 124.0  | Key: Ab Maj       |
|  [|||||||||||||||||...........]               [.............................] |
|  [ > PLAY ] [ || PAUSE ] [ CUE ]              [ > PLAY ] [ || PAUSE ] [ CUE ] |
|                                                                               |
|  +--------------------------- CROSSFADER -----------------------------------+ |
|  |  [<< FADE A]       <------[=====|=====]------>       [FADE B >>]         | |
|  |  Curve: [Smooth 3dB]  |  Auto-DJ: [ON]  |  Transition: [4 Beats (4.0s)]  | |
|  +--------------------------------------------------------------------------+ |
|                                                                               |
|  [ QUICK JINGLE / STAGER PADS ]                                               |
|  +---------+ +---------+ +---------+ +---------+ +---------+ +---------+      |
|  | Applause| | Horn Hit| | Entrance| | Fanfare | | 3-2-1   | | DrumRoll|      |
|  +---------+ +---------+ +---------+ +---------+ +---------+ +---------+      |
|                                                                               |
+-------------------------------------------------------------------------------+
```

#### 3.1 Real-Time Waveform & Beat-Grid Display
- **Dynamic Waveform Overview**: Multi-frequency colored waveforms (Lows = Red, Mids = Green, Highs = Blue) for visual cueing.
- **Automatic BPM & Key Detection**: Background DSP transient analysis computes tempo and musical key on track load.
- **Beat-Synchronized Crossfading**: Crossfader can snap transitions to musical bar boundaries (1 bar, 2 bars, 4 bars).

#### 3.2 Instant Stage Soundboard / Jingle Pads
- **8-16 Instant Trigger Sample Pads**: Low-latency one-shot audio triggers for applause, walk-in stings, hype horns, and countdowns.
- **Hardware Pad Mapping**: Direct mapping to MIDI drum pads (Akai LPD8, Novation Launchpad) or computer keyboard number row (`1..8`).
- **Independent Volume & Ducking**: Dedicated gain control with automatic background music ducking during announcement/jingle triggers.

#### 3.3 YouTube Grabber Enhancements & Smart Library Caching
- **Automated Silence Trimming**: Detect and trim leading/trailing dead air on downloaded tracks.
- **ID3 Tagging & Artwork Fetching**: Embed high-res album art and artist/title metadata into downloaded MP3 files.
- **Local Library Fuzzy Search**: Quick search box in the playlist panel supporting instant keyboard filtering by artist, title, or BPM.

---

### Pillar 4: Web Companion, Wireless Remote & Cloud Backup

#### 4.1 Low-Latency Performer In-Ear Monitor (IEM) Remote
- **Independent Personal Monitor Mixes**: Band members connect via phone/tablet browser to adjust their personal IEM send balances without affecting FOH.
- **Access Control Roles**: Granular login tokens (`Admin / FOH`, `Musician / IEM Only`, `Setlist Prompter View`).

#### 4.2 Progressive Web App (PWA) & Offline Prompter
- **Offline Caching**: Service Worker caching allows the web companion to launch instantly even in venue basements with zero internet connectivity.
- **Full-Screen Dark Stage Theme**: High-contrast OLED dark mode with zero browser URL bar distractions.

#### 4.3 Automated Cloud Backup & Rig Sync
- **Preset & Rig Sync**: One-click export and synchronization of setups, playlists, and gig notes to secure storage (Google Drive, OneDrive, or local NAS).
- **USB Rig Migration Tool**: Standalone export package containing rig JSON, custom samples, and plugin state files for rapid emergency deployment to a backup laptop.

---

### Pillar 5: Codebase Modularization & Software Quality

#### 5.1 Architecture Refactoring Plan

To maintain long-term maintainability and adhere to clean architecture principles, monolithic files will be decomposed into focused single-responsibility domain modules:

| Existing Monolith | Target Modular Architecture |
| :--- | :--- |
| `FanfareEngine.h` (3.3k LOC) | • `Engine/AudioGraph.h` (routing & DSP graph)<br>• `Engine/PluginManager.h` (VST3 lifecycle & off-thread builder)<br>• `Engine/MidiRouter.h` (MIDI learn, harmonizer & arpeggiator)<br>• `Engine/MixerBus.h` (FOH/IEM/Subgroup summing) |
| `MainComponent.cpp` (2.3k LOC) | • `UI/MixerConsoleView.h` (channel strip grid & VU layout)<br>• `UI/TopControlBar.h` (scenes, queue, master metering)<br>• `UI/SplitEditorOverlay.h` (88-key graphical split map)<br>• `UI/DialogManager.h` (modal lifecycle & callout handling) |

#### 5.2 Automated Headless Test Suite (CI/CD)
- **Headless DSP Test Runner**: Non-GUI test harness validating:
  - Audio buffer summing accuracy and XRUN immunity under heavy loads.
  - VST3 state serialization and byte-level deserialization consistency.
  - Crossfader equal-power curve mathematical precision ($\cos^2 + \sin^2 = 1.0$).
  - JSON playlist parsing and song reordering edge cases.

---

## Phased Implementation Roadmap

```
2026 Q3                      2026 Q4                      2027 Q1
+---------------------------+---------------------------+---------------------------+
| PHASE 1: STABILITY &      | PHASE 2: PERFORMANCE &    | PHASE 3: ADVANCED DJ &    |
| REFACTORING               | ERGONOMICS                | ECOSYSTEM                 |
+---------------------------+---------------------------+---------------------------+
| • Lock-free audio core    | • Visual 88-key split map | • Waveform & beat-grid    |
| • Modularize Engine/UI    | • Scene spillover tails   | • 8-pad soundboard jingles|
| • Headless CI test suite  | • Controller MIDI maps    | • Multi-user IEM web mix  |
| • YouTube audio tagger    | • Master limiter / LUFS   | • Cloud rig backup tool   |
+---------------------------+---------------------------+---------------------------+
```

### Phase 1: Engine Hardening & Modular Architecture (Target: Q3 2026)
- [ ] Refactor `FanfareEngine.h` into decoupled domain classes (`AudioGraph`, `PluginManager`, `MidiRouter`).
- [ ] Replace remaining audio thread spinlocks with atomic pointer swaps and lock-free ring buffers.
- [ ] Implement automated headless test suite for DSP summing, VST loading, and JSON roundtrips.
- [ ] Add silence trimming and ID3 metadata tagging to `YoutubeDownloadManager`.

### Phase 2: Stage Ergonomics & Performance Workflows (Target: Q4 2026)
- [ ] Build the interactive **88-Key Visual Split & Velocity Layer Editor**.
- [ ] Implement **Effect Spillover & Parameter Morphing** across scene transitions.
- [ ] Add pre-configured hardware MIDI controller maps (Yamaha CK88, Nord Stage, Roland RD).
- [ ] Introduce Master Bus Suite with lookahead True-Peak Limiter and LUFS metering.

### Phase 3: DJ Enhancements & Wireless Multi-Client Ecosystem (Target: Q1 2027)
- [ ] Implement real-time waveform overview and BPM/Key transient analyzer in `Mp3PlayerProcessor`.
- [ ] Build the **Instant Stage Soundboard / Jingle Pad** component with MIDI pad learn.
- [ ] Enable multi-client role permissions on the Web companion (Personal IEM mixing per bandmate).
- [ ] Launch one-click USB rig backup and cloud synchronization.

---

## Conclusion

Fanfare has already proven itself in demanding live environments. By executing this roadmap, the platform will cement its position as the premier, ultra-reliable, all-in-one live performance workstation for modern gigging musicians, musical directors, and event performers.
