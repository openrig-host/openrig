# How to Compile and Run Fanfare / DaveCore from a Network Host

This document explains the required environment, software, libraries, and steps necessary to compile and run Fanfare (DaveCore) on a secondary Windows host computer while accessing project files stored on the NAS (`Z:\davecore`).

---

## 1. Environment Architecture

- **Project Storage**: Network Attached Storage (NAS) mapped to local drive letter `Z:\davecore`.
- **Framework & Language**: JUCE 8, C++17, MSVC (v143 for VS 2022 or v145 for VS 2026).
- **Target Platform**: Windows 10/11 64-bit (`x64`).
- **Target Audio Subsystem**: Steinberg ASIO (low latency).

---

## 2. Required Applications

Install the following software on the host computer:

### A. Visual Studio 2022 or Visual Studio 2026
1. Download **Visual Studio Community** (free for individual developers) from:
   https://visualstudio.microsoft.com/downloads/
2. In the Visual Studio Installer, select the workload:
   - **Desktop development with C++**
3. Verify that the following optional components are checked within the workload:
   - MSVC v143 (VS 2022) or MSVC v145 (VS 2026) C++ x64/x86 build tools
   - Windows 10 or Windows 11 SDK (latest)
   - C++ CMake tools for Windows (optional)
   - C++ Core Desktop features

### B. Git for Windows
Download and install Git from https://git-scm.com/ to manage code revisions and sync with remote repositories.

### C. Audio Drivers (for Testing & Live Playback)
- For live hardware: Install the official manufacturer ASIO driver for your audio interface (e.g., Focusrite, MOTU, RME).
- For laptops/office testing without dedicated hardware: Install **ASIO4ALL** (https://asio4all.org/) or **FlexASIO** (https://github.com/dechamps/FlexASIO).

---

## 3. Required Local Libraries and SDKs

The Visual Studio project configuration contains hard-coded include paths referencing local drive `C:\`. The following libraries must be installed at exact locations on the host computer:

### A. JUCE 8 Framework
1. Download JUCE 8 from https://juce.com/get-juce or clone from https://github.com/juce-framework/JUCE.
2. Place the directory so that JUCE modules exist at:
   ```text
   C:\JUCE\modules
   ```
3. Verification: Ensure `C:\JUCE\modules\juce_core\juce_core.h` is present.

### B. Steinberg ASIO SDK
The application compiles with `JUCE_ASIO=1` for low-latency live performance. The ASIO SDK headers must be copied into the JUCE folder structure.

1. Download the ASIO SDK from Steinberg:
   https://www.steinberg.net/developers/
2. Extract the SDK archive.
3. Copy the header files from the SDK's `common` directory into:
   ```text
   C:\JUCE\modules\juce_audio_devices\native\asio\
   ```
4. Verification: The following files must exist:
   - `C:\JUCE\modules\juce_audio_devices\native\asio\asio.h`
   - `C:\JUCE\modules\juce_audio_devices\native\asio\asiosys.h`
   - `C:\JUCE\modules\juce_audio_devices\native\asio\iasiodrv.h`

---

## 4. Network Drive Setup

Map the NAS network share to drive letter `Z:` so the path resolves identically across machines:

1. Open File Explorer -> **This PC** -> **Map network drive**.
2. Select Drive: `Z:`
3. Folder: `\\<NAS-IP-OR-NAME>\<ShareName>` (pointing to the root containing `davecore`).
4. Ensure the root path resolves to `Z:\davecore`.

---

## 5. Compiling the Application

### Option A: Building via Visual Studio GUI

1. Open the solution file in Visual Studio:
   ```text
   Z:\davecore\Builds\VisualStudio2026\Fanfare.sln
   ```
2. In the top toolbar:
   - Set Configuration to **Release**.
   - Set Platform to **x64**.
3. **Toolset Check (VS 2022 vs VS 2026)**:
   - If using Visual Studio 2022, the project may prompt to retarget, or you can manually right-click `Fanfare_App` in the Solution Explorer -> **Properties** -> **Configuration Properties** -> **General** -> **Platform Toolset** -> Set to **Visual Studio 2022 (v143)**.
   - If using Visual Studio 2026, keep the toolset at **v145**.
4. Go to **Build** -> **Build Solution** (or press `Ctrl+Shift+B`).
5. Output executable will be produced at:
   ```text
   Z:\davecore\Builds\VisualStudio2026\x64\Release\App\Fanfare.exe
   ```
   *Note: Post-build automatically publishes newest binaries and symbols to `E:\My Drive\Fanfare - Latest versions\` for distribution to other machines.*

### Option B: Building via Command Line (MSBuild)

Open Developer PowerShell or Developer Command Prompt for Visual Studio and run:

```powershell
# For Visual Studio 2022 (v143 toolset):
msbuild "Z:\davecore\Builds\VisualStudio2026\Fanfare.sln" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 -maxCpuCount

# For Visual Studio 2026 (v145 toolset):
msbuild "Z:\davecore\Builds\VisualStudio2026\Fanfare.sln" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 -maxCpuCount
```

---

## 6. Running and Testing Safely

1. **Avoid Running Directly Over Network**:
   To prevent SMB locking issues while you are actively compiling and iterating, copy the compiled binary to a local temporary folder before launching:
   ```powershell
   Copy-Item "Z:\davecore\Builds\VisualStudio2026\x64\Release\App\Fanfare.exe" "C:\temp\Fanfare.exe"
   Start-Process "C:\temp\Fanfare.exe"
   ```
2. **First Run Configuration**:
   - Open **Settings** (gear icon) in Fanfare.
   - Select **Audio Settings** and pick your ASIO device.
   - Set sample rate to 44.1 kHz or 48 kHz, buffer size to 128 or 256 samples.
   - Under **Plugin Scan**, ensure the VST3 folder is scanned (`C:\Program Files\Common Files\VST3`).

---

## 7. Troubleshooting Build Issues

| Issue | Root Cause | Solution |
| :--- | :--- | :--- |
| `fatal error C1083: Cannot open include file: 'asio.h'` | ASIO SDK headers are missing from local JUCE directory. | Place `asio.h` and associated headers in `C:\JUCE\modules\juce_audio_devices\native\asio\`. |
| `fatal error C1083: Cannot open include file: 'juce_core/juce_core.h'` | JUCE is missing or located in a non-standard path. | Move or install JUCE 8 modules to `C:\JUCE\modules`. |
| `LNK1106: invalid file or disk full: cannot seek` | SMB network file lock or file cache conflict when overwriting existing `.exe`. | Ensure `Fanfare.exe` is closed. Delete `Z:\davecore\Builds\VisualStudio2026\x64\Release\App\Fanfare.exe` and re-run build. |
| `MSB8020: The build tools for v145 cannot be found` | Building on VS 2022 machine without the newer v145 toolset. | In Project Properties, change Platform Toolset to `v143`, or pass `/p:PlatformToolset=v143` to MSBuild. |
| Unresolved external symbols on link | Architecture mismatch. | Ensure the target platform is set to **x64** (32-bit `Win32` is unsupported). |
