// LightSwitch Standalone - local logging facility (implementation).
#include "Logging.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace ls
{

namespace
{

std::mutex g_logMutex;
std::wstring g_exeDir;
bool g_resolved = false;

constexpr std::uintmax_t kMaxLogBytes = 4ull * 1024 * 1024;

std::wstring ResolveExeDirectory()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0)
            return L".";
        if (len < buffer.size() - 1)
        {
            const std::wstring path(buffer.data(), len);
            const auto pos = path.find_last_of(L"\\/");
            return (pos == std::wstring::npos) ? L"." : path.substr(0, pos);
        }
        buffer.resize(buffer.size() * 2);
    }
}

} // namespace

std::wstring ExeDirectory()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_resolved)
    {
        g_exeDir = ResolveExeDirectory();
        g_resolved = true;
    }
    return g_exeDir;
}

std::wstring LogFilePath()
{
    // Note: ExeDirectory() takes g_logMutex internally, so callers must not
    // already hold it. LogWrite() below resolves the path first for that reason.
    return ExeDirectory() + L"\\lightswitch.log";
}

void LogWrite(const wchar_t* level, const std::wstring& message)
{
    try
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);

        wchar_t prefix[64];
        swprintf_s(prefix,
                   L"[%04u-%02u-%02u %02u:%02u:%02u] [%s] [t%04lu] ",
                   st.wYear,
                   st.wMonth,
                   st.wDay,
                   st.wHour,
                   st.wMinute,
                   st.wSecond,
                   level,
                   static_cast<unsigned long>(GetCurrentThreadId()));

        const std::wstring line = std::wstring(prefix) + message + L"\r\n";

        // Always available under a debugger / DebugView, even if the file
        // cannot be written.
        OutputDebugStringW(line.c_str());

        // Resolve the path BEFORE taking the lock; LogFilePath() locks too.
        const std::wstring path = LogFilePath();

        std::lock_guard<std::mutex> lock(g_logMutex);

        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        if (exists && !ec)
        {
            const auto size = std::filesystem::file_size(path, ec);
            if (!ec && size > kMaxLogBytes)
            {
                std::filesystem::remove(path, ec);
            }
        }

        const bool create = !std::filesystem::exists(path, ec);

        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (!out)
            return;

        if (create)
        {
            // UTF-16LE BOM so that Notepad renders the file correctly.
            const wchar_t bom = 0xFEFF;
            out.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
        }

        out.write(reinterpret_cast<const char*>(line.data()), static_cast<std::streamsize>(line.size() * sizeof(wchar_t)));
        out.flush();
    }
    catch (...)
    {
        // Logging must never be the reason we crash.
    }
}

} // namespace ls
