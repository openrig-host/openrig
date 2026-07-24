#pragma once

#include <JuceHeader.h>
#include "OpenRigEngine.h"
#include "RigBuilder.h"
#include "RigSerializer.h"

namespace OpenRig {

/**
    RigTransitioner
    ---------------
    Orchestrates trustworthy song-to-song switching with rollback-by-
    construction: the live rack is NEVER touched until a fully validated
    replacement is ready.

    Pipeline (all off the audio thread; the message thread stays responsive):
      1. Load + migrate the rig JSON (RigSerializer).
      2. Build changed plugins off-thread into the staging cache, with silent
         processBlock validation (RigBuilder). Reuse-by-path = zero cost.
      3. If off-thread build failed, retry ONLY the failed plugins on the
         message thread (some VST3s require message-thread instantiation).
      4. Acquire the engine callback lock once and applyRig() (fast: heavy
         work already done, cached instances just swap in).
      5. Clear the staging cache and notify the UI.

    At no point is the current rig partially replaced: any failure before step
    4 leaves the live rack 100% intact.
*/
class RigTransitioner : public juce::Thread {
public:
    struct Callbacks {
        // Invoked on the message thread.
        std::function<void(juce::String)> onProgress;
        // Invoked on the message thread when the transition finishes.
        std::function<void(bool ok, juce::String message, int builtCount)> onComplete;
    };

    RigTransitioner(OpenRigEngine &e)
        : juce::Thread("OpenRig Transitioner"), engine(e) {}

    ~RigTransitioner() override { stopTransition(); }

    bool isBusy() const { return isThreadRunning(); }

    /** Begin an async transition to a rig file. Call from the message thread. */
    void transitionToFile(const juce::File &file, Callbacks cbs) {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        if (isThreadRunning())
            return;
        sourceFile = file;
        sourceJson.clear();
        useFile = true;
        callbacks = std::move(cbs);
        startThread();
    }

    /** Begin an async transition from a JSON string. Call from the message thread. */
    void transitionToJson(const juce::String &json, Callbacks cbs) {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        if (isThreadRunning())
            return;
        sourceJson = json;
        useFile = false;
        callbacks = std::move(cbs);
        startThread();
    }

    void stopTransition() {
        if (isThreadRunning()) {
            signalThreadShouldExit();
            waitForThreadToExit(3000);
        }
        engine.clearStagingCache();
    }

    void run() override {
        logToFile("TRACE: RigTransitioner run() started.");
        if (engine.hasPreloadedPlugins()) {
            logToFile("TRACE: Promoting preloaded plugins to staging cache.");
            engine.promotePreloadedToStaged();
        } else {
            engine.clearStagingCache();
        }

        // 1. Load + migrate.
        postProgress("Loading rig...");
        logToFile("TRACE: RigTransitioner loading setup data.");
        RigSerializer::LoadResult loaded =
            useFile ? RigSerializer::load(sourceFile)
                    : RigSerializer::loadFromString(sourceJson);

        if (!loaded.ok) {
            logToFile("TRACE: RigTransitioner load failed: " + loaded.errorMessage);
            postComplete(false, loaded.errorMessage, 0);
            return;
        }

        // 2. Build on this worker thread (keeps UI responsive; heavy plugins
        //    like Kontakt won't freeze the app). Reuse-by-path ensures we
        //    never create a second instance of the same plugin. Exception
        //    guards in buildPluginFromVar catch any plugin that throws.
        postProgress("Building plugins...");
        RigBuilder::Result build = RigBuilder::build(engine, loaded.rig, [this](const juce::String &msg) {
            postProgress(msg);
        });
        logToFile("TRACE: RigBuilder finished. ok=" + juce::String((int)build.ok) +
                  ", built=" + juce::String(build.builtCount) +
                  ", reused=" + juce::String(build.reusedCount) +
                  ", failed=" + juce::String((int)build.failedEntries.size()));

        // 3. If some plugins failed, log the error but CONTINUE — the staging
        //    cache has the successful ones, and failed slots stay empty.
        //    We do NOT retry on the message thread: heavy plugins like Kontakt
        //    can hang the message thread for minutes, freezing the UI.
        if (!build.ok) {
            logToFile("WARNING: " + juce::String((int)build.failedEntries.size()) +
                      " plugin(s) failed to build: " + build.error);
        }

        if (threadShouldExit()) {
            logToFile("TRACE: RigTransitioner exit requested before apply.");
            postComplete(false, "Cancelled", 0);
            return;
        }

        // 4. Atomic apply under the callback lock on the message thread.
        //    Timeout prevents hanging if a plugin blocks during state restore.
        postProgress("Applying...");
        logToFile("TRACE: RigTransitioner dispatching applyRig to main thread.");
        auto sharedRig = std::make_shared<juce::var>(std::move(loaded.rig));
        auto doneEvent = std::make_shared<juce::WaitableEvent>();
        juce::MessageManager::getInstance()->callAsync([this, sharedRig, doneEvent]() {
            logToFile("TRACE: RigTransitioner applying rig on main thread.");
            engine.applyRig(*sharedRig);
            doneEvent->signal();
        });
        if (!doneEvent->wait(60000)) {
            logToFile("ERROR: applyRig timed out (60s)");
            engine.clearStagingCache();
            postComplete(false, "Apply timed out (60s) — possible plugin hang", 0);
            return;
        }
        logToFile("TRACE: RigTransitioner applyRig finished.");

        // 5. Clean up and notify.
        engine.clearStagingCache();
        int total = build.builtCount + build.reusedCount;
        juce::String msg = "Loaded (" + juce::String(build.builtCount) + " built, " +
                           juce::String(build.reusedCount) + " reused)";
        logToFile("TRACE: RigTransitioner completed successfully: " + msg);
        postComplete(true, msg, total);
    }

private:
    void postProgress(const juce::String &msg) {
        if (callbacks.onProgress) {
            auto cb = callbacks.onProgress;
            juce::MessageManager::callAsync([cb, msg]() { cb(msg); });
        }
    }

    void postComplete(bool ok, const juce::String &msg, int builtCount) {
        if (callbacks.onComplete) {
            auto cb = callbacks.onComplete;
            juce::MessageManager::callAsync([cb, ok, msg, builtCount]() {
                cb(ok, msg, builtCount);
            });
        }
    }

    OpenRigEngine &engine;
    Callbacks callbacks;

    juce::File sourceFile;
    juce::String sourceJson;
    bool useFile = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RigTransitioner)
};

} // namespace OpenRig
