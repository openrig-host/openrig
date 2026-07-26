#pragma once

#include <JuceHeader.h>
#include <thread>
#include "Logger.h"
#include "OpenRigEngine.h"

namespace OpenRig {

// Shared, refcounted state for off-thread -> message-thread plugin builds, so
// the async callback can NEVER dangle if the transitioner thread is torn down
// (e.g. stopTransition) before the message thread runs it. The captured
// shared_ptr keeps this state alive for the lifetime of the callback.


/**
    RigBuilder
    ----------
    Builds a rig's plugin instances OFF the audio and message threads.

    Strategy (mirrors the engine's existing reuse-by-path logic, just moved off
    the hot threads):
      - Snapshot the live rack's plugin paths.
      - For each plugin location in the new rig whose path DIFFERS from the
        live rack (or has no live instance), build + configure + prepare +
        restore-state the instance on this worker thread, then run a silent
        processBlock validation.
      - Matching paths are reused untouched (zero-cost, zero-risk).
      - Built instances are pushed into the engine's staging cache, keyed by
        rack location, for applyRig() to consume later under the callback lock.

    Any failure aborts the build; the live rack is never touched by this class.
    Failed entries are returned so RigTransitioner can retry them on the
    message thread (some VST3s require message-thread instantiation).
*/
class RigBuilder {
public:
    struct FailedEntry {
        juce::String key;
        juce::var pluginVar;
    };

    struct Result {
        bool ok = true;
        juce::String error;
        int builtCount = 0;
        int reusedCount = 0;
        std::vector<FailedEntry> failedEntries;
    };

    static Result build(OpenRigEngine &engine, const juce::var &rig,
                        std::function<void(const juce::String&)> onProgress = nullptr,
                        bool isPreload = false) {
        Result r;
        if (!rig.isObject())
            return r; // nothing to build

        auto snap = engine.snapshotPluginPaths();

        // --- Slot plugin chains ---
        if (auto *chans = rig.getProperty("channels", juce::var()).getArray()) {
            int n = std::min((int)chans->size(), engine.getNumSlots());
            for (int i = 0; i < n; ++i) {
                auto cv = chans->getReference(i);
                if (auto *chain = cv.getProperty("chain", juce::var()).getArray()) {
                    for (int p = 0; p < (int)chain->size() && p < 3; ++p) {
                        auto pv = chain->getReference(p);
                        if (!pv.isObject())
                            continue;

                        juce::String name = pv.getProperty("name", "").toString();
                        if (name.isEmpty()) {
                            name = juce::File(pv.getProperty("path", "").toString()).getFileNameWithoutExtension();
                        }
                        if (onProgress) {
                            onProgress("Loading " + name + "...");
                        }

                        juce::String label = "Slot " + juce::String(i + 1) +
                                             " chain " + juce::String(p + 1);
                        buildOne(engine, pv, OpenRigEngine::stagingKeyFor(i, p, true),
                                 snap.slotChains, i, p, label, r, isPreload);
                    }
                }
            }
        }

        // --- Master FOH FX ---
        if (auto *fx = rig.getProperty("fohFx", juce::var()).getArray()) {
            for (int p = 0; p < (int)fx->size() && p < 3; ++p) {
                auto pv = fx->getReference(p);
                if (!pv.isObject())
                    continue;

                juce::String name = pv.getProperty("name", "").toString();
                if (name.isEmpty()) {
                    name = juce::File(pv.getProperty("path", "").toString()).getFileNameWithoutExtension();
                }
                if (onProgress) {
                    onProgress("Loading " + name + " (FOH master)...");
                }

                juce::String label = "FOH master " + juce::String(p + 1);
                buildMaster(engine, pv, true, p, snap.fohChain, label, r, isPreload);
            }
        }

        // --- Master IEM FX ---
        if (auto *fx = rig.getProperty("iemFx", juce::var()).getArray()) {
            for (int p = 0; p < (int)fx->size() && p < 3; ++p) {
                auto pv = fx->getReference(p);
                if (!pv.isObject())
                    continue;

                juce::String name = pv.getProperty("name", "").toString();
                if (name.isEmpty()) {
                    name = juce::File(pv.getProperty("path", "").toString()).getFileNameWithoutExtension();
                }
                if (onProgress) {
                    onProgress("Loading " + name + " (IEM master)...");
                }

                juce::String label = "IEM master " + juce::String(p + 1);
                buildMaster(engine, pv, false, p, snap.iemChain, label, r, isPreload);
            }
        }

        return r;
    }

