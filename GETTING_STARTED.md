# Getting Started with Fanfare

Welcome to **Fanfare**! This guide walks you through your first 5 minutes with Fanfare, from downloading the executable to running your first live rig, hosting VST3 instruments, and tuning your laptop for dropout-free stage performance.

---

## 1. Download & Launch

1. Go to the [Fanfare Releases Page](https://github.com/openrig-host/openrig/releases/latest).
2. Download `Fanfare.exe`.
3. **No installation is required.** Place `Fanfare.exe` in any local folder on your Windows 10/11 PC and double-click to launch.
4. **Stage Power Rule:** Always run your laptop plugged into its **AC power supply** during rehearsals and gigs. On battery power, Windows and laptop firmware aggressively downclock the CPU, sleep USB ports, and generate battery-polling interrupts that cause audio clicks.

---

## 2. Initial Setup (Audio & MIDI)

When Fanfare launches for the first time, click the **Settings** gear icon in the top header bar:

1. **Audio Settings (ASIO Recommended)**:
   - Select **ASIO** as your audio device type.
   - Choose your audio interface driver (for example, *Yamaha Steinberg USB ASIO*, *UMC ASIO Driver*, *Focusrite USB ASIO*, etc.).
   - Set your sample rate (`44.1 kHz` or `48 kHz`) and buffer size (`128` to `256` samples for low-latency live play).
   - *(Fallback)*: If you do not have an ASIO driver installed, select **Windows Audio (WASAPI)**.

2. **MIDI Input Device**:
   - Enable your keyboard controller(s) in the same Audio & MIDI Settings dialog (for example, *Yamaha CK88*, *Roland RD-88*, *Impact LX61+*, *KeyLab Essential 88*).

---

## 3. Engine & CPU Tuning (New in v1.2)

Click the **Settings** gear icon and open **ENGINE & CPU TUNING** to inspect or tailor how Fanfare schedules real-time audio on your computer:

- **Hybrid CPU Core Mode (P-Cores Only, Default ON):** Locks real-time ASIO and DSP threads onto fast Performance cores while steering UI rendering and background setlist preloading onto Efficiency cores. Turn this to *Use All Cores* if you run an older PC or AMD Ryzen laptop where all cores share the same speed and you want extra polyphony headroom.
- **Shelter CPU Core 0 (Default ON):** Keeps real-time audio off Core 0, where Windows routes Wi-Fi, USB hub, and battery driver interrupts.
- **Block Windows 11 EcoQoS Power Throttling (Default ON):** Prevents Windows 11 from clocking down Fanfare when VST editor windows are in front or the main window is minimized.
- **DSP Multiprocessing Pipeline (Parallel Work-Sharing Default):** Splits heavy multi-synth rigs across physical Performance cores alongside the ASIO thread using a lock-free queue. You can switch to *Sequential Inline* anytime for single-synth rigs or troubleshooting.

---

## 4. Populating Your VST3 Plugins

Fanfare scans standard Windows VST3 directories (`C:\Program Files\Common Files\VST3`) to locate your installed instruments and effects:

1. Click the **Settings** gear icon.
2. Click **SCAN PLUGINS** to populate your local plugin registry.

---

## 5. Build Your First Rig

### Step A: Load a VST3 Instrument into a Slot
1. Click **[EMPTY]** on any channel strip slot.
2. Pick a VST3 instrument from your list (for example, *Hammond B-3X*, *Kontakt 8*, *ZENOLOGY*, *DX7 V*, *Jun-6 V*).
3. Click **E** next to the slot to open the plugin's floating editor window.

### Step B: Set Key Split / Note Range (**NR**)
1. Click **NR** on the channel strip.
2. Click **Learn Range**, then strike the lowest key and highest key on your controller to define the split zone.

### Step C: Map Hardware Knobs & Buttons (**CC**)
1. Click **CC** on the channel strip (or long-press **MUTE**, **FOH**, or **IEM** directly on the mixer).
2. Click **Learn** on any parameter, toggle, or A/B instrument swap, then move a physical knob, fader, or button on your keyboard.

---

## 6. Saving, Setlists & Stage Telemetry

- **Save Rig:** Click **SAVE** in the top bar. Fanfare writes files atomically with rotating `.bak` backups in `%APPDATA%\Fanfare\`.
- **Stage Queue & Preloading:** Assign songs to the numbered setlist buttons. While you play the active song, Fanfare automatically preloads the next song in the background on non-audio cores.
- **Panic Button:** Click the red **PANIC** button (or press `P`) to send All Notes Off and Sustain Off across every slot in the very next audio block.
- **XRUNS Monitor:** The top telemetry bar displays live CPU, RAM, round-trip latency, and `XRUNS`. Click `XRUNS:` anytime to reset the counter to zero and open `audio_underrun_log.txt`.

---

## Next Steps

- Click **DYN** on any strip for the built-in channel strip (Gate, EQ, Compressor, Chorus, Convolution Reverb, and Musette detuner).
- Explore the **8-Pad Sampler**, **Arpeggiator / Harmonizer**, **Dual-Deck Break Music Player**, and **Gig Notepad**.
- See [BUILDING.md](BUILDING.md) if you want to compile Fanfare from source or customize the engine.
