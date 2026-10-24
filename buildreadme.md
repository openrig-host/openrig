# Fanfare (DaveCore) -- Agent & LLM Build Instructions

> **NOTICE FOR AI AGENTS / LLMs**:  
> Read this document completely before attempting to modify or build this codebase. It provides exact, tested commands, absolute path requirements, and common pitfalls to prevent build failures.

---

## 1. System & Architecture Overview

- **Project Root**: `Z:\davecore\` (mapped from NAS `\\MYCLOUDEX2ULTRA\dwaugh\Antigravity`).
- **Target Platform**: Windows 10/11 64-bit (`x64` only, 32-bit `Win32` is unsupported).
- **Language / Framework**: C++17, **JUCE 8.0.12** (`0x8000c`), Steinberg ASIO SDK.
- **Active IDE / Solution**: Visual Studio 2026 (`v145` toolset) or Visual Studio 2022 (`v143` toolset).
- **Solution File**: `Z:\davecore\Builds\VisualStudio2026\Fanfare.sln`
- **Primary Project**: `Z:\davecore\Builds\VisualStudio2026\Fanfare_App.vcxproj`
- **Output Executable**: `Z:\davecore\Builds\VisualStudio2026\x64\Release\App\Fanfare.exe`
- **Cloud Distribution Directory**: `E:\My Drive\Fanfare - Latest versions` (Mandatory deployment destination for newest builds)

---

## 2. Hard-Coded Local Environment Prerequisites

The Visual Studio project files contain hard-coded absolute include and source paths to the local `C:\` drive. **You must verify these exist before running MSBuild.**

### A. JUCE 8 Framework (`C:\JUCE\modules`)
- **Required Version**: **JUCE 8.0.12** (Git tag `8.0.12` / commit `29396c22`).
- **CRITICAL LLM GOTCHA**: Do **NOT** use JUCE 9. Using JUCE 9 causes `#error "This project was last saved using an outdated version of the Projucer!"` and missing `JUCE_WEBVIEW_INTEROP_LIBRARY_VERSION`.
- **Directory Nesting Gotcha**: Ensure modules are located directly at `C:\JUCE\modules`, **NOT** `C:\JUCE\JUCE\modules`.
- **Verification Command**:
  ```powershell
  Test-Path "C:\JUCE\modules\juce_core\juce_core.h" # MUST RETURN TRUE
  ```

### B. Steinberg ASIO SDK Headers
- The ASIO headers must be installed inside the JUCE audio devices module:
  - `C:\JUCE\modules\juce_audio_devices\native\asio\asio.h`
  - `C:\JUCE\modules\juce_audio_devices\native\asio\asiosys.h`
  - `C:\JUCE\modules\juce_audio_devices\native\asio\iasiodrv.h`
- **Verification Command**:
  ```powershell
  Test-Path "C:\JUCE\modules\juce_audio_devices\native\asio\asio.h" # MUST RETURN TRUE
  ```

### C. Local Temporary Run Folder
- To prevent SMB network file locking, the binary must run locally:
  - `C:\temp\` (Create with `New-Item -ItemType Directory -Path "C:\temp" -Force` if missing).

---

## 3. MSBuild Execution Instructions

Always run MSBuild using PowerShell. Locate MSBuild using Visual Studio Installer (`vswhere.exe`) or the standard VS path.

### Option 1: Incremental Build (Fastest -- 2 to 5 seconds)
Use this when iterating on a few source files:
```powershell
# For Visual Studio 2026 (v145 toolset):
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "Z:\davecore\Builds\VisualStudio2026\Fanfare.sln" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 -maxCpuCount

# For Visual Studio 2022 (v143 toolset):
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "Z:\davecore\Builds\VisualStudio2026\Fanfare.sln" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 -maxCpuCount
```

### Option 2: Full Clean Rebuild from Scratch (`/t:Rebuild`)
Use this to verify project integrity or after altering module configurations:
```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "Z:\davecore\Builds\VisualStudio2026\Fanfare.sln" /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 -maxCpuCount
```

---

## 4. Running & Testing Instructions

> [!CAUTION]
> **NEVER execute `Fanfare.exe` directly from the `Z:` network drive during active development.**  
> Running the binary locks the file over the SMB network share. Subsequent MSBuild attempts will fail with:
> `LNK1106: invalid file or disk full: cannot seek`

### Safe Run Workflow:
```powershell
# 1. Ensure any previous instance is terminated
Stop-Process -Name "Fanfare" -Force -ErrorAction SilentlyContinue

