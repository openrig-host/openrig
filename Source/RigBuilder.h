#pragma once

#include <JuceHeader.h>
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <condition_variable>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>
#endif
#include "Logger.h"
#include "FanfareEngine.h"

namespace Fanfare {

// Per-path build gate: at most one build per normalized plugin path may be in
// flight — INCLUDING an abandoned one still running on its detached thread.
// Single-instance plugins (Kontakt, Zenology, Super 8) block or crash when a
// second instance touches them while the first is mid-setStateInformation, so
// a timed-out Kontakt load must fully finish (and destroy its instance)
// before the next Kontakt build starts.
inline std::mutex &buildGateMutex() { static std::mutex m; return m; }
inline std::map<juce::String, int> &liveBuildsByPath() {
  static std::map<juce::String, int> m;
  return m;
}
inline std::condition_variable &buildGateCv() {
  static std::condition_variable cv;
  return cv;
}

// Waits for any in-flight build of `path` to finish (up to timeoutMs), then
// registers this build as the live one for the path. Pair every call with
// releasePathBuildGate on the worker thread that runs the build.
inline void waitForPathBuildGate(const juce::String &path, int timeoutMs) {
  if (path.isEmpty())
    return;
  std::unique_lock<std::mutex> lk(buildGateMutex());
  buildGateCv().wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
    auto it = liveBuildsByPath().find(path);
    return it == liveBuildsByPath().end() || it->second <= 0;
  });
  auto it = liveBuildsByPath().find(path);
  int n = (it == liveBuildsByPath().end() ? 0 : it->second) + 1;
  liveBuildsByPath()[path] = n;
}

inline void releasePathBuildGate(const juce::String &path) {
  if (path.isEmpty())
    return;
  {
    std::lock_guard<std::mutex> lk(buildGateMutex());
    auto it = liveBuildsByPath().find(path);
    if (it != liveBuildsByPath().end() && --it->second <= 0)
      liveBuildsByPath().erase(it);
  }
  buildGateCv().notify_all();
}

