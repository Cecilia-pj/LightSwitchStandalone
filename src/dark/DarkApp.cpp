// DarkApp.cpp - see DarkApp.h for why this wrapper exists.
#include "DarkApp.h"

#include "DarkControls.h"
#include "darkmode.h"
#include "darkui.h"

#include <cwchar>

namespace ls::DarkApp
{

namespace
{

// The module's own preference value. Namespaced under this program so a second
// program built on the same module cannot collide with it. This is only the
// module's cache; settings.json is authoritative and is pushed in from
// wWinMain.
constexpr wchar_t kPrefKey[] = L"Software\\LightSwitchStandalone\\DarkMode";

dm::AppTheme ToModuleTheme(const std::wstring& uiTheme)
{
    if (_wcsicmp(uiTheme.c_str(), L"dark") == 0)
        return dm::ThemeDark;
    if (_wcsicmp(uiTheme.c_str(), L"light") == 0)
        return dm::ThemeLight;
    return dm::ThemeSystem; // "auto", and anything we do not recognise
}

} // namespace

void Startup()
{
    dm::SetPrefKey(kPrefKey);
    dm::Init();

    // Not optional. Without it dm::Apply() reaches `if (g_applyFn)` and silently
    // does nothing for the controls - the failure mode is a dark title bar over
    // a fully light dialog, with no error to follow.
    dm::OnApply(darkui::Apply);

    // Build the palette and set the process-wide app mode before any window is
    // created. The palette is only ever built inside dm::Apply(), so without
    // this call dm::BgBrush() returns null until the settings dialog happens to
    // be opened - which would leave the tray popup menu on the *system* colour
    // scheme even when the user pinned light or dark. Passing null is safe: the
    // modules guard every use of the window handle.
    dm::Apply(nullptr);
}

bool SetFromConfig(const std::wstring& uiTheme)
{
    const dm::AppTheme wanted = ToModuleTheme(uiTheme);
    if (wanted == dm::UserTheme())
        return false;

    dm::SetUserTheme(wanted);
    return true;
}

bool IsFollowingSystem()
{
    return dm::UserTheme() == dm::ThemeSystem;
}

bool DesiredDark()
{
    switch (dm::UserTheme())
    {
    case dm::ThemeDark:
        return true;
    case dm::ThemeLight:
        return false;
    default:
        return dm::IsSystemDark();
    }
}

HBRUSH BgBrush()
{
    return dm::BgBrush();
}

bool IsColorSchemeChange(LPARAM lParam)
{
    return dm::IsColorSchemeChange(lParam);
}

void InstallControls(HWND top)
{
    darkui::Install(top);
    DarkControls::Install(top);
}

void ApplyNow(HWND top)
{
    dm::Apply(top);
}

void ApplyIfNeeded(HWND top)
{
    if (DesiredDark() == dm::IsDark())
        return;

    dm::Apply(top);
}

} // namespace ls::DarkApp
