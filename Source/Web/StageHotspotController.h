#pragma once

#include <JuceHeader.h>
#include <functional>
#include <atomic>
#include <memory>

namespace OpenRig {

/**
 * Controller for managing the Windows 11 Wi-Fi SoftAP / Mobile Hotspot.
 * Allows musicians to launch a dedicated stage Wi-Fi network directly from OpenRig.
 */
class StageHotspotController : public juce::Thread {
public:
    enum class State {
        Stopped,
        Starting,
        Active,
        Stopping,
        Error
    };

    StageHotspotController();
    ~StageHotspotController() override;

    /**
     * Start the stage Wi-Fi hotspot in the background.
     */
    void startHotspot(const juce::String& ssid = "OpenRig-Stage",
                      const juce::String& password = "openrig2026",
                      std::function<void(bool success, juce::String message)> onFinished = nullptr);

    /**
     * Stop the active stage hotspot.
     */
    void stopHotspot(std::function<void(bool success, juce::String message)> onFinished = nullptr);

    /**
     * Check if the hotspot is actively running.
     */
    bool isActive() const { return currentState.load() == State::Active; }
    State getState() const { return currentState.load(); }

    juce::String getSsid() const { return activeSsid; }
    juce::String getPassword() const { return activePassword; }

    /**
     * Get all local IP addresses across active network adapters.
     */
    static juce::StringArray getAvailableIpAddresses();

    /**
     * Callback triggered whenever the hotspot state changes.
     */
    std::function<void(State newState, const juce::String& statusMessage)> onStateChanged;

private:
    void run() override;

    enum class TaskType {
        None,
        Start,
        Stop
    };

    std::atomic<State> currentState{State::Stopped};
    std::atomic<TaskType> pendingTask{TaskType::None};

    juce::String activeSsid{"OpenRig-Stage"};
    juce::String activePassword{"openrig2026"};
    juce::String lastMessage{"Hotspot Stopped"};

    std::function<void(bool, juce::String)> pendingCallback;
    std::mutex taskMutex;

    bool executeWindowsHotspotScript(bool enable, const juce::String& ssid, const juce::String& password, juce::String& outMessage);
};

} // namespace OpenRig
