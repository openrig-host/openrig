#pragma once

#include <JuceHeader.h>
#include "StageHotspotController.h"
#include "QrCodeGenerator.h"

namespace OpenRig {

/**
 * High-contrast Stage Remote & Wi-Fi Hotspot Modal Overlay.
 * Allows musicians to launch the stage SoftAP, scan QR codes, and connect their tablet.
 */
class StageRemoteModalOverlay : public juce::Component, public juce::Timer {
public:
    StageRemoteModalOverlay(StageHotspotController& hotspotCtrl, int webServerPort = 8080);
    ~StageRemoteModalOverlay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onClose;

private:
    void timerCallback() override;
    void updateQrCodes();
    void updateStatusDisplay(StageHotspotController::State state, const juce::String& msg);

    StageHotspotController& hotspotController;
    int port = 8080;

    juce::TextButton closeBtn{"CLOSE"};
    
    // Wi-Fi Section
    juce::Label wifiTitleLabel;
    juce::Label ssidLabel{"ssid", "SSID:"};
    juce::TextEditor ssidEditor;
    juce::Label passLabel{"pass", "PASSWORD:"};
    juce::TextEditor passEditor;
    juce::TextButton hotspotToggleBtn{"START STAGE HOTSPOT"};
    juce::Label hotspotStatusLabel;

    // Web Remote Section
    juce::Label webTitleLabel;
    juce::Label primaryUrlLabel;
    juce::TextButton copyUrlBtn{"COPY URL"};
    juce::TextButton openBrowserBtn{"OPEN IN BROWSER"};
    juce::Label otherIpsLabel;

    // QR Code Components
    class QrDisplayComponent : public juce::Component {
    public:
        void setPayload(const juce::String& p) {
            payload = p;
            repaint();
        }
        void paint(juce::Graphics& g) override {
            QrCodeGenerator::draw(g, getLocalBounds().reduced(4), payload, juce::Colours::black, juce::Colours::white);
        }
    private:
        juce::String payload;
    };

    QrDisplayComponent wifiQrComponent;
    QrDisplayComponent webQrComponent;

    juce::Label wifiQrCaption;
    juce::Label webQrCaption;
    juce::Label usbInfoLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StageRemoteModalOverlay)
};

} // namespace OpenRig
