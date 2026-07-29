#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <exception>
#include <cstdlib>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <psapi.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")
#endif

inline std::atomic<uint32_t> g_audioThreadId{0};

namespace FanfareLog {

struct ProcessMemoryStats {
  size_t workingSetBytes = 0;
  size_t privateCommitBytes = 0;
  size_t totalSystemRamBytes = 0;
  size_t freeSystemRamBytes = 0;
  int systemRamLoadPercent = 0;
};

inline ProcessMemoryStats getMemoryStats() {
  ProcessMemoryStats stats;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
    stats.workingSetBytes = pmc.WorkingSetSize;
    stats.privateCommitBytes = pmc.PrivateUsage;
  }

  MEMORYSTATUSEX memInfo;
  memInfo.dwLength = sizeof(MEMORYSTATUSEX);
  if (GlobalMemoryStatusEx(&memInfo)) {
    stats.totalSystemRamBytes = (size_t)memInfo.ullTotalPhys;
    stats.freeSystemRamBytes = (size_t)memInfo.ullAvailPhys;
    stats.systemRamLoadPercent = (int)memInfo.dwMemoryLoad;
  }
#endif
  return stats;
}

enum class Level { Debug, Info, Warning, Error };

inline Level currentMinLevel = Level::Info;

inline juce::File getLogFile() {
  static const juce::File f =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("Fanfare")
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

  if (level >= Level::Warning && g_audioThreadId.load() != (uint32_t)::GetCurrentThreadId()) {
    // Write warnings and errors synchronously to disk ONLY if on a non-audio thread
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
  if (g_audioThreadId.load() != (uint32_t)::GetCurrentThreadId()) {
    writeDirectlyToFile(fullMsg);
  } else {
    // Audio thread: never touch disk — queue for the message-thread flush.
    const juce::ScopedLock sl(logQueueLock);
    logQueue.add(fullMsg);
  }
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
#define LOG_DEBUG(msg) FanfareLog::log(FanfareLog::Level::Debug, msg)
#define LOG_INFO(msg) FanfareLog::log(FanfareLog::Level::Info, msg)
#define LOG_WARN(msg) FanfareLog::log(FanfareLog::Level::Warning, msg)
#define LOG_ERROR(msg) FanfareLog::log(FanfareLog::Level::Error, msg)

/**
 * Logarithmic scale for VU meters
 * Maps linear amplitude to 0..1 range using a -60dB floor
 */
} // namespace FanfareLog

using FanfareLog::logToFile;

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

// ==============================================================================
// Flight Recorder (Lock-Free Event Ring Buffer)
// ==============================================================================
struct FlightEvent {
  uint64_t timestampMs;
  uint32_t threadId;
  int slotIdx;
  char message[120];
};

constexpr size_t kFlightRecorderSize = 256;
inline FlightEvent g_flightRecorder[kFlightRecorderSize];
inline std::atomic<size_t> g_flightRecorderHead{0};
inline std::atomic<uint32_t> g_mainThreadId{0};

inline void recordFlightEvent(int slotIdx, const char* msg) {
  size_t idx = g_flightRecorderHead.fetch_add(1, std::memory_order_relaxed) % kFlightRecorderSize;
  auto& ev = g_flightRecorder[idx];
  ev.timestampMs = (uint64_t)juce::Time::currentTimeMillis();
  ev.threadId = (uint32_t)::GetCurrentThreadId();
  ev.slotIdx = slotIdx;
  size_t i = 0;
  for (; msg[i] != '\0' && i < sizeof(ev.message) - 1; ++i) {
    ev.message[i] = msg[i];
  }
  ev.message[i] = '\0';
}

#define LOG_FLIGHT(slotIdx, msg) recordFlightEvent(slotIdx, msg)

#ifdef _WIN32
inline const char* getExceptionCodeDescription(DWORD code) {
  switch (code) {
  case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION (0xC0000005)";
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED (0xC000008C)";
  case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT (0x80000003)";
  case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT (0x80000002)";
  case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND (0xC000008D)";
  case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO (0xC000008E)";
  case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT (0xC000008F)";
  case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION (0xC0000090)";
  case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW (0xC0000091)";
  case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK (0xC0000092)";
  case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW (0xC0000093)";
  case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION (0xC000001D)";
  case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR (0xC0000006)";
  case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO (0xC0000094)";
  case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW (0xC0000095)";
  case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION (0xC0000026)";
  case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION (0xC0000025)";
  case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION (0xC0000096)";
  case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP (0x80000004)";
  case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW (0xC00000FD)";
  case 0xE06D7363: return "MSVC C++ Exception (0xE06D7363)";
  default: return "UNKNOWN_EXCEPTION";
  }
}

// Writes minidump FIRST before doing text processing, off OneDrive to %APPDATA%/OpenRig/Crashes/
inline void writeCrashDumpHeapFree(void* exceptionInfo, const wchar_t* dmpPath) {
  HANDLE file = CreateFileW(dmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = static_cast<EXCEPTION_POINTERS*>(exceptionInfo);
    mei.ClientPointers = TRUE;

    MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(
        MiniDumpNormal |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules |
        MiniDumpWithHandleData |
        MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpWithDataSegs);

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, dumpType, &mei, nullptr, nullptr);
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
 * Centralized Heap-Free Crash Handler
 */
inline void crashHandler(void *exceptionInfo) {
#ifdef _WIN32
  // STEP 1: Determine AppData Crash Folder & Timestamped File Paths (Heap-Free)
  wchar_t appData[MAX_PATH] = {0};
  GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
  wchar_t crashDir[MAX_PATH] = {0};
  wsprintfW(crashDir, L"%s\\OpenRig\\Crashes", appData);
  CreateDirectoryW(appData, nullptr);
  CreateDirectoryW(crashDir, nullptr);

  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t dmpPath[MAX_PATH] = {0};
  wchar_t txtPath[MAX_PATH] = {0};
  wsprintfW(dmpPath, L"%s\\OpenRig_%04d%02d%02d_%02d%02d%02d.dmp", crashDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  wsprintfW(txtPath, L"%s\\OpenRig_%04d%02d%02d_%02d%02d%02d.txt", crashDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

  // STEP 2: WRITE MINIDUMP FIRST (Zero Heap Dependency)
  writeCrashDumpHeapFree(exceptionInfo, dmpPath);


  // STEP 3: HEAP-FREE TEXT REPORT WRITER (Raw Win32 WriteFile to stack buffer)
  static char reportBuf[65536];
  size_t offset = 0;

  auto appendRaw = [&](const char* str) {
    while (str && *str && offset < sizeof(reportBuf) - 1) {
      reportBuf[offset++] = *str++;
    }
  };

  appendRaw("==================================================================\n");
  appendRaw("!!! OPENRIG EMERGENCY CRASH REPORT !!!\n");
  appendRaw("==================================================================\n");

  uint32_t currentTid = (uint32_t)::GetCurrentThreadId();
  uint32_t audioTid = g_audioThreadId.load();
  uint32_t mainTid = g_mainThreadId.load();

  char threadRole[64] = "Worker/Other Thread";
  if (currentTid == mainTid) strcpy(threadRole, "Message/UI Thread");
  else if (currentTid == audioTid) strcpy(threadRole, "Audio Callback Thread");

  char line[512];
  wsprintfA(line, "Crash Time: %04d-%02d-%02d %02d:%02d:%02d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  appendRaw(line);
  wsprintfA(line, "Faulting Thread ID: %u (%s)\n", currentTid, threadRole);
  appendRaw(line);

  if (exceptionInfo != nullptr) {
    auto* ep = static_cast<EXCEPTION_POINTERS*>(exceptionInfo);
    if (ep != nullptr && ep->ExceptionRecord != nullptr) {
      DWORD code = ep->ExceptionRecord->ExceptionCode;
      void* addr = ep->ExceptionRecord->ExceptionAddress;
      wsprintfA(line, "Fault Code: %s\nFault Address: 0x%p\n", getExceptionCodeDescription(code), addr);
      appendRaw(line);

      HMODULE hMod = nullptr;
      if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             (LPCWSTR)addr, &hMod) && hMod != nullptr) {
        wchar_t modPath[MAX_PATH] = {0};
        GetModuleFileNameW(hMod, modPath, MAX_PATH);
        char asciiMod[MAX_PATH] = {0};
        WideCharToMultiByte(CP_UTF8, 0, modPath, -1, asciiMod, MAX_PATH, nullptr, nullptr);
        intptr_t modOffset = (intptr_t)addr - (intptr_t)hMod;
        wsprintfA(line, "Faulting Module: %s (+0x%IX)\n", asciiMod, modOffset);
        appendRaw(line);
      }
    }
  }

  void* stack[32];
  WORD frames = CaptureStackBackTrace(0, 32, stack, nullptr);
  wsprintfA(line, "\nCall Stack (%d frames):\n", (int)frames);
  appendRaw(line);
  for (WORD i = 0; i < frames; ++i) {
    HMODULE frameMod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)stack[i], &frameMod) && frameMod != nullptr) {
      wchar_t fPath[MAX_PATH] = {0};
      GetModuleFileNameW(frameMod, fPath, MAX_PATH);
      char asciiFPath[MAX_PATH] = {0};
      WideCharToMultiByte(CP_UTF8, 0, fPath, -1, asciiFPath, MAX_PATH, nullptr, nullptr);
      const char* baseName = strrchr(asciiFPath, '\\');
      baseName = baseName ? baseName + 1 : asciiFPath;
      intptr_t fOffset = (intptr_t)stack[i] - (intptr_t)frameMod;
      wsprintfA(line, "  [%d] %s + 0x%IX\n", (int)i, baseName, fOffset);
    } else {
      wsprintfA(line, "  [%d] 0x%p\n", (int)i, stack[i]);
    }
    appendRaw(line);
  }

  appendRaw("\n------------------------------------------------------------------\n");
  appendRaw("FLIGHT RECORDER (Recent Real-Time Events Prior to Fault):\n");
  appendRaw("------------------------------------------------------------------\n");
  size_t currentHead = g_flightRecorderHead.load(std::memory_order_relaxed);
  size_t startCount = (currentHead > kFlightRecorderSize) ? (currentHead - kFlightRecorderSize) : 0;
  for (size_t i = startCount; i < currentHead; ++i) {
    auto& ev = g_flightRecorder[i % kFlightRecorderSize];
    if (ev.timestampMs > 0) {
      wsprintfA(line, "  [TID:%u Slot:%d] %s\n", ev.threadId, ev.slotIdx, ev.message);
      appendRaw(line);
    }
  }
  appendRaw("==================================================================\n");
  reportBuf[offset] = '\0';

  HANDLE reportFile = CreateFileW(txtPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (reportFile != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(reportFile, reportBuf, (DWORD)offset, &written, nullptr);
    CloseHandle(reportFile);
  }

#endif

  FanfareLog::flushLog();
#ifdef _WIN32
  TerminateProcess(GetCurrentProcess(), 1);
#endif
}

#ifdef _WIN32
inline LONG WINAPI openRigVectoredExceptionHandler(PEXCEPTION_POINTERS ep) {
  if (ep != nullptr && ep->ExceptionRecord != nullptr) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
      static std::atomic<bool> crashHandled{false};
      if (!crashHandled.exchange(true)) {
        crashHandler(ep);
      }
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

inline void setupCrashHandlers() {
  g_mainThreadId.store((uint32_t)::GetCurrentThreadId());
  AddVectoredExceptionHandler(1, openRigVectoredExceptionHandler);
  _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    crashHandler(nullptr);
    ::ExitProcess(1);
  });
  _set_purecall_handler([]() {
    crashHandler(nullptr);
    ::ExitProcess(1);
  });
  ::std::set_terminate([]() {
    crashHandler(nullptr);
    ::ExitProcess(1);
  });
}
#endif

namespace FanfareLog {
  using ::g_audioThreadId;
  using ::amplitudeToLogScale;
  using ::safeExecutePluginCall;
  using ::crashHandler;
#ifdef _WIN32
  using ::setupCrashHandlers;
#endif
}