    // Retry building a single failed entry on the message thread. Pushes the
    // result into the staging cache on success.
    static bool rebuildOnMessageThread(OpenRigEngine &engine,
                                       const FailedEntry &entry,
                                       juce::String &error) {
        logToFile("TRACE: rebuildOnMessageThread starting for key: " + entry.key);
        std::unique_ptr<juce::AudioPluginInstance> inst;
        if (!engine.buildPluginFromVar(entry.pluginVar, inst, error) || !inst) {
            logToFile("TRACE: rebuildOnMessageThread failed to build for key: " + entry.key + ", error: " + error);
            return false;
        }
        if (!engine.validatePluginInstance(*inst)) {
            error = "silent processBlock validation failed";
            logToFile("TRACE: rebuildOnMessageThread validation failed for key: " + entry.key);
            return false;
        }
        engine.pushStagedPlugin(entry.key, std::move(inst));
        logToFile("TRACE: rebuildOnMessageThread succeeded and staged key: " + entry.key);
        return true;
    }

private:
    static bool buildOne(OpenRigEngine &engine, const juce::var &pv,
                         const juce::String &key,
                         const std::vector<juce::StringArray> &slotPaths,
                         int slotIdx, int chainIdx, const juce::String &label,
                         Result &r, bool isPreload = false) {
        if (isPreload ? engine.hasPreloadedPlugin(key) : engine.stagingHasKey(key)) {
            logToFile("TRACE: buildOne " + label + " already staged/preloaded, skipping build.");
            return true;
        }

        juce::String newPath = engine.normalizePath(pv.getProperty("path", "").toString());
        if (newPath.isEmpty())
            return true; // empty slot, nothing to build

        juce::String curPath;
        if (slotIdx < (int)slotPaths.size() && chainIdx < slotPaths[slotIdx].size())
            curPath = slotPaths[slotIdx][chainIdx];

        logToFile("TRACE: buildOne " + label + " (Key: " + key + "). curPath: " + curPath + ", newPath: " + newPath);

        // Reuse-by-path: keep the live instance for same-path plugins.
        // This avoids creating a second instance of Qt-based plugins (e.g.
        // NI Super 8) which abort if instantiated a second time or off-thread.
        // State is restored via setStateInformation in applyRig (message thread).
        if (curPath == newPath) {
            ++r.reusedCount;
            return true;
        }

        bool requiresMessageThread = newPath.containsIgnoreCase("Super 8");

        std::unique_ptr<juce::AudioPluginInstance> inst;
        juce::String err;
        bool ok = false;

        if (!requiresMessageThread) {
            logToFile("TRACE: buildOne " + label + " building off-thread...");
            struct BuildThreadContext {
                OpenRigEngine* engine;
                juce::var pluginVar;
                std::unique_ptr<juce::AudioPluginInstance> inst;
                juce::String err;
                bool ok = false;
                std::atomic<bool> done{false};
            };
            auto bctx = std::make_shared<BuildThreadContext>();
            bctx->engine = &engine;
            bctx->pluginVar = pv;

            std::thread t([bctx]() {
#ifdef _WIN32
                ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
                bctx->ok = bctx->engine->buildPluginFromVar(bctx->pluginVar, bctx->inst, bctx->err) && (bool)bctx->inst;
#ifdef _WIN32
                ::CoUninitialize();
#endif
                bctx->done.store(true);
            });

            // Wait up to 25 seconds for the background VST build to complete
            int waitCount = 0;
            while (!bctx->done.load() && waitCount < 250) {
                juce::Thread::sleep(100);
                waitCount++;
            }

            if (!bctx->done.load()) {
                logToFile("TRACE: buildOne " + label + " timed out (25s) on background build. Detaching thread...");
                t.detach(); // Allow the thread to remain stuck in background without hanging OpenRig
                ok = false;
                err = "build timed out (25s)";
            } else {
                t.join();
                inst = std::move(bctx->inst);
                err = bctx->err;
                ok = bctx->ok;
            }

            if (!ok) {
                logToFile("TRACE: buildOne off-thread build failed for " + label + ", retrying on message thread. Error: " + err);
            }
        }

        if (!ok) {
            logToFile("TRACE: buildOne " + label + " building/retrying on message thread...");
            struct BuildContext {
                OpenRigEngine* engine;
                juce::var pluginVar;
                std::unique_ptr<juce::AudioPluginInstance> inst;
                juce::String err;
                bool ok = false;
            };
            auto ctx = std::make_shared<BuildContext>();
            ctx->engine = &engine;
            ctx->pluginVar = pv;

            juce::WaitableEvent buildDone;
            juce::MessageManager::getInstance()->callAsync(
                [ctx, &buildDone]() {
                    ctx->ok = ctx->engine->buildPluginFromVar(ctx->pluginVar, ctx->inst, ctx->err) &&
                              (bool)ctx->inst;
                    buildDone.signal();
                });

            if (!buildDone.wait(180000)) {
                logToFile("TRACE: buildOne timed out (180s) on message-thread build/retry: " + label);
                r.ok = false;
                r.error = label + ": build timed out (180s)";
                r.failedEntries.push_back({key, pv});
                return false;
            }

            if (!ctx->ok) {
                logToFile("TRACE: buildOne message-thread build/retry failed for " + label + ": " + ctx->err);
                r.ok = false;
                r.error = label + ": " + ctx->err;
                r.failedEntries.push_back({key, pv});
                return false;
            }
            inst = std::move(ctx->inst);
            ok = true;
        }

        logToFile("TRACE: buildOne staging " + label);
        if (isPreload)
            engine.pushPreloadedPlugin(key, std::move(inst));
        else
            engine.pushStagedPlugin(key, std::move(inst));
        ++r.builtCount;
        return true;
    }

