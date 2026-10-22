# Changelog

All notable changes to this component are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
versioning follows the upstream PowerToys Light Switch module's schema — the
on-disk `settings.json` contract is versioned separately from this file and is
only ever extended additively.

## [1.4.0] - 2026-10-22

The schedule switches **Windows themes** instead of flipping two registry
DWORDs. Instead of "light mode / dark mode" you now pick two `.theme` packs,
and the whole pack travels with the switch: wallpaper, colours, cursor, sounds
and accent colour.

This is a behaviour change, not an addition. Anything that relied on the old
meaning of the two checkboxes is gone; see *Migration* below.

### Changed

* **The two "apply to" checkboxes are replaced by two theme dropdowns.** The
  group holds *Light:* and *Dark:* instead of *Change the Windows theme* and
  *Change the application theme*. A `.theme` pack carries `SystemMode` and
  `AppMode` inside itself, so the pair of booleans has nothing left to decide.
  The new group fills exactly the rectangle the old one occupied, so the dialog
  keeps its height — at 125% on a 1080p screen it is already 1040 px against a
  1020 px work area, and one more row would put the button row behind the
  taskbar.
* **Every dropdown entry is a real file.** The list is
  `%LOCALAPPDATA%\Microsoft\Windows\Themes` (recursively, one level, because
  wallpaper packs such as Spotlight keep their `.theme` in a subfolder) plus
  `%SystemRoot%\Resources\Themes`, plus the path already configured even when it
  lives somewhere else, plus a trailing **Browse…** row that opens a file
  picker. `Custom.theme` is skipped: Windows rewrites it behind your back and
  its `ThemeId` changes with it, so listing it would offer a moving target.
* **The status line reads `Now: … / Target: …`.** *Now* is the theme
  `CurrentTheme` points at, re-read each time the dialog repaints, so a theme
  changed from Windows Settings while the dialog was open is reflected. *Target*
  is the pack the schedule is asking for at this minute, derived from the
  controls on screen with the same rules the scheduler uses. With the schedule
  off it names whichever pack is currently on screen, which is the side a
  manual toggle would leave.
* **The tray menu gained two rows.** *Light theme: …* and *Dark theme: …*, each
  bulleted when Windows is running it, each disabled when that side is not
  configured. Choosing one enters manual-override mode, exactly like the
  toggle, so the schedule does not undo an explicit "this one, now" a minute
  later.
* **The tooltip names the current pack** after the schedule summary. The mode
  tells you when it will switch; it never told you what you were looking at.
* Applying a pack is done through `IThemeManager2` (`themeui.dll`,
  `{9324DA94-50EC-4A14-A770-E90CA03E7C8F}`) on a dedicated STA thread. The call
  blocks for seconds — it copies the wallpaper and restarts the shell's theme
  providers — so it never runs on the UI thread.

### Added

* `"themeA"` / `"themeB"` in `settings.json`: absolute paths to the two packs.
  An empty string means "not chosen yet", and automatic switching then stays
  idle rather than guessing.
* Two commands for scripts and for diagnosing a machine where the pack does not
  apply:

  ```
  LightSwitchStandalone.exe --dump-themes  FILE.tsv
  LightSwitchStandalone.exe --theme-check  FILE.txt
  ```

  `--dump-themes` lists every theme the catalogue can see, with its `ThemeId`,
  `SystemMode`/`AppMode` and whether it is the current one. `--theme-check`
  probes the COM class and reports the interface version, the theme count and
  the identity Windows is currently running.
* `--theme FILE` applies one `.theme` file once and exits, without touching the
  configuration.
* `--theme-a` / `--theme-b` as synonyms for `--light` / `--dark`, which are
  kept.

### Fixed

* **A theme changed by hand is no longer fought every minute.** The old code
  compared two booleans, which cannot tell "the user picked another theme" from
  "Windows re-spelled the path" from "our own apply is still settling". The
  comparison is now on identity — `ThemeId` when both files have one, path
  otherwise — with a 30 s grace window after our own apply, because Windows
  rewrites `CurrentTheme` to `Custom.theme` when a pack is applied and both the
  id and the path change while it does.
* A pack whose file has disappeared is treated as a configuration error: the
  scheduler backs off for 30 minutes and logs it once, instead of entering
  manual-override mode and permanently standing down.
* `%SystemRoot%\Resources\Themes` is honoured. Themes installed for all users
  were previously invisible to everything that enumerated themes.

### Migration

* A 1.3.x `settings.json` needs no editing. When neither `themeA` nor `themeB`
  is present and either of the old checkboxes was on, `themeA` is seeded with
  the theme Windows is currently running and `themeB` is left empty — the
  message *"Migrated from 1.3.x: themeA=… Choose the dark-side theme in
  Settings."* is logged once. Nothing is written to disk until you press Save.
* `"changeSystem"` and `"changeApps"` are still round-tripped untouched. They
  no longer drive anything, but leaving them alone is what lets a downgrade to
  1.3.x behave the way that file says.
* **If COM cannot be used** (a locked-down machine, or `themeui.dll` refusing
  to activate), the pack cannot be applied. The program says so —
  `Pack could not be applied; falling back to the light/dark mode in the file.`
  — and writes only `SystemUsesLightTheme` / `AppsUseLightTheme` from the
  pack's `[VisualStyles]` section, which changes colours but not wallpaper,
  cursor or sounds. It does not pretend the switch succeeded.

### Notes

* `settings.json` remains additive: a PowerToys file still loads, and the keys
  this program does not model are still dropped on save, as before.
