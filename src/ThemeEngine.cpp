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
    return target;
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

    if (theme::SameIdentity(report.observed, target.identity))
    {
        report.status = ApplyStatus::AlreadyCurrent;
        report.detail = L"already active";
        return report;
    }

    report = ApplyThemeFile(target.path);

    if (IsSuccess(report) || report.status == ApplyStatus::FileMissing ||
        report.status == ApplyStatus::TimedOut)
    {
        // A timeout is deliberately not retried here: the theme may well have
        // been applied, and a second attempt would just swap the wallpaper
        // again. The caller records the attempt and stays quiet.
        return report;
    }

    LOG_WARN(L"[ThemeEngine] Pack could not be applied (" << ApplyStatusName(report.status)
                                                          << L"); falling back to the light/dark mode in the file.");
    const theme::ThemeInfo info = theme::DescribePath(target.path);
    if (!ApplyRegistryFallback(info))
    {
        LOG_ERROR(L"[ThemeEngine] \"" << target.path
                                      << L"\" has no SystemMode/AppMode either; nothing to fall back to.");
        return report;
    }

    report.status = ApplyStatus::Ok;
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
