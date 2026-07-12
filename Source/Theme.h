#pragma once

#include <JuceHeader.h>
#include <vector>

// Theme.h — role-based color system.
//
// Replaces the ~200 hardcoded `juce::Colour(0x...)` / `Colours::dark*` literals
// scattered across the codebase with a single semantic palette. Components read
// colors via `ThemeManager::get(Theme::Role::background)` so the whole UI can be
// re-skinned (including a proper light theme) without touching component code.
//
// Design: `Theme` holds one palette's worth of named Colour fields. `ThemeManager`
// (in ThemeManager.h) tracks the active palette. Call sites use the static facade
// `ThemeManager::get(Theme::Role::...)`.

struct Theme {
    juce::String id;
    juce::String name;
    bool flat = true; // true = modern/flat rendering, false = skeuomorphic

    // ===== CORE =====
    juce::Colour background;   // window / app background
    juce::Colour panel;        // panels, list backgrounds
    juce::Colour panelAlt;     // alternating rows / raised inset
    juce::Colour raised;       // buttons, raised surfaces
    juce::Colour border;       // hairline borders
    juce::Colour borderStrong; // emphasized borders / focus

    // ===== TEXT =====
    juce::Colour text;
    juce::Colour textDim;
    juce::Colour textFaint;
    juce::Colour textOnAccent; // text drawn on top of the accent color

    // ===== ACCENT + DUAL BUS =====
    juce::Colour accent;       // primary brand accent (cyan/blue/amber...)
    juce::Colour accentDim;
    juce::Colour foh;          // FOH bus color
    juce::Colour iem;          // IEM bus color

    // ===== STATUS =====
    juce::Colour ok;           // success / healthy
    juce::Colour warn;         // caution
    juce::Colour danger;       // error
    juce::Colour panic;        // panic button (always highly visible)
    juce::Colour active;       // active/selected highlight
    juce::Colour bypassed;     // bypassed / dimmed

    // ===== METERS =====
    juce::Colour meterLow;     // green
    juce::Colour meterMid;     // yellow
    juce::Colour meterPeak;    // red

    // ===== SLOT CATEGORIES =====
    juce::Colour catMonitor;   // monitor-in slot
    juce::Colour catKeyboard;  // keyboard slot
    juce::Colour catAccordion; // accordion slot
    juce::Colour catReturn;    // aux return slot
    juce::Colour catDefault;   // generic instrument slot

    // ===== MIDI MONITOR =====
    juce::Colour midiNote;
    juce::Colour midiCC;
    juce::Colour midiPC;
    juce::Colour midiArm;
    juce::Colour midiOther;

    // ===== MISC / RENDERING =====
    juce::Colour scrim;        // overlay dimming (semi-transparent)
    juce::Colour knobFace;     // knob/slider thumb body
    juce::Colour trackGroove;  // slider track recess
    juce::Colour knobThumb;    // knob indicator / thumb highlight

    // ===== SEMANTIC ROLE ACCESS =====
    enum class Role {
        background, panel, panelAlt, raised, border, borderStrong,
        text, textDim, textFaint, textOnAccent,
        accent, accentDim, foh, iem,
        ok, warn, danger, panic, active, bypassed,
        meterLow, meterMid, meterPeak,
        catMonitor, catKeyboard, catAccordion, catReturn, catDefault,
        midiNote, midiCC, midiPC, midiArm, midiOther,
        scrim, knobFace, trackGroove, knobThumb
    };

    juce::Colour get(Role r) const {
        switch (r) {
            case Role::background:  return background;
            case Role::panel:       return panel;
            case Role::panelAlt:    return panelAlt;
            case Role::raised:      return raised;
            case Role::border:      return border;
            case Role::borderStrong:return borderStrong;
            case Role::text:        return text;
            case Role::textDim:     return textDim;
            case Role::textFaint:   return textFaint;
            case Role::textOnAccent:return textOnAccent;
            case Role::accent:      return accent;
            case Role::accentDim:   return accentDim;
            case Role::foh:         return foh;
            case Role::iem:         return iem;
            case Role::ok:          return ok;
            case Role::warn:        return warn;
            case Role::danger:      return danger;
            case Role::panic:       return panic;
            case Role::active:      return active;
            case Role::bypassed:    return bypassed;
            case Role::meterLow:    return meterLow;
            case Role::meterMid:    return meterMid;
            case Role::meterPeak:   return meterPeak;
            case Role::catMonitor:  return catMonitor;
            case Role::catKeyboard: return catKeyboard;
            case Role::catAccordion:return catAccordion;
            case Role::catReturn:   return catReturn;
            case Role::catDefault:  return catDefault;
            case Role::midiNote:    return midiNote;
            case Role::midiCC:      return midiCC;
            case Role::midiPC:      return midiPC;
            case Role::midiArm:     return midiArm;
            case Role::midiOther:   return midiOther;
            case Role::scrim:       return scrim;
            case Role::knobFace:    return knobFace;
            case Role::trackGroove: return trackGroove;
            case Role::knobThumb:   return knobThumb;
        }
        return juce::Colours::magenta; // unhandled role — should never happen
    }

