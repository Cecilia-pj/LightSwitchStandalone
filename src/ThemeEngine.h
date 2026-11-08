// LightSwitch Standalone - theme application helper.
//
// One job: given a target .theme file, make it the active Windows theme - but
// only when it is not already active. Everything below the "is it already
// active" test lives in src/WindowsTheme.cpp (COM, STA thread) and
// src/ThemeCatalog.cpp (identity, file parsing).
//
// Identity, not a path comparison, is what decides "already active": copying a
// theme to a new name gives it a new path but not a new ThemeId, and Windows
// rewrites CurrentTheme to Custom.theme whenever the user tweaks something.
// See SameIdentity in src/ThemeCatalog.h.
//
// NOTE: applying a theme blocks for seconds (wallpaper, colours, sounds). This
// must never be invoked from the UI thread - the scheduler worker and the
// one-shot command line path are the only callers.
#pragma once

#include <string>

#include "ThemeCatalog.h"
#include "WindowsTheme.h"

namespace ls
{

struct ThemeTarget
{
    std::wstring path;             // absolute; empty when the side is unconfigured
    theme::ThemeIdentity identity; // resolved once, so the comparison is not an extra file read

    // What the pack itself asks the shell to switch to. A pack that declares
    // neither is a wallpaper-only theme and there is nothing to verify.
    bool hasSystemMode = false;
    bool systemModeLight = false;
    bool hasAppMode = false;
    bool appModeLight = false;

    bool Empty() const { return path.empty(); }
};

// Parses the file once and captures its identity and declared modes. Cheap
// enough to call per evaluation, and it keeps a deleted-through-the-Explorer
// theme from being mistaken for a valid target.
ThemeTarget MakeTarget(const std::wstring& path);

// True when the live SystemUsesLightTheme / AppsUseLightTheme already agree with
// what the pack declares; true as well when the pack declares nothing.
//
// This is the check identity cannot make. CurrentTheme is only a bookmark to a
// file: it can name a light pack while the two DWORDs say dark - which is
// exactly what the registry fallback leaves behind, and what survives a reboot.
// Deciding "already applied" on identity alone therefore makes the program sit
// still on a desktop it never actually switched.
bool TargetModeSatisfied(const ThemeTarget& target);

// Applies the target unless it is already the current theme *and* the live
// light/dark mode matches the pack. Falls back to the legacy registry switch
// (SystemUsesLightTheme / AppsUseLightTheme) when the COM path is unavailable
// or silently does nothing - a policy-disabled theme service still gets a
// light/dark change, just without the wallpaper and cursors.
ApplyReport ApplyThemeTarget(const ThemeTarget& target);

// True when the report means "nothing is wrong, the desktop is where it should
// be" - Ok and AlreadyCurrent. Callers use this to decide on retry/back-off.
bool IsSuccess(const ApplyReport& report);

} // namespace ls
