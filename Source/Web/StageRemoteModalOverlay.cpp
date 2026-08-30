#include "StageRemoteModalOverlay.h"
#include "../Theme.h"

namespace OpenRig {

StageRemoteModalOverlay::StageRemoteModalOverlay(StageHotspotController& hotspotCtrl, int webServerPort)
    : hotspotController(hotspotCtrl), port(webServerPort) {
    setOpaque(false);

    addAndMakeVisible(closeBtn);
    closeBtn.onClick = [this]() {
        if (onClose) onClose();
    };

    // --- Wi-Fi Section ---
    wifiTitleLabel.setText("STAGE WI-FI SOFTAP (DIRECT LINK)", juce::dontSendNotification);
    wifiTitleLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    wifiTitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF58A6FF));
    addAndMakeVisible(wifiTitleLabel);

    addAndMakeVisible(ssidLabel);
    ssidLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    ssidLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8B949E));

    addAndMakeVisible(ssidEditor);
    ssidEditor.setText(hotspotController.getSsid());
    ssidEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF0D1117));
    ssidEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    ssidEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF30363D));
    ssidEditor.onTextChange = [this]() { updateQrCodes(); };

    addAndMakeVisible(passLabel);
    passLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    passLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8B949E));

    addAndMakeVisible(passEditor);
    passEditor.setText(hotspotController.getPassword());
    passEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF0D1117));
    passEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    passEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF30363D));
    passEditor.onTextChange = [this]() { updateQrCodes(); };

    addAndMakeVisible(hotspotToggleBtn);
    hotspotToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF238636));
    hotspotToggleBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    hotspotToggleBtn.onClick = [this]() {
        if (hotspotController.isActive()) {
            hotspotToggleBtn.setEnabled(false);
            hotspotController.stopHotspot([this](bool, juce::String) {
                hotspotToggleBtn.setEnabled(true);
            });
        } else {
            hotspotToggleBtn.setEnabled(false);
            hotspotController.startHotspot(ssidEditor.getText(), passEditor.getText(), [this](bool, juce::String) {
                hotspotToggleBtn.setEnabled(true);
            });
        }
    };

    addAndMakeVisible(hotspotStatusLabel);
    hotspotStatusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    hotspotStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8B949E));

    addAndMakeVisible(wifiQrComponent);
    wifiQrCaption.setText("Scan with camera to Join Wi-Fi", juce::dontSendNotification);
    wifiQrCaption.setFont(juce::Font(juce::FontOptions(11.0f)));
    wifiQrCaption.setJustificationType(juce::Justification::centred);
    wifiQrCaption.setColour(juce::Label::textColourId, juce::Colour(0xFF8B949E));
    addAndMakeVisible(wifiQrCaption);

    // --- Web Remote Section ---
    webTitleLabel.setText("TABLET STAGE COMPANION (WEB HUD)", juce::dontSendNotification);
    webTitleLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    webTitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF3FB950));
    addAndMakeVisible(webTitleLabel);

    primaryUrlLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    primaryUrlLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(primaryUrlLabel);

    addAndMakeVisible(copyUrlBtn);
    copyUrlBtn.onClick = [this]() {
        juce::SystemClipboard::copyTextToClipboard(primaryUrlLabel.getText());
        copyUrlBtn.setButtonText("COPIED!");
        startTimer(1500);
    };

    addAndMakeVisible(openBrowserBtn);
    openBrowserBtn.onClick = [this]() {
        juce::URL(primaryUrlLabel.getText()).launchInDefaultBrowser();
    };

    otherIpsLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    otherIpsLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8B949E));
    addAndMakeVisible(otherIpsLabel);

    addAndMakeVisible(webQrComponent);
    webQrCaption.setText("Scan to Open Stage HUD in Browser", juce::dontSendNotification);
    webQrCaption.setFont(juce::Font(juce::FontOptions(11.0f)));
    webQrCaption.setJustificationType(juce::Justification::centred);
    webQrCaption.setColour(juce::Label::textColourId, juce::Colour(0xFF8B949E));
    addAndMakeVisible(webQrCaption);

    usbInfoLabel.setText("USB Stage Link: Plug USB cable from laptop to tablet & enable USB Tethering for 0ms latency.", juce::dontSendNotification);
    usbInfoLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::italic)));
    usbInfoLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF7EE787));
    addAndMakeVisible(usbInfoLabel);

    updateQrCodes();
    updateStatusDisplay(hotspotController.getState(), "");

    hotspotController.onStateChanged = [this](StageHotspotController::State st, const juce::String& msg) {
        updateStatusDisplay(st, msg);
    };
}

