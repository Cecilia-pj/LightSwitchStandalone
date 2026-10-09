// LightSwitch Standalone - "start with Windows" management (implementation).
#include "AutoStart.h"

#include "L10n.h"
#include "Logging.h"

#include <windows.h>

#include <vector>

namespace ls
{

namespace
{

std::wstring CurrentExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0)
            return {};
        if (len < buffer.size() - 1)
            return std::wstring(buffer.data(), len);
        buffer.resize(buffer.size() * 2);
    }
}

bool ReadRunValue(std::wstring& value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AutoStart::RegistryKeyPath().c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    DWORD type = 0;
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(key, L"LightSwitchStandalone", nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0)
    {
        RegCloseKey(key);
        return false;
    }

    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(key, L"LightSwitchStandalone", nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS)
        return false;

    while (!buffer.empty() && buffer.back() == L'\0')
        buffer.pop_back();

    value = buffer;
    return true;
}

std::wstring StripQuotes(const std::wstring& text)
{
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
        return text.substr(1, text.size() - 2);
    return text;
}

} // namespace

std::wstring AutoStart::RegistryKeyPath()
{
    return L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}

bool AutoStart::IsEnabled()
{
    std::wstring value;
    if (!ReadRunValue(value))
        return false;

    const std::wstring exe = CurrentExecutablePath();
    if (exe.empty())
        return true;

    // Enabled, but possibly pointing at a stale location after the folder was
    // moved. We report "enabled" in that case and Apply() will fix the path.
    return true;
}

bool AutoStart::Enable()
{
    const std::wstring exe = CurrentExecutablePath();
    if (exe.empty())
    {
        LOG_ERROR(L"[AutoStart] Cannot determine executable path.");
        return false;
    }

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RegistryKeyPath().c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        LOG_ERROR(L"[AutoStart] Cannot open Run key (error " << GetLastError() << L").");
        return false;
    }

    const std::wstring quoted = L"\"" + exe + L"\" --startup";
    const DWORD bytes = static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(quoted.c_str()), bytes);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS)
    {
        LOG_ERROR(L"[AutoStart] Cannot write Run value (error " << status << L").");
        return false;
    }

    LOG_INFO(L"[AutoStart] Enabled: " << quoted);
    return true;
}

bool AutoStart::Disable()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RegistryKeyPath().c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return true; // nothing to remove

    const LSTATUS status = RegDeleteValueW(key, kValueName);
    RegCloseKey(key);

    const bool ok = (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
    if (ok)
        LOG_INFO(L"[AutoStart] Disabled.");
    else
        LOG_ERROR(L"[AutoStart] Cannot delete Run value (error " << status << L").");
    return ok;
}

bool AutoStart::Apply(bool enabled)
{
    return enabled ? Enable() : Disable();
}

std::wstring AutoStart::StatusText()
{
    std::wstring value;
    if (!ReadRunValue(value))
        return L10n::T(Str::AutoStartNotRegistered);

    const std::wstring exe = CurrentExecutablePath();
    const std::wstring registered = StripQuotes(value);

    if (!exe.empty() && registered.rfind(exe, 0) != 0)
        return L10n::F(Str::AutoStartElsewhere, registered);

    return L10n::F(Str::AutoStartRegistered, value);
}

} // namespace ls
