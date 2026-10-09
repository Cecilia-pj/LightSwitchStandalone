// DarkApp.h - the one place that talks to the dark-mode module.
//
// The dark-mode system layer (src/dark/darkmode.*) and the interface layer
// (src/dark/darkui.*) are copied byte-for-byte from the author's own
// `win32-dark-mode` skill and are deliberately left untouched, so their
// provenance stays verifiable with a hash. Everything this program needs on top
// of them lives here instead.
//
// Why a wrapper at all, rather than calling dm:: directly from App.cpp and
// SettingsDialog.cpp:
//   * dm::Init() and dm::OnApply() must both run, in that order, before any
//     window exists. Forgetting OnApply fails *silently* - the title bar goes
//     dark and the palette goes dark, but every control stays light, with no
//     error anywhere. One function that cannot be called half-way removes that
//     class of bug.
//   * The "is the theme already right?" test has to live in exactly one place,
//     because two call sites (the hidden main window and the settings dialog)
//     would otherwise be free to disagree.
//
// Persistence stays in settings.json, like every other setting in this program.
// The module has its own registry value; we namespace it (so it cannot collide
// with another program built on the same module) but never treat it as
// authoritative - settings.json is pushed into the module at startup, which
// overwrites whatever the registry held.
#pragma once

#include <windows.h>

#include <string>

namespace ls::DarkApp
{

// Call as the very first statement of wWinMain - before InitCommonControlsEx and
// before any window is created. uxtheme has to have the preferred app mode set
// before the first window appears, otherwise the first frame flashes light.
void Startup();

// Push a settings.json `uiTheme` value ("auto" / "light" / "dark") into the
// module. Unrecognised values behave like "auto" - a typo in the file degrades to
// following Windows rather than pinning the interface to something wrong.
// Returns true when this actually changed the active selection.
bool SetFromConfig(const std::wstring& uiTheme);

// True when the user asked to follow Windows.
bool IsFollowingSystem();

// What the process should be showing right now, resolved the same way the module
// resolves it. Used to decide whether Apply() is worth running at all.
bool DesiredDark();

// The dialog background brush, already in the current theme's colour. Non-null
// once Startup() has run - which it always has by the time a window exists.
// Forwarded rather than handed out via dm:: so that no caller has to include
// the module's own header.
HBRUSH BgBrush();

// Whether a WM_SETTINGCHANGE lParam means "the colour scheme changed"
// ("ImmersiveColorSet"). Same reasoning as BgBrush().
bool IsColorSchemeChange(LPARAM lParam);

// darkui::Install plus this project's DarkControls::Install. Call once the
// controls exist, and again for any window created later.
void InstallControls(HWND top);

// Unconditional dm::Apply. Wanted for the three dialog actions (live preview,
// save, cancel), because Apply is also what calls FlushMenuThemes - without
// which the tray popup menu keeps serving the *old* colour scheme from
// uxtheme's cache.
void ApplyNow(HWND top);

// dm::Apply, but only when the resolved theme differs from what is on screen.
// dm::Apply rebuilds two GDI brushes and repaints the whole child tree, and the
// tray menu is rebuilt from scratch on every right-click, so the unconditional
// version does not belong there.
void ApplyIfNeeded(HWND top);

} // namespace ls::DarkApp
