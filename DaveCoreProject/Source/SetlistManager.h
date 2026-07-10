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
    ~SetlistPreloaderThread() override { stopPreloading(); }

    void preloadSetup(const juce::File& file) {
        stopPreloading();
        setupFile = file;
        isPreloaded = false;
        isPreloadFailed = false;
        if (engine != nullptr && setupFile.existsAsFile()) {
            startThread(juce::Thread::Priority::normal);
            triggerChangeCallback();
        } else {
            triggerChangeCallback();
        }
    }

    void stopPreloading() {
        if (isThreadRunning()) {
            signalThreadShouldExit();
            waitForThreadToExit(3000);
        }
        isPreloaded = false;
        isPreloadFailed = false;
    }

    void run() override {
        if (engine == nullptr) return;
        engine->clearPreloadedCache();

        auto loaded = RigSerializer::load(setupFile);
        if (!loaded.ok) {
            isPreloadFailed = true;
            triggerChangeCallback();
            return;
        }

        if (threadShouldExit()) return;

        // Build with isPreload = true so instances are stored in preloadedPlugins
        auto buildResult = RigBuilder::build(*engine, loaded.rig, nullptr, true);
        if (!buildResult.ok) {
            isPreloadFailed = true;
        } else {
            isPreloaded = true;
        }
        triggerChangeCallback();
    }

    bool isPreloading() const { return isThreadRunning(); }
    bool getIsPreloaded() const { return isPreloaded; }
    bool getIsPreloadFailed() const { return isPreloadFailed; }
    juce::File getSetupFile() const { return setupFile; }

    std::function<void()> onStateChanged;

private:
    void triggerChangeCallback() {
        if (onStateChanged) {
            juce::MessageManager::getInstance()->callAsync([this]() {
                if (onStateChanged) onStateChanged();
            });
        }
    }

    OpenRigEngine* engine;
    juce::File setupFile;
    std::atomic<bool> isPreloaded{false};
    std::atomic<bool> isPreloadFailed{false};
};

class SetlistManager : public juce::ChangeBroadcaster {
public:
    static SetlistManager& getInstance() {
        static SetlistManager instance;
        return instance;
    }

    void setEngine(OpenRigEngine* newEngine) {
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
        if (hasNext())
            return setups[activeIndex + 1];
        return {};
    }

    juce::File getPrevFile() const {
        if (hasPrev())
            return setups[activeIndex - 1];
        return {};
    }

    bool saveSetlist(const juce::File& file) {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        juce::Array<juce::var> pathsArr;
        for (const auto& f : setups) {
            pathsArr.add(f.getFileName());
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

        juce::File nextFile = getNextFile();
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
