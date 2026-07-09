# Color & Theme Overhaul Plan

*Current state: the entire UI is black-on-dark-grey. Every accent is a "dark" named constant (`darkgreen`, `darkred`, `darkgrey`...). It looks vibecoded because it is. Goal: a real, selectable theme system with at least one proper light theme.*

---

## 1. The problem

### What's there now
- **A theme system that doesn't run.** `BoutiqueLookAndFeel.h` defines a `Scheme` struct with three palettes (Midnight, Amber, Crimson) and a `setScheme()` method. **Nothing calls it.** `getActiveScheme()` is never read outside the file. The three themes are dead code.
- **~200 hardcoded color literals** scattered across 15+ files. Every component paints its own `Colour(0xff121315)` / `Colours::darkgreen` directly in `paint()`. There is no single source of truth.
- **All three "themes" are dark.** Even if the switcher worked, there's no light option. The named constants used everywhere (`darkgreen`, `darkred`, `darkgrey`, `darkorange`, `darkblue`, `darkviolet`, `darkslategrey`, `darkcyan`, `darkmagenta`) are uniformly muted — the textbook AI-generated palette.
- **`useModernStyle` doubles the surface.** A boolean flag in `BoutiqueLookAndFeel` creates two complete rendering paths (flat vs skeuomorphic) with separate color literals in each branch. Maintaining both against a theme is twice the work.

### Files with hardcoded colors (the sweep list)
| File | Severity | Notes |
|---|---|---|
| `RackSlotComponent.cpp` | **Heavy** | Category colors, meters, button-by-purpose colors, a 9-color user palette |
| `MainComponent.cpp` | **Heavy** | Header, transport, scene buttons, setlist buttons, every button gets a hand-set colour |
| `BoutiqueLookAndFeel.h/.cpp` | **Heavy** | Slider thumbs, tracks, ticks — many literals even though it owns the (dead) Scheme |
| `CCMappingComponent.h` | Medium | Invert/passthrough buttons, row selection, meter |
| `SetupBuilderOverlay.h` | Medium | Overlay scrim, list, inputs |
| `MidiMonitorComponent.h` | Medium | Per-event colors (note/CC/PC/arm) |
| `ChannelStripComponent.h` | Medium | Strip background |
| `NoteRangeComponent.h` | Light | Two literals (modern path) |
| `ArpeggiatorComponent.h` | Light | Two literals |
| `SamplerComponent.h` | Light | One literal |
| `WaveformSpliceEditor.h` | Light | One literal |
| `MidiEffectsComponent.h` | Light | Overlay scrim |
| `LoadingOverlay.h` | Light | One literal |
| `SetlistPanel.h` | Light | Active-row highlight |

**~200 literals total.** The two `.cpp` files alone account for over half.

---

## 2. The goal

A **role-based theme system** where:
1. One `Theme` struct holds every color the app uses, by *semantic role* (not by widget).
2. Components read from the active theme, never hardcode.
3. At least **5 themes ship**, including **one proper light theme** and **one high-contrast accessibility theme**.
4. The choice is selectable in the config overlay and persisted to settings.
5. Switching themes repaints the whole UI with zero restart.

---

## 3. Proposed theme collection

Each theme is a full palette. The "core" colors below; semantic status/meter/category roles derive from these in the implementation.

### Core palette (5 themes × 8 anchors)

| Role | Stage Black | Daylight | Amber Console | Sapphire | High Contrast |
|---|---|---|---|---|---|
| **bg** | `#0E1116` | `#EEF1F5` | `#1A1510` | `#0F1419` | `#000000` |
| **panel** | `#171B22` | `#FFFFFF` | `#261F17` | `#161D26` | `#0A0A0A` |
| **panelAlt** | `#1E232C` | `#E6EAEF` | `#2E261C` | `#1E2632` | `#141414` |
| **border** | `#2C333D` | `#C9D2DC` | `#4A3D2C` | `#2A3441` | `#FFFFFF` |
| **text** | `#F2F5F9` | `#1A2330` | `#F5E6D3` | `#E8EDF2` | `#FFFFFF` |
| **textDim** | `#94A1B0` | `#5C6B7E` | `#B09A7C` | `#8A98A8` | `#BFBFBF` |
| **accent** | `#00E5FF` | `#0091EA` | `#FFB300` | `#4FACFE` | `#FFFF00` |
| **foh / iem** | `#00E676` / `#29B6F6` | `#00897B` / `#1565C0` | `#FF8F00` / `#FF6E40` | `#2ECC71` / `#5B8DEF` | `#00FF00` / `#00BFFF` |

