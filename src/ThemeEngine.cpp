// LightSwitch Standalone - theme application helper (implementation).
#include "ThemeEngine.h"

#include "Config.h"
#include "Logging.h"
#include "vendor/ThemeHelper.h"

namespace ls
{

void ApplyTheme(const Config& config, bool shouldBeLight)
{
    if (config.changeSystem)
    {
        const bool isSystemCurrentlyLight = GetCurrentSystemTheme();
        if (shouldBeLight != isSystemCurrentlyLight)
        {
            SetSystemTheme(shouldBeLight);
            LOG_INFO(L"[ThemeEngine] Changed system theme to " << (shouldBeLight ? L"light" : L"dark") << L".");
        }
    }

    if (config.changeApps)
    {
        const bool isAppsCurrentlyLight = GetCurrentAppsTheme();
        if (shouldBeLight != isAppsCurrentlyLight)
        {
            SetAppsTheme(shouldBeLight);
            LOG_INFO(L"[ThemeEngine] Changed apps theme to " << (shouldBeLight ? L"light" : L"dark") << L".");
        }
    }
}

} // namespace ls
