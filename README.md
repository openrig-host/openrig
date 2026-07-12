<div align="center">

# OpenRig

### The Sovereign Live Performance Engine

A rack-based VST3 host built for one job: getting you through a live set without a single dropout, a stuck note, or a fumbled song switch.

[Download](#download) · [Features](#features) · [How It Works](#how-it-works) · [Tested Plugins](#tested-vst-compatibility) · [Make It Yours](#make-it-yours) · [Roadmap](#roadmap)

</div>

---

> ## ⚠️ Read this before you use it live
>
> **OpenRig is a personal project.** It was built by one keyboardist for one keyboardist's own stage rig. It's shared here in case it's useful to anyone else, but it comes with **no warranty, no support SLA, and no compatibility promise** beyond the plugin and hardware list below.
>
> **If you intend to use this on a paid gig, you must:**
> 1. Run it for at least a week of rehearsals with **your** actual songs, **your** actual VSTs, and **your** actual hardware.
> 2. Stress-test song switches, scene changes, and panic-button scenarios under live conditions.
> 3. Have a backup plan (a second laptop, a hardware fallback, a way to bail out).
>
> The author can only vouch for compatibility with the [VSTs they personally use](#tested-vst-compatibility). Other plugins may work, may partially work, or may crash the audio thread. There are known special cases — see the plugin list.
>
> ---
>
> ### 🤖 Honest disclaimer
>
> This is a vibecoded app. AI agents wrote most of it. It has bugs. It will have more bugs tomorrow. It might crash on a plugin you've never tried.
>
> **But it also does some genuinely cool things that commercial stage-rig software doesn't**, because it was designed for one specific live setup instead of trying to please everyone — atomic song switches, rollback-by-construction, dual-bus per slot, a panic button that actually works.
>
> If you find a bug, the answer is right there in the source: fix it, rebuild, ship your own version. That's the point of [Make It Yours](#make-it-yours) below. :)

---

## What is OpenRig?

OpenRig is a Windows desktop application that hosts your VST3 instruments and effects in a fixed, predictable linear rack — the way hardware works. No virtual patch cables. No node graphs. No "let me just click 4 things and I'll have a snare."

It's built around the realities of playing a keyboard live:

- **You can't reload a song in the middle of a verse.** Songs switch in the background, atomic, with rollback if something goes wrong.
- **The sound guy and the in-ear mix are not the same mix.** Every slot sends independently to FOH and IEM.
- **A stuck note cannot end the show.** A panic button kills all MIDI in one audio block. Always.
- **Your modwheel must hit the B3X and only the B3X.** Per-slot MIDI channel routing, plus full CC mapping with arm-then-wiggle learn.
- **The next song needs to be ready before you call it.** Setlists preload the next rig in a background thread while you play.

This is not a DAW. It's a stage instrument.

---

## Features

### The Rack
- **Linear, fixed slot layout** — one slot per instrument, in a known order. No clicks, no surprises.
- **Dual-bus mixing on every slot** — independent FOH (Front of House) and IEM (In-Ear Monitor) levels, mutes, and enables.
- **Per-slot MIDI channel routing** — global default + per-slot override. Send a CC to one instrument and *only* that instrument.
- **Per-instrument stacking** — multiple instances of the same plugin in a slot, with individual level, note range, and enable.
- **Boutique dark-mode UI** — skeuomorphic knobs, glassy panels, no virtual-cable spaghetti.

### Songs, Scenes & Setlists
- **Songs (Rigs)** — a complete patch: plugins, states, channel strips, CC maps, levels. Saved as versioned JSON.
- **Scenes** — variations within a song (verse / chorus / bridge). Snapshot, recall, and MIDI-trigger them via program change.
- **Setlists** — ordered queue of songs with one-click load and **automatic preload of the next rig** on a worker thread.
- **Atomic transitions** — the new rig is built and validated off-thread, then swapped in under lock. A failed build never touches the live rig.
- **Rollback-by-construction** — if anything in the build fails, the current rig keeps playing. No half-loaded songs, ever.

### MIDI
- **Arm-then-wiggle CC learn** — click a knob's "Learn" button, move the physical control, it's bound. Range, min/max, invert, and per-parameter index all supported.
- **Scene MIDI triggers** — assign a Program Change + channel to any scene. Hardware sequencer calls the song.
- **MIDI monitor** — see what your keyboard is actually sending, with learn-capture overlay.
- **MIDI remote settings** — define a hardware device, map its controls once, use them across all rigs.

### DSP & Effects
- **Per-slot channel strip** — gate, parametric EQ, compressor, chorus, reverb. Toggleable per slot.
- **Sampler with waveform splice editor** — load a WAV, set root note, trim the start/end with sample-accurate handles.
- **Arpeggiator** — patterns (Up / Down / Up-Down / Random), 0–4 octave range, gate time, BPM-locked to 1 decimal.
- **Octave harmonizer** — generate sub/up octaves with mode shaping.
- **MIDI effects** — transposer and friends, per slot.
- **Master FOH and IEM FX buses** — global reverb / EQ / compression before the outputs.

### Live Reliability
- **Panic button** — instantly sends All Notes Off to every plugin, in the next audio block. Hardware-fail-safe.
- **Atomic JSON persistence with .bak** — every rig save writes to a temp file, renames atomically, and keeps a backup of the previous version. Crashes during save cannot corrupt your library.
- **SEH-protected message loop** — if a misbehaving VST3 (Qt-based plugins in particular) crashes, the engine survives and the show goes on.
- **Per-plugin exception isolation** — one plugin throwing inside `processBlock` cannot kill the audio thread. The slot mutes; the rest of the rig keeps playing.
- **Versioned rig format with migration** — v1 rigs auto-upgrade to v2. Unknown future versions are refused, not silently misinterpreted.

### Persistence
All data lives under `%APPDATA%/OpenRig/`:

| Folder    | Contents                          |
| --------- | --------------------------------- |
| `songs/`  | Rig files (`.json`)               |
| `sets/`   | Setlists                          |
| `backups/`| Auto-rotated `.bak` of rigs       |
| `settings/`| User preferences, MIDI maps      |

The `.exe` is fully standalone — no external asset files. SVG icons are embedded.

---

## How It Works

```
                       Keyboard
                                │  MIDI
                                ▼
        ┌──────────────────────────────────────┐
        │           OpenRigEngine              │
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

1. SetlistManager calls `RigTransitioner::transitionToFile(nextSong)`.
2. `RigBuilder` runs on a worker thread: instantiate any new plugins, restore state on reused ones, validate with a silent `processBlock`.
3. The transitioner takes the callback lock, **swaps pointers only** (no allocations, no state restore on the audio thread), and signals the message loop.
4. The old rig is unloaded. New rig is live. Total switch time: typically 200–800 ms; the loading overlay covers it.
5. If step 2 failed, the lock is never taken. The current rig keeps playing. *You will not know anything happened, by design.*

---

## Tested VST Compatibility

> **The author can only vouch for the plugins below.** These are the VSTs in the engine's hardcoded plugin registry, which is what the rig builder will scan for by default. If your VST isn't on this list, you can add its path manually — but it is **your** responsibility to test it.

### Verified working

| Plugin | Vendor | Notes |
|---|---|---|
| **Hammond B-3X** | Hammond / SkyLabs | The organ. Has a custom note-range filter (top/bottom of keyboard cut). |
| **Kontakt 8** | Native Instruments | Heavy sampler. Can hang the message thread 30+ s on retry; do not retry. |
| **Super 8** | Native Instruments | Qt-based. Aborts if instantiated twice; reuse-by-path is mandatory. |
| **Supercharger GT** | Native Instruments | Bus compressor. |
| **Omnisphere** | Spectrasonics | Aux-bus layout is non-standard; engine skips strict layout enforcement for it. |
| **UVI Workstation** | UVI | Sampler. |
| **Syntronik 2** | IK Multimedia | Synth rompler. |
| **JUNO-106** | Roland | Synth. |
| **ZENOLOGY** | Roland | Synth. |
| **XV-5080** | Roland | ROMpler. |
| **Jun-6 V** | Arturia | Synth. (Threw `CException` once during state restore; the engine catches and isolates it.) |
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

### Known-broken / special-case

- **Qt-based VST3s** (e.g. NI Super 8) — must be built on the message thread, not a worker thread, and the engine reuses plugin instances by path. This is hard-coded in the engine.
- **VST3s that call back into the host on `prepareToPlay`** — engine wraps this in `try/catch(...)`. A failed plugin is logged and skipped, the rig applies with the rest.
- **VST3s that take 30+ seconds to load** — no timeout per plugin, but the overall `applyRig` has a 60-second timeout. If a single plugin eats the whole budget, the rest of the rig won't apply.

### Hardware tested

- **Yamaha CK88** (master controller)
- **Roland RD88** (controller, send only)
- A generic ASIO audio interface

Other keyboards will work as MIDI sources, but the per-slot MIDI channel routing has only been exercised with these two.

---

## Download

Pre-built Windows binaries (with **ASIO support**) are on the [Releases page](https://github.com/openrig-host/openrig/releases). The download is a ready-to-run `OpenRig.exe` — no installer, no build step.

> Binaries are built locally by the author and uploaded by hand to each Release. There is no auto-build: the ASIO build can't be produced on a public CI runner because ASIO requires Steinberg's proprietary SDK.

**Latest release:** https://github.com/openrig-host/openrig/releases/latest

### Requirements
- Windows 10 / 11 (64-bit)
- A VST3-compatible sound card / audio interface
- An **ASIO driver** for your interface (recommended for live use). Without one, the engine falls back to WASAPI.

### Compiling it yourself

> ⚠️ **ASIO SDK licensing:** the Steinberg ASIO SDK headers **cannot be bundled** in this repository. Before building, download the SDK yourself from the official [Steinberg Developer Portal](https://www.steinberg.net/developers/) (free), then extract its header files into:
> ```
> C:\JUCE\modules\juce_audio_devices\native\asio\
> ```
> This exact path is required — it's where the project's `AppConfig.h` looks. (This is also why there's no auto-build: the ASIO SDK can't live in a public repo or run on CI.)

To build, you need:
1. **Visual Studio 2022+** with the "Desktop development with C++" workload
2. **[JUCE 8](https://juce.com)** installed at `C:\JUCE`
3. **Steinberg ASIO SDK** — downloaded and placed as above

Then open `DaveCoreProject/Builds/VisualStudio2026/DaveCore.sln` in Visual Studio, set **Release / x64**, and build. Output: `OpenRig.exe`.

For the full walkthrough (incl. troubleshooting and the command-line MSBuild path), see **[BUILDING.md](BUILDING.md)**.

> Want to skip ASIO entirely? You can build without it — the app still runs, just without the ASIO device type (WASAPI only).

---

## Make It Yours

**The whole source tree is here. Fork it, modify it, ship your own version.**

This codebase was developed end-to-end with AI coding tools ([Antigravity](https://antigravity.dev) and KiloCode, powered by Gemini, Claude, Minimax, Zai, and Xiaomi). That's not a marketing claim — it's a workflow. The intended way to use this repo is:

1. **Download the source** (green "Code" button → "Download ZIP", or `git clone`).
2. **Unzip it on your machine.**
3. **Point Antigravity at the folder** (or any other AI coding agent that can read a C++/JUCE codebase).
4. **Ask for what you want.** For example:
   - *"Add a new VST to the plugin registry at OpenRigEngine.h:2411."*
   - *"Add VST2 hosting alongside VST3."*
   - *"Add a transpose-offset knob to the channel strip."*
   - *"Migrate the JUCE 8 code to JUCE 9 when it ships."*
   - *"Fix this crash when I load my Arturia plugin."*
5. **Iterate.** The agent has the full source — types, comments, architecture-review doc, the lot. It can make real changes, not just stubs.

The author's tool of choice is Antigravity, but anything that can read a JUCE 8 / C++17 codebase will work. You will need the build environment described in [BUILDING.md](BUILDING.md) (Visual Studio, JUCE 8, and the Steinberg ASIO SDK) to compile whatever the agent produces.

**If you build something useful, a PR back is welcome but not required** — *unless* you distribute your modified version, in which case the [GPL v3](LICENSE) requires you to publish your changes under the same license. Personal/private use has no such obligation.

---

## Philosophy

A few principles drove every design decision:

1. **Predictability over flexibility.** A stage rig is not a sketchbook. Slots are fixed. Routing is fixed. You know exactly what sound comes out of which key, every time.
2. **The worst-case failure must be a no-op.** A song switch that fails should leave the current rig playing. A plugin crash should leave the other slots playing. A panic button must work in the next audio block, always.
3. **Background work, foreground feel.** Plugin loading, preloading, MIDI mapping, persistence — none of it blocks the audio thread, and as little as possible blocks the message thread.
4. **Own your data.** Rigs are JSON. Backups are automatic. There is no cloud, no account, no telemetry. Your setlist is a folder of files you can read, edit, version-control, and back up with any tool.

---

## Roadmap

- [x] **Theme engine** — 5 selectable themes including a light theme
- [ ] **DPI-aware layout** for HiDPI stage displays
- [ ] **Arpeggiator persistence** in the rig JSON
- [ ] **Out-of-process plugin hosting** (current SEH wrapper is containment, not isolation)
- [ ] **Touch-friendly mode** for tablet second screens

See [`DaveCore_Wishlist.md`](DaveCore_Wishlist.md) for the long-form wishlist and [`architecture-review-zai52.md`](architecture-review-zai52.md) for a deep technical review of the engine.

---

## License

**OpenRig is licensed under the [GNU General Public License v3.0](LICENSE).**

The GPL v3 was chosen deliberately: the Steinberg ASIO SDK — which OpenRig links for low-latency audio — is offered under GPL v3 as an alternative to its proprietary license. By licensing OpenRig under GPL v3, the published ASIO-enabled binaries are compliant with the ASIO SDK's terms **without** needing a separate signed agreement from Steinberg.

What that means in practice:
- You're free to use, study, modify, and redistribute OpenRig, including the ASIO builds.
- If you **distribute** a modified version (binary or source), you must release your changes under GPL v3 and make the corresponding source available.
- The ASIO SDK itself still may not be redistributed in this repo — builders download it themselves per [BUILDING.md](BUILDING.md).

## Credits

Built with [JUCE 8](https://juce.com).

Created with [Antigravity](https://antigravity.dev) and KiloCode, powered by [Gemini](https://deepmind.google/technologies/gemini/), [Claude](https://anthropic.com), Minimax, Zai, and Xiaomi.
