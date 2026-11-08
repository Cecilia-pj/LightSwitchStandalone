// LightSwitch Standalone - theme application helper (implementation).
#include "ThemeEngine.h"

#include "Logging.h"
#include "vendor/ThemeHelper.h"

namespace ls
{

namespace
{

// "[VisualStyles] SystemMode" / "AppMode" hold "Light" or "Dark" (and nothing
// else) in every .theme Windows ships or writes.
bool ModeIsLight(const std::wstring& mode, bool& light)
{
    if (_wcsicmp(mode.c_str(), L"Light") == 0)
    {
        light = true;
        return true;
    }
    if (_wcsicmp(mode.c_str(), L"Dark") == 0)
    {
        light = false;
        return true;
    }
    return false;
}

// Last resort: switch the light/dark mode the .theme asks for, without the
// wallpaper, cursors and sounds. This is what a machine with the theme service
// disabled by policy can still do, and it is strictly better than doing
// nothing at all. Reuses the vendored ThemeHelper - deliberately not modified.
bool ApplyRegistryFallback(const theme::ThemeInfo& info)
{
    bool light = false;
    const bool hasSystem = ModeIsLight(info.systemMode, light);
    const bool systemLight = light;
    const bool hasApps = ModeIsLight(info.appMode, light);
    const bool appsLight = light;

    if (!hasSystem && !hasApps)
        return false;

    if (hasSystem && GetCurrentSystemTheme() != systemLight)
        SetSystemTheme(systemLight);
    if (hasApps && GetCurrentAppsTheme() != appsLight)
        SetAppsTheme(appsLight);

    return true;
}

} // namespace

ThemeTarget MakeTarget(const std::wstring& path)
{
    ThemeTarget target;
    if (path.empty())
        return target;

    const theme::ThemeInfo info = theme::DescribePath(path);
    target.path = info.path;
    target.identity = theme::ThemeIdentity{ info.themeId, info.path };
    target.hasSystemMode = ModeIsLight(info.systemMode, target.systemModeLight);
    target.hasAppMode = ModeIsLight(info.appMode, target.appModeLight);
    return target;
}

bool TargetModeSatisfied(const ThemeTarget& target)
{
    if (target.hasSystemMode && GetCurrentSystemTheme() != target.systemModeLight)
        return false;
    if (target.hasAppMode && GetCurrentAppsTheme() != target.appModeLight)
        return false;
    return true;
}

ApplyReport ApplyThemeTarget(const ThemeTarget& target)
{
    ApplyReport report;
    report.requestedPath = target.path;
    report.observed = theme::ReadCurrentIdentity();

    if (target.Empty())
    {
        report.status = ApplyStatus::FileMissing;
        report.detail = L"no theme configured for this side of the schedule";
        return report;
    }

    const bool identityMatches = theme::SameIdentity(report.observed, target.identity);
    const bool modeMatches = TargetModeSatisfied(target);

    if (identityMatches && modeMatches)
    {
        report.status = ApplyStatus::AlreadyCurrent;
        report.detail = L"already active";
        return report;
    }

    if (identityMatches)
    {
        // CurrentTheme already names the pack, but the live light/dark DWORDs
        // disagree. Re-applying is the only way to put them back in step, so
        // OpenTheme has to run even though the identity says "nothing to do".
        LOG_INFO(L"[ThemeEngine] \"" << target.path << L"\" is already CurrentTheme, but the live light/dark "
                 L"mode disagrees (system=" << (GetCurrentSystemTheme() ? L"Light" : L"Dark")
                 << L", apps=" << (GetCurrentAppsTheme() ? L"Light" : L"Dark")
                 << L"). Re-applying the pack.");
    }

    report = ApplyThemeFile(target.path, 12000, identityMatches);

    // Missing file and timeout are both "leave it alone": a timeout may well
    // have applied, and a second attempt would only swap the wallpaper again.
    if (report.status == ApplyStatus::FileMissing || report.status == ApplyStatus::TimedOut)
        return report;

    if (IsSuccess(report) && TargetModeSatisfied(target))
        return report;

    if (IsSuccess(report))
    {
        // themeui accepted the pack and the screen did change, but the two
        // DWORDs did not follow. Writing them is the difference between a real
        // switch and a half-applied one.
        LOG_WARN(L"[ThemeEngine] The pack was applied but SystemUsesLightTheme/AppsUseLightTheme did not "
                 L"follow (system=" << (GetCurrentSystemTheme() ? L"Light" : L"Dark")
                 << L", apps=" << (GetCurrentAppsTheme() ? L"Light" : L"Dark")
                 << L"); writing the registry fallback.");
    }
    else
    {
        LOG_WARN(L"[ThemeEngine] Pack could not be applied (" << ApplyStatusName(report.status)
                 << L", hr=0x" << std::hex << static_cast<unsigned long>(report.hresult) << std::dec
                 << L": " << report.detail << L"); falling back to the light/dark mode in the file.");
    }

    const theme::ThemeInfo info = theme::DescribePath(target.path);
    if (!ApplyRegistryFallback(info))
    {
        LOG_ERROR(L"[ThemeEngine] \"" << target.path
                                      << L"\" has no SystemMode/AppMode either; nothing to fall back to.");
        return report;
    }

    report.status = ApplyStatus::Ok;
    report.usedFallback = true;
    report.detail = L"registry fallback (SystemMode=" + info.systemMode + L", AppMode=" + info.appMode + L")";
    report.observed = theme::ReadCurrentIdentity();
    LOG_WARN(L"[ThemeEngine] Applied the " << report.detail
                                           << L" only - wallpaper, cursor and sounds are unchanged.");
    return report;
}

bool IsSuccess(const ApplyReport& report)
{
    return report.status == ApplyStatus::Ok || report.status == ApplyStatus::AlreadyCurrent;
}

} // namespace ls