StageRemoteModalOverlay::~StageRemoteModalOverlay() {
    hotspotController.onStateChanged = nullptr;
}

void StageRemoteModalOverlay::timerCallback() {
    stopTimer();
    copyUrlBtn.setButtonText("COPY URL");
}

void StageRemoteModalOverlay::updateQrCodes() {
    juce::String ssid = ssidEditor.getText().trim();
    if (ssid.isEmpty()) ssid = "OpenRig-Stage";
    juce::String pass = passEditor.getText().trim();
    if (pass.isEmpty()) pass = "openrig2026";

    wifiQrComponent.setPayload(QrCodeGenerator::makeWifiPayload(ssid, pass));

    auto ips = StageHotspotController::getAvailableIpAddresses();
    juce::String primaryIp = ips[0];
    if (hotspotController.isActive()) primaryIp = "192.168.137.1";
    juce::String url = "http://" + primaryIp + ":" + juce::String(port);

    primaryUrlLabel.setText(url, juce::dontSendNotification);
    webQrComponent.setPayload(url);

    juce::String otherIpsStr = "Available Network IPs: ";
    for (int i = 0; i < ips.size(); ++i) {
        if (i > 0) otherIpsStr += ", ";
        otherIpsStr += ips[i];
    }
    otherIpsLabel.setText(otherIpsStr, juce::dontSendNotification);
}

void StageRemoteModalOverlay::updateStatusDisplay(StageHotspotController::State state, const juce::String& msg) {
    if (state == StageHotspotController::State::Active) {
        hotspotToggleBtn.setButtonText("STOP STAGE HOTSPOT");
        hotspotToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFDA3633));
        hotspotStatusLabel.setText("Status: ACTIVE (Broadcasting " + ssidEditor.getText() + ")", juce::dontSendNotification);
        hotspotStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF3FB950));
    } else if (state == StageHotspotController::State::Starting) {
        hotspotToggleBtn.setButtonText("STARTING...");
        hotspotStatusLabel.setText("Status: Starting Windows Hotspot...", juce::dontSendNotification);
        hotspotStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFD29922));
    } else if (state == StageHotspotController::State::Stopping) {
        hotspotToggleBtn.setButtonText("STOPPING...");
        hotspotStatusLabel.setText("Status: Stopping...", juce::dontSendNotification);
        hotspotStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFD29922));
    } else if (state == StageHotspotController::State::Error) {
        hotspotToggleBtn.setButtonText("START STAGE HOTSPOT");
        hotspotToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF238636));
        hotspotStatusLabel.setText("Error: " + msg, juce::dontSendNotification);
        hotspotStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFF85149));
    } else {
        hotspotToggleBtn.setButtonText("START STAGE HOTSPOT");
        hotspotToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF238636));
        hotspotStatusLabel.setText("Status: Stopped (Offline)", juce::dontSendNotification);
        hotspotStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8B949E));
    }
    updateQrCodes();
}

