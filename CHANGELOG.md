# Changelog

All notable changes to this component are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
versioning follows the upstream PowerToys Light Switch module's schema — the
on-disk `settings.json` contract is versioned separately from this file and is
only ever extended additively.

## [1.2.0] - 2026-10-09

Dark-mode interface for the settings dialog and the tray menu, following the
Windows app theme by default.

### Added

* **Dark mode for the whole interface.** The settings dialog and the tray popup
  menu can be light or dark, chosen independently of the schedule.
  * `"auto"` (default) follows `AppsUseLightTheme` — the same registry value the
    scheduler writes — and follows it **live**: changing the Windows theme while
    the dialog is open re-themes it in place.
  * `"light"` and `"dark"` pin it, so a Windows theme change does not move it.
* `"uiTheme"` key in `settings.json`, defaulting to `"auto"`. Unrecognised
  values degrade to `"auto"` rather than pinning an unwanted theme.
* An *Interface theme* groupbox in the settings dialog (`主题` in Chinese), with
  a `Follow Windows` / `Light` / `Dark` dropdown.
* **Live preview**: changing the dropdown re-themes the open dialog immediately.
  `Save` persists it, `Cancel` restores the value from `settings.json`, and
  `Restore defaults` moves the preview back to *Follow Windows* along with the
  dropdown.
* `src/dark/DarkApp.{h,cpp}` — the single entry point for dark mode, so the
  mandatory `Init()` → `OnApply()` → `Install()` → `Apply()` ordering cannot be
  got half right, and so the two call sites cannot disagree about whether the
  theme is already correct.
* `src/dark/DarkControls.{h,cpp}` — self-drawn dark versions of the two controls
  the underlying module does not cover: `SysDateTimePick32` and
  `msctls_hotkey32`. Only the painting messages are intercepted, so hit-testing,
  keyboard input, the spinner and `DTM_*` / `HKM_*` storage all behave exactly
  as before.
* `src/dark/darkmode.{h,cpp}` and `src/dark/darkui.{h,cpp}` — the author's
  `win32-dark-mode` module, copied in **byte for byte** and compiled verbatim.
  Not upstream PowerToys code; see `UPSTREAM.md`.

### Changed

* Both time pickers are created with **`DTS_UPDOWN`**, replacing the drop-down
  calendar with an up/down spinner. The calendar panel is a system-populated
  window that cannot be darkened, so removing it is the only way to keep the
  dialog free of a bright rectangle. Values, notifications and the stored format
  are unchanged.
* The dialog grew from `340 x 361` to `340 x 397` DLU to make room for the new
  groupbox above the button row.
* The import table grew from **7 to 9** system DLLs: `dwmapi.dll` for the
  immersive dark title bar, `uxtheme.dll` for the undocumented
  `SetPreferredAppMode` / `AllowDarkModeForWindow` ordinals. Both are resolved at
  load time only for these two purposes; the `uxtheme` entry points are still
  fetched by ordinal at runtime. The static-CRT property is unchanged — there is
  still no `VCRUNTIME140.dll` / `MSVCP140.dll` dependency.
* `App::HandleMessage` now also reacts to `WM_SETTINGCHANGE` with
  `lParam == "ImmersiveColorSet"` and to `WM_THEMECHANGED`, so the theme is
  followed while no dialog is open — the tray icon outlives every dialog.
* The tray popup is re-themed on the way into `TrackPopupMenu`, conditionally, so
  a right-click costs nothing when nothing changed but a `settings.json` edited
  by hand is still picked up.
* Version bumped to `1.2.0.0` in both places it is declared — the `VERSIONINFO`
  block in `src/LightSwitch.rc` and `assemblyIdentity` in `src/app.manifest` —
  keeping the executable's product version and its assembly version in step, as
  1.1.0 had to be corrected to do.

### Fixed

* Nothing regressed from 1.1.0, so this section only records defects that were
  found and fixed inside 1.2.0's own new code, during the screenshot review that
  preceded the release. They never shipped.

### Notes

* A `HKM_GETHOTKEY` reading mistake was caught by the screenshot pass before
  release: the virtual key is in the **low** byte and the modifier flags in the
  **high** byte, so reading them the other way round turned `Ctrl+Shift+D`
  (`0x0344`) into `Alt + Scroll Lock`. `SettingsDialog.cpp` had always read it
  correctly; only the new self-drawn hotkey field was affected.
* The dark-mode spinner arrows are scaled against the control's DPI rather than
  against 96 DPI, so on a 125 % display they sit over the region the control
  actually hit-tests rather than a few pixels to the right of it.
* System-drawn surfaces stay light on purpose and cannot be changed from an
  application: the `MessageBox` validation and confirmation dialogs, and the
  tray icon's hover tooltip.
* Windows High Contrast is **not** deferred to; the module keeps its own
  palette. Set `uiTheme` to `"light"` if you rely on High Contrast for
  legibility.
