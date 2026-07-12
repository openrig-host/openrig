# Building OpenRig from Source

OpenRig is a **JUCE 8 / C++17** standalone Windows app. Building it from source is not a one-command job because it depends on three things that aren't in this repo:

1. **Visual Studio 2022+** (the C++ compiler)
2. **JUCE 8** (the framework)
3. **The Steinberg ASIO SDK** (proprietary — required for low-latency ASIO audio)

The published binaries in [Releases](https://github.com/openrig-host/openrig/releases) ship with ASIO enabled. To reproduce that build yourself, follow every step below.

> **Why ASIO matters:** For live use, ASIO is the only way to get reliably low latency on Windows. WASAPI works as a fallback but is not recommended on stage. The ASIO SDK is free but Steinberg's license terms prevent it being bundled here or built on public CI — hence the manual setup.

---

## Step 1 — Install Visual Studio 2022 (or newer)

Download the **free Community Edition** from <https://visualstudio.microsoft.com/downloads/>.

During install, tick the **"Desktop development with C++"** workload. That gives you MSVC, the Windows SDK, and MSBuild. No other workloads are required.

(You do **not** need CMake — this project builds from the Projucer-generated Visual Studio solution.)

---

## Step 2 — Install JUCE 8

1. Download JUCE 8 from <https://juce.com/get-juce> (or clone <https://github.com/juce-framework/JUCE>).
2. Extract/clone it so that the modules live at exactly:

   ```
   C:\JUCE\modules\...
   ```

   This path is **hard-coded** into the committed Visual Studio project, so JUCE **must** live at `C:\JUCE`. If you put it somewhere else, the build won't find the JUCE headers.

Verify: `C:\JUCE\modules\juce_core\juce_core.h` should exist after this step.

---

## Step 3 — Get the Steinberg ASIO SDK

1. Go to <https://www.steinberg.net/developers/> and download the **ASIO SDK** (free, requires accepting Steinberg's license). It arrives as a zip containing `asio.h`, `asiosys.h`, `iasiodrv.h`, etc.
2. Extract the SDK's header files into this exact folder:

   ```
   C:\JUCE\modules\juce_audio_devices\native\asio\
   ```

   After this, the following must exist:

   ```
   C:\JUCE\modules\juce_audio_devices\native\asio\asio.h
   C:\JUCE\modules\juce_audio_devices\native\asio\asiosys.h
   C:\JUCE\modules\juce_audio_devices\native\asio\iasiodrv.h
   ```

   The project's `AppConfig.h` already sets `JUCE_ASIO 1`, so once the headers are in place, ASIO support is compiled in automatically.

> **Don't want ASIO?** You can skip this step and the project will still build, but the resulting `OpenRig.exe` will only offer WASAPI/DirectSound — no ASIO device type. Fine for testing, not for live use.

---

## Step 4 — Open and build the project

### Option A — Visual Studio (recommended for first-timers)

1. Open `DaveCoreProject\Builds\VisualStudio2026\DaveCore.sln` in Visual Studio.
2. At the top, set the configuration to **Release** and the platform to **x64**.
3. **Build → Build Solution** (Ctrl+Shift+B).
4. The output is `OpenRig.exe` in `DaveCoreProject\Builds\VisualStudio2026\x64\Release\App\`.

### Option B — Command line (MSBuild)

```powershell
& "D:\visual studio\MSBuild\Current\Bin\MSBuild.exe" `
    "DaveCoreProject\Builds\VisualStudio2026\DaveCore.sln" `
    -property:Configuration=Release -property:Platform=x64 -maxCpuCount
```

Adjust the MSBuild path to wherever your VS install put it. Output goes to the same `x64\Release\App\` folder.

---

## Step 5 — Run it

Run `OpenRig.exe` directly, or set it as the debug target in Visual Studio.

First launch: open the **Settings** (gear icon) → Audio Settings, pick your ASIO driver and VST3 plugin folder, then **Scan for Plugins**.

---

## Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| `Cannot open include file: 'asio.h'` | Step 3 incomplete — put the ASIO SDK headers in `C:\JUCE\modules\juce_audio_devices\native\asio\`. |
| `Cannot open include file: 'juce_core.h'` | JUCE not at `C:\JUCE` — move it there (Step 2). |
| Lots of `unresolved external symbol` | Wrong configuration — make sure you build **x64**, not Win32/x86. |
| `juce_ASIO_windows.cpp` errors | ASIO SDK version mismatch — use the SDK from Steinberg's current download, not an old copy. |
| Plugins don't appear | You need licensed VST3s installed in the standard `C:\Program Files\Common Files\VST3\`. See the [tested plugin list](README.md#tested-vst-compatibility). |

---

## Regenerating the Visual Studio project (optional)

If you change the JUCE version or module list, regenerate the VS solution with the **Projucer** (ships with JUCE at `C:\JUCE\extras\AudioPluginHost\...` or build `Projucer` from the JUCE repo):

1. Open `DaveCoreProject\DaveCore.jucer` in the Projucer.
2. Confirm the JUCE modules path points to `C:\JUCE`.
3. Click **Save Project and Open in IDE**.

This rewrites the `.sln`/`.vcxproj` to match your installed JUCE version. You only need to do this if you upgrade JUCE.

---

## What you end up with

A standalone `OpenRig.exe` — a single executable (icons are embedded; no external asset files needed). Copy it anywhere and run it. User data lives under `%APPDATA%\OpenRig\`.