    // ===== THE COLLECTION =====
    static const std::vector<Theme>& all() {
        static const std::vector<Theme> themes = {
            // ---------- Stage Black (refined dark default) ----------
            {"stageBlack", "Stage Black", true,
             /*background*/   juce::Colour(0xFF0E1116),
             /*panel*/        juce::Colour(0xFF171B22),
             /*panelAlt*/     juce::Colour(0xFF1E232C),
             /*raised*/       juce::Colour(0xFF242A33),
             /*border*/       juce::Colour(0xFF2C333D),
             /*borderStrong*/ juce::Colour(0xFF3A4350),
             /*text*/         juce::Colour(0xFFF2F5F9),
             /*textDim*/      juce::Colour(0xFF94A1B0),
             /*textFaint*/    juce::Colour(0xFF5C6878),
             /*textOnAccent*/ juce::Colour(0xFF0A0E13),
             /*accent*/       juce::Colour(0xFF00E5FF),
             /*accentDim*/    juce::Colour(0xFF008C99),
             /*foh*/          juce::Colour(0xFF00E676),
             /*iem*/          juce::Colour(0xFF29B6F6),
             /*ok*/           juce::Colour(0xFF00E676),
             /*warn*/         juce::Colour(0xFFFFC107),
             /*danger*/       juce::Colour(0xFFFF5252),
             /*panic*/        juce::Colour(0xFFFF1744),
             /*active*/       juce::Colour(0xFF00E5FF),
             /*bypassed*/     juce::Colour(0xFF4A5260),
             /*meterLow*/     juce::Colour(0xFF00E676),
             /*meterMid*/     juce::Colour(0xFFFFEB3B),
             /*meterPeak*/    juce::Colour(0xFFFF5252),
             /*catMonitor*/   juce::Colour(0xFFFF9800),
             /*catKeyboard*/  juce::Colour(0xFF00BCD4),
             /*catAccordion*/ juce::Colour(0xFFD50000),
             /*catReturn*/    juce::Colour(0xFF1976D2),
             /*catDefault*/   juce::Colour(0xFF3E444D),
             /*midiNote*/     juce::Colour(0xFF00CC66),
             /*midiCC*/       juce::Colour(0xFF3399FF),
             /*midiPC*/       juce::Colour(0xFFCC66FF),
             /*midiArm*/      juce::Colour(0xFFFFC107),
             /*midiOther*/    juce::Colour(0xFFFF6633),
             /*scrim*/        juce::Colour(0xE6121315),
             /*knobFace*/     juce::Colour(0xFF2A2E33),
             /*trackGroove*/  juce::Colour(0xFF0B0C0D),
             /*knobThumb*/    juce::Colour(0xFFFFFFFF)},

            // ---------- Daylight (proper light theme) ----------
            {"daylight", "Daylight", true,
             juce::Colour(0xFFEEF1F5),
             juce::Colour(0xFFFFFFFF),
             juce::Colour(0xFFE6EAEF),
             juce::Colour(0xFFF4F6F9),
             juce::Colour(0xFFC9D2DC),
             juce::Colour(0xFFA8B4C2),
             juce::Colour(0xFF1A2330),
             juce::Colour(0xFF5C6B7E),
             juce::Colour(0xFF8A98A8),
             juce::Colour(0xFFFFFFFF),
             juce::Colour(0xFF0091EA),
             juce::Colour(0xFF0277BD),
             juce::Colour(0xFF00897B),
             juce::Colour(0xFF1565C0),
             juce::Colour(0xFF2E7D32),
             juce::Colour(0xFFEF6C00),
             juce::Colour(0xFFC62828),
             juce::Colour(0xFFD32F2F),
             juce::Colour(0xFF0091EA),
             juce::Colour(0xFFB0BEC5),
             juce::Colour(0xFF2E7D32),
             juce::Colour(0xFFEF6C00),
             juce::Colour(0xFFC62828),
             juce::Colour(0xFFEF6C00),
             juce::Colour(0xFF00838F),
             juce::Colour(0xFFC62828),
             juce::Colour(0xFF1565C0),
             juce::Colour(0xFF90A4AE),
             juce::Colour(0xFF2E7D32),
             juce::Colour(0xFF1565C0),
             juce::Colour(0xFF6A1B9A),
             juce::Colour(0xFFEF6C00),
             juce::Colour(0xFFD84315),
             juce::Colour(0xB3E6EAEF),
             juce::Colour(0xFFD8DEE6),
             juce::Colour(0xFFC9D2DC),
             juce::Colour(0xFF37474F)},

            // ---------- Amber Console (warm vintage) ----------
            {"amberConsole", "Amber Console", true,
             juce::Colour(0xFF1A1510),
             juce::Colour(0xFF261F17),
             juce::Colour(0xFF2E261C),
             juce::Colour(0xFF34291E),
             juce::Colour(0xFF4A3D2C),
             juce::Colour(0xFF5F4E38),
             juce::Colour(0xFFF5E6D3),
             juce::Colour(0xFFB09A7C),
             juce::Colour(0xFF806E58),
             juce::Colour(0xFF1A1510),
             juce::Colour(0xFFFFB300),
             juce::Colour(0xFFFF8F00),
             juce::Colour(0xFFFF8F00),
             juce::Colour(0xFFFF6E40),
             juce::Colour(0xFF9CCC65),
             juce::Colour(0xFFFFCA28),
             juce::Colour(0xFFEF5350),
             juce::Colour(0xFFE53935),
             juce::Colour(0xFFFFB300),
             juce::Colour(0xFF6D5D48),
             juce::Colour(0xFF9CCC65),
             juce::Colour(0xFFFFCA28),
             juce::Colour(0xFFEF5350),
             juce::Colour(0xFFFFA726),
             juce::Colour(0xFFFFB300),
             juce::Colour(0xFFE53935),
             juce::Colour(0xFFFF6E40),
             juce::Colour(0xFF5F4E38),
             juce::Colour(0xFF9CCC65),
             juce::Colour(0xFFFFB300),
             juce::Colour(0xFFFF6E40),
             juce::Colour(0xFFFFCA28),
             juce::Colour(0xFFFFA726),
             juce::Colour(0xE61A1510),
             juce::Colour(0xFF3D3326),
             juce::Colour(0xFF100C08),
             juce::Colour(0xFFF5E6D3)},

            // ---------- Sapphire (professional blue) ----------
            {"sapphire", "Sapphire", true,
             juce::Colour(0xFF0F1419),
             juce::Colour(0xFF161D26),
             juce::Colour(0xFF1E2632),
             juce::Colour(0xFF243040),
             juce::Colour(0xFF2A3441),
             juce::Colour(0xFF3A4858),
             juce::Colour(0xFFE8EDF2),
             juce::Colour(0xFF8A98A8),
             juce::Colour(0xFF5E6B7A),
             juce::Colour(0xFF0F1419),
             juce::Colour(0xFF4FACFE),
             juce::Colour(0xFF1E88E5),
             juce::Colour(0xFF2ECC71),
             juce::Colour(0xFF5B8DEF),
             juce::Colour(0xFF2ECC71),
             juce::Colour(0xFFF1C40F),
             juce::Colour(0xFFE74C3C),
             juce::Colour(0xFFE74C3C),
             juce::Colour(0xFF4FACFE),
             juce::Colour(0xFF4A5868),
             juce::Colour(0xFF2ECC71),
             juce::Colour(0xFFF1C40F),
             juce::Colour(0xFFE74C3C),
             juce::Colour(0xFFE67E22),
             juce::Colour(0xFF4FACFE),
             juce::Colour(0xFFE74C3C),
             juce::Colour(0xFF5B8DEF),
             juce::Colour(0xFF3A4858),
             juce::Colour(0xFF2ECC71),
             juce::Colour(0xFF5B8DEF),
             juce::Colour(0xFF9B59B6),
             juce::Colour(0xFFF1C40F),
             juce::Colour(0xFFE67E22),
             juce::Colour(0xE60F1419),
             juce::Colour(0xFF2A3441),
             juce::Colour(0xFF080C10),
             juce::Colour(0xFFE8EDF2)},

            // ---------- High Contrast (accessibility / outdoor) ----------
            {"highContrast", "High Contrast", true,
             juce::Colour(0xFF000000),
             juce::Colour(0xFF0A0A0A),
             juce::Colour(0xFF141414),
             juce::Colour(0xFF1E1E1E),
             juce::Colour(0xFFFFFFFF),
             juce::Colour(0xFFFFFFFF),
             juce::Colour(0xFFFFFFFF),
             juce::Colour(0xFFBFBFBF),
             juce::Colour(0xFF808080),
             juce::Colour(0xFF000000),
             juce::Colour(0xFFFFFF00),
             juce::Colour(0xFFCCCC00),
             juce::Colour(0xFF00FF00),
             juce::Colour(0xFF00BFFF),
             juce::Colour(0xFF00FF00),
             juce::Colour(0xFFFFFF00),
             juce::Colour(0xFFFF0000),
             juce::Colour(0xFFFF0000),
             juce::Colour(0xFFFFFF00),
             juce::Colour(0xFF666666),
             juce::Colour(0xFF00FF00),
             juce::Colour(0xFFFFFF00),
             juce::Colour(0xFFFF0000),
             juce::Colour(0xFFFF8800),
             juce::Colour(0xFF00FFFF),
             juce::Colour(0xFFFF0000),
             juce::Colour(0xFF00BFFF),
             juce::Colour(0xFF888888),
             juce::Colour(0xFF00FF00),
             juce::Colour(0xFF00BFFF),
             juce::Colour(0xFFFF00FF),
             juce::Colour(0xFFFFFF00),
             juce::Colour(0xFFFF8800),
             juce::Colour(0xE6000000),
             juce::Colour(0xFF1E1E1E),
             juce::Colour(0xFF000000),
             juce::Colour(0xFFFFFFFF)},
        };
        return themes;
    }

    static const Theme* findById(const juce::String& targetId) {
        for (const auto& t : all())
            if (t.id == targetId)
                return &t;
        return nullptr;
    }
};
