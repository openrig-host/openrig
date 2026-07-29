#pragma once

#include <JuceHeader.h>
#include "FanfareConstants.h"

namespace Fanfare {

/**
    RigLibrary
    ----------
    Owns the consolidated on-disk library layout under %APPDATA%/OpenRig and
    performs a one-time, non-destructive migration of the legacy scattered
    files (Desktop rig, Documents/Setups, Documents/Sets, button mappings)
    into backups/legacy/ so no user data is ever lost.
*/
class RigLibrary {
public:
    static juce::File legacyBackupDir() {
        return FanfareConstants::getBackupsDirectory().getChildFile("legacy");
    }

    static juce::File migrationFlagFile() {
        return FanfareConstants::getSettingsDirectory().getChildFile("library_migrated.flag");
    }

    /** Returns the default location for the current full rig (the working copy). */
    static juce::File defaultRigFile() {
        return FanfareConstants::getAppDirectory().getChildFile("current_rig.json");
    }

    /** Songs directory (individual song rigs). */
    static juce::File getSongsDirectory() {
        return FanfareConstants::getSongsDirectory();
    }

    /** Setlists directory (.set files). */
    static juce::File getSetsDirectory() {
        return FanfareConstants::getSetsDirectory();
    }

    /** Channel-strip presets directory (.orstrip files). */
    static juce::File getPresetsDirectory() {
        return FanfareConstants::getAppDirectory().getChildFile("strips");
    }

    static juce::File defaultSetsFile() {
        return FanfareConstants::getSetsDirectory().getChildFile("default.set");
    }

    static juce::File buttonMappingsFile() {
        return FanfareConstants::getSettingsDirectory().getChildFile("button_mappings.json");
    }

    static juce::File knownPluginsFile() {
        return FanfareConstants::getAppDirectory().getChildFile("known_plugins.json");
    }

    /**
        Run once. Copies legacy files into backups/legacy/ (read-only copy;
        originals are NEVER deleted) and writes a flag so it never runs again.
        Safe to call repeatedly: it is a no-op after the first success.
    */
    static void migrateLegacyLibraryIfNeeded() {
        FanfareConstants::getAppDirectory().createDirectory();
        FanfareConstants::getSettingsDirectory().createDirectory();
        legacyBackupDir().createDirectory();

        if (migrationFlagFile().existsAsFile())
            return;

        auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("Fanfare");
        auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("Fanfare");
        auto desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);

        // Legacy scattered sources to preserve (source -> relative name under backups/legacy).
        struct LegacyEntry { juce::File source; juce::String destName; };
        juce::Array<LegacyEntry> entries;

        entries.add({ desktop.getChildFile("FanfareFullRig.json"), "FanfareFullRig.json" });
        entries.add({ docs.getChildFile("Setups"), "Setups" });
        entries.add({ docs.getChildFile("Sets"), "Sets" });
        entries.add({ appData.getChildFile("button_mappings.json"), "button_mappings.json" });

        for (const auto& e : entries) {
            if (!e.source.exists())
                continue;
            juce::File dest = legacyBackupDir().getChildFile(e.destName);
            tryCopy(e.source, dest);
        }

        // Touch the flag. We write regardless of copy success so we don't
        // retry forever on permission errors; originals are untouched anyway.
        migrationFlagFile().create();
    }

private:
    static void tryCopy(const juce::File& source, const juce::File& dest) {
        if (source.isDirectory()) {
            dest.createDirectory();
            for (auto& child : juce::RangedDirectoryIterator(source, false, "*", juce::File::findFiles))
                tryCopy(child.getFile(), dest.getChildFile(child.getFile().getFileName()));
        } else if (source.existsAsFile()) {
            if (!dest.existsAsFile())
                source.copyFileTo(dest);
        }
    }
};

} // namespace Fanfare
