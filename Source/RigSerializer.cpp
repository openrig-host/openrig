#include "RigSerializer.h"

namespace OpenRig {

bool RigSerializer::readSongFromFile(const juce::File& file, Song& outSong) {
    LoadResult result = load(file);
    if (!result.ok)
        return false;

    if (!varToSong(result.rig, outSong))
        return false;

    if (outSong.name.isEmpty())
        outSong.name = file.getFileNameWithoutExtension();

    return true;
}

bool RigSerializer::writeSongToFile(const juce::File& file, const Song& song) {
    return save(file, serializeSong(song));
}

juce::String RigSerializer::serializeSong(const Song& song) {
    return juce::JSON::toString(songToVar(song));
}

bool RigSerializer::varToSong(const juce::var& rig, Song& out) {
    if (!rig.isObject())
        return false;

    out.fohMasterLevel = (float)rig.getProperty("fohMasterLevel", 1.0);
    out.iemMasterLevel = (float)rig.getProperty("iemMasterLevel", 1.0);
    out.fohOutputOffset = rig.getProperty("fohOutputOffset",
                                          OpenRigConstants::kDefaultFohOutputOffset);
    out.iemOutputOffset = rig.getProperty("iemOutputOffset",
                                          OpenRigConstants::kDefaultIemOutputOffset);

    auto readPluginState = [](const juce::var& v) {
        PluginState ps;
        if (v.isObject()) {
            ps.name = v.getProperty("name", "").toString();
            ps.path = v.getProperty("path", "").toString();
            ps.stateBase64 = v.getProperty("state", "").toString();
            ps.uid = (int)v.getProperty("uid", 0);
            ps.lowNote = (int)v.getProperty("lowNote", 0);
            ps.highNote = (int)v.getProperty("highNote", 127);
            ps.level = (float)v.getProperty("level", 1.0);
            ps.enabled = (bool)v.getProperty("enabled", true);
        }
        return ps;
    };

    if (auto* a = rig.getProperty("fohFx", juce::var()).getArray())
        for (int i = 0; i < a->size(); ++i)
            out.fohFx.push_back(readPluginState(a->getReference(i)));

    if (auto* a = rig.getProperty("iemFx", juce::var()).getArray())
        for (int i = 0; i < a->size(); ++i)
            out.iemFx.push_back(readPluginState(a->getReference(i)));

    if (auto* chans = rig.getProperty("channels", juce::var()).getArray()) {
        for (int i = 0; i < chans->size(); ++i) {
            const auto& cv = chans->getReference(i);
            if (!cv.isObject())
                continue;

            SongSlot slot;
            slot.name = cv.getProperty("name", "Slot").toString();
            slot.iconIndex = cv.getProperty("iconIndex", 0);
            slot.channelColor = juce::Colour::fromString(
                cv.getProperty("channelColor", "#ff2a2a2a").toString());
            slot.level = (float)cv.getProperty("level", 0.8);
            slot.fohEnabled = cv.getProperty("foh", true);
            slot.iemEnabled = cv.getProperty("iem", true);
            slot.bypassed = cv.getProperty("mute", false);
            slot.inputChannelIndex = cv.getProperty("inputIndex", -1);
            slot.aux1Send = (float)cv.getProperty("aux1", 0.0);
            slot.aux2Send = (float)cv.getProperty("aux2", 0.0);
            slot.iemOffset = (float)cv.getProperty("iemOffset", 1.0);
            slot.transposeOctaves = cv.getProperty("transposeOctaves", 0);
            slot.transposeSemitones = cv.getProperty("transposeSemitones", 0);
            slot.lowNote = cv.getProperty("lowNote", 0);
            slot.highNote = cv.getProperty("highNote", 127);
            slot.fohCC = cv.getProperty("fohCC", -1);
            slot.iemCC = cv.getProperty("iemCC", -1);
            slot.midiChannelOverride = cv.getProperty("midiChannel", -1);

            juce::String ccList = cv.getProperty("allowedCCs", "64").toString();
            juce::StringArray toks;
            toks.addTokens(ccList, ",", "");
            for (auto& t : toks)
                if (t.isNotEmpty())
                    slot.allowedCCs.insert(t.getIntValue());


            // Arpeggiator
            if (auto* av = cv.getProperty("arpeggiator", juce::var()).getDynamicObject()) {
                slot.arpeggiator.enabled = av->getProperty("enabled");
                slot.arpeggiator.bpm = (float)av->getProperty("bpm");
                slot.arpeggiator.octavesUp = (int)av->getProperty("octavesUp");
                slot.arpeggiator.octavesDown = (int)av->getProperty("octavesDown");
                slot.arpeggiator.gate = (float)av->getProperty("gate");
                slot.arpeggiator.patternIdx = (int)av->getProperty("patternIdx");
            }
            // Harmonizer
            if (auto* hv = cv.getProperty("harmonizer", juce::var()).getDynamicObject()) {
                slot.harmonizer.enabled = hv->getProperty("enabled");
                slot.harmonizer.octavesUp = (int)hv->getProperty("octavesUp");
                slot.harmonizer.octavesDown = (int)hv->getProperty("octavesDown");
                slot.harmonizer.africaMode = hv->hasProperty("africaMode") ? (int)hv->getProperty("africaMode") : 0;
                slot.harmonizer.harmonyTargetSlot = hv->hasProperty("harmonyTargetSlot") ? (int)hv->getProperty("harmonyTargetSlot") : -1;
            }
            if (auto* sv = cv.getProperty("sampler", juce::var()).getDynamicObject()) {
                slot.sampler.enabled = sv->getProperty("enabled");
                if (auto* sls = sv->getProperty("slots").getArray()) {
                    for (int sIdx = 0; sIdx < juce::jmin(8, sls->size()); ++sIdx) {
                        const auto& slo = sls->getReference(sIdx);
                        if (slo.isObject()) {
                            slot.sampler.slots[sIdx].wavPath = slo.getProperty("wavPath", "").toString();
                            slot.sampler.slots[sIdx].rootNote = slo.getProperty("rootNote", 60);
                            slot.sampler.slots[sIdx].keyLow = slo.getProperty("keyLow", 0);
                            slot.sampler.slots[sIdx].keyHigh = slo.getProperty("keyHigh", 127);
                            slot.sampler.slots[sIdx].pitchOffsetSemitones = (float)slo.getProperty("pitchOffset", 0.0);
                            slot.sampler.slots[sIdx].volume = (float)slo.getProperty("volume", 1.0);
                            slot.sampler.slots[sIdx].startRatio = (float)slo.getProperty("startRatio", 0.0);
                            slot.sampler.slots[sIdx].endRatio = (float)slo.getProperty("endRatio", 1.0);
                        }
                    }
                }
            }
            if (auto* so = cv.getProperty("strip", juce::var()).getDynamicObject()) {
                slot.strip.gateEnabled = so->getProperty("gateEnabled");
                slot.strip.gateThreshold = (float)so->getProperty("gateThreshold");
                slot.strip.eqEnabled = so->getProperty("eqEnabled");
                slot.strip.hpfFreq = (float)so->getProperty("hpfFreq");
                slot.strip.lowShelfGain = (float)so->getProperty("eqLow");
                slot.strip.highShelfGain = (float)so->getProperty("eqHigh");
                slot.strip.compEnabled = so->getProperty("compEnabled");
                slot.strip.compAmount = (float)so->getProperty("compAmount");
                slot.strip.chorusEnabled = so->hasProperty("chorusEnabled") ? (bool)so->getProperty("chorusEnabled") : false;
                slot.strip.chorusRate = so->hasProperty("chorusRate") ? (float)so->getProperty("chorusRate") : 1.0f;
                slot.strip.chorusMix = so->hasProperty("chorusMix") ? (float)so->getProperty("chorusMix") : 0.0f;
                slot.strip.reverbEnabled = so->hasProperty("reverbEnabled") ? (bool)so->getProperty("reverbEnabled") : false;
                slot.strip.reverbSize = so->hasProperty("reverbSize") ? (float)so->getProperty("reverbSize") : 0.5f;
                slot.strip.reverbMix = so->hasProperty("reverbMix") ? (float)so->getProperty("reverbMix") : 0.0f;
        }

            if (auto* maps = cv.getProperty("ccMappings", juce::var()).getArray()) {
                for (int m = 0; m < maps->size(); ++m) {
                    const auto& mv = maps->getReference(m);
                    if (!mv.isObject())
                        continue;
                    CCMapping cm;
                    cm.cc = mv.getProperty("cc", -1);
                    cm.chainIndex = mv.getProperty("chainIndex", 0);
                    cm.paramId = mv.getProperty("paramId", "").toString();
                    cm.parameterIndex =
                        mv.getProperty("paramIndex", mv.getProperty("parameterIndex", -1));
                    cm.minValue = (float)mv.getProperty("minValue", 0.0);
                    cm.maxValue = (float)mv.getProperty("maxValue", 1.0);
                    cm.invert = mv.getProperty("invert", false);
                    slot.ccMappings.push_back(cm);
                }
            }

            if (auto* pts = cv.getProperty("ccPassthroughs", juce::var()).getArray()) {
                for (int m = 0; m < pts->size(); ++m) {
                    const auto& pv = pts->getReference(m);
                    if (!pv.isObject())
                        continue;
                    CCPassthrough cp;
                    cp.incomingCC = pv.getProperty("incomingCC", -1);
                    cp.outgoingCC = pv.getProperty("outgoingCC", -1);
                    if (cp.incomingCC >= 0 && cp.outgoingCC >= 0)
                        slot.ccPassthroughs.push_back(cp);
                }
            }

            if (auto* ch = cv.getProperty("chain", juce::var()).getArray())
                for (int p = 0; p < ch->size(); ++p)
                    slot.chain.push_back(readPluginState(ch->getReference(p)));

            out.slots.push_back(slot);
        }
    }

    if (auto* scs = rig.getProperty("scenes", juce::var()).getArray()) {
        for (int i = 0; i < scs->size(); ++i) {
            const auto& sv = scs->getReference(i);
            if (!sv.isObject())
                continue;
            Scene sc;
            sc.name = sv.getProperty("name", "Scene").toString();
            if (auto* sts = sv.getProperty("states", juce::var()).getArray()) {
                for (int s = 0; s < sts->size(); ++s) {
                    const auto& stv = sts->getReference(s);
                    SlotState st;
                    st.bypassed = stv.getProperty("bypassed", false);
                    st.channelLevel = (float)stv.getProperty("level", 0.8);
                    st.fohEnabled = stv.getProperty("foh", true);
                    st.iemEnabled = stv.getProperty("iem", true);
                    sc.slotStates.push_back(st);
                }
            }
            out.scenes.push_back(sc);
        }
    }

    out.currentSceneIndex = rig.getProperty("currentSceneIndex", 0);
    return true;
}

juce::var RigSerializer::songToVar(const Song& song) {
    auto* rig = new juce::DynamicObject();
    rig->setProperty("version", kCurrentSchemaVersion);
    rig->setProperty("schemaVersion", kCurrentSchemaVersion);
    rig->setProperty("fohMasterLevel", (double)song.fohMasterLevel);
    rig->setProperty("iemMasterLevel", (double)song.iemMasterLevel);
    rig->setProperty("fohOutputOffset", song.fohOutputOffset);
    rig->setProperty("iemOutputOffset", song.iemOutputOffset);

    auto writePluginState = [](const PluginState& ps) {
        auto* o = new juce::DynamicObject();
        o->setProperty("name", ps.name);
        o->setProperty("state", ps.stateBase64);
        o->setProperty("uid", ps.uid);
        o->setProperty("path", ps.path);
        o->setProperty("lowNote", ps.lowNote);
        o->setProperty("highNote", ps.highNote);
        o->setProperty("level", (double)ps.level);
        o->setProperty("enabled", ps.enabled);
        return juce::var(o);
    };

    juce::Array<juce::var> fohNodes;
    for (const auto& ps : song.fohFx) {
        if (ps.path.isNotEmpty())
            fohNodes.add(writePluginState(ps));
        else
            fohNodes.add(juce::var());
    }
    rig->setProperty("fohFx", fohNodes);

    juce::Array<juce::var> iemNodes;
    for (const auto& ps : song.iemFx) {
        if (ps.path.isNotEmpty())
            iemNodes.add(writePluginState(ps));
        else
            iemNodes.add(juce::var());
    }
    rig->setProperty("iemFx", iemNodes);

    juce::Array<juce::var> channelNodes;
    for (const auto& s : song.slots) {
        auto* st = new juce::DynamicObject();
        st->setProperty("name", s.name);
        st->setProperty("iconIndex", s.iconIndex);
        st->setProperty("channelColor", s.channelColor.toString());
        st->setProperty("level", (double)s.level);
        st->setProperty("foh", s.fohEnabled);
        st->setProperty("iem", s.iemEnabled);
        st->setProperty("mute", s.bypassed);
        st->setProperty("inputIndex", s.inputChannelIndex);
        st->setProperty("aux1", (double)s.aux1Send);
        st->setProperty("aux2", (double)s.aux2Send);
        st->setProperty("iemOffset", (double)s.iemOffset);
        st->setProperty("transposeOctaves", s.transposeOctaves);
        st->setProperty("transposeSemitones", s.transposeSemitones);
        st->setProperty("lowNote", s.lowNote);
        st->setProperty("highNote", s.highNote);

        juce::String ccList;
        for (int cc : s.allowedCCs)
            ccList += juce::String(cc) + ",";
        st->setProperty("allowedCCs", ccList);

        st->setProperty("fohCC", s.fohCC);
        st->setProperty("iemCC", s.iemCC);
        st->setProperty("midiChannel", s.midiChannelOverride);

        auto* stripObj = new juce::DynamicObject();
        stripObj->setProperty("gateEnabled", (bool)s.strip.gateEnabled);
        stripObj->setProperty("gateThreshold", (double)s.strip.gateThreshold);
        stripObj->setProperty("eqEnabled", (bool)s.strip.eqEnabled);
        stripObj->setProperty("hpfFreq", (double)s.strip.hpfFreq);
        stripObj->setProperty("eqLow", (double)s.strip.lowShelfGain);
        stripObj->setProperty("eqHigh", (double)s.strip.highShelfGain);
        stripObj->setProperty("compEnabled", (bool)s.strip.compEnabled);
        stripObj->setProperty("compAmount", (double)s.strip.compAmount);
        stripObj->setProperty("chorusEnabled", (bool)s.strip.chorusEnabled);
        stripObj->setProperty("chorusRate", (double)s.strip.chorusRate);
        stripObj->setProperty("chorusMix", (double)s.strip.chorusMix);
        stripObj->setProperty("reverbEnabled", (bool)s.strip.reverbEnabled);
        stripObj->setProperty("reverbSize", (double)s.strip.reverbSize);
        stripObj->setProperty("reverbMix", (double)s.strip.reverbMix);
        st->setProperty("strip", stripObj);
        auto* arpObj = new juce::DynamicObject();
        arpObj->setProperty("enabled", s.arpeggiator.enabled);
        arpObj->setProperty("bpm", (double)s.arpeggiator.bpm);
        arpObj->setProperty("octavesUp", s.arpeggiator.octavesUp);
        arpObj->setProperty("octavesDown", s.arpeggiator.octavesDown);
        arpObj->setProperty("gate", (double)s.arpeggiator.gate);
        arpObj->setProperty("patternIdx", s.arpeggiator.patternIdx);
        st->setProperty("arpeggiator", arpObj);
        auto* harmObj = new juce::DynamicObject();
        harmObj->setProperty("enabled", s.harmonizer.enabled);
        harmObj->setProperty("octavesUp", s.harmonizer.octavesUp);
        harmObj->setProperty("octavesDown", s.harmonizer.octavesDown);
        harmObj->setProperty("africaMode", s.harmonizer.africaMode);
        harmObj->setProperty("harmonyTargetSlot", s.harmonizer.harmonyTargetSlot);
        st->setProperty("harmonizer", harmObj);

        auto* samplerObj = new juce::DynamicObject();
        samplerObj->setProperty("enabled", s.sampler.enabled);
        juce::Array<juce::var> samplerSlots;
        for (int idx = 0; idx < 8; ++idx) {
            auto* slo = new juce::DynamicObject();
            slo->setProperty("wavPath", s.sampler.slots[idx].wavPath);
            slo->setProperty("rootNote", s.sampler.slots[idx].rootNote);
            slo->setProperty("keyLow", s.sampler.slots[idx].keyLow);
            slo->setProperty("keyHigh", s.sampler.slots[idx].keyHigh);
            slo->setProperty("pitchOffset", (double)s.sampler.slots[idx].pitchOffsetSemitones);
            slo->setProperty("volume", (double)s.sampler.slots[idx].volume);
            slo->setProperty("startRatio", (double)s.sampler.slots[idx].startRatio);
            slo->setProperty("endRatio", (double)s.sampler.slots[idx].endRatio);
            samplerSlots.add(juce::var(slo));
        }
        samplerObj->setProperty("slots", samplerSlots);
        st->setProperty("sampler", samplerObj);

        juce::Array<juce::var> ccMappingNodes;
        for (const auto& m : s.ccMappings) {
            auto* mo = new juce::DynamicObject();
            mo->setProperty("cc", m.cc);
            mo->setProperty("chainIndex", m.chainIndex);
            mo->setProperty("paramId", m.paramId);
            mo->setProperty("paramIndex", m.parameterIndex);
            mo->setProperty("minValue", (double)m.minValue);
            mo->setProperty("maxValue", (double)m.maxValue);
            mo->setProperty("invert", m.invert);
            ccMappingNodes.add(juce::var(mo));
        }
        st->setProperty("ccMappings", ccMappingNodes);

        juce::Array<juce::var> ccPassthroughNodes;
        for (const auto& pt : s.ccPassthroughs) {
            auto* po = new juce::DynamicObject();
            po->setProperty("incomingCC", pt.incomingCC);
            po->setProperty("outgoingCC", pt.outgoingCC);
            ccPassthroughNodes.add(juce::var(po));
        }
        st->setProperty("ccPassthroughs", ccPassthroughNodes);

        juce::Array<juce::var> chainNodes;
        for (const auto& ps : s.chain) {
            if (ps.path.isNotEmpty())
                chainNodes.add(writePluginState(ps));
            else
                chainNodes.add(juce::var());
        }
        st->setProperty("chain", chainNodes);
        channelNodes.add(juce::var(st));
    }
    rig->setProperty("channels", channelNodes);

    juce::Array<juce::var> sceneNodes;
    for (const auto& scene : song.scenes) {
        auto* so = new juce::DynamicObject();
        so->setProperty("name", scene.name);
        juce::Array<juce::var> states;
        for (const auto& ss : scene.slotStates) {
            auto* sso = new juce::DynamicObject();
            sso->setProperty("bypassed", ss.bypassed);
            sso->setProperty("level", (double)ss.channelLevel);
            sso->setProperty("foh", ss.fohEnabled);
            sso->setProperty("iem", ss.iemEnabled);
            states.add(juce::var(sso));
        }
        so->setProperty("states", states);
        sceneNodes.add(juce::var(so));
    }
    rig->setProperty("scenes", sceneNodes);
    rig->setProperty("currentSceneIndex", song.currentSceneIndex);

    return juce::var(rig);
}

juce::String RigSerializer::serializeStrip(const SongSlot& slot) {
    auto writePluginState = [](const PluginState& ps) {
        auto* o = new juce::DynamicObject();
        o->setProperty("name", ps.name);
        o->setProperty("state", ps.stateBase64);
        o->setProperty("uid", ps.uid);
        o->setProperty("path", ps.path);
        o->setProperty("lowNote", ps.lowNote);
        o->setProperty("highNote", ps.highNote);
        o->setProperty("level", (double)ps.level);
        o->setProperty("enabled", ps.enabled);
        return juce::var(o);
    };

    auto* st = new juce::DynamicObject();
    st->setProperty("stripFormat", 1);
    st->setProperty("name", slot.name);
    st->setProperty("iconIndex", slot.iconIndex);
    st->setProperty("channelColor", slot.channelColor.toString());
    st->setProperty("level", (double)slot.level);
    st->setProperty("foh", slot.fohEnabled);
    st->setProperty("iem", slot.iemEnabled);
    st->setProperty("mute", slot.bypassed);
    st->setProperty("inputIndex", slot.inputChannelIndex);
    st->setProperty("aux1", (double)slot.aux1Send);
    st->setProperty("aux2", (double)slot.aux2Send);
    st->setProperty("iemOffset", (double)slot.iemOffset);
    st->setProperty("transposeOctaves", slot.transposeOctaves);
    st->setProperty("transposeSemitones", slot.transposeSemitones);
    st->setProperty("lowNote", slot.lowNote);
    st->setProperty("highNote", slot.highNote);

    juce::String ccList;
    for (int cc : slot.allowedCCs)
        ccList += juce::String(cc) + ",";
    st->setProperty("allowedCCs", ccList);
    st->setProperty("fohCC", slot.fohCC);
    st->setProperty("iemCC", slot.iemCC);
    st->setProperty("midiChannel", slot.midiChannelOverride);

    auto* stripObj = new juce::DynamicObject();
    stripObj->setProperty("gateEnabled", (bool)slot.strip.gateEnabled);
    stripObj->setProperty("gateThreshold", (double)slot.strip.gateThreshold);
    stripObj->setProperty("eqEnabled", (bool)slot.strip.eqEnabled);
    stripObj->setProperty("hpfFreq", (double)slot.strip.hpfFreq);
    stripObj->setProperty("eqLow", (double)slot.strip.lowShelfGain);
    stripObj->setProperty("eqHigh", (double)slot.strip.highShelfGain);
    stripObj->setProperty("compEnabled", (bool)slot.strip.compEnabled);
    stripObj->setProperty("compAmount", (double)slot.strip.compAmount);
    stripObj->setProperty("chorusEnabled", (bool)slot.strip.chorusEnabled);
    stripObj->setProperty("chorusRate", (double)slot.strip.chorusRate);
    stripObj->setProperty("chorusMix", (double)slot.strip.chorusMix);
    stripObj->setProperty("reverbEnabled", (bool)slot.strip.reverbEnabled);
    stripObj->setProperty("reverbSize", (double)slot.strip.reverbSize);
    stripObj->setProperty("reverbMix", (double)slot.strip.reverbMix);
    st->setProperty("strip", stripObj);
        auto* arpObj = new juce::DynamicObject();
        arpObj->setProperty("enabled", slot.arpeggiator.enabled);
        arpObj->setProperty("bpm", (double)slot.arpeggiator.bpm);
        arpObj->setProperty("octavesUp", slot.arpeggiator.octavesUp);
        arpObj->setProperty("octavesDown", slot.arpeggiator.octavesDown);
        arpObj->setProperty("gate", (double)slot.arpeggiator.gate);
        arpObj->setProperty("patternIdx", slot.arpeggiator.patternIdx);
        st->setProperty("arpeggiator", arpObj);

        auto* harmObj = new juce::DynamicObject();
        harmObj->setProperty("enabled", slot.harmonizer.enabled);
        harmObj->setProperty("octavesUp", slot.harmonizer.octavesUp);
        harmObj->setProperty("octavesDown", slot.harmonizer.octavesDown);
        harmObj->setProperty("africaMode", slot.harmonizer.africaMode);
        harmObj->setProperty("harmonyTargetSlot", slot.harmonizer.harmonyTargetSlot);
        st->setProperty("harmonizer", harmObj);

        auto* samplerObj = new juce::DynamicObject();
        samplerObj->setProperty("enabled", slot.sampler.enabled);
        juce::Array<juce::var> samplerSlots;
        for (int idx = 0; idx < 8; ++idx) {
            auto* slo = new juce::DynamicObject();
            slo->setProperty("wavPath", slot.sampler.slots[idx].wavPath);
            slo->setProperty("rootNote", slot.sampler.slots[idx].rootNote);
            slo->setProperty("keyLow", slot.sampler.slots[idx].keyLow);
            slo->setProperty("keyHigh", slot.sampler.slots[idx].keyHigh);
            slo->setProperty("pitchOffset", (double)slot.sampler.slots[idx].pitchOffsetSemitones);
            slo->setProperty("volume", (double)slot.sampler.slots[idx].volume);
            slo->setProperty("startRatio", (double)slot.sampler.slots[idx].startRatio);
            slo->setProperty("endRatio", (double)slot.sampler.slots[idx].endRatio);
            samplerSlots.add(juce::var(slo));
        }
        samplerObj->setProperty("slots", samplerSlots);
        st->setProperty("sampler", samplerObj);

    juce::Array<juce::var> ccMappingNodes;
    for (const auto& m : slot.ccMappings) {
        auto* mo = new juce::DynamicObject();
        mo->setProperty("cc", m.cc);
        mo->setProperty("chainIndex", m.chainIndex);
        mo->setProperty("paramId", m.paramId);
        mo->setProperty("paramIndex", m.parameterIndex);
        mo->setProperty("minValue", (double)m.minValue);
        mo->setProperty("maxValue", (double)m.maxValue);
        mo->setProperty("invert", m.invert);
        ccMappingNodes.add(juce::var(mo));
    }
    st->setProperty("ccMappings", ccMappingNodes);

    juce::Array<juce::var> ccPassthroughNodes;
    for (const auto& pt : slot.ccPassthroughs) {
        auto* po = new juce::DynamicObject();
        po->setProperty("incomingCC", pt.incomingCC);
        po->setProperty("outgoingCC", pt.outgoingCC);
        ccPassthroughNodes.add(juce::var(po));
    }
    st->setProperty("ccPassthroughs", ccPassthroughNodes);

    juce::Array<juce::var> chainNodes;
    for (const auto& ps : slot.chain) {
        if (ps.path.isNotEmpty())
            chainNodes.add(writePluginState(ps));
        else
            chainNodes.add(juce::var());
    }
    st->setProperty("chain", chainNodes);

    return juce::JSON::toString(juce::var(st));
}

bool RigSerializer::readStripFromFile(const juce::File& file, SongSlot& outSlot) {
    if (!file.existsAsFile())
        return false;

    juce::String text = file.loadFileAsString();
    auto parsed = juce::JSON::parse(text);
    if (!parsed.isObject())
        return false;

    const auto& cv = parsed;

    outSlot.name = cv.getProperty("name", "Strip").toString();
    outSlot.iconIndex = cv.getProperty("iconIndex", 0);
    outSlot.channelColor = juce::Colour::fromString(
        cv.getProperty("channelColor", "#ff2a2a2a").toString());
    outSlot.level = (float)cv.getProperty("level", 0.8);
    outSlot.fohEnabled = cv.getProperty("foh", true);
    outSlot.iemEnabled = cv.getProperty("iem", true);
    outSlot.bypassed = cv.getProperty("mute", false);
    outSlot.inputChannelIndex = cv.getProperty("inputIndex", -1);
    outSlot.aux1Send = (float)cv.getProperty("aux1", 0.0);
    outSlot.aux2Send = (float)cv.getProperty("aux2", 0.0);
    outSlot.iemOffset = (float)cv.getProperty("iemOffset", 1.0);
    outSlot.transposeOctaves = cv.getProperty("transposeOctaves", 0);
    outSlot.transposeSemitones = cv.getProperty("transposeSemitones", 0);
    outSlot.lowNote = cv.getProperty("lowNote", 0);
    outSlot.highNote = cv.getProperty("highNote", 127);
    outSlot.fohCC = cv.getProperty("fohCC", -1);
    outSlot.iemCC = cv.getProperty("iemCC", -1);
    outSlot.midiChannelOverride = cv.getProperty("midiChannel", -1);

    juce::String ccListStr = cv.getProperty("allowedCCs", "64").toString();
    juce::StringArray toks;
    toks.addTokens(ccListStr, ",", "");
    for (auto& t : toks)
        if (t.isNotEmpty())
            outSlot.allowedCCs.insert(t.getIntValue());

    if (auto* so = cv.getProperty("strip", juce::var()).getDynamicObject()) {
        outSlot.strip.gateEnabled = so->getProperty("gateEnabled");
        outSlot.strip.gateThreshold = (float)so->getProperty("gateThreshold");
        outSlot.strip.eqEnabled = so->getProperty("eqEnabled");
        outSlot.strip.hpfFreq = (float)so->getProperty("hpfFreq");
        outSlot.strip.lowShelfGain = (float)so->getProperty("eqLow");
        outSlot.strip.highShelfGain = (float)so->getProperty("eqHigh");
        outSlot.strip.compEnabled = so->getProperty("compEnabled");
        outSlot.strip.compAmount = (float)so->getProperty("compAmount");
        outSlot.strip.chorusEnabled = so->hasProperty("chorusEnabled") ? (bool)so->getProperty("chorusEnabled") : false;
        outSlot.strip.chorusRate = so->hasProperty("chorusRate") ? (float)so->getProperty("chorusRate") : 1.0f;
        outSlot.strip.chorusMix = so->hasProperty("chorusMix") ? (float)so->getProperty("chorusMix") : 0.0f;
        outSlot.strip.reverbEnabled = so->hasProperty("reverbEnabled") ? (bool)so->getProperty("reverbEnabled") : false;
        outSlot.strip.reverbSize = so->hasProperty("reverbSize") ? (float)so->getProperty("reverbSize") : 0.5f;
        outSlot.strip.reverbMix = so->hasProperty("reverbMix") ? (float)so->getProperty("reverbMix") : 0.0f;
    }

    if (auto* av = cv.getProperty("arpeggiator", juce::var()).getDynamicObject()) {
        outSlot.arpeggiator.enabled = av->getProperty("enabled");
        outSlot.arpeggiator.bpm = (float)av->getProperty("bpm");
        outSlot.arpeggiator.octavesUp = (int)av->getProperty("octavesUp");
        outSlot.arpeggiator.octavesDown = (int)av->getProperty("octavesDown");
        outSlot.arpeggiator.gate = (float)av->getProperty("gate");
        outSlot.arpeggiator.patternIdx = (int)av->getProperty("patternIdx");
    }

    if (auto* hv = cv.getProperty("harmonizer", juce::var()).getDynamicObject()) {
        outSlot.harmonizer.enabled = hv->getProperty("enabled");
        outSlot.harmonizer.octavesUp = (int)hv->getProperty("octavesUp");
        outSlot.harmonizer.octavesDown = (int)hv->getProperty("octavesDown");
        outSlot.harmonizer.africaMode = hv->hasProperty("africaMode") ? (int)hv->getProperty("africaMode") : 0;
        outSlot.harmonizer.harmonyTargetSlot = hv->hasProperty("harmonyTargetSlot") ? (int)hv->getProperty("harmonyTargetSlot") : -1;
    }

    if (auto* sv = cv.getProperty("sampler", juce::var()).getDynamicObject()) {
        outSlot.sampler.enabled = sv->getProperty("enabled");
        if (auto* sls = sv->getProperty("slots").getArray()) {
            for (int sIdx = 0; sIdx < juce::jmin(8, sls->size()); ++sIdx) {
                const auto& slo = sls->getReference(sIdx);
                if (slo.isObject()) {
                    outSlot.sampler.slots[sIdx].wavPath = slo.getProperty("wavPath", "").toString();
                    outSlot.sampler.slots[sIdx].rootNote = slo.getProperty("rootNote", 60);
                    outSlot.sampler.slots[sIdx].keyLow = slo.getProperty("keyLow", 0);
                    outSlot.sampler.slots[sIdx].keyHigh = slo.getProperty("keyHigh", 127);
                    outSlot.sampler.slots[sIdx].pitchOffsetSemitones = (float)slo.getProperty("pitchOffset", 0.0);
                    outSlot.sampler.slots[sIdx].volume = (float)slo.getProperty("volume", 1.0);
                    outSlot.sampler.slots[sIdx].startRatio = (float)slo.getProperty("startRatio", 0.0);
                    outSlot.sampler.slots[sIdx].endRatio = (float)slo.getProperty("endRatio", 1.0);
                }
            }
        }
    }

    if (auto* maps = cv.getProperty("ccMappings", juce::var()).getArray()) {
        for (int m = 0; m < maps->size(); ++m) {
            const auto& mv = maps->getReference(m);
            if (!mv.isObject()) continue;
            CCMapping cm;
            cm.cc = mv.getProperty("cc", -1);
            cm.chainIndex = mv.getProperty("chainIndex", 0);
            cm.paramId = mv.getProperty("paramId", "").toString();
            cm.parameterIndex = mv.getProperty("paramIndex", mv.getProperty("parameterIndex", -1));
            cm.minValue = (float)mv.getProperty("minValue", 0.0);
            cm.maxValue = (float)mv.getProperty("maxValue", 1.0);
            cm.invert = mv.getProperty("invert", false);
            outSlot.ccMappings.push_back(cm);
        }
    }

    if (auto* pts = cv.getProperty("ccPassthroughs", juce::var()).getArray()) {
        for (int m = 0; m < pts->size(); ++m) {
            const auto& pv = pts->getReference(m);
            if (!pv.isObject()) continue;
            CCPassthrough cp;
            cp.incomingCC = pv.getProperty("incomingCC", -1);
            cp.outgoingCC = pv.getProperty("outgoingCC", -1);
            if (cp.incomingCC >= 0 && cp.outgoingCC >= 0)
                outSlot.ccPassthroughs.push_back(cp);
        }
    }

    if (auto* ch = cv.getProperty("chain", juce::var()).getArray()) {
        for (int p = 0; p < ch->size(); ++p) {
            const auto& pv = ch->getReference(p);
            PluginState ps;
            if (pv.isObject()) {
                ps.name = pv.getProperty("name", "").toString();
                ps.path = pv.getProperty("path", "").toString();
                ps.stateBase64 = pv.getProperty("state", "").toString();
                ps.uid = (int)pv.getProperty("uid", 0);
                ps.lowNote = (int)pv.getProperty("lowNote", 0);
                ps.highNote = (int)pv.getProperty("highNote", 127);
                ps.level = (float)pv.getProperty("level", 1.0);
                ps.enabled = (bool)pv.getProperty("enabled", true);
            }
            outSlot.chain.push_back(ps);
        }
    }

    return true;
}

} // namespace OpenRig
