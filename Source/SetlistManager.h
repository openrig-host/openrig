#pragma once

#include <JuceHeader.h>
#include "OpenRigEngine.h"
#include "RigLibrary.h"
#include "RigBuilder.h"
#include "RigSerializer.h"

namespace OpenRig {

class SetlistPreloaderThread : public juce::Thread {
public:
    SetlistPreloaderThread(OpenRigEngine* e)
        : juce::Thread("Setlist Preloader"), engine(e) {}

    ~SetlistPreloaderThread() override {
        if (isThreadRunning()) {
            signalThreadShouldExit();
            requestSignal();
            stopThread(5000);
        }
    }

    // (Re)target the preload. Safe to call any time, even while a build is in
    // flight: the in-flight build is invalidated and the latest target wins
    // once the current build finishes. Never restarts a running thread.
    void preloadSetup(const juce::File& file) {
        if (engine == nullptr) return;
        {
            const juce::ScopedLock sl(targetLock);
            cancelFlag.store(true);          // any in-flight build result is now stale
            pendingTarget = file;
            hasPendingTarget = file.existsAsFile();
        }
        isPreloaded = false;
        isPreloadFailed = false;
        requestSignal();
        if (!isThreadRunning())
            startThread(juce::Thread::Priority::normal);
        triggerChangeCallback();
    }

    // Cancel the current/queued preload. Does not kill the worker thread (the
    // destructor does that), so re-preloading later never races a restart.
    void stopPreloading() {
        {
            const juce::ScopedLock sl(targetLock);
            cancelFlag.store(true);
            hasPendingTarget = false;
            pendingTarget = juce::File{};
            setupFile = juce::File{};
        }
        isPreloaded = false;
        isPreloadFailed = false;
        requestSignal();
        triggerChangeCallback();
    }

    void run() override {
        while (!threadShouldExit()) {
            juce::File target;
            {
                const juce::ScopedLock sl(targetLock);
                if (hasPendingTarget) {
                    target = pendingTarget;
                    setupFile = target;
                    hasPendingTarget = false;
                    cancelFlag.store(false); // fresh build
                }
            }

            if (target == juce::File{}) {
                targetChanged.wait(100); // idle until a new target arrives
                continue;
            }

            if (engine == nullptr) break;

            isBuilding = true;
            triggerChangeCallback();

            engine->clearPreloadedCache();
            if (threadShouldExit() || cancelFlag.load()) { isBuilding = false; continue; }

            auto loaded = RigSerializer::load(target);
            if (threadShouldExit() || cancelFlag.load()) { isBuilding = false; continue; }
            if (!loaded.ok) {
                isPreloadFailed = true;
                isBuilding = false;
                triggerChangeCallback();
                continue;
            }

            // Build with isPreload = true so instances are stored in preloadedPlugins
            auto buildResult = RigBuilder::build(*engine, loaded.rig, nullptr, true);
            if (threadShouldExit() || cancelFlag.load()) { isBuilding = false; continue; }

            if (!buildResult.ok)
                isPreloadFailed = true;
            else
                isPreloaded = true;
            isBuilding = false;
            triggerChangeCallback();
        }
    }

    bool isPreloading() const { return isBuilding.load(); }
    bool getIsPreloaded() const { return isPreloaded; }
    bool getIsPreloadFailed() const { return isPreloadFailed; }
    juce::File getSetupFile() const {
        const juce::ScopedLock sl(targetLock);
        return setupFile;
    }

    std::function<void()> onStateChanged;

private:
    void requestSignal() { targetChanged.signal(); }
    void triggerChangeCallback() {
        if (onStateChanged) {
            juce::WeakReference<SetlistPreloaderThread> safe(this);
            juce::MessageManager::getInstance()->callAsync([safe]() {
                if (safe.get() != nullptr && safe->onStateChanged)
                    safe->onStateChanged();
            });
        }
    }

    OpenRigEngine* engine;
    mutable juce::CriticalSection targetLock;
    juce::File pendingTarget;
    bool hasPendingTarget = false;
    juce::File setupFile;
    juce::WaitableEvent targetChanged;
    std::atomic<bool> cancelFlag{false};
    std::atomic<bool> isBuilding{false};
    std::atomic<bool> isPreloaded{false};
    std::atomic<bool> isPreloadFailed{false};

    JUCE_DECLARE_WEAK_REFERENCEABLE(SetlistPreloaderThread)
};

class SetlistManager : public juce::ChangeBroadcaster {
public:
    static SetlistManager& getInstance() {
        static SetlistManager instance;
        return instance;
    }

    void setEngine(OpenRigEngine* newEngine) {
        if (preloaderThread)
            preloaderThread->stopPreloading(); // cancel any in-flight build first
        engine = newEngine;
        preloaderThread = std::make_unique<SetlistPreloaderThread>(engine);
        preloaderThread->onStateChanged = [this]() {
            sendChangeMessage();
        };
    }

    bool isPreloading() const {
        return preloaderThread ? preloaderThread->isPreloading() : false;
    }
    bool isPreloaded() const {
        return preloaderThread ? preloaderThread->getIsPreloaded() : false;
    }
    bool isPreloadFailed() const {
        return preloaderThread ? preloaderThread->getIsPreloadFailed() : false;
    }
    juce::String getPreloadSetupName() const {
        return preloaderThread ? preloaderThread->getSetupFile().getFileNameWithoutExtension() : "";
    }

    const juce::Array<juce::File>& getSetups() const { return setups; }
    int getActiveIndex() const { return activeIndex; }

    void addSetup(const juce::File& file) {
        if (file.existsAsFile()) {
            setups.add(file);
            sendChangeMessage();
            triggerPreloadOfNext();
        }
    }

