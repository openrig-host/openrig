# Fanfare (DaveCore) -- AI Agent & Developer Quickstart (`starthere.md`)

> **FOR AI AGENTS & LLMs**:  
> This file is your operational index and master guide for building, running, and debugging Fanfare directly from the local offline workspace at `C:\davecore\`.

---

## 1. Quick Reference & Core Facts

| Attribute | Value |
| :--- | :--- |
| **Local Project Root** | `C:\davecore\` |
| **NAS Root (Home/Online)** | `Z:\davecore\` (`\\MYCLOUDEX2ULTRA\dwaugh\Antigravity`) |
| **Language / Standard** | C++17 |
| **Audio Framework** | **JUCE 8.0.12** (`0x8000c`) -- *Do NOT use JUCE 9!* |
| **Audio Drivers** | Steinberg ASIO SDK + WASAPI + DirectSound |
| **Primary Solution** | `C:\davecore\Builds\VisualStudio2026\Fanfare.sln` |
| **Primary Project** | `C:\davecore\Builds\VisualStudio2026\Fanfare_App.vcxproj` |
| **Output Executable** | `C:\davecore\Builds\VisualStudio2026\x64\Release\App\Fanfare.exe` |
| **Active Git Branch** | `trunk` (`origin https://github.com/openrig-host/openrig.git`) |

---

## 2. Hard-Coded System Prerequisites

Before running any build, confirm these paths exist on the local `C:\` drive:

1. **JUCE 8.0.12 Modules**:
   - Path: `C:\JUCE\modules\juce_core\juce_core.h`
   - *Verification*: `Test-Path "C:\JUCE\modules\juce_core\juce_core.h"` (Must be `True`)
2. **Steinberg ASIO Headers**:
   - Path: `C:\JUCE\modules\juce_audio_devices\native\asio\asio.h`
   - *Verification*: `Test-Path "C:\JUCE\modules\juce_audio_devices\native\asio\asio.h"` (Must be `True`)
3. **Local Staging Folder**:
   - Path: `C:\temp\` (Used to stage the `.exe` so rebuilds never get locked by a running instance)

---

## 3. Build Instructions (Exact MSBuild Commands)

Always use PowerShell to execute MSBuild.

### A. Fast Incremental Build (1-3 Seconds)
Use this command for standard development and code iterations:

```powershell
# Visual Studio 2026 (v145 toolset) -- DEFAULT
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\davecore\Builds\VisualStudio2026\Fanfare.sln" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 -maxCpuCount

# Visual Studio 2022 (v143 toolset fallback)
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\davecore\Builds\VisualStudio2026\Fanfare.sln" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 -maxCpuCount
```

### B. Clean Rebuild (`/t:Rebuild`)
Use this when modifying header inclusions, Projucer definitions, or after Git branch switching:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "C:\davecore\Builds\VisualStudio2026\Fanfare.sln" /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 -maxCpuCount
```

### C. Web Companion Asset Regeneration
If you modify static files in `C:\davecore\Source\Web\static\` (`app.js`, `index.html`, `style.css`), regenerate the embedded C++ byte array header:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\davecore\Source\Web\generate_assets.ps1"
```

---

## 4. Safe Run & Testing Workflow

To prevent binary file locking while testing and iterating:

```powershell
# 1. Terminate any active Fanfare instance
Stop-Process -Name "Fanfare" -Force -ErrorAction SilentlyContinue

# 2. Stage binary to local temp directory
Copy-Item "C:\davecore\Builds\VisualStudio2026\x64\Release\App\Fanfare.exe" "C:\temp\Fanfare.exe" -Force

# 3. Launch staged binary
Start-Process "C:\temp\Fanfare.exe"
```

- **Web Companion UI**: Open a browser to `http://localhost:8080/` while Fanfare is running.

---

## 5. Debugging & Crash Diagnostics

Fanfare contains an emergency SEH (Structured Exception Handling) crash handler, a real-time lock-free flight recorder, and off-thread file logging.

### A. File Locations

| Diagnostic Item | Location | Purpose |
| :--- | :--- | :--- |
| **Runtime Log** | `%APPDATA%\Fanfare\OpenRig_log.txt` | Chronological app logs, plugin scan traces, slot load traces, MIDI events. |
| **Rotated Log** | `%APPDATA%\Fanfare\OpenRig_log.bak.txt` | Backup log automatically rotated when `OpenRig_log.txt` exceeds 10 MB. |
| **Crash Reports (Text)** | `%APPDATA%\Fanfare\Crashes\Fanfare_*.txt` | Heap-free text crash reports containing fault code, faulting module, address, 32-frame stack backtrace, flight recorder events, and RAM stats. |
| **Crash Dumps (Minidump)** | `%APPDATA%\Fanfare\Crashes\Fanfare_*.dmp` | Full Win32 minidumps for opening in Visual Studio or WinDbg with symbol inspection. |
| **Audio Underrun Log** | `%APPDATA%\Fanfare\audio_underrun_log.txt` | Telemetry for buffer underruns, ASIO dropouts, and audio device hiccups. |
| **Plugin Cache** | `%APPDATA%\Fanfare\known_plugins.json` | Scanned VST3 plugins and state cache. |
| **Database** | `%APPDATA%\Fanfare\fanfare_library.db` | SQLite library database (strips, songs, presets). |
| **Audio Config** | `%APPDATA%\Fanfare\audio_settings.xml` | Last active audio device, sample rate, buffer size. |

