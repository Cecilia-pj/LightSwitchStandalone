# Changelog

All notable changes to this component are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
versioning follows the upstream PowerToys Light Switch module's schema — the
on-disk `settings.json` contract is versioned separately from this file and is
only ever extended additively.

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
