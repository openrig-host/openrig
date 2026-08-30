#include "StageHotspotController.h"

namespace OpenRig {

StageHotspotController::StageHotspotController()
    : juce::Thread("StageHotspotWorker") {
}

StageHotspotController::~StageHotspotController() {
    stopThread(3000);
}

void StageHotspotController::startHotspot(const juce::String& ssid,
                                          const juce::String& password,
                                          std::function<void(bool, juce::String)> onFinished) {
    {
        std::lock_guard<std::mutex> lock(taskMutex);
        activeSsid = ssid.isNotEmpty() ? ssid : "OpenRig-Stage";
        activePassword = password.length() >= 8 ? password : "openrig2026";
        pendingCallback = onFinished;
        pendingTask = TaskType::Start;
        currentState = State::Starting;
    }

    if (onStateChanged) {
        juce::MessageManager::callAsync([this]() {
            if (onStateChanged) onStateChanged(State::Starting, "Starting Stage Wi-Fi Hotspot...");
        });
    }

    if (!isThreadRunning()) {
        startThread();
    } else {
        notify();
    }
}

void StageHotspotController::stopHotspot(std::function<void(bool, juce::String)> onFinished) {
    {
        std::lock_guard<std::mutex> lock(taskMutex);
        pendingCallback = onFinished;
        pendingTask = TaskType::Stop;
        currentState = State::Stopping;
    }

    if (onStateChanged) {
        juce::MessageManager::callAsync([this]() {
            if (onStateChanged) onStateChanged(State::Stopping, "Stopping Stage Wi-Fi Hotspot...");
        });
    }

    if (!isThreadRunning()) {
        startThread();
    } else {
        notify();
    }
}

void StageHotspotController::run() {
    while (!threadShouldExit()) {
        TaskType task = pendingTask.exchange(TaskType::None);
        if (task == TaskType::None) {
            wait(500);
            continue;
        }

        juce::String currentSsid, currentPassword;
        std::function<void(bool, juce::String)> cb;
        {
            std::lock_guard<std::mutex> lock(taskMutex);
            currentSsid = activeSsid;
            currentPassword = activePassword;
            cb = pendingCallback;
            pendingCallback = nullptr;
        }

        bool isStarting = (task == TaskType::Start);
        juce::String outMessage;
        bool success = executeWindowsHotspotScript(isStarting, currentSsid, currentPassword, outMessage);

        State newState = isStarting ? (success ? State::Active : State::Error)
                                    : (success ? State::Stopped : State::Error);
        currentState = newState;
        lastMessage = outMessage;

        juce::MessageManager::callAsync([this, newState, outMessage, cb, success]() {
            if (onStateChanged) onStateChanged(newState, outMessage);
            if (cb) cb(success, outMessage);
        });
    }
}

bool StageHotspotController::executeWindowsHotspotScript(bool enable,
                                                         const juce::String& ssid,
                                                         const juce::String& password,
                                                         juce::String& outMessage) {
#if JUCE_WINDOWS
    juce::File scriptFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("openrig_stage_hotspot.ps1");

    juce::String psScript;
    psScript << "[CmdletBinding()]\n";
    psScript << "param([string]$Action = '" << (enable ? "start" : "stop") << "',\n";
    psScript << "      [string]$SSID = '" << ssid << "',\n";
    psScript << "      [string]$Password = '" << password << "')\n\n";

    psScript << "Add-Type -AssemblyName System.Runtime.WindowsRuntime\n";
    psScript << "$asTaskGeneric = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' }[0]\n\n";

    psScript << "Function AwaitResult($WinRtTask, $ResultType) {\n";
    psScript << "    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)\n";
    psScript << "    $netTask = $asTask.Invoke($null, @($WinRtTask))\n";
    psScript << "    $netTask.Wait(12000) | Out-Null\n";
    psScript << "    return $netTask.Result\n";
    psScript << "}\n\n";

    psScript << "$connProfile = [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime]::GetInternetConnectionProfile()\n";
    psScript << "if (-not $connProfile) {\n";
    psScript << "    $profiles = [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime]::GetConnectionProfiles()\n";
    psScript << "    if ($profiles.Count -gt 0) { $connProfile = $profiles[0] }\n";
    psScript << "}\n\n";

    psScript << "if (-not $connProfile) {\n";
    psScript << "    Write-Output 'ERROR: No active network adapter found on Windows.'\n";
    psScript << "    exit 1\n";
    psScript << "}\n\n";

    psScript << "$manager = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]::CreateFromConnectionProfile($connProfile)\n\n";

    psScript << "if ($Action -eq 'start') {\n";
    psScript << "    try {\n";
    psScript << "        $config = $manager.GetCurrentAccessPointConfiguration()\n";
    psScript << "        if ($config) {\n";
    psScript << "            $config.Ssid = $SSID\n";
    psScript << "            $config.Passphrase = $Password\n";
    psScript << "            $manager.ConfigureAccessPointAsync($config)\n";
    psScript << "        }\n";
    psScript << "    } catch {}\n\n";
    psScript << "    $res = AwaitResult ($manager.StartTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])\n";
    psScript << "    if ($res.Status -eq [Windows.Networking.NetworkOperators.TetheringOperationStatus]::Success) {\n";
    psScript << "        Write-Output ('SUCCESS: Hotspot active. SSID: ' + $SSID)\n";
    psScript << "        exit 0\n";
    psScript << "    } else {\n";
    psScript << "        Write-Output ('ERROR: ' + $res.Status)\n";
    psScript << "        exit 1\n";
    psScript << "    }\n";
    psScript << "} elseif ($Action -eq 'stop') {\n";
    psScript << "    $res = AwaitResult ($manager.StopTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])\n";
    psScript << "    Write-Output 'SUCCESS: Hotspot stopped'\n";
    psScript << "    exit 0\n";
    psScript << "}\n";

    scriptFile.replaceWithText(psScript);

    juce::ChildProcess proc;
    juce::String command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + scriptFile.getFullPathName() + "\"";
    if (proc.start(command)) {
        juce::String output = proc.readAllProcessOutput().trim();
        proc.waitForProcessToFinish(15000);
        scriptFile.deleteFile();

        if (output.containsIgnoreCase("SUCCESS")) {
            outMessage = output;
            return true;
        } else {
            outMessage = output.isNotEmpty() ? output : "Failed to start Windows Mobile Hotspot.";
            return false;
        }
    }

    outMessage = "Could not launch PowerShell process.";
    return false;
#else
    outMessage = "Stage Hotspot controller is supported on Windows.";
    return false;
#endif
}

juce::StringArray StageHotspotController::getAvailableIpAddresses() {
    juce::StringArray ips;
    auto addrs = juce::IPAddress::getAllAddresses();
    for (const auto& addr : addrs) {
        juce::String s = addr.toString();
        if (!addr.isNull() && s != "127.0.0.1" && s.contains(".")) {
            ips.add(s);
        }
    }
    if (ips.isEmpty()) {
        ips.add("192.168.137.1"); // Windows default hotspot IP
    }
    return ips;
}

} // namespace OpenRig
