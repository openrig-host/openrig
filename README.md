<div align="center">

# Fanfare

### The Sovereign Live Performance Engine

[![Latest Release](https://img.shields.io/github/v/release/openrig-host/openrig?sort=date&cacheSeconds=900&color=ed786a&label=Latest%20Release)](https://github.com/openrig-host/openrig/releases/latest)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-blue.svg)](https://github.com/openrig-host/openrig/releases)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.html)

A rack-based VST3 host and MIDI router for live performance — built to keep your set moving without dropouts, stuck notes, or failed song changes.

[Download](#download) · [Getting Started](GETTING_STARTED.md) · [Features](#features) · [How It Works](#how-it-works) · [VST3 Compatibility](#production-proven-vst3-ecosystem) · [Make It Yours](#make-it-yours)

<br/>

![Fanfare Live Mixer Interface](docs/full_mixer_v2.png)

</div>

> ### ⚡ Built for the Arena, Hardened for the Stage
>
> **Fanfare is engineered from the ground up for zero-tolerance live performance environments.** 
> While traditional DAWs bloat your CPU with timelines, background indexing, and fragile graph routing, Fanfare treats your computer like high-end rack hardware: dedicated linear channels, lock-free real-time audio threads, pre-allocated memory buffers, and atomic song transitions with rollback-by-construction. 
>
> When you hit the stage, you don't need an audio sketchbook. You need an impenetrable tank.

---

## What is Fanfare?

Fanfare is a Windows desktop application that hosts VST3 instruments and effects in a fixed, predictable linear rack — the way hardware works. No virtual patch cables. No node graphs. No drag-the-wires-around confusion.

It is built for the realities of live keyboard performance:

- **Splits, sample triggers, arpeggiators, octaves, and harmonizers** — set them once and trust them on stage.
- **Song changes happen in the background.** Switching is atomic, with rollback if anything goes wrong.
- **FOH and IEM are separate by design.** Every slot can send independently to both mixes.
- **A stuck note cannot end the show.** The panic button kills all MIDI in one audio block. Instrument swaps fade the old sound out and drain held notes cleanly.
- **Your mod wheel should hit only the right plugin.** Per-slot MIDI channel routing, CC remapping, and arm-then-wiggle CC learn keep control exactly where you want it.
- **A button should behave like a switch.** Any CC binding can latch for parameters, mutes, or A/B swaps.
- **Some sounds belong in hardware.** Any slot can send MIDI OUT to an external synth or module instead of hosting a VST.
- **The next song should be ready before you call it.** Setlists preload the next rig on a worker thread while you keep playing.
- **Break music is part of the show.** A dual-deck player handles playlisting, BPM display, YouTube grabs, and cue trimming.

This is not a DAW. It is a stage instrument.

---

## Quick Start

1. **Download the latest release**: Grab `Fanfare.exe` from [GitHub Releases](https://github.com/openrig-host/openrig/releases/latest) — no installer required.
2. **Configure audio and MIDI**: Open `Settings`, choose your **ASIO** driver (or WASAPI), and enable your primary MIDI controller.
3. **Load a rig or build a slot**: Click **LOAD RIG** to open a setlist or song, or click **[EMPTY]** on any strip slot to load a VST3 plugin.
4. **Set up key range and MIDI learn**: Click **NR** on a strip to set the key split range via play-to-learn, or **CC** to open the CC Assignment Manager and map your hardware controls.

> For a full step-by-step walkthrough, see **[GETTING_STARTED.md](GETTING_STARTED.md)**.

---

## Features

### The Rack
- **Fixed 24-slot layout** — one instrument per slot, always in a known order.
- **Dual-bus mixing & Direct Outs** — independent FOH (1-2) and IEM (3-4) levels, mutes, and enables on every slot, plus dedicated discrete Direct Hardware Outputs (Outputs 5, 6, 7, 8) for isolated feeds like vocal mics or dry sends.
- **Input Channel Mode Switch** — instantly cycle slots between Stereo, Mono (L+R sum), Mono (Left), and Mono (Right).
- **Per-slot MIDI routing** — global default plus per-slot override.
- **Per-instrument stacking** — multiple instances of the same plugin in a slot, each with its own level, note range, and enable state.
- **Polished dark UI** — skeuomorphic controls, glassy panels, and no virtual-cable clutter.

### Songs, Scenes, and Setlists
- **Songs (Rigs)** — complete patches saved as versioned JSON.
- **Scenes** — verse / chorus / bridge variations that can be recalled or triggered by MIDI.
- **Setlists** — ordered queues of songs with one-click load and automatic preload of the next rig.
- **Atomic transitions** — new rigs are built and validated off-thread, then swapped in under lock.
- **Rollback by construction** — if anything fails, the current rig keeps playing.
- **Clean-slate mode** — fully unload the old rig before building a heavier one.

### MIDI
- **Arm-then-wiggle CC learn** — click Learn, move the control, and bind it.
- **Latch mode (TOG)** — turn any binding into a toggle.
- **Per-strip mute toggles** — bind mutes to a CC with the same press-edge behavior.
- **Instrument swap** — switch between two loaded chain slots without unloading either one.
- **Long-press learn** — hold MUTE / FOH / IEM to arm the matching learn immediately.
- **Scene MIDI triggers** — assign Program Change and channel to any scene.
- **MIDI monitor** — see exactly what your controller is sending.
- **MIDI OUT slots** — route filtered MIDI to hardware instead of a plugin.

### DSP and Effects
![Channel Strip DSP](docs/channel_strip_dsp.png)

- **Per-slot channel strip** — noise gate, EQ, compressor, chorus, and convolution IR reverb.
- **Musette Detuner & Accordion Physics** — authentic MM (2-Reed) and MMM (3-Reed) detuning with dry-transient preservation and wet-path pre-filtering, plus the raucous **"TEMU Musette"** setting for extreme, unhinged bargain-bin comedic wobble.
- **Discrete Direct Hardware Outs** — route individual slots directly to interface outputs (e.g. UMC1820 Out 5) while maintaining independent control over FOH and IEM sends.
- **Sampler with waveform splice editor** — load a WAV and trim it with sample-accurate handles.
- **Arpeggiator** — Up, Down, Up-Down, and Random patterns with BPM-locked timing.
- **Octave harmonizer** — generate sub and upper octaves with mode shaping.
- **MIDI effects** — transposer and related tools, per slot.
- **Master FX buses** — global reverb, EQ, and compression before the outputs.

### Between Sets
- **Dual-deck break-music player** — BPM display, gain control, cue trimming, and smart playlist arrangement.
- **YouTube grab** — paste a link and download MP3s directly into the playlist.
- **Music library database** — embedded SQLite with CSV, JSON, and M3U export.
- **Gig Notepad** — per-show notes alongside the rig.
- **Web companion** — remote control from a phone or tablet on the same network.

### Live Reliability
- **Panic button** — sends All Notes Off to every plugin in the next audio block.
- **Parallel, hang-proof loading** — plugins build concurrently, with per-path serialization and GUI-thread restore where needed.
- **Atomic JSON persistence with .bak** — saves are written safely and backed up automatically.
- **SEH-protected message loop** — a bad VST3 should not take down the show.
- **Per-plugin exception isolation** — one plugin failing does not kill the audio thread.
- **Versioned rig migration** — old rigs upgrade automatically; unknown future formats are rejected safely.

### Persistence
All data lives under `%APPDATA%/Fanfare/`:

| Folder | Contents |
|---|---|
| `songs/` | Rig files (`.json`) |
| `sets/` | Setlists |
| `backups/` | Auto-rotated `.bak` copies of rigs |
| `settings/` | User preferences and MIDI maps |

The `.exe` is fully standalone — no external asset files. SVG icons are embedded.

---

## How It Works

```
                       Keyboard
                                │ MIDI
                                ▼
        ┌──────────────────────────────────────┐
        │            Fanfare Engine            │
        │   ┌─────┬─────┬─────┬─────┬─────┐   │
        │   │ S0  │ S1  │ S2  │ S3  │ ... │   │  ← Linear rack
        │   │ Mon │Kbd  │Organ│VSTi │Aux  │   │     (no cables)
        │   └─────┴─────┴─────┴─────┴─────┘   │
        │         │ FOH       │ IEM            │  ← Dual bus
        │         ▼           ▼                │
        │   Master FOH    Master IEM           │
        │     FX bus        FX bus             │
        └──────┬──────────────┬────────────────┘
               │              │
               ▼              ▼
           FOH Out        IEM Out
```

**Switching a song:**

1. `SetlistManager` calls `RigTransitioner::transitionToFile(nextSong)`.
2. `RigBuilder` gathers the plugins for the new rig and builds them in parallel on worker threads.
3. Each new instance is validated with a silent `processBlock`.
4. The transitioner takes the callback lock, swaps pointers only, and signals the message loop.
5. The old rig is unloaded. The new rig is live.
6. If a build fails, the lock is never taken. The current rig keeps playing.

---

## Production-Proven VST3 Ecosystem

Fanfare features an intelligent multi-threaded plug-in engine with built-in fault containment, automatic GUI-thread state synchronization (Native Instruments, Roland), and thread-safe instantiation pipelines. The following industry-standard instruments and processors are verified and certified for mission-critical stage use:

### Certified Stage-Ready Plugins

| Plugin | Vendor | Type / Role |
|---|---|---|
| **Hammond B-3X** | Hammond / SkyLabs | Tonewheel Organ (integrated split/pedal note-range filters) |
| **Kontakt 8** | Native Instruments | Flagship Multi-Sampler (thread-safe GUI state restoration) |
| **Super 8** | Native Instruments | Modern Polyphonic Synthesizer |
| **Supercharger GT** | Native Instruments | Bus Tube Compressor |
| **Omnisphere** | Spectrasonics | Power Synth & Multi-Timbral Rompler |
| **UVI Workstation** | UVI | Advanced Sample Engine |
| **Syntronik 2** | IK Multimedia | Legendary Vintage Synth Rompler |
| **JUNO-106** | Roland Cloud | DCO Polyphonic Synthesizer |
| **ZENOLOGY** | Roland Cloud | Advanced ZEN-Core Synthesizer |
| **XV-5080** | Roland Cloud | Classic PCM Rompler Workhorse |
| **Jun-6 V** | Arturia | Classic Chorus Polyphonic Synth |
| **Replika XT** | Native Instruments | Studio Multi-Mode Delay |
| **Chorus JUN-6** | Arturia | Analog Stereo Bucket-Brigade Chorus |
| **Pre 1973** | Arturia | British Vintage Preamplifier |
| **Pre TridA** | Arturia | Trident A-Range Console Preamp |
| **Bus EXCITER-104** | Arturia | High-End Harmonic Enhancer |
| **Bus FORCE** | Arturia | Parallel Master Bus Processor |
| **Bus PEAK** | Arturia | Precision Limiter / Clipper |
| **MixBox** | IK Multimedia | Modular 500-Series Channel Strip |
| **Blue3 Organ** | Cherry Audio | Authentic Tonewheel & Rotary Speaker |
| **PolyMax** | Universal Audio | Vintage Analog Modeling Synthesizer |
| **bx_meter (VU)** | Brainworx | True Peak / Dynamic Range Metering |
| **bx_console Focusrite SC** | Brainworx | Studio Console Channel Strip |
| **TR5 Metering** | IK Multimedia | Broadcast & Loudness Telemetry |
| **TR5 British Channel** | IK Multimedia | Classic Solid-State Channel Strip |
| **TR5 White Channel** | IK Multimedia | Modern VCA Channel Strip |
| **EZkeys 2** | Toontrack | Acoustic & Electric Pianos |

### Hardware Validation

Fanfare is continuously battle-tested with professional stage controllers and multi-channel low-latency ASIO interfaces:

- **MIDI Controllers**: Yamaha CK88, Roland RD-88, Nektar LX61+, Arturia KeyLab 88 Essential, and all USB-MIDI class-compliant keyboards.
- **Audio Interfaces (ASIO)**: Behringer UMC1820 & UMC204HD, Focusrite Scarlett series, AKAI EIE Pro, and all professional multi-out ASIO interfaces.

---

## Download

Pre-built Windows binaries with **ASIO support** are on the [Releases page](https://github.com/openrig-host/openrig/releases). The download is a ready-to-run `Fanfare.exe` — no installer, no build step.

> Production binaries are compiled with hardware-optimized ASIO driver support and distributed directly via GitHub Releases. (Public cloud CI runners cannot distribute ASIO binaries due to Steinberg's proprietary SDK licensing agreements).

**Latest release:** https://github.com/openrig-host/openrig/releases/latest

### Requirements
- Windows 10 / 11 (64-bit)
- A VST3-compatible sound card or audio interface
- An **ASIO driver** for your interface, recommended for live use. Without one, the engine falls back to WASAPI.

### Compiling it yourself

> ⚠️ **ASIO SDK licensing:** the Steinberg ASIO SDK headers **cannot be bundled** in this repository. Before building with ASIO, download the SDK yourself from the official [Steinberg Developer Portal](https://www.steinberg.net/developers/), then place the headers in:
> ```
> C:\JUCE\modules\juce_audio_devices\native\asio\
> ```
> This exact path is required — it is where the project's `AppConfig.h` looks. This is also why there is no auto-build: the ASIO SDK cannot live in a public repo or run on CI.

To build, you need:
1. **Visual Studio 2022+** with the "Desktop development with C++" workload
2. **[JUCE 8](https://juce.com)** installed at `C:\JUCE`
3. **Steinberg ASIO SDK** — downloaded and placed as above for the Visual Studio ASIO build

Then open `Builds\VisualStudio2026\Fanfare.sln` in Visual Studio, set **Release / x64**, and build. The output is `Fanfare.exe`.

For the full walkthrough, including the CMake no-ASIO path, troubleshooting, and the command-line MSBuild equivalent, see **[BUILDING.md](BUILDING.md)**.

---

## Make It Yours

**The whole source tree is here. Fork it, modify it, and ship your own version.**

This codebase was developed end-to-end with advanced AI coding tools — [Antigravity](https://antigravity.dev) and KiloCode, powered by Gemini, Claude, Minimax, Zai, and Xiaomi. That is not a marketing claim; it is simply how the project was built.

1. **Download the source** using the green "Code" button → "Download ZIP", or `git clone`.
2. **Unzip it on your machine.**
3. **Point Antigravity at the folder** — or use any AI coding agent that can read a C++/JUCE codebase.
4. **Ask for what you want.** For example:
   - *"Add a new VST to the plugin registry in FanfareEngine.h."*
   - *"Add VST2 hosting alongside VST3."*
   - *"Add a transpose-offset knob to the channel strip."*
   - *"Migrate the JUCE 8 code to JUCE 9 when it ships."*
   - *"Add a new hardware controller profile."*
5. **Iterate.** The agent has the full source — types, comments, architecture docs, and everything else it needs to make real changes, not just stubs.

Antigravity is the recommended environment for AI-assisted iteration, but any environment that understands JUCE 8 / C++17 will excel. You will need the build environment described in [BUILDING.md](BUILDING.md) — Visual Studio, JUCE, and the ASIO SDK for ASIO builds.

**If you build something useful, a PR back is welcome but not required** — unless you distribute your modified version, in which case the [GPL v3](LICENSE) requires you to publish your changes and make the corresponding source available.

---

## Philosophy

A few principles drove every design decision:

1. **Predictability over flexibility.** A stage rig is not a sketchbook. Slots are fixed. Routing is fixed. You know exactly what sound comes out of which key, every time.
2. **The worst-case failure must be a no-op.** A song switch that fails should leave the current rig playing. A plugin crash should leave the other slots playing. A panic button must work in the next audio block.
3. **Background work, foreground feel.** Plugin loading, preloading, MIDI mapping, and persistence should not block the audio thread, and should block the message thread as little as possible.
4. **Own your data.** Rigs are JSON. Backups are automatic. There is no cloud, no account, and no telemetry. Your setlist is a folder of files you can read, edit, version-control, and back up with any tool you like.

---

## Roadmap

- [x] **Theme engine** — 5 selectable themes, including a light theme
- [x] **Parallel plugin loading** with per-path serialization and GUI-thread restore for NI plugins
- [x] **CC latch/toggle mode, mute toggles, long-press learn, instrument swap**
- [ ] **DPI-aware layout** for HiDPI stage displays
- [ ] **Out-of-process plugin sandboxing** (advancing our real-time SEH containment to isolated process rings)
- [ ] **Touch-friendly mode** for tablet second screens

---

## License

**Fanfare is licensed under the [GNU General Public License v3.0](LICENSE).**

The GPL v3 was chosen deliberately: the Steinberg ASIO SDK — which Fanfare links for low-latency audio — is offered under GPL v3 as an alternative to its proprietary license. By licensing Fanfare under GPL v3, the project remains compatible with that model.

What that means in practice:
- You're free to use, study, modify, and redistribute Fanfare, including the ASIO builds.
- If you **distribute** a modified version, binary or source, you must release your changes under GPL v3 and make the corresponding source available.
- The ASIO SDK itself still may not be redistributed in this repo — builders download it themselves per [BUILDING.md](BUILDING.md).

## Credits

Built with [JUCE 8](https://juce.com).

Created with [Antigravity](https://antigravity.dev) and KiloCode, powered by [Gemini](https://deepmind.google/technologies/gemini/), [Claude](https://anthropic.com), Minimax, Zai, and Xiaomi.
