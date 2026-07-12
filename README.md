# OpenRig
**The Sovereign Live Performance Engine**

OpenRig is a high-performance, rack-based VST3 host built using the JUCE framework. It is designed to prioritize stability, ease of use, and deep MIDI integration for live keyboard performances.

## Core Philosophies
- **Non-Wiry UI**: Linear rack-based interface.
- **Dual-Bus Mixing**: Independent FOH and IEM mixes.
- **Robust Song Switching**: Background build + atomic swap with rollback-by-construction.
- **CC-Learn**: Arm-then-wiggle MIDI learn for plugin parameters and faders.
- **CK88 Integration**: Configurable MIDI channel routing for the Yamaha CK88.

## Architecture
- `DaveCoreProject/Source/`: C++ Engine and UI code (JUCE 8, C++17)
- `DaveCoreProject/Builds/VisualStudio2026/`: VS solution and build output
- `DaveCoreProject/Source/Resources/`: Icons, fonts, and assets

## Key Components
- **OpenRigEngine**: Audio processing, MIDI routing, plugin management, staging cache
- **RigTransitioner**: Async song transitions (background build → atomic swap → rollback)
- **RigBuilder**: Off-thread plugin instantiation + silent validation
- **RigSerializer**: Versioned JSON persistence with atomic writes and migration
- **MidiLearnBus**: Singleton CC-learn bus for arm-then-wiggle binding
- **CCMappingComponent**: CC assignment UI with learn, min/max range, conflict detection

## Data Storage
All data under `%APPDATA%/OpenRig/`: `songs/`, `sets/`, `backups/`, `settings/`. Atomic saves with .bak backups.

## Master Controller
Yamaha CK88. MIDI channel routing is configurable (global default + per-slot override).


Want a Windows binary? Skip over to https://github.com/openrig-host/openrig/releases
---
*Last updated: July 2026*
