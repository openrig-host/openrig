#pragma once

#include <JuceHeader.h>
#include "OpenRigConstants.h"
#include "RigModel.h"

namespace OpenRig {

/**
    RigSerializer
    -------------
    Versioned, atomic, non-destructive JSON persistence for OpenRig rigs/songs.

    Responsibilities:
      - Read & validate rig JSON, refusing unknown future schema versions.
      - Migrate legacy v1 documents (paramIndex-only CC mappings) to the
        current schema (v2: paramId-primary with index fallback) in memory.
      - Write atomically (temp file -> moveFileTo) and keep a .bak of the
        previous file so user data is never destroyed.

    The serializer intentionally works on the engine's existing `var` rig
    representation rather than a bespoke model, so it composes with the
    current exportRigToJson/importRigFromJson path (used as shims during
    migration, per the architecture plan).
*/
class RigSerializer {
public:
    static constexpr int kCurrentSchemaVersion = 2;

    struct LoadResult {
        bool ok = false;
        juce::String errorMessage;
        juce::var rig;          // Parsed rig object (valid only when ok == true)
        int detectedVersion = 0;
        bool wasMigrated = false;
    };

    /** Read, validate and (if needed) migrate a rig file. */
    static LoadResult load(const juce::File& file) {
        if (!file.existsAsFile())
            return fail("File does not exist: " + file.getFullPathName());

        juce::String text = file.loadFileAsString();
        if (text.trim().isEmpty())
            return fail("File is empty: " + file.getFullPathName());

        return loadFromString(text);
    }

    /** Read, validate and (if needed) migrate a rig JSON string. */
    static LoadResult loadFromString(const juce::String& jsonText) {
        auto parsed = juce::JSON::parse(jsonText);
        if (!parsed.isObject())
            return fail("Not a valid JSON object.");

        int version = detectVersion(parsed);

        // Refuse future versions we don't understand rather than silently
        // mis-applying them.
        if (version > kCurrentSchemaVersion) {
            return fail("This file uses schema version " + juce::String(version) +
                        ", which is newer than the supported version (" +
                        juce::String(kCurrentSchemaVersion) +
                        "). Please update OpenRig.");
        }

        juce::String validationError = validate(parsed);
        if (validationError.isNotEmpty())
            return fail(validationError);

        LoadResult r;
        r.detectedVersion = version;
        if (version < kCurrentSchemaVersion) {
            migrate(parsed, version);
            r.wasMigrated = true;
        }
        r.ok = true;
        r.rig = parsed;
        return r;
    }

    /**
        Atomic, non-destructive write.
        Backs up any existing file to <name>.bak first, then writes a temp
        file and atomically moves it over the target.
    */
    static bool save(const juce::File& file, const juce::String& jsonText) {
        auto parent = file.getParentDirectory();
        if (!parent.exists())
            parent.createDirectory();

        // Non-destructive backup of the prior file (if any).
        if (file.existsAsFile()) {
            juce::File bak = file.getSiblingFile(file.getFileName() + ".bak");
            file.copyFileTo(bak);
        }

        juce::File temp = file.getSiblingFile(file.getFileName() + ".tmp");
        // Clean a stray temp file from a previous failed write.
        if (temp.existsAsFile())
            temp.deleteFile();

        if (!temp.replaceWithText(jsonText))
            return false;

        // moveFileTo uses MOVEFILE_REPLACE_EXISTING on Windows -> atomic replace.
        return temp.moveFileTo(file);
    }

    /** Validate required structural fields. Returns a non-empty string on error. */
    static juce::String validate(const juce::var& rig) {
        if (!rig.isObject())
            return "Root is not a JSON object.";

        // "channels" is the only hard structural requirement. A rig may
        // legitimately have an empty slot array.
        const auto& channels = rig.getProperty("channels", juce::var());
        if (!channels.isVoid() && !channels.isArray())
            return "\"channels\" must be an array.";

        const auto& scenes = rig.getProperty("scenes", juce::var());
        if (!scenes.isVoid() && !scenes.isArray())
            return "\"scenes\" must be an array.";

        return {};
    }