# 2. Copy fresh binary to local temp folder
Copy-Item "Z:\davecore\Builds\VisualStudio2026\x64\Release\App\Fanfare.exe" "C:\temp\Fanfare.exe" -Force

# 3. Launch process locally
Start-Process "C:\temp\Fanfare.exe"
```

---

## 5. Directory Hygiene & Safety Rules for LLMs

- **DO NOT DELETE**:
  - `Source\` (Active application code)
  - `JuceLibraryCode\` (Active JUCE glue headers)
  - `Fanfare.jucer` (Active Projucer configuration)
  - `CMakeLists.txt`
  - `Builds\VisualStudio2026\Fanfare.sln` and `Fanfare_App.vcxproj*`
  - `Builds\VisualStudio2026\x64\Release\App\Web\` (Static HTML/JS/CSS served by the built-in HTTP server)
  - `Golden_Aug30\` (Aug 30 golden reference snapshot)
  - `_archive\` (Historical test builds and zip backups)
- **DO NOT pollute the root directory**:
  - Never dump `.exe`, `.zip`, `.dmp`, or `.txt` debug logs into `Z:\davecore\`.
  - Place any versioned binaries or backups in `Z:\davecore\_archive\`.

---

## 6. Cloud Distribution Requirement (Google Drive)

- **Target Path**: `E:\My Drive\Fanfare - Latest versions\`
- **Requirement**: Every successful build must produce a copy of the newest executable (`Fanfare.exe`) and debug symbols (`Fanfare.pdb`) in this Google Drive folder so it automatically syncs and is immediately available to secondary host machines.
- **Automation**: Configured directly in `Fanfare_App.vcxproj` and `Fanfare.jucer` via Post-Build Events (and `CMakeLists.txt` via post-build custom command). If building manually or from other scripts, ensure `Fanfare.exe` is copied to this location.

---

## 7. LLM Troubleshooting Diagnostic Table

| Error / Symptom | Root Cause | Solution for LLM |
| :--- | :--- | :--- |
| `fatal error C1083: Cannot open include file: 'asio.h'` | ASIO SDK headers missing from local JUCE directory. | Copy Steinberg ASIO headers to `C:\JUCE\modules\juce_audio_devices\native\asio\`. |
| `fatal error C1083: Cannot open include file: 'juce_core/juce_core.h'` | JUCE 8 modules are missing or nested at `C:\JUCE\JUCE\modules`. | Ensure modules reside directly at `C:\JUCE\modules`. |
| `error C1189: #error: "This project was last saved using an outdated version of the Projucer!"` | JUCE 9 is installed in `C:\JUCE` instead of JUCE 8. | Clone git tag `8.0.12` from `https://github.com/juce-framework/JUCE.git` into `C:\JUCE`. |
| `error C1189: #error: "Missing JUCE_WEBVIEW_INTEROP_LIBRARY_VERSION definition"` | JUCE 9 was included. | Downgrade `C:\JUCE` to JUCE 8.0.12. |
| `LNK1106: invalid file or disk full: cannot seek` | `Fanfare.exe` is currently running over SMB or locked. | Run `Stop-Process -Name Fanfare -Force` and delete the locked `.exe` before rebuilding. |
| `warning MSB8028: The intermediate directory contains files shared from another project` | Leftover `.pdb` or `.iobj` files from `DaveCore_App` or `OpenRig_App`. | Remove stale `DaveCore.*` and `OpenRig.*` files from `Builds\VisualStudio2026\x64\Release\App\`. |
| `MSB8020: The build tools for v145 cannot be found` | Running on a VS 2022 machine without VS 2026 tools. | Append `/p:PlatformToolset=v143` to the MSBuild invocation. |