* `--dump-themes` and `--theme-check` write UTF-8 with a BOM so a plain
  `Get-Content` or `type` shows them correctly.

## [1.3.1] - 2026-10-10

The tray icon no longer disappears when `explorer.exe` restarts.

### Fixed

* **The tray icon comes back after an Explorer restart instead of staying gone.**
  The notification area belongs to `explorer.exe`, so replacing that process
  takes every icon in it; Windows keeps no record of who registered what and
  never asks anyone to register again, so the icon used to be lost until the
  program itself was restarted. It now listens for the shell's `TaskbarCreated`
  broadcast and registers itself again, tooltip included. That covers all three
  ways the shell is replaced: *Restart Explorer* in the tray menu, the button of
  the same name in the settings dialog, and an `explorer.exe` that crashed or was
  killed from outside.
* *Restart Explorer*, the program's own path, no longer depends on that broadcast
  arriving. It terminates explorer, waits, then starts a new one, with the UI
  thread parked throughout; Winlogon's `AutoRestartShell` (on by default) can
  revive the shell during that wait, and the broadcast is then sent to a thread
  that is not pumping messages and is dropped. A bounded retry timer covers that
  ordering.
* A **repeated** registration request is no longer reported as a failure.
  `NIM_ADD` is refused when the shell already holds an icon for the same
  `(hWnd, uID)`, and one shell restart can produce two attempts — the broadcast
  and the retry timer react to the same event. That used to end in ten refused
  attempts and `The tray icon could not be registered again after 10 attempts;
  restart the program to get it back.`, logged at `ERROR`, while the icon was in
  fact on screen. `ReAdd()` now falls back to `NIM_MODIFY`, which only succeeds if
  the shell really does hold the icon, and reports
  `This shell already had the icon; refreshed it instead.`

### Notes

* No `settings.json` change. The key set is identical to 1.3.0's, so nothing has
  to be migrated.

## [1.3.0] - 2026-10-10

A tray icon left click no longer toggles the theme unless you ask for it, so a
touch screen cannot change the theme by accident.

### Added

* `"trayClickTogglesTheme"` key in `settings.json`, defaulting to **`false`**.
  When it is false a single left click on the tray icon does nothing at all, and
  the log records `Left click ignored; "trayClickTogglesTheme" is off.` at
  `DEBUG` level so the silence is diagnosable rather than mysterious.
* A *Left click on tray icon toggles theme* checkbox in the settings dialog
  (`单击托盘图标切换主题` in Chinese), in the group that holds the other way to
  trigger a toggle. Off by default; `Restore defaults` unticks it.

### Changed

* **A single left click on the tray icon no longer toggles the theme by
  default.** This is a deliberate behaviour change, not a regression: a tap on a
  touch screen arrives as the same `WM_LBUTTONUP` a mouse click produces, so the
  old default meant any finger brushing past the icon flipped the Windows theme
  instantly, with no confirmation and no undo.
  The other three ways to toggle are unaffected and always available: the global
  hotkey, *Toggle theme now* in the tray context menu, and *Toggle now* in the
  settings dialog. Double-clicking the icon still opens the settings dialog, and
  right-clicking still opens the menu.
* `DialogGroupShortcut` now reads *Toggle shortcuts* (was *Shortcut*) /
  *切换方式* (was *快捷键*). The group holds the tray-click option as well as the
  hotkey, so the old caption no longer described its contents.

### Fixed

* The tray icon's click behaviour was undocumented: `README.md` described the
  context menu and the double click but never mentioned that a single left click
  toggled the theme, which is the one gesture that could surprise a user. The
  table in *Trigger conditions* now covers every gesture, including the two that
  the shell delivers for touch.

### Notes

* **No press-duration logic exists, and none was removed.** There is no
  long-press gesture anywhere in `src/`: no `SetTimer`, no `WM_LBUTTONDOWN`
  handler on the tray path, no `GetMessageTime`. The gesture being guarded is a
  plain single click. On a touch screen Windows maps a tap to `WM_LBUTTONUP` and
  a press-and-hold to `WM_CONTEXTMENU`, so press-and-hold opens the menu and
  never toggled the theme even before this change.
* Settings written by 1.2.0 or earlier have no `trayClickTogglesTheme` key, and
  the missing key falls back to `false`. Upgrading therefore never silently
  re-enables the gesture.
* The checkbox had to go into the *Toggle shortcuts* group rather than a group of
  its own. The dialog is 397 dlu tall, which is already 1040 px at 125 % scaling
  on a 1080p screen against a 1020 px work area; adding a group pushed the button
  row to 1048 px, behind the taskbar and unclickable. Reusing space that group
  already had keeps the geometry identical to 1.2.0.

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

* Nothing regressed from 1.1.0, so this section records only defects found in
  1.2.0's own new code and packaging, before the release.
* `.gitattributes` did not pin the four vendored dark-mode files to LF. With
  `* text=auto` and `core.autocrlf=true` — the Windows default — a fresh clone
  checked `src/dark/darkmode.*` and `src/dark/darkui.*` out with CRLF endings, so
  the `sha256sum` command in `README.md` and `UPSTREAM.md` failed on a freshly
  cloned tree even though the files were correct in the repository. The four files
  are now pinned `eol=lf`, which is what makes the published provenance check
  reproducible. A `git add --renormalize .` pass confirmed no other file in the
  repository changed, so nothing else is affected.

### Notes

* The two defects below were found and fixed inside 1.2.0's own new code during
  the screenshot review that preceded the release. Neither ever shipped.

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