> **Warning: two app-data roots exist -- always read `Fanfare\`, never `OpenRig\`.**
>
> `%APPDATA%\Fanfare\` is the **current** root. A legacy `%APPDATA%\OpenRig\` root from
> earlier builds may still be present, and it is a trap: it contains a **stale file with
> the identical name** `OpenRig_log.txt` (only ~22 KB, last written 2026-09-19) alongside
> old `OpenRig_*.dmp` dumps. Reading that root makes a healthy, chatty app look completely
> silent. Verify freshness before drawing conclusions:
>
> ```powershell
> Get-Item "$env:APPDATA\Fanfare\OpenRig_log.txt" | Select-Object LastWriteTime, Length
> ```
>
> Note the rename legacy: the log *file* is still called `OpenRig_log.txt` and crash dumps
> are prefixed `Fanfare_`, but the *directory* is `Fanfare`. The name is a leftover from
> the DaveCore -> Fanfare -> OpenRig rename; the code is the source of truth
> (`Source/Logger.h`).

### B. Quick PowerShell Diagnostic Commands for LLMs

#### 1. Inspect the Latest Crash Report
```powershell
Get-ChildItem "$env:APPDATA\Fanfare\Crashes\*.txt" | Sort-Object LastWriteTime -Descending | Select-Object -First 1 | Get-Content
```

#### 2. Tail the Live Runtime Log
```powershell
Get-Content "$env:APPDATA\Fanfare\OpenRig_log.txt" -Tail 50
```

#### 3. Search Runtime Log for Errors and Warnings
```powershell
Select-String -Path "$env:APPDATA\Fanfare\OpenRig_log.txt" -Pattern "\[ERROR\]|\[WARN\]|Fault|Exception" | Select-Object -Last 30
```

#### 4. Check for Audio Underruns
```powershell
if (Test-Path "$env:APPDATA\Fanfare\audio_underrun_log.txt") {
    Get-Content "$env:APPDATA\Fanfare\audio_underrun_log.txt" -Tail 20
}
```

### C. Interpreting Crash Reports
When reading a `Fanfare_*.txt` report:
1. **Fault Code**: e.g., `EXCEPTION_ACCESS_VIOLATION (0xC0000005)` indicates null pointer or bad memory dereference.
2. **Faulting Module**: Check whether the fault occurred inside `Fanfare.exe` or a third-party plugin (e.g. `MixBox.vst3`, `Kontakt.vst3`).
3. **Faulting Thread ID & Role**: Tells you if the crash occurred on the `Audio Callback Thread`, `Message/UI Thread`, or `Worker/Other Thread`. Crashes on the audio thread are critical real-time safety violations.
4. **Flight Recorder**: Shows the last 256 real-time events immediately preceding the crash (e.g., slot activations, preset loads, CC changes).

---

## 6. Architecture & Codebase Map

| File / Directory | Description |
| :--- | :--- |
| [`Source/Main.cpp`](file:///C:/davecore/Source/Main.cpp) | Application entry point, crash handler hookup, window lifecycle. |
| [`Source/MainComponent.cpp`](file:///C:/davecore/Source/MainComponent.cpp) | Master UI container, top toolbar, master VU meters, tab management. |
| [`Source/FanfareEngine.h`](file:///C:/davecore/Source/FanfareEngine.h) | Central audio processing engine, lock-free parallel work-sharing DSP pool, master audio graph, slot routing. |
| [`Source/EngineTuningModal.h`](file:///C:/davecore/Source/EngineTuningModal.h) | In-app Engine & CPU Optimization modal (Hybrid P-Core/E-Core affinity, Core 0 DPC shielding, EcoQoS, Parallel/Sequential toggle, AC power stage advisory). |
| [`Source/RackSlotComponent.cpp`](file:///C:/davecore/Source/RackSlotComponent.cpp) | Individual rack slot UI, volume/pan faders, bypass, plugin editor launcher. |
| [`Source/RackSlot.h`](file:///C:/davecore/Source/RackSlot.h) | Slot data structure, plugin instance hosting, inline DSP chain. |
| [`Source/RigSerializer.cpp`](file:///C:/davecore/Source/RigSerializer.cpp) | JSON serialization/deserialization for rigs, strips, and scenes. |
| [`Source/Mp3PlayerProcessor.h`](file:///C:/davecore/Source/Mp3PlayerProcessor.h) | DJ & Break engine, WSOLA key-lock, audio playback decks. |
| [`Source/GigNotepadComponent.h`](file:///C:/davecore/Source/GigNotepadComponent.h) | Stage teleprompter, chord tokenizer, lyrics viewer. |
| [`Source/Web/OpenRigWebServer.cpp`](file:///C:/davecore/Source/Web/OpenRigWebServer.cpp) | Built-in HTTP and WebSocket server (port 8080) for remote companion tablet control. |
| [`Source/Logger.h`](file:///C:/davecore/Source/Logger.h) | Multi-tier logging, SEH exception handler, lock-free flight recorder, minidump generator. |
| [`Builds/VisualStudio2026/x64/Release/App/Web/`](file:///C:/davecore/Builds/VisualStudio2026/x64/Release/App/Web/) | Static files served by the embedded web server at runtime. |

---

## 7. Critical Safety & Hygiene Rules for LLMs

1. **NEVER downgrade or replace JUCE with JUCE 9**:
   - JUCE 9 will break Projucer compatibility and missing `JUCE_WEBVIEW_INTEROP_LIBRARY_VERSION`.
2. **DO NOT run MSBuild while `Fanfare.exe` is running**:
   - Windows locks open executables. Kill running processes first (`Stop-Process -Name Fanfare -Force`).
3. **DO NOT delete `Builds\VisualStudio2026\x64\Release\App\Web\`**:
   - The executable serves this folder at runtime if disk files are preferred over compiled-in fallback.
4. **DO NOT add hardcoded `Z:\` paths in new code**:
   - Use relative paths or dynamic paths (`juce::File::getSpecialLocation(...)`) so code runs seamlessly both on `C:\davecore` (offline) and `Z:\davecore` (home NAS).
5. **Synchronizing with NAS (`Z:\`) when Returning Home**:
   - Commit changes via Git (`git commit`) or use robocopy:
     ```powershell
     robocopy "C:\davecore\Source" "Z:\davecore\Source" /E
     ```


---

## 8. Real-Time Audio & Hybrid CPU Invariants (v1.2+)

Never break these invariants when modifying `FanfareEngine.h`, `MainComponent.cpp`, or `RackSlot.h`:

1. **NEVER enroll DSP worker threads in MMCSS (`FanfareConstants::kMmcssEnabled` MUST stay `false`)**:
   - Enrolling worker threads in `"Pro Audio"` MMCSS triggers Windows' 80% MMCSS CPU quota throttle across shared cores, dropping real-time threads to low priority and causing 200+ underrun storms.
2. **Preserve 3-Tier Priority Separation (`ABOVE_NORMAL_PRIORITY_CLASS` = 10, DSP Helpers = 15, ASIO = 24)**:
   - Keep the process in `ABOVE_NORMAL_PRIORITY_CLASS` (with restoration guards in `audioDeviceAboutToStart` and `timerCallback`, since ASIO drivers like `ysusb_asio64.dll` call `SetPriorityClass(HIGH_PRIORITY_CLASS)` on open). In `HIGH_PRIORITY_CLASS` (Base 13), dynamic +2 wake boosts push internal Kontakt/Roland threads to Priority 15, tying and preempting DSP helpers.
3. **Preserve Split Hybrid Core Affinity (`queryCpuTopology` + `applyEngineTuningSettings`)**:
   - **Process mask** (`SetProcessAffinityMask`): shelters only `CPU 0` (`0xFFE`), keeping Efficiency cores open for OS/driver/plugin background threads.
   - **Real-time ASIO + DSP helper mask** (`SetThreadAffinityMask`): locks exclusively to sheltered P-cores (`0x00E`), with active helpers capped at `physicalPCores - 1` (`1` helper + ASIO thread = 1 worker per physical P-core).
   - **UI + `SetlistPreloaderThread` + `RigBuilder` mask**: steered to E-cores (`0xFF0`) so background VST3 builds never contend with live ASIO.
4. **Preserve Exact `numSamples` Logical Sizing on `RackSlot::scratchBuffer`**:
   - Pre-allocating `scratchBuffer` capacity (`32 x 8192`) in `RackSlot::prepare()` must immediately resize logical length back to `maxBlockSize` (`avoidReallocating = true`), and `RackSlot::processBlock()` must check `!= numSamp` so VST3 `processBlock()` never receives `numSamples > maxSamplesPerBlock`.