    /** Bring a parsed rig up to the current schema version, in place. */
    static void migrate(juce::var& rig, int fromVersion) {
        if (fromVersion >= kCurrentSchemaVersion)
            return;

        // --- v1 -> v2 ---
        // 1. Ensure each channel carries the new optional fields with safe
        //    defaults so the engine's importer can read them unconditionally.
        if (auto* chans = rig.getProperty("channels", juce::var()).getArray()) {
            for (int i = 0; i < chans->size(); ++i) {
                auto& cv = chans->getReference(i);
                if (auto* obj = cv.getDynamicObject()) {
                    if (!obj->hasProperty("fohCC"))       obj->setProperty("fohCC", -1);
                    if (!obj->hasProperty("iemCC"))       obj->setProperty("iemCC", -1);
                    if (!obj->hasProperty("midiChannel")) obj->setProperty("midiChannel", -1);

                    // 2. Give every CC mapping an empty paramId (index is the
                    //    fallback at apply time; paramId gets populated on the
                    //    next save once plugins are introspected).
                    if (auto* maps = obj->getProperty("ccMappings").getArray()) {
                        for (int m = 0; m < maps->size(); ++m) {
                            auto& mv = maps->getReference(m);
                            if (auto* mo = mv.getDynamicObject()) {
                                if (!mo->hasProperty("paramId"))
                                    mo->setProperty("paramId", "");
                            }
                        }
                    }
                }
            }
        }

        // 3. Global default MIDI channel (1 = legacy behaviour).
        if (auto* obj = rig.getDynamicObject()) {
            if (!obj->hasProperty("defaultMidiChannel"))
                obj->setProperty("defaultMidiChannel", OpenRigConstants::kDefaultMidiChannel);
        }

        // Stamp the new version so a re-save persists the migration.
        if (auto* obj = rig.getDynamicObject()) {
            obj->setProperty("version", kCurrentSchemaVersion);
            obj->setProperty("schemaVersion", kCurrentSchemaVersion);
        }
    }

    /** Detect the schema version of a parsed rig (defaults to 1 for legacy files). */
    static int detectVersion(const juce::var& rig) {
        int v = rig.getProperty("schemaVersion",
                                rig.getProperty("version", 1));
        return v > 0 ? v : 1;
    }

    /** Ensure the on-disk OpenRig library layout exists. */
    static void ensureLibraryLayout() {
        using namespace OpenRigConstants;
        getAppDirectory().createDirectory();
        getSongsDirectory().createDirectory();
        getSetsDirectory().createDirectory();
        getBackupsDirectory().createDirectory();
        getSettingsDirectory().createDirectory();
    }

    // ----- Song-level (de)serialization -----
    // These bridge the engine's `var` rig representation and the OpenRig::Song
    // POD model used by the async RigBuilder/RigTransitioner path.

    /** Read, validate, migrate and convert a rig file into a Song. */
    static bool readSongFromFile(const juce::File& file, Song& outSong);

    /** Convert a Song to JSON and write it atomically (temp + rename), keeping
        a .bak of any prior file. Returns false on write failure. */
    static bool writeSongToFile(const juce::File& file, const Song& song);

    /** Serialize a Song to a JSON string (mirrors exportRigToJson shape). */
    static juce::String serializeSong(const Song& song);

    /** Convert a validated/migrated rig `var` into a Song. */
    static bool varToSong(const juce::var& rig, Song& out);

    /** Convert a Song into a rig `var` (mirrors exportRigToJson). */
    static juce::var songToVar(const Song& song);

    /** Serialize a single strip (SongSlot) to JSON. */
    static juce::String serializeStrip(const SongSlot& slot);

    /** Read a single strip from a JSON file. */
    static bool readStripFromFile(const juce::File& file, SongSlot& outSlot);

private:
    static LoadResult fail(const juce::String& msg) {
        LoadResult r;
        r.ok = false;
        r.errorMessage = msg;
        return r;
    }
};

} // namespace OpenRig
