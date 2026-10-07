<div align="center">

# Fanfare

### The Sovereign Live Performance Engine

[![Latest Release](https://img.shields.io/github/v/release/openrig-host/openrig?sort=date&cacheSeconds=900&color=ed786a&label=Latest%20Release)](https://github.com/openrig-host/openrig/releases/latest)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-blue.svg)](https://github.com/openrig-host/openrig/releases)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.html)

A rack-based VST3 host and MIDI router for live performance — built to keep your set moving without dropouts, stuck notes, or failed song changes.

[Download](#download) · [Getting Started](GETTING_STARTED.md) · [Features](#features) · [How It Works](#how-it-works) · [Tested Plugins](#tested-vst-compatibility) · [Make It Yours](#make-it-yours)

<br/>

![Fanfare Live Mixer Interface](docs/full_mixer_v2.png)

</div>

---

> ## ⚠️ Read this before you use it live
>
> **Fanfare is a personal project.** It was built by one keyboardist for one keyboardist's own stage rig. It's shared here in case it helps others, but it comes with **no warranty, no support, and no guarantee it will behave perfectly on your setup**.
>
> **If you plan to use this on a paid gig, you should:**
> 1. Rehearse with **your** songs, **your** VSTs, and **your** hardware for at least a week.
> 2. Stress-test song switches, scene changes, and panic-button scenarios under live conditions.
> 3. Have a backup plan: a second laptop, a hardware fallback, or another way to finish the set.
>
> The author can only vouch for the VSTs they personally use. Other plugins may work, may partially work, or may crash the audio thread. Some failures are handled gracefully; others are not.
>
> ---
>
> ### 🤖 Honest disclaimer
>
> This is a vibecoded app. AI agents wrote most of it. It has bugs. It will have more bugs tomorrow. It may crash on a plugin you've never tried.
>
> **But it also does a few things commercial stage-rig software often doesn't**, because it was designed for one specific live setup instead of trying to serve everyone — atomic song switching, rollback-by-construction, per-slot MIDI routing, and more.
>
> If you find a bug, the answer is right there in the source: fix it, rebuild it, and ship your own version. That's the point of [Make It Yours](#make-it-yours).

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
- **Dual-bus mixing** — independent FOH and IEM levels, mutes, and enables on every slot.
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

## Tested VST Compatibility

> **The author can only vouch for the plugins below.** These are the VSTs in the engine's hardcoded plugin registry, which is what the rig builder scans by default. If your VST is not on this list, it may still work, but it has not been verified.

### Verified working

| Plugin | Vendor | Notes |
|---|---|---|
| **Hammond B-3X** | Hammond / SkyLabs | The organ. Has a custom note-range filter for the top and bottom of the keyboard. |
| **Kontakt 8** | Native Instruments | Heavy sampler. Builds on the GUI thread and restores large multis without deadlocking. |
| **Super 8** | Native Instruments | Qt-based. Must be reused by path; instantiating it twice will fail. |
| **Supercharger GT** | Native Instruments | Bus compressor. |
| **Omnisphere** | Spectrasonics | Uses a non-standard aux-bus layout; the engine skips strict layout enforcement for it. |
| **UVI Workstation** | UVI | Sampler. |
| **Syntronik 2** | IK Multimedia | Synth rompler. |
| **JUNO-106** | Roland | Synth. |
| **ZENOLOGY** | Roland | Synth. |
| **XV-5080** | Roland | ROMpler. |
| **Jun-6 V** | Arturia | Synth. The engine catches and isolates a `CException` seen during state restore. |
| **Replika XT** | Arturia | Delay. |
| **Chorus JUN-6** | Arturia | Modulation. |
| **Pre 1973** | Arturia | Preamp. |
| **Pre TridA** | Arturia | Preamp. |
| **Bus EXCITER-104** | Arturia | Bus processor. |
| **Bus FORCE** | Arturia | Bus processor. |
| **Bus PEAK** | Arturia | Bus processor. |
| **MixBox** | IK Multimedia | Channel strip / multi-FX. |
| **Blue3 Organ** | Cherry Audio | Tonewheel organ. |
| **PolyMax** | Universal Audio | Synth. |
| **bx_meter (VU)** | Brainworx | Metering. |
| **bx_console Focusrite SC** | Brainworx | Channel. |
| **TR5 Metering** | IK Multimedia | Metering. |
| **TR5 British Channel** | IK Multimedia | Channel. |
| **TR5 White Channel** | IK Multimedia | Channel. |
| **EZkeys 2** | Toontrack | Piano. |

### Known-broken / special case

- **Qt-based VST3s** (for example, NI Super 8) must be built on the message thread, not a worker thread, and the engine reuses plugin instances by path.
- **NI plugins in general** — Kontakt restores state on the GUI thread; background-thread `setStateInformation` on a second instance of the same plugin can deadlock. Handled automatically.
- **VST3s that call back into the host on `prepareToPlay`** — the engine wraps this in `try/catch(...)`. A failed plugin is logged and skipped; the rig applies with the rest.
- **VST3s that stall indefinitely** — each build has a 600 s timeout. After that, the entry is skipped and that plugin path is not retried for the rest of the session.

### Hardware tested

MIDI controllers:
- **Yamaha CK88** (88-key controller with drawbars)
- **Roland RD88** (controller, send only)
- **Nektar LX61+** (compact primary controller)
- **Arturia KeyLab 88 Essential**

Audio interfaces (ASIO):
- **Behringer UMC204HD**
- **Behringer UMC1820**
- **AKAI EIE Pro**

Other keyboards will work as MIDI sources, but the per-slot MIDI channel routing has only been exercised with these. Other class-compliant ASIO interfaces should work the same way the tested ones do.

---

## Download

Pre-built Windows binaries with **ASIO support** are on the [Releases page](https://github.com/openrig-host/openrig/releases). The download is a ready-to-run `Fanfare.exe` — no installer, no build step.

> Binaries are built locally by the author and uploaded by hand to each release. There is no auto-build: the ASIO build cannot be produced on a public CI runner because ASIO requires Steinberg's proprietary SDK.

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

This codebase was developed end-to-end with AI coding tools — [Antigravity](https://antigravity.dev) and KiloCode, powered by Gemini, Claude, Minimax, Zai, and Xiaomi. That is not a marketing claim; it is simply how the project was built.

1. **Download the source** using the green "Code" button → "Download ZIP", or `git clone`.
2. **Unzip it on your machine.**
3. **Point Antigravity at the folder** — or use any AI coding agent that can read a C++/JUCE codebase.
4. **Ask for what you want.** For example:
   - *"Add a new VST to the plugin registry in FanfareEngine.h."*
   - *"Add VST2 hosting alongside VST3."*
   - *"Add a transpose-offset knob to the channel strip."*
   - *"Migrate the JUCE 8 code to JUCE 9 when it ships."*
   - *"Fix this crash when I load my Arturia plugin."*
5. **Iterate.** The agent has the full source — types, comments, architecture docs, and everything else it needs to make real changes, not just stubs.

The author's tool of choice is Antigravity, but anything that can read a JUCE 8 / C++17 codebase will work. You will need the build environment described in [BUILDING.md](BUILDING.md) — Visual Studio, JUCE, and the ASIO SDK for ASIO builds.

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
- [ ] **Out-of-process plugin hosting** (current SEH wrapper is containment, not isolation)
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