void StageRemoteModalOverlay::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xB0000000)); // Dim background

    auto bounds = getLocalBounds();
    auto dialogArea = bounds.withSizeKeepingCentre(std::min(780, bounds.getWidth() - 30),
                                                   std::min(530, bounds.getHeight() - 30));

    // Main Card
    g.setColour(juce::Colour(0xFF161B22));
    g.fillRoundedRectangle(dialogArea.toFloat(), 8.0f);

    g.setColour(juce::Colour(0xFF30363D));
    g.drawRoundedRectangle(dialogArea.toFloat(), 8.0f, 1.5f);

    // Title Bar
    auto titleArea = dialogArea.removeFromTop(44);
    g.setColour(juce::Colour(0xFF0D1117));
    g.fillRoundedRectangle(titleArea.toFloat(), 8.0f);
    g.fillRect(titleArea.getX(), titleArea.getBottom() - 8, titleArea.getWidth(), 8);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    g.drawText("STAGE REMOTE & WI-FI HOTSPOT CONTROLLER", titleArea.reduced(16, 0), juce::Justification::centredLeft);

    // Divider between left (Wi-Fi) and right (Web HUD) columns
    int midX = dialogArea.getX() + dialogArea.getWidth() / 2;
    g.setColour(juce::Colour(0xFF21262D));
    g.drawLine((float)midX, (float)dialogArea.getY() + 10, (float)midX, (float)dialogArea.getBottom() - 40, 1.0f);
}

void StageRemoteModalOverlay::resized() {
    auto bounds = getLocalBounds();
    auto dialogArea = bounds.withSizeKeepingCentre(std::min(780, bounds.getWidth() - 30),
                                                   std::min(530, bounds.getHeight() - 30));

    auto titleArea = dialogArea.removeFromTop(44);
    closeBtn.setBounds(titleArea.removeFromRight(80).reduced(8, 8));

    auto bottomArea = dialogArea.removeFromBottom(40);
    usbInfoLabel.setBounds(bottomArea.reduced(16, 8));

    int colWidth = dialogArea.getWidth() / 2;
    auto leftCol = dialogArea.removeFromLeft(colWidth).reduced(16, 12);
    auto rightCol = dialogArea.reduced(16, 12);

    // --- Left Column (Wi-Fi SoftAP) ---
    wifiTitleLabel.setBounds(leftCol.removeFromTop(24));
    leftCol.removeFromTop(6);

    auto ssidRow = leftCol.removeFromTop(28);
    ssidLabel.setBounds(ssidRow.removeFromLeft(75));
    ssidEditor.setBounds(ssidRow);

    leftCol.removeFromTop(6);
    auto passRow = leftCol.removeFromTop(28);
    passLabel.setBounds(passRow.removeFromLeft(75));
    passEditor.setBounds(passRow);

    leftCol.removeFromTop(8);
    hotspotToggleBtn.setBounds(leftCol.removeFromTop(32));
    hotspotStatusLabel.setBounds(leftCol.removeFromTop(22));

    leftCol.removeFromTop(6);
    auto wifiQrArea = leftCol.removeFromTop(130).withSizeKeepingCentre(130, 130);
    wifiQrComponent.setBounds(wifiQrArea);
    wifiQrCaption.setBounds(leftCol.removeFromTop(20));

    // --- Right Column (Web Companion HUD) ---
    webTitleLabel.setBounds(rightCol.removeFromTop(24));
    rightCol.removeFromTop(6);

    primaryUrlLabel.setBounds(rightCol.removeFromTop(28));

    auto btnRow = rightCol.removeFromTop(32);
    copyUrlBtn.setBounds(btnRow.removeFromLeft(btnRow.getWidth() / 2 - 4));
    openBrowserBtn.setBounds(btnRow.removeFromRight(btnRow.getWidth() - 4));

    otherIpsLabel.setBounds(rightCol.removeFromTop(26));

    rightCol.removeFromTop(6);
    auto webQrArea = rightCol.removeFromTop(130).withSizeKeepingCentre(130, 130);
    webQrComponent.setBounds(webQrArea);
    webQrCaption.setBounds(rightCol.removeFromTop(20));
}

} // namespace OpenRig
