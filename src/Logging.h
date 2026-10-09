// LightSwitch Standalone - local logging facility.
// Replaces the PowerToys logger (not vendored).
//
// Design:
//   * Always writes to the debugger (OutputDebugStringW) AND appends to a
//     rotating-ish plain-text log file next to the executable.
//   * A mutex guards the file so the scheduler thread, the settings watcher
//     thread and the night-light observer thread can log concurrently.
//   * Logging must never throw: everything is wrapped in try/catch.
#pragma once

#include <windows.h>

#include <sstream>
#include <string>

namespace ls
{

// Directory that contains the running executable (no trailing backslash).
std::wstring ExeDirectory();

// Full path of lightswitch.log (next to the exe, or %LOCALAPPDATA% fallback).
std::wstring LogFilePath();

// Write one line. `level` is one of L"INFO" / L"WARN" / L"ERROR" / L"DEBUG".
void LogWrite(const wchar_t* level, const std::wstring& message);

} // namespace ls

#define LS_LOG_IMPL(level, expr)                       \
    do                                                 \
    {                                                  \
        std::wostringstream ls_log_oss;                \
        ls_log_oss << expr;                            \
        ::ls::LogWrite(level, ls_log_oss.str());       \
    } while (0)

#define LOG_DEBUG(expr) LS_LOG_IMPL(L"DEBUG", expr)
#define LOG_INFO(expr) LS_LOG_IMPL(L"INFO", expr)
#define LOG_WARN(expr) LS_LOG_IMPL(L"WARN", expr)
#define LOG_ERROR(expr) LS_LOG_IMPL(L"ERROR", expr)
