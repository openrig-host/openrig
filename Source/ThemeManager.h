#pragma once

#include <JuceHeader.h>
#include "Theme.h"

// ThemeManager.h — singleton owning the active theme.
//
// Usage in components:
//   g.setColour(ThemeManager::get(Theme::Role::background));
//
// Switching themes:
//   ThemeManager::getInstance().setCurrentById("daylight");
//   // -> broadcasts change; listeners repaint.
//
// Persistence: the selected theme id is stored in
//   %APPDATA%/OpenRig/settings/theme.json

class ThemeManager : public juce::ChangeBroadcaster {
public:
    static ThemeManager& getInstance() {
        static ThemeManager instance;
        return instance;
    }

    // The active palette.
    const Theme& active() const { return Theme::all()[juce::jlimit(0, (int)Theme::all().size() - 1, currentIndex)]; }

    // Convenience facade used by components: returns the active theme's color for a role.
    static juce::Colour get(Theme::Role r) { return getInstance().active().get(r); }

    // Selection.
    int getCurrentIndex() const { return currentIndex; }
    juce::String getCurrentId() const { return active().id; }
    juce::StringArray getThemeNames() const {
        juce::StringArray names;
        for (const auto& t : Theme::all())
            names.add(t.name);
        return names;
    }
    bool isFlat() const { return active().flat; }

    void setCurrentByIndex(int idx) {
        idx = juce::jlimit(0, (int)Theme::all().size() - 1, idx);
        if (idx == currentIndex)
            return;
        currentIndex = idx;
        saveToSettings();
        sendChangeMessage();
    }

    void setCurrentById(const juce::String& id) {
        if (const Theme* t = Theme::findById(id)) {
            for (size_t i = 0; i < Theme::all().size(); ++i) {
                if (Theme::all()[i].id == id) {
                    setCurrentByIndex((int)i);
                    return;
                }
            }
        }
    }

    // ===== Persistence =====
    // Loads saved theme id from settings. Call once at startup.
    void loadFromSettings() {
        auto f = settingsFile();
        if (f.existsAsFile()) {
            auto parsed = juce::JSON::parse(f);
            if (auto* obj = parsed.getDynamicObject()) {
                juce::String id = obj->getProperty("themeId").toString();
                if (const Theme* t = Theme::findById(id)) {
                    for (size_t i = 0; i < Theme::all().size(); ++i) {
                        if (Theme::all()[i].id == id) {
                            currentIndex = (int)i;
                            return;
                        }
                    }
                }
            }
        }
        // Default to the first theme (Stage Black).
        currentIndex = 0;
    }

    void saveToSettings() {
        auto f = settingsFile();
        f.getParentDirectory().createDirectory();
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("themeId", getCurrentId());
        juce::FileOutputStream fos(f);
        if (fos.openedOk()) {
            fos.setPosition(0);
            fos.truncate();
            juce::JSON::writeToStream(fos, juce::var(obj.get()));
        }
    }

private:
    ThemeManager() = default;

    static juce::File settingsFile() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("OpenRig/settings/theme.json");
    }

    int currentIndex = 0; // Stage Black by default

public:
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;
};
