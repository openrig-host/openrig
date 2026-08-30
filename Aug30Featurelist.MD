# Fanfare — Comprehensive Master Feature List
**Document Date**: August 30, 2026  
**Target Platform**: Windows 10 / Windows 11 (x64)  
**Framework & Language**: JUCE 8 / Modern C++17  
**Status**: Complete Architectural & Feature Inventory  

---

## Executive Overview

**Fanfare** is a sovereign, low-latency live keyboard performance workstation, VST3 plugin host, stage mixer, and dual-deck break music engine. Built from the ground up for gigging musicians, musical directors, and single-operator live stage performers, Fanfare replaces fragile digital audio workstations (DAWs) and complex node graphs with a hardware-like, fixed linear rack layout.

The engine prioritizes **stage determinism**, **atomic zero-dropout transitions**, **fail-safe audio resilience**, and **real-time dual-bus mixing (FOH and IEM)**.

```
+---------------------------------------------------------------------------------------------------+
|                                   FANFARE LIVE PERFORMANCE SYSTEM                                 |
+---------------------------------------------------------------------------------------------------+
|                                                                                                   |
|  +---------------------------+  +-------------------------------+  +---------------------------+  |
|  |     MIDI ENGINE & FX      |  |    12-SLOT AUDIO RACK & DSP   |  |   DUAL-DECK DJ / BREAK    |  |
|  | - Channel Routing (1..16) |  | - 12 Fixed Instrument Slots   |  | - Deck A & Deck B Engine  |  |
|  | - Play-to-Learn Split Map |  | - 3-Plugin Chaining / Stacking|  | - Equal-Power Crossfader  |  |
|  | - Dynamic CC Learn Bus    |  | - 10-Band EQ & Noise Gate     |  | - Auto-DJ Continuous Mix  |  |
|  | - Arpeggiator & Harmonizer|  | - Studio Compressor + Presets |  | - yt-dlp 192k Grabber     |  |
|  | - Hardware MIDI Out Ports |  | - Convolution IR Cab / Reverb |  | - BPM Detector & Pitch Lock|  |
|  +-------------+-------------+  +---------------+---------------+  +-------------+-------------+  |
|                |                                |                                |                |
|                +--------------------------------+--------------------------------+                |
|                                                 |                                                 |
|                                     +-----------v-----------+                                     |
|                                     |  REAL-TIME DSP CORE   |                                     |
|                                     | (Win32 MMCSS ProAudio)|                                     |
|                                     +-----------+-----------+                                     |
|                                                 |                                                 |
|                        +------------------------+------------------------+                        |
|                        |                                                 |                        |
|             +----------v----------+                           +----------v----------+             |
|             |  BOUTIQUE JUCE GUI  |                           |  EMBEDDED WEB HUD   |             |
|             | - 5 Pro Stage Themes|                           | - Zero-Install Web  |             |
|             | - Dual Analog VUs   |                           | - Wi-Fi SoftAP QR   |             |
|             | - Gig Notepad Live  |                           | - Touch Mixer & Cues|             |
|             | - Setlist Preloader |                           | - Low-Latency Telemetry|          |
|             +---------------------+                           +---------------------+             |
|                                                                                                   |
+---------------------------------------------------------------------------------------------------+
```

---

## 1. Core Audio Engine & Real-Time DSP Infrastructure

### 1.1 Low-Latency Audio Driver Support
- **Native ASIO Hosting**: Direct integration with professional ASIO drivers via Steinberg ASIO SDK headers for sub-millisecond audio block rendering.
- **WASAPI Fallback**: High-performance Windows Audio Session API fallback for portable operation without dedicated audio hardware.
- **Dynamic Device Switching**: Hot-plug and runtime driver reset (`RESET AUDIO`) without requiring application restart.
- **Adjustable Buffer & Sample Rates**: Support for 44.1 kHz, 48.0 kHz, 88.2 kHz, 96.0 kHz at buffer sizes from 64 to 2048 samples.

### 1.2 Windows Pro Audio Thread Prioritization
- **MMCSS Integration**: Real-time thread priority boosting via Windows Multimedia Class Scheduler Service (`AvSetMmThreadCharacteristicsW` registered to `"Pro Audio"`).
- **Audio Thread Protection**: Zero memory allocations, zero disk I/O, and zero string formatting on the real-time audio thread.

### 1.3 High-Performance DSP Scheduling & Summing
- **Adaptive Execution Paths**:
  - *Sequential Fast-Path*: Runs inline on the main audio thread when active slots $\le 3$, eliminating thread synchronization latency, context-switching overhead, and spinlock contention on smaller patches.
  - *Parallel Multi-Threaded Dispatcher*: Uses an internal `juce::ThreadPool` with pre-allocated thread jobs to distribute heavy multi-plugin loads across multiple CPU cores.
- **Real-Time Scratch Buffers**: Pre-allocated internal multi-channel scratch audio buffers (up to 32 internal channels) ensuring zero runtime heap allocation.
- **Master Bus Multi-Channel Summing**: Independent left/right master summing paths for Front of House (FOH) and In-Ear Monitors (IEM).

---

## 2. Mixer Rack & Channel Strip Architecture

