# Light Switch Standalone

A single-process Windows tray application that switches the Windows light/dark
theme on a schedule or by sunrise/sunset, extracted from the PowerToys
**Light Switch** module (`src/modules/LightSwitch/`) and rebuilt so that it does
not need PowerToys, the PowerToys Runner, the PowerToys Settings UI, vcpkg,
NuGet, or the Visual C++ redistributable.

Deliverable: one executable. Copy it anywhere, run it, and delete the folder to
uninstall.

```
bin/Release/LightSwitchStandalone.exe   ~520 KB, static CRT
```

---

## What it does

* Switches `HKCU\...\Themes\Personalize` values `SystemUsesLightTheme` and
  `AppsUseLightTheme`, then broadcasts `WM_SETTINGCHANGE` with
  `lParam = L"ImmersiveColorSet"` plus `WM_THEMECHANGED` so the shell picks the
  change up. Switching to light also resets `ColorPrevalence` to 0 — same as
  upstream.
* Four schedule modes:
  * **Off** — does nothing automatically.
  * **Fixed hours** — light between two wall-clock times.
  * **Sunset to sunrise** — NOAA sunrise/sunset maths from a latitude/longitude
    pair, with independent offsets in minutes.
  * **Follow Night Light** — mirrors the Windows Night Light toggle, detected by
    watching the CloudStore registry key.
* Global toggle shortcut (default `Ctrl+Win+Shift+D`) that flips the theme by
  hand and suspends automatic switching until the next schedule boundary.
* Tray icon with a context menu; native Win32 settings dialog; optional
  "start when I sign in"; single-instance enforcement.
* Localised interface — **English** and **简体中文** — that follows the Windows
  display language by default and can be overridden in the settings dialog.
  Switching takes effect immediately, without a restart.
* Live reload: editing `settings.json` in a text editor takes effect within a
  second, no restart needed.

---

## Building

Requirements:

* Visual Studio 2022 (or the Build Tools) with the **Desktop development with
  C++** workload — MSVC v143 and a Windows 10/11 SDK.
* Nothing else. No vcpkg, no NuGet, no Windows App SDK, no .NET.

```bat
build.bat            :: Release x64  ->  bin\Release\LightSwitchStandalone.exe
build.bat Debug
```

`build.bat` locates MSBuild through `vswhere`, so it works from a plain command
prompt. Building from the IDE works too — open `LightSwitchStandalone.vcxproj`.

