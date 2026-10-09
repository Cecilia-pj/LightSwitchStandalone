// DarkControls.h - self-drawn replacements for the two controls the dark-mode
// interface layer does not cover.
//
// Why this file exists
// --------------------
// Windows ships dark resources for only a handful of controls. src/dark/darkui.cpp
// covers Static, Edit, ListBox, ComboBox, Button, CheckBox, RadioButton,
// GroupBox, ListView, TreeView, StatusBar, the menu bar and popup menus - but not
// the two controls this settings dialog also uses:
//
//   * SysDateTimePick32  (the two time pickers)
//   * msctls_hotkey32    (the shortcut box)
//
// The skill's own control-recipes document lists DateTimePicker under "not
// covered, needs a full WM_PAINT takeover". Neither control has a dark theme
// class to switch to (the DarkMode_* classes do not exist), and both ignore
// every WM_CTLCOLOR* message, so there is nothing to turn on - the drawing has to
// be taken over.
//
// What the takeover does and does not do
// --------------------------------------
// The subclasses below intercept the *painting* messages and nothing else. Mouse
// hit-testing, keyboard input, the spinner, and the DTM_* / HKM_* message storage
// all stay with the control, so the controls keep working exactly as before -
// only the pixels change. This is the same trade the skill makes for the status
// bar (control-recipes.md section 10): take over the drawing, leave the data
// alone.
//
// Colours are read from dm::Colors() at paint time instead of being cached, so
// this module has no ordering dependency on darkui::Apply. It does require that
// dm::Apply() has run at least once (which builds the palette) - ls::DarkApp::
// Startup() guarantees that.
#pragma once

#include <windows.h>

namespace ls::DarkControls
{

// Subclass every SysDateTimePick32 and msctls_hotkey32 found directly under
// `top`. Idempotent: controls that already carry the subclass are skipped, so it
// is safe to call again after new controls appear (a dialog rebuilt for a
// language change goes through this path).
void Install(HWND top);

} // namespace ls::DarkControls