### 2.1 Fixed Linear Rack Topology
- **12 Dedicated Instrument / Audio Slots**: Predictable, fixed linear layout mirroring physical stage racks without virtual patch-cable spaghetti:
  - **Slot 1 (Monitor In)**: Hardware Input 1 passthrough (dedicated to performer monitor feeds, hardcoded FOH mute, zero aux feedback).
  - **Slot 2 (CK88 / Master Keyboard)**: Primary stage keyboard controller (Hardware Input 11).
  - **Slots 3–11 (Instrument & Audio Slots)**: General-purpose VST3 instruments, built-in samplers, and DJ break music players.
  - **Slot 12 (Accordion / Aux Hardware)**: Secondary hardware input (Hardware Input 13).
  - **Aux Returns 1 & 2**: Dedicated stereo return channels for master FX processing.

### 2.2 Dual-Bus Mixing (Independent FOH & IEM Feeds)
- **FOH & IEM Dual Faders**: Every channel strip features independent volume faders for Front-of-House (house PA) and In-Ear Monitors (stage in-ears).
- **Fader Link Mode**: Link FOH and IEM faders for single-throw stage adjustments while maintaining an adjustable IEM gain offset (`iemOffset`).
- **Independent Mute & Enable Toggles**: Mute/unmute FOH or IEM feeds independently per channel strip.
- **Stereo Aux Sends**: Variable `Aux 1 Send` and `Aux 2 Send` rotary controls for routing channel audio into shared spatial and delay effects.
- **Subgroup Summing & Output Routing**:
  - Direct routing dropdown (`outputTarget`) on every channel strip.
  - Channels can sum into downstream slots acting as subgroup buses before hitting the master outputs.
  - Subgroup routing fully preserved across JSON rig saves and scene presets.

---

## 3. Instrument Stacking, Multi-Plugin Chains & VST3 Hosting

### 3.1 Multi-Plugin Stacking per Slot
- **3-Slot Chain per Strip**: Host up to 3 VST3 plugins simultaneously in a single rack slot.
- **Independent Plugin Parameters**:
  - Individual plugin enable/bypass switches.
  - Per-plugin output level trim (`level`).
  - Per-plugin keyboard split ranges (`lowNote` to `highNote`).
- **External Hardware MIDI Out Forwarding**:
  - Any chain slot can be configured as a **MIDI OUT** destination instead of hosting a software VST.
  - Forwards filtered and transposed MIDI events to external hardware sound modules, rack synthesizers, or secondary MIDI interfaces.

### 3.2 Off-Thread Background Plugin Building
- **Non-Blocking Plugin Instantiation**: VST3 plugin loading, stereo bus negotiation, `prepareToPlay`, and binary state restoration execute on background worker threads (`RigBuilder.h`).
- **25-Second Watchdog Timer**: Isolated background instantiation threads automatically bypass hung or corrupted 3rd-party DLLs without freezing the user interface.
- **COM Apartment Optimization (MTA)**: Background worker threads initialize under `COINIT_MULTITHREADED`, preventing COM deadlocks when heavy plugins (such as Native Instruments Kontakt 8) invoke internal COM helpers.
- **Message-Thread Fallback**: Specialized handling for GUI/Qt-dependent plugins (e.g., Native Instruments Super 8) with instance reuse-by-path.

### 3.3 Dynamic Plugin Window Management
- **Floating VST GUI Windows**: Full access to native 3rd-party plugin graphical user interfaces.
- **Asynchronous Destruction Protection**: Safe asynchronous teardown of VST3 editor windows preventing reentrant Win32 message loop crashes on patch change or shutdown.

---

## 4. Channel Strip DSP Suite & Inline Processing