The project is a single `.vcxproj` with two configurations, both **x64 only**
(upstream's service project is x64-only as well). Key properties:

| Setting | Value | Why |
|---|---|---|
| `PlatformToolset` | `v143` | |
| `WindowsTargetPlatformVersion` | `10.0.22621.0` | |
| `LanguageStandard` | `stdcpp20` | Structured bindings in `ThemeScheduler.cpp`, `contains()` elsewhere. |
| `RuntimeLibrary` | `MultiThreaded` / `MultiThreadedDebug` | **Static CRT**, so no VC++ redistributable and a truly portable folder. |
| `GenerateManifest` | `false` | The manifest is embedded by `src/LightSwitch.rc` as `RT_MANIFEST`; that avoids `mt.exe` having to merge two manifests that both declare an `assemblyIdentity`. |
| Link libraries | `Advapi32 User32 Gdi32 Shell32 Shlwapi Ole32 Comctl32 Kernel32` | Everything else is static CRT + STL. |

Resulting import table, for reference:

```
ADVAPI32.dll  USER32.dll  GDI32.dll  SHELL32.dll  ole32.dll  COMCTL32.dll  KERNEL32.dll
```

### Building on a machine where MSBuild is blocked

Some locked-down environments refuse to launch `MSBuild.exe`. The same
configuration can be driven directly through the toolchain — set `INCLUDE`,
`LIB` and `PATH` from the VS installation, then:

```sh
cl /nologo /c /MP /EHsc /std:c++20 /utf-8 /W4 /permissive- /GS /O2 /MT /DNDEBUG \
   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE /I src /Fo obj/ src/*.cpp src/vendor/*.cpp
rc /nologo /I src /fo obj/LightSwitch.res src/LightSwitch.rc
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:bin/Release/LightSwitchStandalone.exe \
   obj/*.obj obj/LightSwitch.res Advapi32.lib User32.lib Gdi32.lib Shell32.lib Shlwapi.lib Ole32.lib Comctl32.lib Kernel32.lib
```

---

## Running

```
LightSwitchStandalone.exe              run in the notification area
LightSwitchStandalone.exe --settings   run and open the settings dialog
LightSwitchStandalone.exe --startup    same as no arguments (this is what the Run key uses)
LightSwitchStandalone.exe --toggle     flip the theme once, then exit
LightSwitchStandalone.exe --light      force the light theme once, then exit
LightSwitchStandalone.exe --dark       force the dark theme once, then exit
LightSwitchStandalone.exe --lang CODE  force the interface language for this run only
                                       (auto | en-US | zh-CN)
LightSwitchStandalone.exe --dump-strings FILE
                                       write every interface string to FILE, then exit
LightSwitchStandalone.exe --help       show the above
```

`--lang` overrides `settings.json` for one process and never writes the file —
handy for taking screenshots in both languages, or for typing a translated
dialog layout. `--dump-strings` writes UTF-8 with a BOM and exits before the
single-instance check, so it works while the app is already running; it is meant
for diffing a translation and for machine-checking that both languages carry the
same ids in the same order (see *Localisation* below).

Tray menu: *Toggle theme now*, *Settings…*, *Reload settings from disk*, *Open
settings folder*, *Restart Explorer (refresh shell)*, *View log*, *Exit*. The
menu is rebuilt on every right-click, so it always shows the active language.
Double-clicking the tray icon opens the settings dialog.

### Files it creates

| File | Location | Notes |
|---|---|---|
| `settings.json` | next to the executable | Falls back to `%LOCALAPPDATA%\LightSwitchStandalone\` if the executable's folder is read-only. |
| `lightswitch.log` | next to the executable | UTF-16LE, truncated at 4 MB. |

### `settings.json`

The schema is the PowerToys one, including the per-property `{"value": ...}`
wrapper, so a PowerToys `%LOCALAPPDATA%\Microsoft\PowerToys\LightSwitch\settings.json`
can be copied over verbatim for comparison.

```json
{
  "changeSystem":     { "value": true },
  "changeApps":       { "value": true },
  "scheduleMode":     { "value": "Off" },
  "lightTime":        { "value": 480 },
  "darkTime":         { "value": 1200 },
  "sunrise_offset":   { "value": 0 },
  "sunset_offset":    { "value": 0 },
  "latitude":         { "value": "0.0" },
  "longitude":        { "value": "0.0" },
  "toggle-theme-hotkey": { "value": { "win": true, "ctrl": true, "alt": false, "shift": true, "code": 68 } },
  "startOnLogin":     { "value": false },
  "language":         { "value": "auto" }
}
```

`scheduleMode` accepts `"Off"`, `"FixedHours"`, `"SunsetToSunrise"`,
`"FollowNightLight"`. `lightTime` / `darkTime` are minutes since midnight.
`code` is a virtual-key code (`68` = `D`). `startOnLogin` and `language` are the
only two standalone-specific extensions.

`language` accepts `"auto"` (follow the Windows display language), `"en-US"` /
`"en"`, or `"zh-CN"` / `"zh"` / `"zh-Hans"`, case-insensitively. The value is
matched on the primary subtag, so `en-GB` and `zh-TW` land somewhere sensible
rather than falling back to English. Anything unrecognised — including a typo —
degrades to `"auto"` instead of pinning a language, and the app rewrites the
canonical form on the next save. An explicit code pins the interface even if
Windows is running in another language.

The file watcher compares content, so a write from the app itself never causes a
reload loop. A malformed file is reported in the log and ignored — the previous
good settings stay in effect and the file is not overwritten, so you can fix it
by hand.

---

## Architecture

One process, several threads — deliberately *not* the upstream
"interface DLL + service EXE + Runner keyboard hook" trio.

| Thread | File | Responsibility |
|---|---|---|
| UI / message loop | `App.cpp`, `TrayIcon.cpp`, `HotkeyManager.cpp`, `SettingsDialog.cpp` | Tray icon, `WM_HOTKEY`, `WM_POWERBROADCAST`, the modal settings dialog. Never touches the registry. |
| Scheduler worker | `Scheduler.cpp`, `StateManager.cpp`, `ThemeEngine.cpp` | Minute ticks, state machine, all registry writes and broadcasts, night-light watcher lifecycle. |
| Night-light observer | `vendor/NightLightRegistryObserver.h` | `RegNotifyChangeKeyValue` loop; signals the worker. |
| Settings watcher | `Config.cpp` | `ReadDirectoryChangesW` + debounce + content comparison; signals the worker. |

**The single most important rule:** `SetSystemTheme` / `SetAppsTheme` end with a
`SendMessageTimeout(HWND_BROADCAST, ..., SMTO_ABORTIFHUNG, 5000)` broadcast that
can block for seconds. It is therefore called *only* from the scheduler worker.
The hotkey and the tray command post an event, they never apply a theme inline.

```
main.cpp            single instance, command line, message loop bootstrap
App.{h,cpp}         tray, hotkey, dialogs, lifecycle, Windows integration
Scheduler.{h,cpp}   worker thread + wait set + external-change detection
StateManager.{h,cpp} decision logic (port of upstream LightSwitchStateManager)
ThemeEngine.{h,cpp} ApplyTheme() wrapper honouring changeSystem / changeApps
Config.{h,cpp}      settings.json model, atomic write, content-compared watcher
Json.{h,cpp}        minimal JSON reader/writer (object/array/string/number/bool)
Logging.{h,cpp}     OutputDebugString + UTF-16 log file, thread safe
Text.{h,cpp}        UTF-8 <-> UTF-16 helpers
L10n.{h,cpp}        the whole string table, language resolution, UI font choice
HotkeyManager.*     RegisterHotKey, low-level keyboard hook fallback
TrayIcon.*          Shell_NotifyIcon, GDI-drawn icon, popup menu
SettingsDialog.*    Win32 dialog (no WPF/WinUI)
AutoStart.*         HKCU\...\Run management
vendor/             unmodified upstream logic (see UPSTREAM.md)
```

---

## Localisation

Every user-visible string lives in **one table** in `src/L10n.cpp`, keyed by the
`Str` enumeration and carrying a symbolic name plus the English and Simplified
Chinese text. A `static_assert` keeps the enumeration, the table, and its order
in lockstep, so a forgotten entry fails the build instead of falling back
silently.

Two design decisions are worth knowing before you touch it:

* **The table is compiled into the binary, not stored as Win32 resources.** One
  source of truth per string instead of one per language per resource, and no
  `.rc` code page to get wrong — every translation unit is compiled with
  `/utf-8`, so wide literals in a `.cpp` are unambiguous. English is the source
  language *and* the hard fallback for every id.
* **The dialog template in `LightSwitch.rc` carries geometry and control ids
  only.** All text is filled in at `WM_INITDIALOG`, and the `.rc` keeps the
  English wording as a trailing comment so the layout stays readable.

That second point is why each translated label needs a real control id rather
than `-1`, and why three functions in `SettingsDialog.cpp` exist:

| Function | Job |
|---|---|
| `ApplyLocalizedText()` | `SetWindowTextW` for every control, plus the dialog caption. |
| `FitLabels()` | Measures each single-line static and widens it if the translation overflows the template box. A full-width CJK glyph is ≈1 em, so `浅色主题开始时间：` (9 glyphs) needs more room than `Light theme starts at:`. Only controls that actually share the row are treated as boundaries — without that vertical test the button row, which sits far below but further right, clamped every label to a few pixels. When it widens something it logs `Widened a label from N to M px` at DEBUG level. |
| `LayoutButtonRow()` | Right-packs the button row to measured text widths. This also fixed a pre-existing defect: the hand-placed row had `Restore defaults` overlapping `Save`. |

Runtime font selection matters too. **Segoe UI has no CJK glyphs**, and relying
on Windows font linking makes the result depend on which fonts are installed, so
`L10n::UiFontFace()` returns `Microsoft YaHei UI` for Chinese and `Segoe UI`
otherwise. The face is applied with an explicit child `WM_SETFONT` rather than
`WM_SETFONT` on the dialog alone, so every control picks it up.

### Switching language

* **Default is `auto`** — `LanguageFromSystem()` in `src/L10n.cpp` reads
  `GetUserDefaultUILanguage()`, which is what the user picked under *Settings >
  Time & language > Language* (not the locale or the system default).
* **Manual override** lives in the settings dialog's *Interface language*
  dropdown: *Follow Windows*, *English*, *简体中文*.
* **Switching is immediate.** Saving a different language makes `DialogProc`
  return the sentinel `kReloadDialog` (`0x4C53`) instead of `IDOK`, and
  `ShowSettingsDialog()` loops, recreating the dialog in the new language before
  it returns — so you see the change without restarting the app. The dropdown
  itself re-renders in the newly selected language, which is a quick visual
  confirmation.
* **Windows language changes are followed live.** `App` handles
  `WM_SETTINGCHANGE` with `lParam == "intl"` and calls `L10n::RefreshFromSystem()`,
  which re-resolves `auto` and refreshes the tray tooltip. An explicitly pinned
  language is left alone.

### Adding a language

Four mechanical steps, no other file needs to change:

1. add the value to `enum class Lang` in `src/L10n.h`;
2. add a column to `struct Entry` in `src/L10n.cpp` and fill it in for all rows;
3. extend the `switch` in `TextOf()`;
4. append it to `kLanguages` (`L10n::All()` drives the dropdown order).

Then re-run `--dump-strings` and diff — the ids are stable, so a translation can
be delivered as that file and checked mechanically.

### Not translated on purpose

| Thing | Why |
|---|---|
| `lightswitch.log` | A developer artefact. One language keeps two bug reports comparable. |
| The registry value names, `settings.json` keys and mode strings (`"FixedHours"`, …) | They are the on-disk contract, shared with upstream PowerToys. |

> **Traditional Chinese readers get the Simplified translation, not English.**
> This build ships one Chinese translation and `PRIMARYLANGID == LANG_CHINESE`
> matches both, so `zh-TW` resolves to `zh-CN`. That is deliberate — a reader of
> either script is better served by the other Chinese than by English. If you
> want a separate `zh-Hant` column, follow *Adding a language* above and make
> `TryMatchCode()` test the script subtag before the primary one.

---

## Behaviour fidelity vs. upstream

### Kept identical

| Behaviour | Upstream | Verified by |
|---|---|---|
| Registry targets | `SystemUsesLightTheme`, `AppsUseLightTheme` under `HKCU\...\Themes\Personalize` | `vendor/ThemeHelper.cpp` is verbatim |
| Broadcast sequence | `WM_SETTINGCHANGE`+`ImmersiveColorSet`, then `WM_THEMECHANGED`, `SMTO_ABORTIFHUNG`/5000 ms | ditto |
| `ColorPrevalence` reset | Only when switching **to light** | ditto |
| Sunrise/sunset | NOAA algorithm, zenith 90.833, local-time conversion via `GetTimeZoneInformation` | `vendor/ThemeScheduler.cpp` is verbatim |
| Cross-midnight schedules | `ShouldBeLight` normalises into `[0,1439]` and handles wrap-around | `vendor/LightSwitchUtils.h` is verbatim |
| Decision order | `OnSettingsChanged` / `OnTick` / `OnManualOverride` / `OnNightLightChange` all funnel into the same `EvaluateAndApplyIfNeeded` | ported 1:1 |
| Manual override | Hotkey flips the theme and toggles the override flag; automatic switching resumes when a schedule boundary is crossed | ported 1:1 |
| Off mode | Early-out, does not touch the cached boundaries | ported 1:1 |
| Tick alignment | Wakes at the top of each wall-clock minute | ported 1:1 |
| External change detection | If the registry no longer matches the schedule, enter manual override instead of fighting the user | ported 1:1 |
| Sleep/wake catch-up | Cached theme state is compared to the real one, so a missed transition is applied on the next tick | ported 1:1 |
| Default shortcut | `Ctrl+Win+Shift+D` | `Config.h` / `Config::Serialize` |

### Intentionally different

| Change | Reason | Impact |
|---|---|---|
| No `NotifyPowerDisplayThemeChanged()` | That signalled another PowerToys module (Power Display) over a named event. The module does not exist here. | None, unless you also want Power Display integration — then re-add a `CreateEventW`/`SetEvent` on the same event names. |
| Sunset/sunrise results are **not** written back to `settings.json` | Upstream persists the computed times so its UI can show them. Combined with a file watcher that creates a feedback loop where every recomputation re-triggers a settings reload. | The dialog shows the computed times live instead. The stored `lightTime`/`darkTime` keep the user's own values rather than being overwritten daily. |
| `SettingsObserver` replaced by a Win32 event | Upstream's observer set is tied to `LightSwitchSettings::instance()`. | None. |
| Global hotkey is owned by the app | Upstream modules declare hotkeys and the Runner's centralised low-level hook captures them. | The shortcut works without PowerToys. |
| Native Win32 dialog instead of a WinUI 3 page | The project's "Windows SDK only" constraint. | No WinUI/WinAppSDK dependency; appearance is a classic property sheet. |
| Win key is a separate checkbox in the dialog | `msctls_hotkey32` cannot represent `MOD_WIN` at all. | You set "Include Win key" plus the rest of the combination. |
| `settings.json` lives next to the exe, not under `%LOCALAPPDATA%` | Portable/green distribution. | Delete the folder = uninstall. |
| Autostart uses `HKCU\...\Run`, not a scheduled task | PowerToys uses a scheduled task because it can start elevated; a portable app cannot create one without elevation, and the path would break on relocation. | Visible in Task Manager's Startup tab. |
| Toggle shortcut only flips *enabled* targets | Upstream's module interface did the same, but silently. | If neither "Windows system" nor "Applications" is enabled, the toggle logs a warning and does nothing; the dialog warns before saving that configuration. |
| Everything that touches the theme runs off the UI thread | PowerToys' hotkey handler ran on the hook thread; here the UI thread must stay responsive. | Rapid toggling coalesces into one pending toggle. |
| Interface strings live in a compiled-in table, not in Win32 string resources | One source of truth per string, and no `.rc` code page to get wrong for non-ASCII. Upstream has no localisation at all. | Translating means editing `src/L10n.cpp` and rebuilding; there is no satellite DLL to drop in. See *Localisation*. |
| The dialog template carries no text | The same single-source-of-truth reason. Each label needs a real control id so it can be set at `WM_INITDIALOG`. | `LightSwitch.rc` is geometry + ids; the English wording survives only as comments. |
| The button row and over-long labels are laid out at runtime | Translated labels have different widths, and CJK labels are wider than they look. | No more `Restore defaults` overlapping `Save`, and a translated label is widened rather than clipped. |

---

## Risks and mitigations

| Risk | Mitigation in this code |
|---|---|
| **Night Light blob parsing is undocumented.** `IsNightLightEnabled()` reads a `REG_BINARY` CloudStore value and requires `data[23] == 0x10 && data[24] == 0x00`. The offset can change with a Windows update. | Failure to open the key, a short value, or a read error all return `false` (treated as "Night Light off"), never an error. Only affects `FollowNightLight` mode. If it ever misbehaves, use Fixed hours or Sunset to sunrise. |
| **`MOD_WIN` hotkeys are reserved by the OS.** MS Learn states that shortcuts involving the Windows key are reserved; `RegisterHotKey` can fail. | `HotkeyManager` tries `RegisterHotKey` first and falls back to a `WH_KEYBOARD_LL` hook that reproduces what the PowerToys Runner did. The hook callback only compares modifiers and `PostMessage`s — no blocking work. On the test machine `RegisterHotKey` with `MOD_WIN` actually succeeded, so the hook stays dormant. |
| **Shell surfaces may not repaint after the broadcast.** Taskbar/Start occasionally cache the theme. | The tray menu offers *Restart Explorer (refresh shell)*, which first sends `SHChangeNotify(SHCNE_ASSOCCHANGED)` (cheap, non-destructive) and only restarts Explorer after an explicit confirmation dialog. Explorer is never killed silently. |
| **Weak rules against low-level hooks.** Some anti-cheat/security products block `SetWindowsHookEx`. | If both mechanisms fail the app still runs; the log records that the shortcut is unavailable. Everything else keeps working. |
| **Corrupt `settings.json`.** | Parse failures are logged and ignored; the last good settings stay active and the file is left alone so it can be repaired. Times are clamped to `[0,1439]` on load. |
| **Polar day/night.** `CalculateSunriseSunset` returns -1 for those. | The state manager falls back to the stored fixed times rather than wedging. |
| **Two instances.** | A `Local\` named mutex; the second instance finds the first window and asks it to open the settings dialog, then exits. |
| **Vendor drift.** The upstream module is still evolving. | `UPSTREAM.md` pins the commit and `tools/fetch_upstream.ps1` diffs the local copies against upstream on demand. |
| **Upstream writes a settings file with more keys** (Power Display profile fields). | Unknown keys are preserved in the file? **No** — the app rewrites the file from its own model, so Power Display keys are dropped if you point it at a real PowerToys file. Back up first, or use a separate folder. |
| **A translation is wider than the English it replaces, and a label gets clipped.** Chinese is usually *narrower*, but a full-width glyph is ≈1 em and the template boxes were sized for English. | `FitLabels()` measures every static and widens it at runtime, logging each change at DEBUG level. The fallback only ever grows a box and never pushes it past its nearest same-row neighbour, so it cannot make things worse. **Adding a language or re-wording a string still deserves a visual check** — the automatic fitter does not reflow the layout, it only widens. If you see a clipped label, that is a bug: attach `lightswitch.log` (the `Widened a label…` lines show what the fitter did). |
| **A translation forgets a `%1` placeholder.** | `--dump-strings` makes this checkable: diff the two language sections and confirm every id carries the same placeholder set. `Format()` leaves an unmatched placeholder verbatim rather than eating it, so the bug is visible in the UI instead of silent. |

---

## Licensing

* Vendored code is MIT, `Copyright (c) Microsoft Corporation`. `LICENSE`
  carries Microsoft's notice alongside the project's own, and each vendored file
  keeps a header pointing at it, as MIT requires.
* MIT grants no trademark rights. **"PowerToys" is a Microsoft trademark**, so
  this component is not named after it and does not reuse Microsoft's icon: the
  tray icon is drawn at runtime by `TrayIcon::CreateIcon()`. Describing it as
  "compatible with Microsoft PowerToys" in prose is fine.
* The interface strings in `src/L10n.cpp` and the Simplified Chinese translation
  are original to this component, not derived from upstream. They add no new
  licensing obligation and change the status of no MIT file.

---

## 中文说明（Chinese notes）

> 这一节只是给中文读者的快速上手说明；权威内容以本文英文正文和各源文件注释为准。

### 这是什么

一个独立运行的 Windows 托盘小工具，按时间计划或日出日落自动切换系统的浅色/深色主题。
它从 Microsoft PowerToys 的 **Light Switch** 模块中抽取而来，但**不依赖 PowerToys**：
不需要 PowerToys 本体、Runner、设置界面，也不需要 vcpkg / NuGet / VC++ 运行库。

交付物就是一个 exe。**拷到任何地方直接运行，删掉目录就是卸载。**

### 基本用法

双击 `LightSwitchStandalone.exe` 即常驻通知区域（托盘）。右键托盘图标是菜单，
双击托盘图标打开设置对话框。

命令行：

```
LightSwitchStandalone.exe               常驻通知区域运行
LightSwitchStandalone.exe --settings    运行并立即打开设置对话框
LightSwitchStandalone.exe --toggle      按当前设置切换一次主题后退出
LightSwitchStandalone.exe --light       强制切到浅色主题后退出
LightSwitchStandalone.exe --dark        强制切到深色主题后退出
LightSwitchStandalone.exe --lang 代码   仅在本次运行强制使用指定界面语言
                                        （auto | en-US | zh-CN）
LightSwitchStandalone.exe --dump-strings 文件
                                        把所有界面字符串导出到该文件后退出
LightSwitchStandalone.exe --help        显示以上说明
```

### 界面语言怎么设

1. **默认跟随 Windows。** 程序读的是 Windows 显示语言（就是「设置 > 时间和语言 >
   语言」里选的那个），不是系统区域格式。
2. **想手动指定**，打开设置对话框，最后一个分组「界面语言」，下拉选
   `跟随 Windows` / `English` / `简体中文`，然后点「保存」。
3. **切换立即生效，不用重启**：保存后对话框会当场用新语言重建，下拉框本身也会变成
   新语言，一眼就能看出成功。
4. 设置存在 `settings.json` 的 `language` 键里：

   ```json
   { "language": { "value": "auto" } }
   ```

   `"auto"` 表示跟随系统；填 `"en-US"` 或 `"zh-CN"` 就是**锁定**该语言，
   之后系统语言再怎么变都不动。填了无法识别的值（比如打错字）会当作 `"auto"` 处理，
   不会把界面锁死。
5. Windows 显示语言**在运行期间改变**也会自动跟上（只要你还处在 `auto`）。

### 文件放在哪

| 文件 | 位置 |
|---|---|
| `settings.json` | exe 同目录；若该目录只读，自动回落到 `%LOCALAPPDATA%\LightSwitchStandalone\` |
| `lightswitch.log` | exe 同目录，UTF-16LE，超过 4 MB 截断 |

在文本编辑器里改 `settings.json` 会在一秒内自动生效，不用重启。
写坏了不会崩：日志里记一条，继续沿用上一次的好配置，也不会覆盖你正在改的文件，
方便你手工修回来。

### 常见问题

* **日志能出中文吗？** 不能，而且是有意的。日志是排障用的开发产物，统一用英文，
  两个人的问题报告才好直接对照。
* **中文标签显示不全（被裁断）？** 属于 bug。程序会在运行时自动量宽度并加宽标签，
  正常情况下不该出现；遇到请把 `lightswitch.log` 一起发出来，里面
  `Widened a label from N to M px` 这类记录能说明当时量到了多少。
* **繁体中文用户看到的是简体译文**，不是英文。这是刻意的：本构建只带一套中文译文，
  `zh-TW` 会落到简体。对繁体读者来说，另一套中文也比英文更好用。
* **托盘图标没反应 / 快捷键无效？** 先看 `lightswitch.log`。涉及 Win 键的组合被系统
  保留，程序会退回键盘钩子；若连钩子也被安全软件拦了，程序照常运行，只是快捷键不可用。
* **任务栏颜色没变？** 部分外壳界面会缓存主题。用托盘菜单里的
  「重启资源管理器（刷新外壳）」——它会先发一个无破坏性的刷新通知，只有你确认后才真的
  重启 Explorer，绝不会静默杀掉它。