// Paths that stalled past their timeout this session (a Kontakt multi whose
// state restore never returns — typically missing samples raising an
// un-clickable dialog inside the plugin). Later entries on the same path skip
// immediately instead of burning another timeout each.
inline std::set<juce::String> &poisonedBuildPaths() {
  static std::set<juce::String> s;
  return s;
}
inline bool isPathPoisoned(const juce::String &path) {
  if (path.isEmpty())
    return false;
  std::lock_guard<std::mutex> lk(buildGateMutex());
  return poisonedBuildPaths().count(path) > 0;
}
inline void poisonPath(const juce::String &path) {
  if (path.isEmpty())
    return;
  {
    std::lock_guard<std::mutex> lk(buildGateMutex());
    poisonedBuildPaths().insert(path);
  }
  buildGateCv().notify_all();
}

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

    static Result build(FanfareEngine &engine, const juce::var &rig,
                        std::function<void(const juce::String&)> onProgress = nullptr,
                        bool isPreload = false) {
        Result r;
        if (!rig.isObject())
            return r; // nothing to build

        auto snap = engine.snapshotPluginPaths();

        // --- Collect every plugin entry first (slot chains + master FX) so we
        // can dispatch the builds in parallel, interleaved by plugin path:
        // same-path plugins (a rack full of Kontakt multis!) serialize on the
        // per-path build gate, while DIFFERENT plugins build concurrently
        // alongside them instead of queueing behind each Kontakt.
        struct BuildEntry {
            juce::var pv;
            juce::String key;
            juce::String label;
            juce::String name;
            juce::String path;
            int slotIdx = -1, chainIdx = -1;        // buildOne when slotIdx >= 0
            bool isFoh = false; int masterIdx = -1; // buildMaster otherwise
        };
        std::vector<BuildEntry> entries;

        auto entryName = [](const juce::var &pv) {
            juce::String name = pv.getProperty("name", "").toString();
            if (name.isEmpty())
                name = juce::File(pv.getProperty("path", "").toString())
                           .getFileNameWithoutExtension();
            return name;
        };

        if (auto *chans = rig.getProperty("channels", juce::var()).getArray()) {
            int n = std::min((int)chans->size(), engine.getNumSlots());
            for (int i = 0; i < n; ++i) {
                auto cv = chans->getReference(i);
                if (auto *chain = cv.getProperty("chain", juce::var()).getArray()) {
                    for (int p = 0; p < (int)chain->size() && p < 3; ++p) {
                        auto pv = chain->getReference(p);
                        if (!pv.isObject())
                            continue;
                        BuildEntry e;
                        e.pv = pv;
                        e.key = FanfareEngine::stagingKeyFor(i, p, true);
                        e.slotIdx = i;
                        e.chainIdx = p;
                        e.label = "Slot " + juce::String(i + 1) +
                                  " chain " + juce::String(p + 1);
                        e.name = entryName(pv);
                        e.path = engine.normalizePath(
                            pv.getProperty("path", "").toString());
                        entries.push_back(std::move(e));
                    }
                }
            }
        }

        // Aux return strips (slotIdx >= 100 convention)
        if (auto *auxArr = rig.getProperty("auxReturns", juce::var()).getArray()) {
            for (int a = 0; a < auxArr->size(); ++a) {
                auto av = auxArr->getReference(a);
                if (auto *chain = av.getProperty("chain", juce::var()).getArray()) {
                    for (int p = 0; p < (int)chain->size() && p < 3; ++p) {
                        auto pv = chain->getReference(p);
                        if (!pv.isObject())
                            continue;
                        BuildEntry e;
                        e.pv = pv;
                        e.key = FanfareEngine::stagingKeyFor(100 + a, p, true);
                        e.slotIdx = 100 + a;
                        e.chainIdx = p;
                        e.label = "Aux " + juce::String(a + 1) +
                                  " chain " + juce::String(p + 1);
                        e.name = entryName(pv);
                        e.path = engine.normalizePath(
                            pv.getProperty("path", "").toString());
                        entries.push_back(std::move(e));
                    }
                }
            }
        }

        auto collectMasterFx = [&](const char *prop, bool isFoh,
                                   const juce::StringArray &masterPaths) {
            if (auto *fx = rig.getProperty(prop, juce::var()).getArray()) {
                for (int p = 0; p < (int)fx->size() && p < 3; ++p) {
                    auto pv = fx->getReference(p);
                    if (!pv.isObject())
                        continue;
                    BuildEntry e;
                    e.pv = pv;
                    e.isFoh = isFoh;
                    e.masterIdx = p;
                    e.label = juce::String(isFoh ? "FOH master " : "IEM master ") +
                              juce::String(p + 1);
                    e.name = entryName(pv);
                    e.path = engine.normalizePath(
                        pv.getProperty("path", "").toString());
                    entries.push_back(std::move(e));
                }
            }
        };
        collectMasterFx("fohFx", true, snap.fohChain);
        collectMasterFx("iemFx", false, snap.iemChain);

        // Round-robin the dispatch order by plugin path so consecutive builds
        // are rarely the same plugin: Kontakt, Rhodes, Kontakt, B3, Kontakt...
        {
            std::map<juce::String, std::deque<int>> byPath;
            for (int idx = 0; idx < (int)entries.size(); ++idx) {
                juce::String k = entries[idx].path.isEmpty()
                                     ? "@none@" + juce::String(idx)
                                     : entries[idx].path;
                byPath[k].push_back(idx);
            }
            std::vector<BuildEntry> ordered;
            ordered.reserve(entries.size());
            bool any = true;
            while (any) {
                any = false;
                for (auto &kv : byPath) {
                    if (!kv.second.empty()) {
                        ordered.push_back(std::move(entries[kv.second.front()]));
                        kv.second.pop_front();
                        any = true;
                    }
                }
            }
            entries.swap(ordered);
        }

        logToFile("TRACE: RigBuilder dispatching " + juce::String((int)entries.size()) +
                  " plugin build(s) in parallel (interleaved by path).");

        // --- Parallel dispatch: one worker per entry. Same-path entries line
        // up on the per-path build gate inside buildOne/buildMaster; different
        // paths run at the same time.
        std::vector<std::thread> workers;
        std::vector<std::unique_ptr<Result>> results(entries.size());
        for (int idx = 0; idx < (int)entries.size(); ++idx) {
            results[idx] = std::make_unique<Result>();
            BuildEntry e = entries[idx]; // per-worker copy
            Result *res = results[idx].get();
            workers.emplace_back([&engine, &snap, e, res, isPreload,
                                  onProgress]() {
                if (onProgress)
                    onProgress("Loading " + e.name + "...");
                if (e.slotIdx >= 0)
                    buildOne(engine, e.pv, e.key, snap.slotChains,
                             snap.auxChains, e.slotIdx, e.chainIdx, e.label,
                             *res, isPreload);
                else
                    buildMaster(engine, e.pv, e.isFoh, e.masterIdx,
                                e.isFoh ? snap.fohChain : snap.iemChain,
                                e.label, *res, isPreload);
            });
        }
        for (auto &t : workers)
            t.join();

        // --- Merge the per-entry results
        for (auto &res : results) {
            r.builtCount += res->builtCount;
            r.reusedCount += res->reusedCount;
            if (!res->failedEntries.empty()) {
                r.ok = false;
                if (r.error.isNotEmpty())
                    r.error += "; ";
                r.error += res->error;
                r.failedEntries.insert(r.failedEntries.end(),
                                       res->failedEntries.begin(),
                                       res->failedEntries.end());
            }
        }
        return r;
    }

    // Retry building a single failed entry on the message thread. Pushes the
    // result into the staging cache on success.
    static bool rebuildOnMessageThread(FanfareEngine &engine,
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
    static bool buildOne(FanfareEngine &engine, const juce::var &pv,
                         const juce::String &key,
                         const std::vector<juce::StringArray> &slotPaths,
                         const std::vector<juce::StringArray> &auxPaths,
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
        if (slotIdx >= 100) {
            int auxIdx = slotIdx - 100;
            if (auxIdx < (int)auxPaths.size() && chainIdx < auxPaths[auxIdx].size())
                curPath = auxPaths[auxIdx][chainIdx];
        } else if (slotIdx < (int)slotPaths.size() && chainIdx < slotPaths[slotIdx].size()) {
            curPath = slotPaths[slotIdx][chainIdx];
        }

        logToFile("TRACE: buildOne " + label + " (Key: " + key + "). curPath: " + curPath + ", newPath: " + newPath);

        // Reuse-by-path: keep the live instance for same-path plugins.
        // This avoids creating a second concurrent instance of single-instance plugins (Kontakt, Roland Zenology/XV-5080, Super 8)
        // which block or crash in their internal singleton lock when instantiated concurrently.
        if (curPath == newPath) {
            logToFile("TRACE: buildOne " + label + " reusing existing plugin instance (curPath == newPath): " + newPath);
            ++r.reusedCount;
            return true;
        }

        if (Fanfare::isPathPoisoned(newPath)) {
            logToFile("WARNING: buildOne " + label + " skipped — this plugin path already stalled past its timeout this session (" + newPath + "). Fix the offending multi (e.g. missing samples in Kontakt) or restart to retry.");
            r.ok = false;
            r.error = label + ": skipped (plugin stalled earlier this session)";
            r.failedEntries.push_back({key, pv});
            return false;
        }

        // Super 8 (NI) needs message-thread instantiation. Kontakt (NI) needs
        // it for state restore: bulk background-thread setStateInformation on
        // the 2nd+ instance in a process deadlocks reliably (Last Waltz rig,
        // Oct 2026), while interactive message-thread loads of the same
        // instances work. Restores are sub-second when they work.
        // Replika XT (Arturia): instantiation hangs off-thread in bulk loads
        // (Oct 7 — stalled the whole rig build on its first aux-return save).
        // HALion 7 (Steinberg): off-thread instantiation is racy — hung once,
        // built once (Oct 8) — and its worker AV'd when abandoned.
        bool requiresMessageThread =
            newPath.containsIgnoreCase("Super 8") ||
            newPath.containsIgnoreCase("Kontakt") ||
            newPath.containsIgnoreCase("Replika") ||
            newPath.containsIgnoreCase("HALion");

        std::unique_ptr<juce::AudioPluginInstance> inst;
        juce::String err;
        bool ok = false;

        if (!requiresMessageThread) {
            // Serialize per path: wait out any earlier build of this same
            // plugin (possibly an abandoned one still restoring state).
            Fanfare::waitForPathBuildGate(newPath, 120000);
            logToFile("TRACE: buildOne " + label + " building off-thread...");
            struct BuildThreadContext {
                FanfareEngine* engine;
                juce::var pluginVar;
                std::unique_ptr<juce::AudioPluginInstance> inst;
                juce::String err;
                bool ok = false;
                std::atomic<bool> done{false};
                std::atomic<bool> abandoned{false};
            };
            auto bctx = std::make_shared<BuildThreadContext>();
            bctx->engine = &engine;
            bctx->pluginVar = pv;
            juce::String gatePath = newPath;

            std::thread t([bctx, gatePath]() {
#ifdef _WIN32
                ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
                std::unique_ptr<juce::AudioPluginInstance> tmpInst;
                juce::String tmpErr;
                bool tmpOk = bctx->engine->buildPluginFromVar(bctx->pluginVar, tmpInst, tmpErr) && (bool)tmpInst;
                if (!bctx->abandoned.load()) {
                    bctx->inst = std::move(tmpInst);
                    bctx->err = tmpErr;
                    bctx->ok = tmpOk;
                } else {
                    tmpInst = nullptr; // Instantly release instance and host resources if thread was abandoned
                }
#ifdef _WIN32
                ::CoUninitialize();
#endif
                Fanfare::releasePathBuildGate(gatePath);
                bctx->done.store(true);
            });

            // Wait up to 10 seconds for the background VST build. If it
            // stalls (engines whose instantiation needs the host's message
            // thread — UVI Sep 7, Replika XT Oct 7 — hang here forever),
            // abandon the worker and fall through to the message-thread
            // retry below, which is the proven cure. Without this the whole
            // rig load blocks on one plugin until the 600s timeout.
            int waitCount = 0;
            while (!bctx->done.load() && waitCount < 100) {
                juce::Thread::sleep(100);
                waitCount++;
            }

            bool timedOut = false;
            (void)timedOut;
            if (!bctx->done.load()) {
                bctx->abandoned.store(true);
                logToFile("TRACE: buildOne " + label + " off-thread build stalled (10s) — abandoning worker, retrying on message thread...");
                t.detach(); // the stuck worker destroys its instance when/if it returns
                ok = false;
                err = "off-thread build stalled (10s)";
            } else {
                t.join();
                inst = std::move(bctx->inst);
                err = bctx->err;
                ok = bctx->ok;
            }

            if (!ok) {
                logToFile("TRACE: buildOne off-thread build failed for " + label + ". Error: " + err);
            }
        }

        if (!ok) {
            logToFile("TRACE: buildOne " + label + " building/retrying on message thread...");
            struct BuildContext {
                FanfareEngine* engine;
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

            if (!buildDone.wait(60000)) {
                Fanfare::poisonPath(newPath);
                logToFile("TRACE: buildOne timed out (60s) on message-thread build/retry: " + label);
                r.ok = false;
                r.error = label + ": build timed out (60s)";
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

    static bool buildMaster(FanfareEngine &engine, const juce::var &pv, bool isFoh,
                            int chainIdx, const juce::StringArray &masterPaths,
                            const juce::String &label, Result &r, bool isPreload = false) {
        juce::String key = FanfareEngine::stagingKeyFor(-1, chainIdx, isFoh);
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

        // Reuse-by-path for master FX plugins.
        if (curPath == newPath) {
            logToFile("TRACE: buildMaster " + label + " reusing existing plugin instance (curPath == newPath): " + newPath);
            ++r.reusedCount;
            return true;
        }

        if (Fanfare::isPathPoisoned(newPath)) {
            logToFile("WARNING: buildMaster " + label + " skipped — this plugin path already stalled past its timeout this session (" + newPath + ").");
            r.ok = false;
            r.error = label + ": skipped (plugin stalled earlier this session)";
            r.failedEntries.push_back({key, pv});
            return false;
        }

        // Same rule as buildOne: Super 8 needs a message-thread build, and
        // Kontakt state restores deadlock off-thread on the 2nd+ instance.
        // Replika XT instantiation hangs off-thread in bulk loads (Oct 7).
        // HALion 7 off-thread instantiation is racy (Oct 8).
        bool requiresMessageThread =
            newPath.containsIgnoreCase("Super 8") ||
            newPath.containsIgnoreCase("Kontakt") ||
            newPath.containsIgnoreCase("Replika") ||
            newPath.containsIgnoreCase("HALion");

        std::unique_ptr<juce::AudioPluginInstance> inst;
        juce::String err;
        bool ok = false;

        if (!requiresMessageThread) {
            Fanfare::waitForPathBuildGate(newPath, 120000);
            logToFile("TRACE: buildMaster " + label + " building off-thread...");
            struct BuildThreadContext {
                FanfareEngine* engine;
                juce::var pluginVar;
                std::unique_ptr<juce::AudioPluginInstance> inst;
                juce::String err;
                bool ok = false;
                std::atomic<bool> done{false};
                std::atomic<bool> abandoned{false};
            };
            auto bctx = std::make_shared<BuildThreadContext>();
            bctx->engine = &engine;
            bctx->pluginVar = pv;
            juce::String gatePath = newPath;

            std::thread t([bctx, gatePath]() {
#ifdef _WIN32
                ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
                std::unique_ptr<juce::AudioPluginInstance> tmpInst;
                juce::String tmpErr;
                bool tmpOk = bctx->engine->buildPluginFromVar(bctx->pluginVar, tmpInst, tmpErr) && (bool)tmpInst;
                if (!bctx->abandoned.load()) {
                    bctx->inst = std::move(tmpInst);
                    bctx->err = tmpErr;
                    bctx->ok = tmpOk;
                } else {
                    tmpInst = nullptr; // Instantly release instance and host resources if thread was abandoned
                }
#ifdef _WIN32
                ::CoUninitialize();
#endif
                Fanfare::releasePathBuildGate(gatePath);
                bctx->done.store(true);
            });

            // Wait up to 10 seconds (see buildOne: stall → message-thread retry)
            int waitCount = 0;
            while (!bctx->done.load() && waitCount < 100) {
                juce::Thread::sleep(100);
                waitCount++;
            }

            if (!bctx->done.load()) {
                bctx->abandoned.store(true);
                logToFile("TRACE: buildMaster " + label + " off-thread build stalled (10s) — abandoning worker, retrying on message thread...");
                t.detach(); // Allow thread to remain stuck in background
                ok = false;
                err = "off-thread build stalled (10s)";
            } else {
                t.join();
                inst = std::move(bctx->inst);
                err = bctx->err;
                ok = bctx->ok;
            }

            if (!ok) {
                logToFile("TRACE: buildMaster off-thread build failed for " + label + ". Error: " + err);
            }
        }

        if (!ok) {
            logToFile("TRACE: buildMaster " + label + " building/retrying on message thread...");
            struct BuildContext {
                FanfareEngine* engine;
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

            if (!buildDone.wait(60000)) {
                Fanfare::poisonPath(newPath);
                logToFile("TRACE: buildMaster timed out (60s) on message-thread build/retry: " + label);
                r.ok = false;
                r.error = label + ": build timed out (60s)";
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

} // namespace Fanfare
