#pragma once

#include <JuceHeader.h>
#include <cmath>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

// ==============================================================================
// OpenRig Logger
// Centralized logging with severity levels.
//
// Thread safety: log() pushes to a mutex-protected queue or writes synchronously
// depending on level. logToFile() writes synchronously. All file writes are
// serialized under a single lock (logQueueLock), maintaining chronological order.
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

// Helper to write directly to disk while flushing any queued messages first to preserve order
inline void writeDirectlyToFile(const juce::String &fullMsg) {
  const juce::ScopedLock sl(logQueueLock);
  auto file = getLogFile();

  // Simple rotation: if > 10 MB, archive to .bak and start fresh
  if (file.existsAsFile() && file.getSize() > 10 * 1024 * 1024) {
    juce::File bak = file.getSiblingFile("OpenRig_log.bak.txt");
    bak.deleteFile();
    file.moveFileTo(bak);
  }

  // Flush any queued logs first to maintain chronological order
  if (!logQueue.isEmpty()) {
    juce::String queuedText;
    for (const auto &s : logQueue)
      queuedText << s << "\n";
    file.appendText(queuedText);
    logQueue.clear();
  }

  file.appendText(fullMsg + "\n");
}

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

  if (level >= Level::Warning) {
    // Write warnings and errors synchronously to disk to prevent loss on crash
    writeDirectlyToFile(fullMsg);
  } else {
    // Queue info and debug logs to keep disk I/O off the hot path
    const juce::ScopedLock sl(logQueueLock);
    logQueue.add(fullMsg);
  }

  DBG(fullMsg);
}

// Synchronous file logger for trace entries (replaces independent logToFile)
inline void logToFile(const juce::String &msg) {
  juce::String fullMsg =
      juce::Time::getCurrentTime().toString(true, true) + " - " + msg;
  writeDirectlyToFile(fullMsg);
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

#ifdef _WIN32
inline void writeCrashDump(void* exceptionInfo) {
  juce::File dumpFile =
      juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
          .getChildFile("OpenRig_CrashDump.dmp");

  HANDLE file = CreateFileW(dumpFile.getFullPathName().toWideCharPointer(),
                            GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);

  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = static_cast<EXCEPTION_POINTERS*>(exceptionInfo);
    mei.ClientPointers = TRUE;

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      MiniDumpNormal, &mei, nullptr, nullptr);

    CloseHandle(file);
  }
}

#pragma warning(push)
#pragma warning(disable: 4509)
inline __declspec(noinline) bool executeRawSehCall(void (*fn)(void*), void* data) {
  __try {
    fn(data);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
#pragma warning(pop)

template <typename Func>
inline bool safeExecutePluginCall(Func&& action, const juce::String& actionName) {
  struct LambdaRunner {
    static void run(void* ctx) {
      auto* f = static_cast<std::remove_reference_t<Func>*>(ctx);
      (*f)();
    }
  };
  bool ok = executeRawSehCall(&LambdaRunner::run, &action);
  if (!ok) {
    LOG_ERROR("SEH Access Violation / Fault caught during plugin action: " + actionName);
  }
  return ok;
}
#else
template <typename Func>
inline bool safeExecutePluginCall(Func&& action, const juce::String& actionName) {
  try {
    action();
    return true;
  } catch (...) {
    LOG_ERROR("Exception caught during plugin action: " + actionName);
    return false;
  }
}
#endif


/**
 * Centralized Crash Handler
 */
inline void crashHandler(void *exceptionInfo) {
  LOG_ERROR("!!! APPLICATION CRASH DETECTED !!!");
#ifdef _WIN32
  LOG_ERROR("Writing Windows crash minidump to Desktop...");
  writeCrashDump(exceptionInfo);
#endif
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
#ifdef _WIN32
  report << "A crash minidump was written to OpenRig_CrashDump.dmp on your Desktop.\n";
#endif

  crashFile.replaceWithText(report);
}

} // namespace OpenRigLog

// Expose logToFile globally to maintain compatibility with existing usages
using OpenRigLog::logToFile;