**Theme rationale:**
- **Stage Black** — the refined default. Keeps the current cyan accent (it works), but replaces flat-black with a bluer near-black for depth.
- **Daylight** — *the anti-vibecode theme.* Soft off-white background (not pure white — reduces glare), deeper accents tuned for contrast on light. For rehearsal rooms, daytime, and screenshots that don't look AI-generated.
- **Amber Console** — vintage hardware aesthetic. Warm browns + amber LED accent. Reads as "expensive outboard gear," not "AI dark mode."
- **Sapphire** — professional blue, the Logic/Ableton register. Calm, neutral, presentable.
- **High Contrast** — accessibility/outdoor. Pure black + white + yellow. Maximal visibility on a stage display in daylight.

### Semantic roles (the full set the Theme struct must hold)
```
CORE:        bg, panel, panelAlt, raised, border, borderStrong,
             text, textDim, textFaint, textOnAccent
ACCENT:      accent, accentDim, foh, iem
STATUS:      ok, warn, danger, panic, active, bypassed
METER:       meterLow, meterMid, meterPeak
CATEGORY:    catMonitor, catKeyboard, catAccordion, catReturn, catDefault
MIDI MON:    midiNote, midiCC, midiPC, midiArm, midiOther
MISC:        scrim, knobFace, trackGroove, knobThumb
STYLE:       flat (bool) — replaces useModernStyle
```
~30 roles. This is the granularity needed so no component ever reaches for a raw literal again.

---

## 4. Architecture

### New types

```cpp
// Theme.h (new file)
struct Theme {
    juce::String id, name;
    bool flat = true;                 // replaces useModernStyle
    // ... all ~30 Colour fields by role ...
};

class ThemeManager : public juce::ChangeBroadcaster {
public:
    static ThemeManager& getInstance();
    const Theme& current() const;
    void setTheme(const juce::String& id);     // broadcasts change
    juce::StringArray getThemeNames() const;
    void loadFromSettings();                    // on startup
    void saveToSettings();
private:
    std::vector<Theme> themes;                  // the collection
    int currentIndex = 0;
};
```

### How components read colors
Replace every `Colour(0x...)` / `Colours::darkgreen` with a lookup:
```cpp
g.setColour(ThemeManager::getInstance().current().meterPeak);
```
Or, for LookAndFeel-owned widgets, keep using the JUCE colour-ID system but have `setTheme()` push all ~30 roles into the relevant colour IDs in one pass.

