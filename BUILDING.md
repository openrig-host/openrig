# Building Fanfare from Source

Fanfare is a **JUCE 8 / C++17** standalone Windows application. There are two
ways to build it: the **Projucer → Visual Studio** route (full-featured, ASIO
support) or the **CMake** route (no ASIO, CI-friendly). Both produce a single
standalone `Fanfare.exe`.

## Requirements

1. **Visual Studio 2022 or 2026** (Community is fine) with the
   **Desktop development with C++** workload. No other workloads needed.
2. **JUCE 8**, with the modules at exactly:

   ```
   C:\JUCE\modules\...
   ```

   Verify: `C:\JUCE\modules\juce_core\juce_core.h` exists.
   (CMake builds can use any location — pass `-DJUCE_ROOT=C:/path/to/JUCE`.)

3. **Steinberg ASIO SDK** *(Projucer route only, only if you want ASIO)*:
   free from <https://www.steinberg.net/developers/> — copy the headers from
   the SDK's `common` folder into:

   ```
   C:\JUCE\modules\juce_audio_devices\native\asio\
   ```

   (`asio.h`, `asiosys.h`, `iasiodrv.h` must exist there.) ASIO matters for
   live low-latency use; WASAPI works as a fallback.

## Option A — Visual Studio (recommended)

1. Open `Builds\VisualStudio2026\Fanfare.sln`.
2. Set configuration to **Release**, platform **x64**.
3. Build Solution (`Ctrl+Shift+B`).
4. Output: `Builds\VisualStudio2026\x64\Release\App\Fanfare.exe`.

Command line equivalent:

```powershell
msbuild "Builds\VisualStudio2026\Fanfare.sln" /p:Configuration=Release /p:Platform=x64 -maxCpuCount
```

On VS 2022, retarget the project to the v143 toolset if prompted
(VS 2026 uses v145).

> The committed `JuceLibraryCode\` wrapper and generated project already match
> the source — you only need the Projucer app if you change the module list
> or add source files (`Fanfare.jucer`).

## Option B — CMake

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -DJUCE_ROOT=C:/JUCE
cmake --build build --config Release
```

Output: `build\Fanfare_artefacts\Release\Fanfare.exe`.
**Note:** the CMake path has ASIO disabled (the Steinberg SDK can't be
distributed or fetched by CI). WASAPI/DirectSound cover a release build.

## First launch

Open **Settings** (gear icon) → Audio Settings, pick your ASIO/WASAPI device,
set sample rate and buffer size, then scan your VST3 plugin folder
(`C:\Program Files\Common Files\VST3`). User data lives under
`%APPDATA%\Fanfare\`.

## Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| `Cannot open include file: 'asio.h'` | ASIO SDK headers missing — see requirement 3, or build via CMake (no ASIO). |
| `Cannot open include file: 'juce_core.h'` | JUCE not at `C:\JUCE` — see requirement 2. |
| `unresolved external symbol` | Build **x64**, not Win32. |
| `MSB8020: build tools for v145 cannot be found` | Building on VS 2022 — retarget to v143. |
| `LNK1104` / `LNK1106` writing the exe | The app is still running — close it, delete the exe, rebuild. |
| Plugins don't appear | Scan `C:\Program Files\Common Files\VST3` in Settings. |
