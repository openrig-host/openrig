#pragma once

#include <JuceHeader.h>
#include <cmath>

// ==============================================================================
// OpenRig Logger
// Centralized logging with severity levels.
//
// Thread safety: log() pushes to a mutex-protected queue (no disk I/O).
// flushLog() drains the queue and writes to disk — call it from the message
// thread (e.g. a Timer). log() must NOT be called from the audio thread.
// ==============================================================================

namespace OpenRigLog {

enum class Level { Debug, Info, Warning, Error };

inline Level currentMinLevel = Level::Info;

inline juce::File getLogFile() {
  static const juce::File f =
      juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
          .getChildFile("OpenRig_log.txt");
  return f;
}

inline void setMinLevel(Level level) { currentMinLevel = level; }

// --- Thread-safe queue: log() pushes, flushLog() drains + writes ---
inline juce::CriticalSection logQueueLock;
inline juce::StringArray logQueue;

inline void log(Level level, const juce::String &msg) {
  if (level < currentMinLevel)
    return;

  const char *prefix = "[INFO]  ";
  switch (level) {
  case Level::Debug:
    prefix = "[DEBUG] "; break;
  case Level::Info:
    prefix = "[INFO]  "; break;
  case Level::Warning:
    prefix = "[WARN]  "; break;
  case Level::Error:
    prefix = "[ERROR] "; break;
  }

  juce::String fullMsg =
      juce::Time::getCurrentTime().toString(true, true) + " " + prefix + msg;

  {
    const juce::ScopedLock sl(logQueueLock);
    logQueue.add(fullMsg);
  }

  DBG(fullMsg);
}

// Drain the queue and write to disk. Call from the message thread (e.g. Timer).
inline void flushLog() {
  juce::StringArray toWrite;
  {
    const juce::ScopedLock sl(logQueueLock);
    if (logQueue.isEmpty())
      return;
    toWrite.swapWith(logQueue);
  }

  auto file = getLogFile();

  // Simple rotation: if > 10 MB, archive to .bak and start fresh
  if (file.existsAsFile() && file.getSize() > 10 * 1024 * 1024) {
    juce::File bak = file.getSiblingFile("OpenRig_log.bak.txt");
    bak.deleteFile();
    file.moveFileTo(bak);
  }

  juce::String text;
  for (const auto &s : toWrite)
    text << s << "\n";
  file.appendText(text);
}

// Convenience macros
#define LOG_DEBUG(msg) OpenRigLog::log(OpenRigLog::Level::Debug, msg)
#define LOG_INFO(msg) OpenRigLog::log(OpenRigLog::Level::Info, msg)
#define LOG_WARN(msg) OpenRigLog::log(OpenRigLog::Level::Warning, msg)
#define LOG_ERROR(msg) OpenRigLog::log(OpenRigLog::Level::Error, msg)

/**
 * Logarithmic scale for VU meters
 * Maps linear amplitude to 0..1 range using a -60dB floor
 */
inline float amplitudeToLogScale(float linear) {
  if (linear <= 0.001f)
    return 0.0f; // -60dB floor
  float db = 20.0f * std::log10(linear);
  return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

/**
 * Centralized Crash Handler
 */
inline void crashHandler(void *) {
  LOG_ERROR("!!! APPLICATION CRASH DETECTED !!!");
  LOG_ERROR("Writing final state to emergency log...");
  flushLog(); // drain everything immediately

  juce::File crashFile =
      juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
          .getChildFile("DAVE_CORE_CRASH_REPORT.txt");

  juce::String report;
  report << "OpenRig Crash Report\n";
  report << "Time: " << juce::Time::getCurrentTime().toString(true, true)
         << "\n";
  report << "------------------------------------------------\n";
  report << "The application has encountered an unhandled exception.\n";
  report << "Check OpenRig_log.txt for the last few trace entries.\n";

  crashFile.replaceWithText(report);
}

} // namespace OpenRigLog
