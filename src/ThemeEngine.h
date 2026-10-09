// LightSwitch Standalone - theme application helper.
//
// Mirrors the upstream `ApplyTheme(bool shouldBeLight)` free function from
// LightSwitchService.cpp: it honours the changeSystem / changeApps switches and
// only writes the registry when the current value actually differs.
//
// NOTE: SetSystemTheme/SetAppsTheme broadcast WM_SETTINGCHANGE with a 5 second
// SMTO_ABORTIFHUNG timeout, so this call can block for several seconds. It must
// never be invoked from the UI thread.
#pragma once

namespace ls
{

struct Config;

void ApplyTheme(const Config& config, bool shouldBeLight);

} // namespace ls
