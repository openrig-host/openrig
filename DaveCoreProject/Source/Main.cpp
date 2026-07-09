/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include "MainComponent.h"
#include <JuceHeader.h>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#pragma comment(lib, "winmm.lib")
#endif

//==============================================================================
class OpenRigApplication : public juce::JUCEApplication {
public:
  //==============================================================================
  OpenRigApplication() {}

  const juce::String getApplicationName() override {
    return "OpenRig";
  }
  const juce::String getApplicationVersion() override {
    return ProjectInfo::versionString;
  }
  bool moreThanOneInstanceAllowed() override { return true; }

  //==============================================================================
  void initialise(const juce::String &commandLine) override {
    juce::ignoreUnused(commandLine);

#ifdef _WIN32
    // Set Windows multimedia timer to 1ms resolution for accurate audio timing
    timeBeginPeriod(1);
#endif

    // Set global crash handler
    juce::SystemStats::setApplicationCrashHandler(OpenRigLog::crashHandler);

    mainWindow.reset(new MainWindow(getApplicationName()));
  }

  void shutdown() override {
    mainWindow = nullptr; // (deletes our window)

#ifdef _WIN32
    // Restore default Windows timer resolution
    timeEndPeriod(1);
#endif
  }

  //==============================================================================
  void systemRequestedQuit() override {
    // This is called when the app is being asked to quit: you can ignore this
    // request and let the app carry on running, or call quit() to allow the app
    // to close.
    quit();
  }

  void anotherInstanceStarted(const juce::String &commandLine) override {
    juce::ignoreUnused(commandLine);
    // When another instance of the app is launched while this one is running,
    // this method is invoked, and the commandLine parameter tells you what
    // the other instance's command-line arguments were.
  }

  //==============================================================================
  /*
      This class implements the desktop window that contains an instance of
      our MainComponent class.
  */
  class MainWindow : public juce::DocumentWindow {
  public:
    MainWindow(juce::String name)
        : DocumentWindow(
              name,
              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                  juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new MainComponent(), true);

#if JUCE_IOS || JUCE_ANDROID
      setFullScreen(true);
#else
      setResizable(true, true);
      centreWithSize(getWidth(), getHeight());
#endif

      setVisible(true);
    }

    void closeButtonPressed() override {
      // This is called when the user tries to close this window. Here, we'll
      // just ask the app to quit when this happens, but you can change this to
      // do whatever you need.
      JUCEApplication::getInstance()->systemRequestedQuit();
    }

    /* Note: Be careful if you override any DocumentWindow methods - the base
       class uses a lot of them, so by overriding you might break its
       functionality. It's best to do all your work in your content component
       instead, but if you really have to override any DocumentWindow methods,
       make sure your subclass also calls the superclass's method.
    */

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

private:
  std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
// Custom entry point with SEH-protected message dispatch.
//
// Plugins like Kontakt and Super 8 create internal hidden windows during
// initialization. When the JUCE message pump dispatches Windows messages to
// these windows, the plugin's window procedure can access-violate (null
// pointer). Standard C++ try/catch cannot catch access violations (0xC0000005)
// because they are SEH (structured) exceptions, not C++ exceptions.
//
// By wrapping runDispatchLoopUntil() in __try/__except, the access violation
// is caught and the stack unwound to a safe point. The app survives; the
// plugin may be unstable but the host keeps running. This is critical for
// live performance reliability.

#ifdef _WIN32
// Must be a separate function: MSVC C2712 forbids __try in functions that
// contain C++ objects with destructors.
static void runDispatchLoopSEH() {
  __try {
    juce::MessageManager::getInstance()->runDispatchLoop();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // Plugin access violation in message dispatch — swallowed.
  }
}
#endif

// Factory for the application instance.
static juce::JUCEApplicationBase *juce_CreateApplication() {
  return new OpenRigApplication();
}

#ifdef _WIN32
extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
  juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;

  juce::ScopedJuceInitialiser_GUI libraryInitialiser;
  auto app = std::unique_ptr<juce::JUCEApplicationBase>(
      juce::JUCEApplicationBase::createInstance());
  jassert(app != nullptr);

  app->initialise(juce::String(lpCmdLine));

  // SEH-protected message loop: each plugin window crash is caught and
  // contained. The loop continues processing subsequent messages.
  while (!juce::MessageManager::getInstance()->hasStopMessageBeenSent())
    runDispatchLoopSEH();

  app->shutdown();
  return 0;
}
#else
START_JUCE_APPLICATION(OpenRigApplication)
#endif
