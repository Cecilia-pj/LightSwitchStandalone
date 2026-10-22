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

    bool Empty() const { return path.empty(); }
};

// Parses the file once and captures its identity. Cheap enough to call per
// evaluation, and it keeps a deleted-through-the-Explorer theme from being
// mistaken for a valid target.
ThemeTarget MakeTarget(const std::wstring& path);

// Applies the target unless it is already the current theme. Falls back to the
// legacy registry switch (SystemUsesLightTheme / AppsUseLightTheme) when the
// COM path is unavailable - a policy-disabled theme service still gets a
// light/dark change, just without the wallpaper and cursors.
ApplyReport ApplyThemeTarget(const ThemeTarget& target);

// True when the report means "nothing is wrong, the desktop is where it should
// be" - Ok and AlreadyCurrent. Callers use this to decide on retry/back-off.
bool IsSuccess(const ApplyReport& report);

} // namespace ls
