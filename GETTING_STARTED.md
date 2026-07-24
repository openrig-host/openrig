# Getting Started with OpenRig

Welcome to **OpenRig**! This guide walks you through your first 5 minutes with OpenRig—from initial download to running your first live rig and hosting your own VST3 instruments.

---

## 1. Download & Launch

1. Go to the [OpenRig Releases Page](https://github.com/openrig-host/openrig/releases/latest).
2. Download the `OpenRig.exe` executable (or latest release package).
3. **No installation is required**. Simply place `OpenRig.exe` in any folder on your Windows 10/11 system and double-click to launch.

---

## 2. Initial Setup (Audio & MIDI)

When OpenRig launches for the first time, click the **Settings ⚙️** icon in the top header bar:

1. **Audio Output (ASIO Recommended)**:
   - Select **ASIO** as your audio device type.
   - Choose your audio interface driver (e.g. *Focusrite USB ASIO*, *Universal Audio Thunderbolt*, etc.).
   - Set your sample rate (e.g. `44.1 kHz` or `48 kHz`) and buffer size (`64` to `256` samples for low latency).
   - *(Fallback)*: If you do not have an ASIO device connected, select **WASAPI Exclusive**.

2. **MIDI Input Device**:
   - Enable your master keyboard controller (e.g., *Yamaha CK88*, *Roland RD88*, *MIDI Controller*).

---

## 3. Load Your First Rig (Demo / Saved Setlist)

OpenRig comes with simple preset management and setlist preloading out of the box:

1. Click **LOAD RIG** in the top right header (or click **Setlist** in the left sidebar).
2. Select a rig file (e.g., `Warm Piano`, `Hungry-wolf-setup`, or any `.json` patch).
3. OpenRig will instantiate the channels off-thread and swap them in under 1 second without audio dropouts.
4. Play your keyboard—you should hear audio metering through the **FOH** and **IEM** master faders!

---

## 4. Building Your Own Setup (Adding VST3 Plugins)

Building a custom 12-slot rack from scratch is fast and intuitive:

### Step A: Load a VST3 Instrument into a Slot
1. Click **[EMPTY]** on any available channel strip slot (Slots 1–12).
2. Select your VST3 plugin from the scanned plugin list (e.g., *Hammond B-3X*, *Kontakt 8*, *ZENOLOGY*, *JUNO-106*).
3. Click the plugin title on the strip to open its native floating editor UI.

### Step B: Set Key Split / Note Range (**NR**)
1. Click the **NR** button at the bottom of the channel strip.
2. Click **Learn Range**, then play the lowest note and highest note on your keyboard to set the split zone.

### Step C: Map Hardware Knobs (**CC**)
1. Click the **CC** button at the bottom of the channel strip.
2. Click **Learn** on any parameter, then wiggle a physical knob/slider on your MIDI keyboard to bind it instantly.

---

## 5. Saving & Stage Best Practices

- **Save Rig**: Click **SAVE RIG** in the top bar. OpenRig saves atomically with automatic `.bak` backup rotations so your patch data is never corrupted.
- **Stage Panic Button**: If a physical cable is yanked or a plugin receives a rogue note, click the red **PANIC** button (or trigger via MIDI) to instantly flush all active notes inside the very next audio block.

---

## Next Steps

- Explore built-in DSP effects (EQ, Compression, Reverb, Chorus) by clicking the **DYN** button on any strip.
- Try the **8-Pad One-Shot Sampler** and **Arpeggiator** modules inside the channel strip editor.
- Check out [BUILDING.md](BUILDING.md) if you want to compile OpenRig from source or customize the engine!