    void removeSetup(int index) {
        if (index >= 0 && index < setups.size()) {
            setups.remove(index);
            if (activeIndex >= setups.size())
                activeIndex = setups.size() - 1;
            sendChangeMessage();
            triggerPreloadOfNext();
        }
    }

    void clearSetlist() {
        setups.clear();
        activeIndex = -1;
        currentSetlistFile = juce::File{};
        if (engine != nullptr)
            engine->clearPreloadedCache();
        sendChangeMessage();
    }

    void moveSetupUp(int index) {
        if (index > 0 && index < setups.size()) {
            setups.swap(index, index - 1);
            if (activeIndex == index)
                activeIndex = index - 1;
            else if (activeIndex == index - 1)
                activeIndex = index;
            sendChangeMessage();
            triggerPreloadOfNext();
        }
    }

    void moveSetupDown(int index) {
        if (index >= 0 && index < setups.size() - 1) {
            setups.swap(index, index + 1);
            if (activeIndex == index)
                activeIndex = index + 1;
            else if (activeIndex == index + 1)
                activeIndex = index;
            sendChangeMessage();
            triggerPreloadOfNext();
        }
    }

    void setActiveIndex(int index) {
        if (index >= 0 && index < setups.size()) {
            activeIndex = index;
            sendChangeMessage();
            triggerPreloadOfNext();
        }
    }

    juce::File getActiveFile() const {
        if (activeIndex >= 0 && activeIndex < setups.size())
            return setups[activeIndex];
        return {};
    }

    bool hasNext() const {
        return activeIndex >= 0 && activeIndex < setups.size() - 1;
    }

    bool hasPrev() const {
        return activeIndex > 0 && activeIndex < setups.size();
    }

    juce::File getNextFile() const {
        return getNextPreloadTarget();
    }

    juce::File getPrevFile() const {
        if (hasPrev())
            return setups[activeIndex - 1];
        return {};
    }

    juce::File getNextPreloadTarget() const {
        const int n = setups.size();
        if (n == 0)
            return {};
        const int start = (activeIndex >= 0) ? activeIndex : 0;
        // Scan forward (wrapping) for the next slot that has a real setup file,
        // skipping empty slots. Wraps to CORE (slot 0) naturally if needed.
        for (int offset = 1; offset < n; ++offset) {
            const int idx = (start + offset) % n;
            if (setups[idx].existsAsFile())
                return setups[idx];
        }
        return {};
    }

    void setSlotSetup(int index, const juce::File& file) {
        if (index < 0) return;
        while (setups.size() <= index) {
            setups.add(juce::File{});
        }
        setups.set(index, file);
        sendChangeMessage();
        triggerPreloadOfNext();
    }

    bool saveSetlist(const juce::File& file) {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        juce::Array<juce::var> pathsArr;
        auto songsDir = RigLibrary::getSongsDirectory();
        for (const auto& f : setups) {
            // Store paths relative to the songs directory so setlists are
            // portable. For in-dir files this is a bare filename; for files
            // elsewhere it is a ../ or absolute path, both resolvable on reload.
            pathsArr.add(f.getRelativePathFrom(songsDir));
        }
        obj->setProperty("setups", pathsArr);
        obj->setProperty("activeIndex", activeIndex);

        file.getParentDirectory().createDirectory();
        juce::FileOutputStream fos(file);
        if (fos.openedOk()) {
            fos.setPosition(0);
            fos.truncate();
            juce::JSON::writeToStream(fos, juce::var(obj.get()));
            currentSetlistFile = file;
            return true;
        }
        return false;
    }

    bool loadSetlist(const juce::File& file) {
        if (!file.existsAsFile())
            return false;

        auto parsed = juce::JSON::parse(file);
        if (auto* obj = parsed.getDynamicObject()) {
            setups.clear();
            if (auto* arr = obj->getProperty("setups").getArray()) {
                for (int i = 0; i < arr->size(); ++i) {
                    juce::String name = arr->getReference(i).toString();
                    juce::File setupFile = RigLibrary::getSongsDirectory().getChildFile(name);
                    if (setupFile.existsAsFile()) {
                        setups.add(setupFile);
                    } else {
                        setups.add(juce::File(name));
                    }
                }
            }
            activeIndex = obj->getProperty("activeIndex");
            if (activeIndex >= setups.size())
                activeIndex = setups.size() - 1;
            
            currentSetlistFile = file;
            sendChangeMessage();
            triggerPreloadOfNext();
            return true;
        }
        return false;
    }

    juce::File getCurrentSetlistFile() const { return currentSetlistFile; }

    void triggerPreloadOfNext() {
        if (engine == nullptr || preloaderThread == nullptr)
            return;

        juce::File nextFile = getNextPreloadTarget();
        if (nextFile.existsAsFile()) {
            preloaderThread->preloadSetup(nextFile);
        } else {
            preloaderThread->stopPreloading();
            engine->clearPreloadedCache();
            sendChangeMessage();
        }
    }

private:
    SetlistManager() {
        preloaderThread = std::make_unique<SetlistPreloaderThread>(nullptr);
        preloaderThread->onStateChanged = [this]() {
            sendChangeMessage();
        };
    }
    ~SetlistManager() override {
        if (preloaderThread)
            preloaderThread->stopPreloading();
    }

    OpenRigEngine* engine = nullptr;
    std::unique_ptr<SetlistPreloaderThread> preloaderThread;
    juce::Array<juce::File> setups;
    int activeIndex = -1;
    juce::File currentSetlistFile;

    JUCE_DECLARE_NON_COPYABLE(SetlistManager)
};

} // namespace OpenRig