* No file under `src/vendor/` was touched by this release; the vendored file
  diffs recorded in `UPSTREAM.md` are unchanged.

## [1.1.0] - 2026-10-07

Localised interface, plus the dialog layout work that fell out of it.

### Added

* **Localised interface: English and 简体中文.** Follows the Windows display
  language by default (`GetUserDefaultUILanguage()`), with a manual override in
  the settings dialog. Switching takes effect immediately — the dialog is
  rebuilt in the new language on save, no restart. A Windows display-language
  change is followed live via `WM_SETTINGCHANGE` / `lParam == "intl"`.
* `--lang CODE` command-line option (`auto` | `en-US` | `zh-CN`) — overrides the
  interface language for one process without writing `settings.json`.
* `--dump-strings FILE` command-line option — writes every interface string in
  every language to `FILE` as UTF-8 with a BOM, then exits before the
  single-instance check so it works while the app is running. Ids are stable, so
  a translation can be delivered as this file and diffed mechanically.
* `"language"` key in `settings.json`, defaulting to `"auto"`. Unrecognised
  values degrade to `"auto"` rather than pinning a language.
* `src/L10n.{h,cpp}` — the single source of truth for every user-visible string,
  keyed by a `Str` enumeration. `static_assert`s keep the enumeration, the table
  and their order in lockstep, so a forgotten entry fails the build.

### Changed

* The settings dialog template in `LightSwitch.rc` now carries **geometry and
  control ids only**. All text is set at `WM_INITDIALOG`, which keeps one source
  of truth per string and keeps non-ASCII literals out of a `.rc` file, where
  resource-compiler code page handling is a classic source of mojibake.
  Translated labels therefore need real control ids instead of `-1`.
* The dialog font is chosen per language (`Microsoft YaHei UI` for Chinese,
  `Segoe UI` otherwise) and applied with an explicit per-child `WM_SETFONT`,
  rather than relying on Windows font linking to supply CJK glyphs.
* The dialog grew to `340 x 361` DLU and gained an *Interface language*
  groupbox. Label boxes are sized for the wider of the two languages.
* The button row is laid out at runtime to measured text widths.
* The tray menu is rebuilt on every right-click, so it always reflects the active
  language.

### Fixed

* **`Restore defaults` overlapped `Save`** in the hand-placed button row. The row
  is now right-packed from measured label widths, so the overlap cannot recur in
  any language.
* **`FitLabels()` never widened anything.** Its boundary search treated the
  button row — far below, but further right — as each label's nearest neighbour,
  clamping every label to a few pixels. It now requires the two controls to
  actually share a row (a vertical-overlap test) before treating one as a
  boundary. `浅色主题开始时间：` and `日落偏移（分钟）：` were clipped in Chinese
  before this fix.
* `lightswitch.log` and the `--dump-strings` output disagreed on line breaks: a
  multi-line string (a message box body) became a real line break in the dump and
  silently split one entry into two lines. The dump now escapes `\r`, `\n`, `\t`
  and `\`, keeping the format line-oriented and machine-checkable.
* `src/app.manifest` still declared `version="1.0.0.0"` while `LightSwitch.rc`
  declared `1.1.0.0`, so the executable's assembly version and product version
  disagreed. Both are now `1.1.0.0`.

### Notes

* The log file is **not** translated on purpose: it is a developer artefact, and
  one language keeps two bug reports comparable.
* The registry value names, `settings.json` keys and mode strings
  (`"FixedHours"`, …) are the on-disk contract shared with upstream and are not
  translated either.
* `zh-Hant` (Traditional Chinese) resolves to the Simplified translation rather
  than to English, deliberately — see `README.md`.
* No file under `src/vendor/` was touched by this release; the vendored file
  diffs recorded in `UPSTREAM.md` are unchanged.

## [1.0.0] - 2026-10-07

First standalone release: the PowerToys Light Switch module extracted into a
single-process tray application with no PowerToys, Runner, Settings UI, vcpkg,
NuGet or Visual C++ redistributable dependency.

* Registry targets, broadcast sequence, `ColorPrevalence` reset, NOAA
  sunrise/sunset maths and the cross-midnight schedule logic are vendored
  verbatim from upstream; the decision logic is ported 1:1 from
  `LightSwitchStateManager`.
* Four schedule modes: Off, Fixed hours, Sunset to sunrise, Follow Night Light.
* Global toggle shortcut (default `Ctrl+Win+Shift+D`) with a low-level keyboard
  hook fallback, since Windows reserves `MOD_WIN` combinations.
* Native Win32 settings dialog; tray icon drawn at runtime; `HKCU\...\Run`
  autostart; single-instance enforcement.
* Live reload of `settings.json` via `ReadDirectoryChangesW` with content
  comparison, so a write from the app itself never causes a reload loop.
* Static CRT; the import table is seven system DLLs only.