Every rack slot features a built-in, low-latency DSP processing suite implemented with transcendental math and atomic parameter controls in [ChannelStripProcessor.h](file:///z:/davecore/Source/ChannelStripProcessor.h):

```
+---------------------------------------------------------------------------------------+
|                             INLINE CHANNEL STRIP DSP SUITE                            |
+---------------------------------------------------------------------------------------+
|                                                                                       |
|  [ INPUT ] -> [ NOISE GATE ] -> [ 10-BAND EQ ] -> [ COMPRESSOR ] -> [ STEREO CHORUS ] |
|                                                                          |            |
|  [ OUTPUT ] <- [ CONVOLUTION IR ] <- [ REVERB ENGINE ] <-----------------+            |
|                                                                                       |
+---------------------------------------------------------------------------------------+
```

### 4.1 SimpleGate (Noise Gate)
- **Fast Attack & Smooth Release**: 1ms fast attack with 200ms exponential release curve.
- **Adjustable Threshold**: Continuous dB threshold control (-80 dB to 0 dB).
- **Zero Latency**: Real-time sample-by-sample gate attenuation with smooth coefficient ramps.

### 4.2 10-Band Precision Parametric / Graphic EQ
- **Biquad Filter Architecture**: 10 normalized biquad peak/shelf filters eliminating per-sample division and divide-by-zero risks.
- **ISO Center Frequencies**: Standard octave bands: `31.25 Hz`, `62.5 Hz`, `125 Hz`, `250 Hz`, `500 Hz`, `1 kHz`, `2 kHz`, `4 kHz`, `8 kHz`, and `16 kHz`.
- **Gain Range**: Continuous $\pm 12\text{ dB}$ gain adjustment per band.
- **Master & Volume Trims**: Dedicated pre- and post-EQ volume gain multipliers.

### 4.3 Studio Compressor with Smart Presets & Makeup Gain
- **Dual-Envelope Compression**: Attack coefficients (0.5ms to 30ms) and release coefficients (5ms to 280ms).
- **Curated Dynamics Presets**:
  1. *MP3 / Track Leveler*: Smooth threshold (-26 dB, ratio 3.5:1, +6 dB makeup) for evening out backing tracks.
  2. *Vocal / Lead Focus*: Punchy dynamic control (-20 dB, ratio 4:1, +4 dB makeup).
  3. *Punchy Drums / Beats*: Snappy transient response (-16 dB, ratio 4:1, +3 dB makeup).
  4. *Brickwall Peak Control*: Hard peak control (-4 dB, ratio 12:1, 0 dB makeup).
  5. *Manual Mode*: Dynamic continuous control via user Amount slider.
- **Gain Reduction Metering**: Real-time gain reduction dB telemetry reported to UI meters.

### 4.4 Stereo Chorus
- **Dual LFO Phase Modulation**: Left channel and Right channel modulated $90^\circ$ out of phase ($\frac{\pi}{2}$) for wide stereo spatialization.
- **Variable Delay Line**: Interpolated 10ms to 20ms delay buffer with 4-point smoothing.
- **Rate & Mix**: Adjustable LFO modulation rate (0.1 Hz to 5.0 Hz) and wet/dry mix.

### 4.5 Algorithmic Studio Reverb
- **JUCE Reverb Core**: Smooth room emulation with room size, damping, stereo width, and wet/dry balance controls.

### 4.6 Convolution Reverb & Cabinet Impulse Response Loader
- **Impulse Response (.WAV) Engine**: Powered by `juce::dsp::Convolution` for guitar speaker cabinets, vintage plates, and realistic acoustic hall simulation.
- **Message-Thread Safe Loading**: Background IR loading with atomic swap onto the real-time audio thread.
- **Stereo Trimming & Normalization**: Automatic sample start-trimming and peak loudness normalization on load.

---

## 5. Hardware & Software MIDI Subsystem

### 5.1 Flexible MIDI Routing & Channels
- **Global & Per-Slot Channel Routing**: Accept MIDI on global channel or restrict specific slots to individual MIDI channels (1..16).
- **SysEx Filtering**: Automatic sanitization of incoming SysEx messages to prevent driver buffer overflows.

### 5.2 Dynamic CC Mapping & "Arm-then-Wiggle" CC Learn
- **MidiLearnBus Architecture**: Centralized thread-safe MIDI learn manager ([MidiLearnBus.h](file:///z:/davecore/Source/MidiLearnBus.h)).
- **Arm-and-Wiggle Workflow**:
  1. Click **Learn** on any UI knob, slider, or fader.
  2. Move any physical knob/slider on your MIDI hardware controller.
  3. The CC number, channel, and parameter index are bound instantly.
- **Advanced CC Configuration**:
  - Min and Max parameter scaling ranges (0.0 to 1.0).
  - Parameter inversion toggle.
  - Conflict and duplicate CC detection warnings.
  - Direct mapping for FOH faders, IEM faders, and plugin VST parameters.

### 5.3 Yamaha CK88 Stage Controller Default Profiles
- **Out-of-the-Box Hardware Mappings**: Built-in definitions for the Yamaha CK88 / RD series controllers:
  - Modulation Wheel (CC 9)
  - Sustain Pedal (CC 64)
  - Chorus / Delay Knob (CC 93)
  - Reverb Knob (CC 92)
  - B3 Tonewheel Drawbars: 16' (CC 77), 5-1/3' (CC 78), 8' (CC 79), 4' (CC 80), 2-2/3' (CC 81), 2' (CC 82), 1-3/5' (CC 83), 1-1/3' (CC 85), 1' (CC 86).

### 5.4 Play-to-Learn Key Split & Note Range Limiter
- **Note Range Learn Overlay**: Open the **NR** dialog, press the lowest key on your physical keyboard, press the highest key, and the split zone is locked immediately.
- **Visual Split Indicators**: Color-coded note range indicators across all active instruments.

### 5.5 Real-Time MIDI Activity Monitor
- **Visual Log Overlay**: Displays real-time incoming MIDI messages color-coded by type:
  - Note-On (Green) & Note-Off (Dim Green) with velocity.
  - Control Change (Blue) with CC number and value.
  - Active Learn Capture Events (Gold).
  - Program Changes (Purple) & Pitch Bend (Orange).
- **Latency & Event History**: Rolling 30ms timer-driven display preventing GUI stalls during heavy MIDI input.

---

## 6. Live MIDI Performance Effects

Located in [MidiEffectsComponent.h](file:///z:/davecore/Source/MidiEffectsComponent.h), [SimpleArpeggiator.h](file:///z:/davecore/Source/SimpleArpeggiator.h), and [OctaveHarmonizer.h](file:///z:/davecore/Source/OctaveHarmonizer.h):

### 6.1 Chromatic Pitch Transposer
- **Wide Transposition Range**: -48 to +48 semitones ($\pm 4$ octaves) in precise 1-semitone increments.
- **Fast-Action UI**: Slider plus hold-repeat increment/decrement buttons for rapid stage key changes.

### 6.2 SimpleArpeggiator
- **4 Selectable Arp Patterns**:
  - `Up`: Lowest to highest note.
  - `Down`: Highest to lowest note.
  - `UpDown`: Ascending then descending.
  - `Random`: True pseudo-random note selection.
- **Octave Spans**: 0 to 4 octaves up and down.
- **Gate Length Control**: Adjustable note duration (1% to 100% of 16th-note step).
- **High-Precision BPM Engine**: Decimal tempo sync (e.g. 127.5 BPM) with sample-accurate step timing.

### 6.3 OctaveHarmonizer & Specialty Performance Modes
- **Octave Multipliers**: Add 1 to 4 stacked octaves above and/or below held notes without voice stealing.
- **Toto "Africa" Synth Modes**:
  - *Mode 1 (5th Down)*: Generates a parallel 4th/5th harmony interval (5 semitones below) for classic synth riffs.
  - *Mode 2 (Diatonic Major/Minor 3rd)*: Smart diatonic 3rd harmonies based on musical scale lookup.
  - *Mode 3 (Split Africa Solo)*: Splits the keyboard at middle C (note 48), transposing upper notes by -1 semitone and lower notes by +36 semitones with parallel fifths.
- **Inter-Slot Harmony Routing**: Divert generated harmony notes away from the host slot and inject them directly into another channel strip's instrument plugin.

---

## 7. Integrated 8-Pad Sampler & Waveform Splice Editor

Located in [SamplerProcessor.h](file:///z:/davecore/Source/SamplerProcessor.h), [SamplerComponent.h](file:///z:/davecore/Source/SamplerComponent.h), and [WaveformSpliceEditor.h](file:///z:/davecore/Source/WaveformSpliceEditor.h):

### 7.1 Multi-Pad Polyphonic Sampler
- **8 Independent Sample Pads**: Low-latency one-shot and looping sample playback engine.
- **Per-Pad Parameters**:
  - Audio file loader (supports `.wav`, `.aif`, `.mp3`, `.flac`, `.ogg`).
  - MIDI Root Note and Trigger Note Range (`keyLow` to `keyHigh`).
  - Semitone Pitch Tuning ($\pm 24$ semitones).
  - Volume trim and MIDI CC volume mapping.
  - Envelope Attack (5ms) and Release (100ms) curves.
  - Looping toggle.

### 7.2 Interactive Graphical Waveform Splice Editor
- **Dynamic Waveform Display**: Multi-channel waveform thumbnail preview powered by `juce::AudioThumbnail`.
- **Dual Drag Handles**: Interactive start and end ratio handles with real-time shaded crop regions.
- **Sample-Accurate Trimming**: Eliminate unwanted silence at the start or end of triggered stage sound effects.

---

## 8. Dual-Deck DJ, Auto-DJ & Break Music System

Located in [Mp3PlayerProcessor.h](file:///z:/davecore/Source/Mp3PlayerProcessor.h) and [Mp3PlayerComponent.h](file:///z:/davecore/Source/Mp3PlayerComponent.h):

```
+---------------------------------------------------------------------------------------+
|                         DUAL-DECK DJ & BREAK MUSIC WORKSTATION                        |
+---------------------------------------------------------------------------------------+
|                                                                                       |
|  [ DECK A (Cyan) ]                                                [ DECK B (Orange) ] |
|  • Position Scrub Slider                                          • Position Scrub    |
|  • High-Order Catmull-Rom                                         • High-Order        |
|  • WSOLA Key-Lock Engine                                          • WSOLA Key-Lock    |
|  • Time & BPM Telemetry                                           • Time & BPM        |
|                                                                                       |
|  +--------------------------- EQUAL-POWER CROSSFADER -------------------------------+ |
|  |  [<< FADE A]         <---------[=====|=====]--------->         [FADE B >>]       | |
|  |  • 3dB Equal Power       • Manual Slider Position      • Auto-DJ 5s Crossfade    | |
|  +----------------------------------------------------------------------------------+ |
|                                                                                       |
|  [ YOUTUBE-TO-MP3 192K GRABBER ] -> Paste URL -> Background yt-dlp/ffmpeg -> Auto-Play|
|  [ 6 PLAYLIST PRESET BANKS ]     -> Hits, Oldies, Dinner, Dance, Ballads, Break Music |
|                                                                                       |
+---------------------------------------------------------------------------------------+
```

### 8.1 Dual-Deck Audio Architecture
- **Deck A and Deck B**: Two independent streaming playback decks running dedicated high-priority buffering threads.
- **Dual Playback Modes**:
  - *Clean Vinyl Mode (High-Order Resampler)*: Uses 4-point Catmull-Rom cubic interpolation for 100% pristine, artifact-free studio audio with zero phasing.
  - *Key-Lock Mode (Time-Stretcher)*: WSOLA (Waveform Similarity Overlap-Add) time-stretching allowing $\pm 25\%$ tempo changes (0.80x to 1.25x) while locking the musical pitch.

### 8.2 DJ Equal-Power Crossfading
- **Constant Power Curve**: $3\text{ dB}$ equal-power sine/cosine crossfade curve ($\sin^2(\theta) + \cos^2(\theta) = 1.0$) ensuring constant acoustic loudness throughout transitions.
- **Automated Crossfade Buttons**: One-click smooth transitions: `<< FADE A` and `FADE B >>`.
- **Configurable Transition Times**: 2s, 4s, 6s, 8s, or 10s fade durations.
### 8.3 Auto-DJ Continuous Stage Mixing
- **Zero Dead-Air Playback**: When active song approaches its final seconds (or designated Cue-Out trigger point), Auto-DJ automatically cues, starts, and crossfades into the next track in the playlist.
- **Shuffle & Loop Modes**: Repeat Track, Repeat All, or pseudo-random playlist shuffling.

### 8.4 Per-Track In/Out Cue Points & Auto-Trimming
- **Intro Shaving (Cue-In)**: Set a custom start offset (`cueInSeconds`) to skip talking preambles, click-counts, or slow build-ups and start playing directly on the main beat.
- **Fadeout / Outro Trimming (Cue-Out)**: Set an early crossfade trigger (`cueOutSeconds`) to bypass trailing silence or 30-second acoustic fadeouts, immediately transitioning to the next deck with zero energy loss.
- **On-Deck Quick Capture**: One-click `[SET IN]`, `[SET OUT]`, and `[CLR]` buttons on Deck A and Deck B to capture playhead positions live during playback.
- **Persistent Caching**: Cue points and detected BPMs are cached per-file in `%APPDATA%/Fanfare/Playlists/bpm_cache.json` and JSON playlist files, preserving trimming across sessions without altering original MP3 audio files on disk.

### 8.5 Intelligent BPM-Aware Playlist Arranger (Smart Set Flow)
- **1-Click Smart Arranger Menu (`[SMART ARRANGE ⚡]`)**:
  - *Smooth BPM Ramp (Ascending / Descending)*: Sorts tracks gradually by tempo (e.g. 95 $\rightarrow$ 128 BPM) for dinner-to-dance warm-up arcs.
  - *Minimal Tempo Step (Nearest-Neighbor TSP)*: Reorders the playlist to minimize tempo jumps between consecutive songs ($|\Delta\text{BPM}| \le 3-5\text{ BPM}$) with half-time/double-time harmonic matching ($70 \times 2 = 140\text{ BPM}$).
  - *Energy Wave (Build & Peak)*: Groups tracks into dynamic tiers (Warmup $\rightarrow$ Mid-Groove $\rightarrow$ Peak 1 $\rightarrow$ Groove $\rightarrow$ Peak Crescendo) mimicking a professional DJ set curve.
  - *Target Duration Fitting (30m / 45m / 60m)*: Calculates exact effective playtime:
    $$\text{Set Duration} = \sum (\text{CueOut}_i - \text{CueIn}_i) - (N-1) \times \text{CrossfadeDuration}$$
    and automatically selects the optimal track sequence to fill a 30, 45, or 60-minute break without cutting off tracks mid-song.

### 8.6 YouTube-to-MP3 Background Grabber
- **Integrated Download Manager**: Powered by [YoutubeDownloadManager.h](file:///z:/davecore/Source/YoutubeDownloadManager.h) utilizing `yt-dlp` and `ffmpeg`.
- **Direct 192kbps Audio Extraction**: Paste any YouTube / web music URL to extract high-quality stereo 192k MP3 audio directly into the stage music library.
- **Instant Metadata Extraction**: Pre-fetches video titles in the background to name tracks immediately.
- **Grab & Play**: One-click action to download a requested song and immediately begin playback on stage.

### 8.7 6 Persistent Playlist Preset Banks
- **Instant Bank Switching**: 6 tactile bank buttons (e.g. *Hits*, *Oldies*, *Dinner*, *Dance*, *Ballads*, *Break*).
- **Right-Click Context Menu**: Right-click any bank button to assign a music folder, rename the bank, or refresh directory contents.

---

## 9. Gig Notepad, Stage Prompter & Live Chord Charts

Located in [GigNotepadComponent.h](file:///z:/davecore/Source/GigNotepadComponent.h) and [GigNotepadWindow.h](file:///z:/davecore/Source/GigNotepadWindow.h):

### 9.1 Stage-Optimized Markdown & Chord Chart Parser
- **Live Syntax Tokenizer**:
  - **Chord Badges**: Format chords like `[C#m7]` or `[G/B]` into distinct stage chord badges.
  - **Color Highlights**: Tag text with semantic colors: `[blue]Verse 1[/blue]`, `[green]Chorus[/green]`, `[yellow]Bridge[/yellow]`, `[red]CUE[/red]`, or `[white]Notes[/white]`.
  - **Headings & Sections**: `# Heading 1`, `## Heading 2`, `### Heading 3`.
  - **Bold Text**: `**Important Cue**`.

### 9.2 High-Contrast Stage Display & Prompter
- **Live View vs Edit View**: High-contrast OLED dark stage viewing mode with instant one-click toggle to the text editor.
- **Dynamic Font Rescaling**: Adjust font size from 14px up to 40px for easy readability on stage laptop screens.
- **Monospace Mode**: Align chord charts and lyrics accurately for lead sheets and Nashville number charts.

### 9.3 Multi-Monitor Floating Window
- **Detachable Stage Window**: Detach the Gig Notepad into a borderless, resizable secondary monitor window (`GigNotepadWindow`) positioned on a second screen or stage prompter display.
- **Per-Setup Persistence**: Notes are saved directly inside the active Song JSON rig file, automatically updating when a new song or scene is loaded.

---

## 10. Embedded Web Server, Stage Wi-Fi Hotspot & Companion Web HUD

Located in [Web/OpenRigWebServer.h](file:///z:/davecore/Source/Web/OpenRigWebServer.h), [Web/StageHotspotController.h](file:///z:/davecore/Source/Web/StageHotspotController.h), and [Web/static/](file:///z:/davecore/Source/Web/static):

```
+---------------------------------------------------------------------------------------+
|                       STAGE WI-FI & TABLET WEB HUD ARCHITECTURE                       |
+---------------------------------------------------------------------------------------+
|                                                                                       |
|  [ HOST LAPTOP (Fanfare) ]                                                            |
|  • Windows Wi-Fi SoftAP Hotspot ("Fanfare-Stage")                                     |
|  • Embedded C++ HTTP & WebSocket Server (Port 8080)                                   |
|  • 30Hz Telemetry Broadcast (CPU, RAM, XRUNs, Logarithmic VU Meters)                 |
|                                                                                       |
|       ^ Direct Wi-Fi Connection (Scan QR Code to Join)                                |
|       |                                                                               |
|  [ STAGE TABLETS & SMARTPHONES (iPad / Android / Phone) ]                             |
|  +---------------------------------------------------------------------------------+  |
|  | TAB 1: STAGE HUD   | Song hero, 88-key Yamaha split map, fast queue, slot cards |  |
|  | TAB 2: SONGS GRID  | Tactile bubble buttons, search & category filtering        |  |
|  | TAB 3: NOTEPAD     | Fullscreen chord charts, live prompter, font scaling       |  |
|  | TAB 4: TOUCH MIXER | Multi-touch FOH/IEM faders, mutes, master sliders          |  |
|  | TAB 5: BREAK / DJ  | Deck transport, crossfader, YouTube-to-MP3 grabber         |  |
|  +---------------------------------------------------------------------------------+  |
|                                                                                       |
+---------------------------------------------------------------------------------------+
```

### 10.1 Embedded HTTP & WebSocket Server
- **Zero-Install Web Companion**: Built-in C++ HTTP server listening on port 8080 serving a complete Progressive Web App (PWA) directly from host memory or local files.
- **Bi-Directional WebSocket Bridge**: Real-time bi-directional messaging with sub-10ms control latency.
- **30Hz Telemetry & Logarithmic Meter Stream**: Broadcasts real-time master FOH L/R and IEM L/R logarithmic VU levels, CPU percentage, RAM usage, and audio buffer underrun telemetry to all connected tablets.

### 10.2 Windows Stage Wi-Fi SoftAP Hotspot & QR Generator
- **Direct Wireless Link**: Creates a standalone Wi-Fi hotspot directly from the host Windows laptop without requiring venue routers or internet access.
- **QR Code Pairing**: Generates two on-screen QR codes in the **REMOTE** modal:
  1. *Wi-Fi QR*: Scan with smartphone camera to connect to the stage network automatically.
  2. *Web HUD QR*: Scan to launch the browser companion directly to `http://192.168.137.1:8080`.

### 10.3 5 Web Companion HUD Views (1024x600 Stage Optimized)
1. **Stage HUD**:
   - High-visibility Current Song title and Position badge.
   - Up-Next song banner with preload status dot.
   - Fast Song Queue Strip with tap-to-switch buttons.
   - Yamaha 88-Key Graphical Split Map (A0 to C8).
   - Active instrument cards with live audio meters and MIDI activity dots.
2. **Songs Grid**:
   - Large tactile bubble buttons for all songs in the library.
   - Category and tag filtering pills.
   - Instant search box.
3. **Gig Notepad**:
   - Real-time synchronized lyrics and chord charts.
   - Format toolbar, font size scaling, and monospace toggle.
4. **Touch Mixer**:
   - Touch-friendly multi-touch vertical channel faders for FOH and IEM levels.
   - Master FOH and Master IEM output faders.
5. **Break Music & DJ**:
   - Track progress scrub bar and transport controls (Play, Pause, Crossfade Next).
   - YouTube link paste bar with remote MP3 grabber.

---

## 11. Song (Rig), Scene, Setlist & Library Management

Located in [RigModel.h](file:///z:/davecore/Source/RigModel.h), [RigSerializer.h](file:///z:/davecore/Source/RigSerializer.h), [SetlistManager.h](file:///z:/davecore/Source/SetlistManager.h), and [LibraryPanel.h](file:///z:/davecore/Source/LibraryPanel.h):

### 11.1 Versioned JSON Rig Serialization
- **Human-Readable & Version-Controlled**: Complete rig configuration stored as clean, structured JSON.
- **Atomic File Writes & Auto-Rotated `.bak` Files**: Every save writes to a temporary file before atomic renaming, preserving previous backups to prevent corrupt library states.
- **Version Migration**: Automatic forward migration from v1 format to current v2 specifications.

### 11.2 Song Scenes & Snapshot Variations
- **Scene Snapshots**: Store song section variations (*Verse*, *Chorus*, *Bridge*, *Solo*) inside a single song file.
- **Instant Recall**: Recalls slot bypasses, fader gains, aux sends, pitch transpose offsets, key split zones, and inline DSP parameters.
- **MIDI Program Change Triggers**: Assign any MIDI Program Change number and channel to switch scenes on the fly from hardware footswitches.

### 11.3 Setlist Manager & Worker-Thread Preloading
- **Ordered Setlist Queue**: Create, save, and reorder full gig setlists (`.orset`).
- **Background Worker-Thread Preloader**: Automatically preloads and initializes the next upcoming song in the setlist on a background worker thread (`SetlistPreloaderThread.h`).
- **Instant Atomic Song Swapping**: When the user switches to the preloaded song, the audio engine takes the lock and performs an instantaneous pointer swap, reducing song load time to near zero.

### 11.4 Collapsible 3-Tab Library Panel
- **3 Categorized Tabs**:
  - *Sets*: Gig setlists.
  - *Setups*: Individual song rigs (`.json`).
  - *Strips*: Modular channel strip presets (`.orstrip`).
- **Search & Favorites**: Instant text filtering and star favorites system.
- **Setup Builder Overlay**: Modular tool to assemble new 12-slot rigs quickly from saved `.orstrip` presets.

---

## 12. Boutique Theme Engine & Graphical User Interface

Located in [Theme.h](file:///z:/davecore/Source/Theme.h), [ThemeManager.h](file:///z:/davecore/Source/ThemeManager.h), [BoutiqueLookAndFeel.h](file:///z:/davecore/Source/BoutiqueLookAndFeel.h), [AnalogVuMeter.h](file:///z:/davecore/Source/AnalogVuMeter.h), and [LEDIndicator.h](file:///z:/davecore/Source/LEDIndicator.h):

### 12.1 Semantic Role-Based Color System
- **Zero Hardcoded Colors**: Over 200 legacy color literals refactored into centralized semantic theme tokens (`ThemeManager::get(Theme::Role::...)`).

### 12.2 5 Curated Stage Themes
1. **Stage Black**: Refined OLED dark theme optimized for low-glare dark concert stages.
2. **Daylight**: High-contrast light theme engineered for outdoor festivals and daytime rehearsals.
3. **Amber Console**: Warm vintage analog console aesthetic.
4. **Sapphire**: Modern deep blue studio workstation theme.
5. **High Contrast**: Pure black/white/yellow accessibility theme for bright outdoor sunlight.

### 12.3 Skeuomorphic & Analog Visual Elements
- **Dual Analog VU Needle Meters**: Modeled ballistic needle response for Master FOH L/R and Master IEM L/R audio channels.
- **Segmented LED Indicators**: Multi-segment signal presence and peak LED meters.
- **Boutique Rotary Knobs & Faders**: Metal-textured rotary knobs with arc indicators and tactile channel faders.

---

## 13. System Telemetry, Diagnostics, Crash Handling & Reliability

Located in [Logger.h](file:///z:/davecore/Source/Logger.h), [ResourceInspectorModal.h](file:///z:/davecore/Source/ResourceInspectorModal.h), and [Main.cpp](file:///z:/davecore/Source/Main.cpp):

### 13.1 Real-Time System Telemetry
- **CPU Load Monitor**: Continuous tracking of real-time audio thread CPU percentage.
- **Process Memory Telemetry**: Throttled querying (0.5 Hz) of working set memory via Windows `GetProcessMemoryInfo` to prevent kernel page-table lock contention.
- **XRUN & Buffer Underrun Detection**: Precision audio callback timing analysis detecting buffer underruns with calibrated jitter filtering (2.2x threshold).
- **Resource & DSP Inspector Modal**: Comprehensive live diagnostic table detailing per-slot CPU utilization, loaded VST paths, and component status.

### 13.2 Fail-Safe Panic System
- **One-Block All Notes Off**: Dedicated **PANIC** button on desktop GUI and mobile companion that broadcasts MIDI All Notes Off (`CC 123`) and All Sound Off (`CC 120`) to all slots in the immediate next audio block.

### 13.3 Structured Exception Handling (SEH) & Crash Diagnostics
- **Windows SEH Exception Isolation**: Centralized crash handler capturing full C++ stack traces even for `terminate()` and `abort()` exceptions.
- **Per-Plugin Call Protection**: Wraps high-risk plugin operations in exception guards to prevent misbehaving 3rd-party plugins from crashing the host process.
- **Clean Audio Driver Teardown**: Immediate process termination on application shutdown to prevent zombie background threads from locking audio and MIDI driver handles.

---

## 14. Tested Hardware & Verified VST Compatibility Registry

### 14.1 Verified Stage Hardware Controllers
- **Yamaha CK88 / CP88**: Full MIDI channel, CC, drawbar, and pedal integration.
- **Roland RD-88 / RD-2000**: Stage controller integration.
- **Tascam US-1800 / Behringer U-Phoria UMC1820**: Multi-channel 18-in / 8-out USB audio interfaces.

### 14.2 Tested & Verified VST3 Plugins
| Plugin Name | Developer / Vendor | Category | Special Notes |
| :--- | :--- | :--- | :--- |
| **Hammond B-3X** | Hammond / SkyLabs | Tonewheel Organ | Built-in top/bottom note cut filter supported. |
| **Kontakt 8** | Native Instruments | Sampler Workstation | COM MTA multithreading avoids STA deadlock. |
| **Super 8** | Native Instruments | Polyphonic Synth | Qt-based VST; path reuse-by-instance supported. |
| **Supercharger GT** | Native Instruments | Bus Compressor | Verified stable in FX chains. |
| **Omnisphere 2** | Spectrasonics | Synth Workstation | Non-standard aux-bus layout bypass handled. |
| **UVI Workstation** | UVI | Sample Player | Rock solid multi-timbral hosting. |
| **Syntronik 2** | IK Multimedia | Vintage Synth Rompler | Fully supported. |
| **JUNO-106 / Jun-6 V** | Roland / Arturia | Analog Poly Synth | State restoration exception isolation verified. |
| **ZENOLOGY / XV-5080** | Roland Cloud | Synthesizer / ROMpler | Low-latency live hosting verified. |
| **Replika XT** | Native Instruments | Delay Processor | Verified on master/aux return channels. |
| **MixBox** | IK Multimedia | Multi-FX Channel Strip | High-density multi-FX processing supported. |
| **Blue3 Organ** | Cherry Audio | Tonewheel Organ | Fully compatible with CK88 drawbar CCs. |
| **PolyMax** | Universal Audio | Virtual Analog Synth | Low-latency verified. |
| **bx_console / bx_meter** | Brainworx | Channel Strip / Meter | Verified for monitoring and channel EQ. |
| **TR5 Series** | IK Multimedia | Studio Processors | Verified on Master FOH and IEM FX buses. |
| **EZkeys 2** | Toontrack | Virtual Piano | Rock solid stage playback. |

---

## 15. Source Code File Reference Matrix

| Source File | Component / Role | Key Capabilities |
| :--- | :--- | :--- |
| [FanfareEngine.h](file:///z:/davecore/Source/FanfareEngine.h) | Master Audio Engine Core | MMCSS scheduling, 12 rack slots, scene switching, FOH/IEM buses, master FX. |
| [RackSlot.h](file:///z:/davecore/Source/RackSlot.h) | Slot Data & Signal Chain | 3-plugin chaining, MIDI Out routing, dual faders, aux sends, subgroup routing. |
| [ChannelStripProcessor.h](file:///z:/davecore/Source/ChannelStripProcessor.h) | Inline DSP Suite | 10-band EQ, noise gate, compressor with presets, chorus, algorithmic reverb, IR convolution. |
| [Mp3PlayerProcessor.h](file:///z:/davecore/Source/Mp3PlayerProcessor.h) | DJ & Break Engine | Dual decks, Catmull-Rom resampler, WSOLA key-lock, Auto-DJ, equal-power crossfader. |
| [Mp3PlayerComponent.h](file:///z:/davecore/Source/Mp3PlayerComponent.h) | DJ Player UI | Dual transport, scrub sliders, 6 preset banks, playlist table, YouTube grabber bar. |
| [YoutubeDownloadManager.h](file:///z:/davecore/Source/YoutubeDownloadManager.h) | YouTube Grabber | `yt-dlp` & `ffmpeg` background 192k audio extraction with live progress telemetry. |
| [GigNotepadComponent.h](file:///z:/davecore/Source/GigNotepadComponent.h) | Stage Prompter & Notes | Markdown chord tokenizer, color tags, live stage viewer, font scaling, monospace toggle. |
| [Web/OpenRigWebServer.h](file:///z:/davecore/Source/Web/OpenRigWebServer.h) | Embedded Web Server | HTTP & WebSocket server (port 8080), 30Hz VU telemetry stream, REST API endpoints. |
| [Web/StageHotspotController.h](file:///z:/davecore/Source/Web/StageHotspotController.h) | Stage Wi-Fi Controller | Windows SoftAP hotspot launcher, SSID/password management, Wi-Fi QR generator. |
| [Web/static/app.js](file:///z:/davecore/Source/Web/static/app.js) | Web Companion Client | PWA tablet frontend: Stage HUD, Touch Mixer, Songs Grid, Prompter, DJ Deck. |
| [RigSerializer.h](file:///z:/davecore/Source/RigSerializer.h) | Rig Persistence | Atomic JSON serialization, `.bak` file rotation, forward migration. |
| [SetlistManager.h](file:///z:/davecore/Source/SetlistManager.h) | Setlist Preloader | Background worker-thread preloading of upcoming songs for instantaneous zero-wait switches. |
| [Theme.h](file:///z:/davecore/Source/Theme.h) | Role-Based Theme Engine | 5 curated themes (Stage Black, Daylight, Amber Console, Sapphire, High Contrast). |
| [SimpleArpeggiator.h](file:///z:/davecore/Source/SimpleArpeggiator.h) | MIDI Arpeggiator | Up, Down, UpDown, Random patterns; 0-4 octaves; gate time; decimal BPM lock. |
| [OctaveHarmonizer.h](file:///z:/davecore/Source/OctaveHarmonizer.h) | MIDI Harmonizer | Octave stacking, Toto "Africa" modes 1/2/3, inter-slot harmony note routing. |
| [WaveformSpliceEditor.h](file:///z:/davecore/Source/WaveformSpliceEditor.h) | Sampler Splice Editor | Visual waveform display with dual interactive drag handles for sample trimming. |
| [ResourceInspectorModal.h](file:///z:/davecore/Source/ResourceInspectorModal.h) | Diagnostics Modal | Per-slot CPU telemetry, RAM statistics, plugin status table. |
| [Logger.h](file:///z:/davecore/Source/Logger.h) | Crash Diagnostics | Win32 SEH crash backtrace handler, off-thread logging, memory diagnostics. |

---

*Fanfare — Engineered for rock-solid live stage performance.*