### useModernStyle resolution
- Add `bool flat` to `Theme`.
- `BoutiqueLookAndFeel::useModernStyle` reads from `current().flat` instead of being a standalone flag.
- The two render branches stay (they're legitimately different aesthetics) but are now theme-driven, not a global toggle.

### Persistence
- Save the theme `id` to `%APPDATA%/OpenRig/settings/theme.json`.
- Load on startup in `MainComponent` constructor, before any component paints.

### Theme picker UI
- Add to `ConfigModalOverlay` (the existing settings overlay): a `ComboBox` of theme names + a live preview.
- On change → `ThemeManager::setTheme(id)` → repaint all top-level components.

---

## 5. Migration phases

Do this incrementally; each phase compiles and runs on its own.

### Phase 0 — Infrastructure (no visual change)
- [ ] Create `Theme.h` with the ~30-role struct + 5 filled palettes.
- [ ] Create `ThemeManager` singleton (broadcasts, load/save).
- [ ] Seed `ThemeManager` from the existing dead `Scheme` colors so the default looks identical.
- **Checkpoint:** app unchanged, but `ThemeManager::getInstance().current()` works.

### Phase 1 — Wire the LookAndFeel
- [ ] `BoutiqueLookAndFeel` reads `ThemeManager::current()` for thumb/track/tick/button colours instead of literals.
- [ ] `useModernStyle` → delegates to `current().flat`.
- [ ] Delete the dead `Scheme` struct, `getActiveScheme`, `setScheme`, `getSchemeNames`.
- **Checkpoint:** sliders/buttons/buttons now theme-aware; everything else still literal.

### Phase 2 — Component sweep (the bulk)
Replace literals file-by-file, easiest first (verifies the pattern) then the two heavy `.cpp` files:
- [ ] Light files: `LoadingOverlay`, `WaveformSpliceEditor`, `SamplerComponent`, `ArpeggiatorComponent`, `NoteRangeComponent`, `MidiEffectsComponent`, `SetlistPanel`
- [ ] Medium: `MidiMonitorComponent` (per-event colours → `midiNote`/`midiCC`/...), `SetupBuilderOverlay`, `ChannelStripComponent`, `CCMappingComponent`
- [ ] **Heavy: `MainComponent.cpp`** — header bg, every button colour, transport, scene/setlist buttons
- [ ] **Heavy: `RackSlotComponent.cpp`** — category colours, meters, status LEDs
- **Checkpoint:** zero `Colour(0x...)` or `Colours::dark*` literals outside the `Theme` definition. `grep` proves it.

### Phase 3 — Theme picker + persistence
- [ ] Add theme `ComboBox` to `ConfigModalOverlay` with live preview.
- [ ] Wire `ThemeManager::loadFromSettings()` at startup.
- [ ] On theme change, repaint all open overlays + main window.
- **Checkpoint:** user can switch between 5 themes live.

### Phase 4 — Polish & validation
- [ ] Audit each theme on actual screenshots (light theme is the riskiest — check every overlay, every meter, every selected-row state).
- [ ] Ensure the user-facing **custom slot color palette** (`RackSlotComponent.cpp:1030`) still works — those are user-chosen slot colours, not theme colours; keep as data, just restyle the swatch buttons to the theme.
- [ ] Verify `findColour(...)` callers still get correct values (theme → colour-ID mapping in LookAndFeel).
- [ ] DPI pass (themes should look right scaled, ties into existing roadmap item).

---

## 6. Verification gate (definition of done)

1. `rg "Colour\(0x[0-9a-fA-F]{8}\)" Source/` returns **only** matches inside `Theme.h`.
2. `rg "Colours::(dark|black|grey)" Source/` returns **zero** matches outside `Theme.h` (a few `Colours::white`/`transparentBlack` may remain as legitimate alphas).
3. All 5 themes render every screen without unreadable text (especially Daylight — light themes expose contrast bugs that dark themes hide).
4. Theme choice survives restart.
5. Switching theme mid-session doesn't require a restart or a reload of the current rig.

---

## 7. Non-goals / risks

- **Don't redesign the layout.** This is a color pass only. Coordinate-painting stays (separate roadmap item T3.4).
- **Don't touch the SVG assets** in `BinaryData.h` (knobs/toggles) unless a theme genuinely needs recolored assets — prefer tinting via `Drawable::replaceColour` or parameterized SVG if needed, defer otherwise.
- **Light-theme contrast is the main risk.** Dark themes hide a lot. Budget time for a real pass over every overlay, list selection, and meter state once Daylight exists.
- **The 9-color custom-slot palette** is user data, not theme data — keep it functional, don't fold it into themes.
- Estimated effort: Phase 2 is ~60% of the work. The literal sweep is mechanical but wide. Good candidate for an AI agent pass (it's exactly the kind of find-replace-by-role work that's reliable to automate), with a human review of the light-theme contrast afterward.

---

*Proposed July 2026. Follows the architecture-review-zai52.md convention of honest, opinionated, actionable.*
