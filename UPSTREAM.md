# Vendored upstream provenance

All files under `src/vendor/` are copied from
[microsoft/PowerToys](https://github.com/microsoft/PowerToys) and remain under
the MIT license (see `LICENSE`).

| Upstream (pinned) | |
|---|---|
| Repository | `microsoft/PowerToys` |
| Branch at capture time | `main` |
| Commit | `0a7a919e538aba513cffbd1ec76acbc4d4cc624a` |
| Commit date | 2026-10-07 |
| Captured on | 2026-10-07 |

## File-by-file

| Local file | Upstream path | Changes |
|---|---|---|
| `src/vendor/ThemeHelper.h` | `src/modules/LightSwitch/LightSwitchLib/ThemeHelper.h` | None (byte-identical apart from the added MIT header comment). |
| `src/vendor/ThemeHelper.cpp` | `src/modules/LightSwitch/LightSwitchLib/ThemeHelper.cpp` | **Modified** — see below. |
| `src/vendor/ThemeScheduler.h` | `src/modules/LightSwitch/LightSwitchService/ThemeScheduler.h` | None. |
| `src/vendor/ThemeScheduler.cpp` | `src/modules/LightSwitch/LightSwitchService/ThemeScheduler.cpp` | None. |
| `src/vendor/LightSwitchUtils.h` | `src/modules/LightSwitch/LightSwitchService/LightSwitchUtils.h` | None. |
| `src/vendor/NightLightRegistryObserver.h` | `src/modules/LightSwitch/LightSwitchService/NightLightRegistryObserver.h` | None. |
| `src/vendor/SettingsConstants.h` | `src/modules/LightSwitch/LightSwitchService/SettingsConstants.h` | None. |

### `ThemeHelper.cpp` — the only edited vendor file

Four edits, all of them about removing PowerToys build glue:

1. Removed `#include "pch.h"` — this project has no precompiled header.
2. Removed `#include <logger/logger.h>` — the PowerToys logger is not vendored.
3. Replaced the single `Logger::info(L"[LightSwitchLib] Reset ColorPrevalence ...")`
   call with the local `LOG_INFO(...)` macro from `src/Logging.h`.
4. Added explicit `#include <windows.h>` / `#include <vector>`, which upstream
   picked up transitively through `pch.h`.

No behavioural line was touched. `SetSystemTheme`, `SetAppsTheme`,
`GetCurrentSystemTheme`, `GetCurrentAppsTheme` and `IsNightLightEnabled` are
verbatim upstream.

The other six vendored files differ from upstream **only** by the added MIT
header comment (plus a stripped UTF-8 BOM in `LightSwitchUtils.h` and a trailing
newline in `NightLightRegistryObserver.h`). That has been verified by diffing
against the pinned commit.

### Deliberately NOT vendored

| Upstream file | Why |
|---|---|
| `LightSwitchService/LightSwitchSettings.{h,cpp}` | Depends on PowerToys' `common/SettingsAPI`, `FileWatcher`, `Trace` and GPO helpers. Re-implemented as `src/Config.{h,cpp}` keeping the same settings.json schema. |
| `LightSwitchService/SettingsObserver.h` | Only useful together with `LightSwitchSettings::instance()`. The local config store signals a plain Win32 event instead. |
| `LightSwitchService/LightSwitchStateManager.{h,cpp}` | Its logic is ported (not copied) into `src/StateManager.{h,cpp}`, minus the Power Display cross-module hook and the settings.json write-back. Read the upstream pair side by side when reviewing behaviour. |
| `LightSwitchService/LightSwitchService.cpp` | Service control dispatcher, GPO check, EtwTrace and the parent-process watchdog are all PowerToys-host concerns. |
| `LightSwitchModuleInterface/*` | Implements `PowertoyModuleIface` and launches the service; the Runner loads it. Not applicable. |
| `src/settings-ui/**` | WinUI 3 settings page + ViewModel; replaced by a native Win32 dialog. |
| `src/runner/**`, `src/common/**` | Runner, SettingsAPI, logger, interop, notifications. Replaced by the local modules. |

## Standalone-only additions (not upstream-derived)

Everything below has no upstream counterpart. Nothing here creates a sync
obligation, and — importantly — **neither the localisation work nor the
dark-mode work touched any file under `src/vendor/`**, so the per-file table
above (and therefore the output of `tools/fetch_upstream.ps1`) is unaffected.

| Addition | Where | Notes |
|---|---|---|
| Interface string table and language resolution | `src/L10n.{h,cpp}` | Upstream Light Switch has **no** localisation mechanism at all — its UI strings are hard-coded in the WinUI settings page. There is nothing to sync. |
| Runtime dialog text, font and layout | `src/SettingsDialog.cpp` (`ApplyLocalizedText`, `FitLabels`, `LayoutButtonRow`) | The upstream settings page is XAML, replaced here by a native dialog; translation-driven measurement is a consequence of that. |
| Dark-mode system layer and interface layer | `src/dark/darkmode.{h,cpp}`, `src/dark/darkui.{h,cpp}` | **Not PowerToys code.** See the next section. |
| Dark-mode integration and self-drawn controls | `src/dark/DarkApp.{h,cpp}`, `src/dark/DarkControls.{h,cpp}` | Project-side; written for this component. |
| `--lang CODE`, `--dump-strings FILE` | `src/main.cpp` | Upstream has no command line of its own; the module is loaded by the Runner. |
| `"language"` key in `settings.json` | `src/Config.{h,cpp}` | An additive extension. Upstream never reads it, and the app only drops *unknown* keys it does not model — `language` is modelled, so it survives a round trip. |
| `"startOnLogin"` key in `settings.json` | `src/Config.{h,cpp}` | Same story; see the "Intentionally different" table in `README.md`. |
| `"uiTheme"` key in `settings.json` | `src/Config.{h,cpp}` | Additive, same as the two above. Note the distinction from the scheduler: `uiTheme` only decides how *this application's* windows are drawn. Nothing in `src/vendor/` reads or writes it. |

If upstream ever grows a localisation mechanism of its own, translate the
concept rather than the identifier names: the on-disk contract (`settings.json`
mode strings, registry value names) must stay as upstream has it, and only the
user-visible layer moves into `src/L10n.cpp`.

### `src/dark/darkmode.*` and `src/dark/darkui.*` are NOT upstream PowerToys code

These four files come from a different source entirely: the author's own
`win32-dark-mode` skill — a reusable, hardware-verified Win32 dark-mode module
with no relationship to Microsoft or to PowerToys. They are **not** derived from
`src/modules/LightSwitch/`, they were not vendored from `microsoft/PowerToys`,
and nothing in this repository has an obligation to keep them in sync with
upstream.

They are also **not modified here**. They are copied byte for byte, which is the
whole point: it lets a hash prove that the code running in this binary is exactly
the code that was tested in the skill, so a dark-mode bug can be reported against
the skill rather than against a local fork. Verify with:

```sh
sha256sum src/dark/darkmode.h src/dark/darkmode.cpp src/dark/darkui.h src/dark/darkui.cpp
```

Those hashes only mean something if the files arrive byte for byte, so
`.gitattributes` pins these four paths to `eol=lf`. Left to `* text=auto` and
`core.autocrlf=true` — the Windows default — a clone would check them out with
CRLF endings and every published hash would fail, even though the repository copy
was correct.

| File | Lines | Role |
|---|---|---|
| `src/dark/darkmode.h` / `.cpp` | ~105 / ~350 | System layer: DWM chrome, the undocumented `uxtheme` ordinals, system-theme detection, palette, preference persistence. No control code. |
| `src/dark/darkui.h` / `.cpp` | ~77 / ~860 | Interface layer: turns an existing control tree dark by class name. Creates nothing, lays out nothing, changes no ids. |

Two consequences are visible in the project files and are deliberate:

* Both `.cpp` files are compiled at **warning level 3**, not the project's level
  4. That is the level they were written and verified against. Raising it would
   mean editing them, which would destroy the hash.
* `src/vendor/` is untouched by any of this. If you are auditing the vendored
  upstream diff, dark mode is not part of the answer.

## Keeping in sync

Run `tools/fetch_upstream.ps1` (PowerShell 5.1 is enough). It downloads the
upstream copies into `tools/upstream-cache/` and diffs them against
`src/vendor/`, printing a per-file verdict.

Recommended cadence and rules:

* **Follow closely** (low churn, high behavioural value):
  * `ThemeHelper.cpp` — the registry + broadcast sequence is the heart of the
    component. If Microsoft changes `ColorPrevalence` handling or the message
    list, this must be picked up.
  * `ThemeScheduler.cpp` — sunrise/sunset maths. Edge cases (polar day/night,
    DST) are the ones users notice.
  * `LightSwitchUtils.h` — `ShouldBeLight` cross-midnight logic.
* **Follow with care**:
  * `NightLightRegistryObserver.h` — if the CloudStore blob layout stabilises,
    upstream may switch to a documented API; adopt that.
* **Treat as frozen reference only**:
  * `LightSwitchStateManager.cpp` — we intentionally diverged (no Power Display,
    no settings write-back). Use it to verify decision order, not to copy.
* **Ignore**: everything under `src/settings-ui/` and `src/runner/`.

When you do take an upstream change:

1. Apply it to `src/vendor/...`, re-adding the three `ThemeHelper.cpp` edits.
2. Update the commit hash and date table at the top of this file.
3. Add a line to the "Intentionally different" table in `README.md` if the change
   alters observable behaviour, and to `CHANGELOG.md`.

## Licensing / trademark

* PowerToys is MIT: `Copyright (c) Microsoft Corporation. All rights reserved.`
  The license requires the copyright notice and the permission notice to be
  included in all copies or substantial portions. `LICENSE` at the repository
  root carries both notices, and every vendored file keeps a header pointing at
  it.
* MIT grants **no trademark rights**. "PowerToys" is a Microsoft trademark, so
  this component is *not* called "PowerToys Light Switch" and does not reuse
  Microsoft's artwork; its tray icon is drawn at runtime by
  `TrayIcon::CreateIcon()`.
