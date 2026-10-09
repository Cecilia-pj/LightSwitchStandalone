// LightSwitch Standalone - small text helpers (implementation).
#include "Text.h"

#include <windows.h>

namespace ls
{

std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};

    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};

    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
        return {};

    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0)
        return {};

    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

bool EqualsIgnoreCaseAscii(const std::string& a, const char* b)
{
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i)
    {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb)
            return false;
    }
    return i == a.size() && b[i] == '\0';
}

} // namespace ls