    static bool buildMaster(OpenRigEngine &engine, const juce::var &pv, bool isFoh,
                            int chainIdx, const juce::StringArray &masterPaths,
                            const juce::String &label, Result &r, bool isPreload = false) {
        juce::String key = OpenRigEngine::stagingKeyFor(-1, chainIdx, isFoh);
        if (isPreload ? engine.hasPreloadedPlugin(key) : engine.stagingHasKey(key)) {
            logToFile("TRACE: buildMaster " + label + " already staged/preloaded, skipping build.");
            return true;
        }

        juce::String newPath = engine.normalizePath(pv.getProperty("path", "").toString());
        if (newPath.isEmpty())
            return true;

        juce::String curPath =
            (chainIdx < masterPaths.size()) ? masterPaths[chainIdx] : "";

        logToFile("TRACE: buildMaster " + label + " (Key: " + key + "). curPath: " + curPath + ", newPath: " + newPath);

        // Reuse-by-path (see buildOne comment above).
        if (curPath == newPath) {
            ++r.reusedCount;
            return true;
        }

        bool requiresMessageThread = newPath.containsIgnoreCase("Super 8");

        std::unique_ptr<juce::AudioPluginInstance> inst;
        juce::String err;
        bool ok = false;

        if (!requiresMessageThread) {
            logToFile("TRACE: buildMaster " + label + " building off-thread...");
            struct BuildThreadContext {
                OpenRigEngine* engine;
                juce::var pluginVar;
                std::unique_ptr<juce::AudioPluginInstance> inst;
                juce::String err;
                bool ok = false;
                std::atomic<bool> done{false};
            };
            auto bctx = std::make_shared<BuildThreadContext>();
            bctx->engine = &engine;
            bctx->pluginVar = pv;

            std::thread t([bctx]() {
#ifdef _WIN32
                ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
                bctx->ok = bctx->engine->buildPluginFromVar(bctx->pluginVar, bctx->inst, bctx->err) && (bool)bctx->inst;
#ifdef _WIN32
                ::CoUninitialize();
#endif
                bctx->done.store(true);
            });

            // Wait up to 25 seconds for the background VST build to complete
            int waitCount = 0;
            while (!bctx->done.load() && waitCount < 250) {
                juce::Thread::sleep(100);
                waitCount++;
            }

            if (!bctx->done.load()) {
                logToFile("TRACE: buildMaster " + label + " timed out (25s) on background build. Detaching thread...");
                t.detach(); // Allow thread to remain stuck in background
                ok = false;
                err = "build timed out (25s)";
            } else {
                t.join();
                inst = std::move(bctx->inst);
                err = bctx->err;
                ok = bctx->ok;
            }

            if (!ok) {
                logToFile("TRACE: buildMaster off-thread build failed for " + label + ", retrying on message thread. Error: " + err);
            }
        }

        if (!ok) {
            logToFile("TRACE: buildMaster " + label + " building/retrying on message thread...");
            struct BuildContext {
                OpenRigEngine* engine;
                juce::var pluginVar;
                std::unique_ptr<juce::AudioPluginInstance> inst;
                juce::String err;
                bool ok = false;
            };
            auto ctx = std::make_shared<BuildContext>();
            ctx->engine = &engine;
            ctx->pluginVar = pv;

            juce::WaitableEvent buildDone;
            juce::MessageManager::getInstance()->callAsync(
                [ctx, &buildDone]() {
                    ctx->ok = ctx->engine->buildPluginFromVar(ctx->pluginVar, ctx->inst, ctx->err) &&
                              (bool)ctx->inst;
                    buildDone.signal();
                });

            if (!buildDone.wait(180000)) {
                logToFile("TRACE: buildMaster timed out (180s) on message-thread build/retry: " + label);
                r.ok = false;
                r.error = label + ": build timed out (180s)";
                r.failedEntries.push_back({key, pv});
                return false;
            }

            if (!ctx->ok) {
                logToFile("TRACE: buildMaster message-thread build/retry failed for " + label + ": " + ctx->err);
                r.ok = false;
                r.error = label + ": " + ctx->err;
                r.failedEntries.push_back({key, pv});
                return false;
            }
            inst = std::move(ctx->inst);
            ok = true;
        }

        logToFile("TRACE: buildMaster staging " + label);
        if (isPreload)
            engine.pushPreloadedPlugin(key, std::move(inst));
        else
            engine.pushStagedPlugin(key, std::move(inst));
        ++r.builtCount;
        return true;
    }
};

} // namespace OpenRig
