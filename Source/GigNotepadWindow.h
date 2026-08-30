#pragma once

#include <JuceHeader.h>
#include "GigNotepadComponent.h"
#include "ThemeManager.h"

class GigNotepadWindow : public juce::DocumentWindow {
public:
    GigNotepadWindow(FanfareEngine& eng, std::function<void()> onClose)
        : DocumentWindow("OpenRig Gig Notepad & Stage Notes",
                         ThemeManager::get(Theme::Role::panel),
                         DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          onCloseCallback(onClose)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(400, 300, 1920, 1080);

        auto* notepad = new GigNotepadComponent(eng);
        notepad->setSize(650, 520);
        setContentOwned(notepad, true);

        centreWithSize(650, 520);
        setVisible(true);
        toFront(true);
    }

    ~GigNotepadWindow() override {
        clearContentComponent();
    }

    void closeButtonPressed() override {
        if (onCloseCallback)
            onCloseCallback();
    }

private:
    std::function<void()> onCloseCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GigNotepadWindow)
};
