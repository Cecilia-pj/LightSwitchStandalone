# Light Switch Standalone

A single-process Windows tray application that switches between two **Windows
themes** (`.theme` packs) on a schedule or by sunrise/sunset, extracted from the
PowerToys **Light Switch** module (`src/modules/LightSwitch/`) and rebuilt so
that it does not need PowerToys, the PowerToys Runner, the PowerToys Settings
UI, vcpkg, NuGet, or the Visual C++ redistributable.

Deliverable: one executable. Copy it anywhere, run it, and delete the folder to
uninstall.

```
bin/Release/LightSwitchStandalone.exe   ~620 KB, static CRT
```

Until 1.3.x this switched the light/dark **colour mode** (two registry DWORDs).
Since 1.4.0 it switches whole theme packs, which is a superset: a pack carries
wallpaper, colours, cursor, sounds and accent colour, and the light/dark mode
travels inside it as `[VisualStyles] SystemMode` / `AppMode`. See *Themes*.

---

## What it does

* Applies a `.theme` file through `IThemeManager2` (`themeui.dll`), which is what
  Windows Settings itself uses. The whole pack changes: wallpaper, window
  colours, mouse cursor, sound scheme, accent colour, and the light/dark mode
  for both the shell and applications. A successful apply is confirmed by
  re-reading `HKCU\...\CurrentVersion\Themes\CurrentTheme`, not by trusting the
  call to have worked.
* Two packs are configured — one for each side of the schedule — and you can
  pick either of them by name from the tray menu at any time.
* Four schedule modes:
  * **Off** — does nothing automatically.
  * **Fixed hours** — light between two wall-clock times.
  * **Sunset to sunrise** — NOAA sunrise/sunset maths from a latitude/longitude
    pair, with independent offsets in minutes.
  * **Follow Night Light** — mirrors the Windows Night Light toggle, detected by
    watching the CloudStore registry key.
* Global toggle shortcut (default `Ctrl+Win+Shift+D`) that swaps the theme by
  hand and suspends automatic switching until the next schedule boundary.
* Tray icon with a context menu; native Win32 settings dialog; optional
  "start when I sign in"; single-instance enforcement. A single left click on the
  icon does **not** toggle the theme unless you turn that on (see *Trigger
  conditions*), so a touch screen cannot flip the theme by accident. The icon
  registers itself again when Windows Explorer restarts (see *Explorer
  restarts*), including through this program's own *Restart Explorer*.
* Localised interface — **English** and **简体中文** — that follows the Windows
  display language by default and can be overridden in the settings dialog.
  Switching takes effect immediately, without a restart.
* Dark-mode interface. The settings dialog and the tray menu follow the Windows
  app theme by default, and can be pinned to light or dark. Switching is live,
  and the dialog is fully dark — no white ComboBox drop-down, no light date
  picker panel, no system-coloured group boxes.
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
| Link libraries | `Advapi32 User32 Gdi32 Shell32 Shlwapi Ole32 Comctl32 Comdlg32 Kernel32 Dwmapi Uxtheme` | Everything else is static CRT + STL. `Dwmapi` is for the immersive dark title bar, `Uxtheme` for the undocumented `SetPreferredAppMode` / `AllowDarkModeForWindow` ordinals — see *Dark mode*. `Comdlg32` is for the *Browse…* file picker. `IThemeManager2` needs no import library: it is created with `CoCreateInstance` from `Ole32`. |
| Warning level | `Level4` | `src/dark/darkmode.cpp` and `src/dark/darkui.cpp` are pinned to `Level3`; see *Dark mode > Provenance*. |

Resulting import table, for reference:

```
ADVAPI32.dll  USER32.dll  GDI32.dll  SHELL32.dll  ole32.dll  COMCTL32.dll  COMDLG32.dll
KERNEL32.dll  dwmapi.dll  UxTheme.dll
```

### Building on a machine where MSBuild is blocked

Some locked-down environments refuse to launch `MSBuild.exe`. The same
configuration can be driven directly through the toolchain — set `INCLUDE`,
`LIB` and `PATH` from the VS installation, then:

```sh
cl /nologo /c /MP /EHsc /std:c++20 /utf-8 /W4 /permissive- /GS /O2 /MT /DNDEBUG \
   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE /I src /Fo obj/ \
   src/*.cpp src/dark/DarkApp.cpp src/dark/DarkControls.cpp src/vendor/*.cpp
# src/dark/darkmode.cpp and darkui.cpp are compiled separately, at /W3
rc /nologo /I src /fo obj/LightSwitch.res src/LightSwitch.rc
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:bin/Release/LightSwitchStandalone.exe \
   obj/*.obj obj/LightSwitch.res \
   Advapi32.lib User32.lib Gdi32.lib Shell32.lib Shlwapi.lib Ole32.lib Comctl32.lib Comdlg32.lib \
   Kernel32.lib Dwmapi.lib Uxtheme.lib
```

---

## Running

```
LightSwitchStandalone.exe              run in the notification area
LightSwitchStandalone.exe --settings   run and open the settings dialog
LightSwitchStandalone.exe --startup    same as no arguments (this is what the Run key uses)
LightSwitchStandalone.exe --toggle     swap to the other pack once, then exit
LightSwitchStandalone.exe --light      apply the light-side pack once, then exit
                                       (--theme-a is a synonym)
LightSwitchStandalone.exe --dark       apply the dark-side pack once, then exit
                                       (--theme-b is a synonym)
LightSwitchStandalone.exe --theme FILE apply one arbitrary .theme file once, then exit
LightSwitchStandalone.exe --lang CODE  force the interface language for this run only
                                       (auto | en-US | zh-CN)
LightSwitchStandalone.exe --dump-strings FILE
                                       write every interface string to FILE, then exit
LightSwitchStandalone.exe --dump-themes FILE
                                       write every theme the catalogue can see, then exit
LightSwitchStandalone.exe --theme-check FILE
                                       probe IThemeManager2 and write the verdict, then exit
LightSwitchStandalone.exe --help       show the above
```

`--lang` overrides `settings.json` for one process and never writes the file —
handy for taking screenshots in both languages, or for typing a translated
dialog layout. `--dump-strings` writes UTF-8 with a BOM and exits before the
single-instance check, so it works while the app is already running; it is meant
for diffing a translation and for machine-checking that both languages carry the
same ids in the same order (see *Localisation* below). `--dump-themes` and
`--theme-check` are the two diagnostics described under *Themes*.

The one-shot commands (`--toggle`, `--light`, `--dark`, `--theme`) exit 0 when
the pack was applied **or was already the current one**, 2 when that side is not
configured, 3 when the file is missing and 4 when it could not be applied — so a
script can tell "nothing to do" from "this is broken".

Tray menu: *Toggle theme now*, *Light theme: …*, *Dark theme: …*, *Settings…*,
*Reload settings from disk*, *Open settings folder*, *Restart Explorer (refresh
shell)*, *View log*, *Exit*. The two theme rows carry a bullet on whichever pack
Windows is running and are greyed out when that side is not configured. The menu
is rebuilt on every right-click, so it always shows the active language and the
current pair.

### Trigger conditions

Every gesture the tray icon responds to, and what it does. The "shell sends"
column matters on a touch screen: Windows does not have separate tap messages,
so a tap is a mouse click as far as the application is concerned.

| Gesture | Shell sends | What happens | Toggles the theme? |
|---|---|---|---|
| Single left click — **a tap on a touch screen** | `WM_LBUTTONUP` | Nothing, unless `trayClickTogglesTheme` is `true`; either way the reason is written to the log | **Only if enabled** (off by default) |
| Double click | `WM_LBUTTONDBLCLK` plus two `WM_LBUTTONUP` | Opens the settings dialog. The two `WM_LBUTTONUP` messages cancel each other out, so this never flips the theme | No |
| Right click | `WM_RBUTTONUP` / `WM_CONTEXTMENU` | Opens the context menu, which has its own *Toggle theme now* entry | No |
| **Press and hold on a touch screen** | `WM_CONTEXTMENU` | Opens the context menu. This is why a long press has never toggled the theme | No |
| Global hotkey, `Ctrl+Win+Shift+D` by default | `WM_HOTKEY` | Swaps the two packs | Yes |
| *Toggle now* in the settings dialog | — | Swaps the two packs | Yes |
| *Light theme: …* / *Dark theme: …* in the tray menu | — | Applies that pack specifically | Yes, to that side |

So there is no press-duration logic anywhere in the program: a long press opens
the menu, and the gesture that used to flip the theme by accident was a plain
single click. `trayClickTogglesTheme` (default `false`) gates that one gesture
and nothing else. If you want the click back, set the checkbox in *Toggle
shortcuts* or edit `settings.json`; the hotkey and both *Toggle now* entries are
always available either way.

### Explorer restarts

The notification area is owned by `explorer.exe`, so replacing that process takes
every icon in it. Windows keeps no record of who registered what and never asks
anyone to register again: an application that does nothing loses its icon until
it is restarted itself. This one listens for the shell's `TaskbarCreated`
broadcast and registers the icon again, tooltip included.

That covers all three ways the shell gets replaced — *Restart Explorer* in the
tray menu, the button of the same name in the settings dialog, and an
`explorer.exe` that crashed or was killed from outside.

For its own restart the program does not depend on the broadcast alone. It kills
explorer, waits, then starts a new one, and the UI thread is parked for that
whole stretch; Winlogon's `AutoRestartShell` (on by default, under
`HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon`) can bring the shell
back during that wait, in which case the broadcast is sent to a thread that is
not pumping messages and is lost. A bounded retry timer covers that ordering. If
the icon still cannot be registered, the log says so at `ERROR` after ten
attempts rather than failing quietly.

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
  "language":         { "value": "auto" },
  "uiTheme":          { "value": "auto" },
  "themeA":           { "value": "" },
  "themeB":           { "value": "" },
  "trayClickTogglesTheme": { "value": false }
}
```

`scheduleMode` accepts `"Off"`, `"FixedHours"`, `"SunsetToSunrise"`,
`"FollowNightLight"`. `lightTime` / `darkTime` are minutes since midnight.
`code` is a virtual-key code (`68` = `D`). `startOnLogin`, `language`,
`uiTheme`, `themeA`, `themeB` and `trayClickTogglesTheme` are the only
standalone-specific extensions.

`themeA` / `themeB` are **absolute paths to `.theme` files** — A is applied
during the light side of the schedule, B during the dark side. An empty string
means "not chosen yet", and automatic switching then stays idle instead of
applying something the user did not pick. Paths are stored rather than
`ThemeId`s so the file stays legible and a theme can be moved and the entry
corrected by hand; the id is re-read from the file every time it is needed (see
*Themes*).

`changeSystem` / `changeApps` are **inert since 1.4.0.** They are the historic
PowerToys switches for "apply to the Windows chrome" and "apply to
applications"; a `.theme` pack carries both modes in its own `[VisualStyles]`
section, so there is nothing left for them to decide. They are still written
back untouched, because that is what lets a downgrade to 1.3.x behave the way
the file says, and the upgrade migration uses them to tell whether switching was
ever enabled.

`language` accepts `"auto"` (follow the Windows display language), `"en-US"` /
`"en"`, or `"zh-CN"` / `"zh"` / `"zh-Hans"`, case-insensitively. The value is
matched on the primary subtag, so `en-GB` and `zh-TW` land somewhere sensible
rather than falling back to English. Anything unrecognised — including a typo —
degrades to `"auto"` instead of pinning a language, and the app rewrites the
canonical form on the next save. An explicit code pins the interface even if
Windows is running in another language.

`uiTheme` accepts `"auto"` (follow the Windows **app** theme,
`AppsUseLightTheme`), `"light"` or `"dark"`, case-insensitively and ignoring
surrounding whitespace. Anything unrecognised — including a typo — becomes
`"auto"`, so a bad value degrades to following Windows rather than pinning the
interface to a theme nobody asked for.

> `uiTheme` is written by this component only. It is **not** written by the
> Light Switch scheduling code, even though both ultimately look at
> `AppsUseLightTheme`: the scheduler's job is to *set* that value from a
> schedule, while `uiTheme` only *reads* it to decide how to draw this
> application's own windows. Setting `uiTheme` never changes the Windows theme,
> and the scheduler never reads `uiTheme`.

`trayClickTogglesTheme` is a boolean and defaults to **`false`**. It controls one
gesture only: a single left click on the tray icon. It is off by default because
a tap on a touch screen arrives as the same message a mouse click does, so
without the guard a finger brushing past the icon flips the theme of the whole
machine instantly — no confirmation, no undo. A settings.json written by 1.2.0 or
earlier has no such key, and the missing key falls back to `false`, so upgrading
never silently re-enables the gesture.

The file watcher compares content, so a write from the app itself never causes a
reload loop. A malformed file is reported in the log and ignored — the previous
good settings stay in effect and the file is not overwritten, so you can fix it
by hand.

---

## Themes

A `.theme` file is an INI file describing a complete look: wallpaper, window
colours, mouse cursors, sound scheme, desktop icons, screensaver, and the
light/dark mode to use for the shell and for applications. On this machine they
are plain single-byte CP936 — **not** UTF-16 — so they are read with
`GetPrivateProfileStringW`, exactly the way `themeui.dll` reads them. A
`DisplayName` that starts with `@` is an indirect string
(`@%SystemRoot%\System32\themeui.dll,-2060`, which is what every inbox theme
uses) and is resolved with `SHLoadIndirectString`, so a localised Windows shows
localised theme names.

### Which files are offered

| Directory | Why |
|---|---|
| `%LOCALAPPDATA%\Microsoft\Windows\Themes` | Per-user themes. Scanned **one level deep**, because wallpaper packs (Spotlight's `Flower (d)`, for instance) keep their `.theme` inside a subfolder whose name ends in `.theme`'s parent — the folder is not the theme, the file inside it is. |
| `%SystemRoot%\Resources\Themes` | Themes installed for all users. |
| the path already configured | Even when it lives somewhere else, so a theme kept on a USB stick or in `D:\Themes` stays selectable. A path whose file has since been deleted is listed too, marked `(missing)`, so the broken entry can be seen and replaced. |

`Custom.theme` is deliberately **not** listed. Windows rewrites it whenever the
user tweaks a colour or wallpaper in Settings, and its `ThemeId` changes with
it: offering it would give the user a moving target that is never quite the
theme they saved.

The settings dialog adds one row at the bottom of each dropdown, *Browse…*, which
opens a file picker on the per-user theme folder and accepts any `.theme`
anywhere. Cancelling it puts the previous selection back — the row is a command
wearing a list item's clothes and must not be left "selected".

### What "the same theme" means

Comparing by path alone breaks the moment the user copies a pack or Windows
rewrites `CurrentTheme`; comparing by `ThemeId` alone breaks for older packs that
never had one. So a theme's identity is `ThemeId` when the file has one, and the
path otherwise:

```cpp
struct ThemeIdentity { std::wstring themeId; std::wstring path; };
bool SameIdentity(const ThemeIdentity&, const ThemeIdentity&);
```

Every comparison in the program — "is this already the current theme?", "did the
user change it by hand?", "are these two dropdowns pointing at the same pack?" —
goes through that function, with `CompareStringOrdinal(…, TRUE)` so case never
matters on a case-insensitive filesystem.

### How a pack is applied

`IThemeManager2` from `themeui.dll`, CLSID `{9324DA94-50EC-4A14-A770-E90CA03E7C8F}`
interface `{c1e8c83e-845d-4d95-81db-e283fdffc000}`. That interface has 24 methods
and no header in the SDK, so it is declared in `src/WindowsTheme.cpp` from a
single X-macro list — the vtable slot of `OpenTheme` (19) is asserted at compile
time, because a wrong slot does not fail loudly, it silently calls a different
method with the wrong signature.

The call blocks for seconds: it copies the wallpaper file, rewrites the registry
and restarts the shell's theme providers. It runs on a **dedicated STA thread**
(`CoInitializeEx(COINIT_APARTMENTTHREADED)`, no message pump) that the scheduler
hands single-slot jobs to, so a newer request replaces an older one instead of
queueing behind it, and the UI thread never waits.

A successful apply is then **confirmed by reading it back**:
`HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\CurrentTheme` must point
at the file that was applied, polled for two seconds. Trusting the call to have
worked is how a "switched" theme that did not switch goes unnoticed.

### If the pack cannot be applied

`LS_DISABLE_THEME_COM=1` in the environment, or a machine where the COM class
refuses to activate, disables the pack path. The program then says so in the log
(*"Pack could not be applied; falling back to the light/dark mode in the file."*)
and writes only `SystemUsesLightTheme` / `AppsUseLightTheme` from the pack's
`[VisualStyles]` section. Colours change; wallpaper, cursor and sounds do not. It
does not claim the switch succeeded.

### Telling our own apply from the user's

Windows rewrites `CurrentTheme` to `Custom.theme` while a pack is being applied —
both the path *and* the id change — so a naive comparison sees "the user changed
the theme" immediately after every apply and enters manual-override mode
forever. Three escape hatches sit in front of the real comparison, in this order:

1. the identity already matches the target — on schedule, nothing to do;
2. less than **30 s** since our own apply — attribute it to ourselves;
3. the identity still equals the pack we last wrote — Windows merely re-spelled
   the path.

Only after all three does a different theme count as "the user did it", which
suspends automatic switching until the next boundary. A target whose *file* has
disappeared is treated differently: it is a configuration error, not the user
taking over, so the scheduler backs off for 30 minutes and logs it once.

### Diagnostics

```
LightSwitchStandalone.exe --dump-themes themes.tsv
LightSwitchStandalone.exe --theme-check check.txt
```

`--dump-themes` prints one line per theme: path, `ThemeId`, `SystemMode`,
`AppMode`, whether it came from the user directory, whether the file is missing,
and whether it is the current one. `--theme-check` reports whether the COM class
activates, how many themes `IThemeManager2` reports, and the identity Windows is
running right now — the three facts needed to tell "our catalogue is wrong" from
"the apply is wrong". Both write UTF-8 with a BOM.

### Migration from 1.3.x

A 1.3.x `settings.json` has neither `themeA` nor `themeB`. If either of the old
checkboxes was on, `themeA` is seeded with the theme Windows is currently running
and `themeB` is left empty, and the log says so once:

```
[Config] Migrated from 1.3.x: themeA="C:\…\Custom.theme". Choose the dark-side theme in Settings.
```

Nothing is written to disk by the migration — the seeding is in memory and only
reaches the file when the user presses *Save*. Until then the dark side is
unconfigured and automatic switching stays idle, which is what 1.3.x did when
neither target was ticked.

## Architecture

One process, several threads — deliberately *not* the upstream
"interface DLL + service EXE + Runner keyboard hook" trio.

| Thread | File | Responsibility |
|---|---|---|
| UI / message loop | `App.cpp`, `TrayIcon.cpp`, `HotkeyManager.cpp`, `SettingsDialog.cpp`, `dark/` | Tray icon, `WM_HOTKEY`, `WM_POWERBROADCAST`, the modal settings dialog, and all dark-mode handling. Never touches the registry. |
| Scheduler worker | `Scheduler.cpp`, `StateManager.cpp`, `ThemeEngine.cpp` | Minute ticks, state machine, external-change detection, night-light watcher lifecycle, and the two registry fallbacks (`SetSystemTheme` / `SetAppsTheme`). |
| Theme COM worker (STA) | `WindowsTheme.cpp` | The only place `IThemeManager2` is called: one job at a time, newest wins, `CoInitializeEx(COINIT_APARTMENTTHREADED)` without a message pump. Started on first use, joined in `Shutdown()`. |
| Night-light observer | `vendor/NightLightRegistryObserver.h` | `RegNotifyChangeKeyValue` loop; signals the worker. |
| Settings watcher | `Config.cpp` | `ReadDirectoryChangesW` + debounce + content comparison; signals the worker. |

**The single most important rule:** nothing that applies a theme may run on the
UI thread. Applying a pack blocks for seconds inside `OpenTheme` (wallpaper copy
plus a shell theme reload), and the registry fallback ends with a
`SendMessageTimeout(HWND_BROADCAST, ..., SMTO_ABORTIFHUNG, 5000)` broadcast that
can block for five more. The scheduler therefore hands pack applies to the STA
worker and does the fallback itself; the hotkey and the tray commands only post
an event.

```
main.cpp            single instance, command line, message loop bootstrap
App.{h,cpp}         tray, hotkey, dialogs, lifecycle, Windows integration
Scheduler.{h,cpp}   worker thread + wait set + external-change detection
StateManager.{h,cpp} decision logic (port of upstream LightSwitchStateManager)
ThemeEngine.{h,cpp} ThemeTarget + ApplyThemeTarget(): pack first, registry fallback
ThemeCatalog.*      .theme enumeration, INI parsing, ThemeIdentity comparison
WindowsTheme.*      the IThemeManager2 declaration and its STA worker
Config.{h,cpp}      settings.json model, atomic write, content-compared watcher
Json.{h,cpp}        minimal JSON reader/writer (object/array/string/number/bool)
Logging.{h,cpp}     OutputDebugString + UTF-16 log file, thread safe
Text.{h,cpp}        UTF-8 <-> UTF-16 helpers
L10n.{h,cpp}        the whole string table, language resolution, UI font choice
HotkeyManager.*     RegisterHotKey, low-level keyboard hook fallback
TrayIcon.*          Shell_NotifyIcon, GDI-drawn icon, popup menu
SettingsDialog.*    Win32 dialog (no WPF/WinUI)
AutoStart.*         HKCU\...\Run management
dark/               dark-mode support; see Dark mode below
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

## Dark mode

The settings dialog and the tray popup menu can be drawn light or dark,
independently of what the scheduler is doing to the Windows theme.

### The three modes

| `uiTheme` | Menu label | Behaviour |
|---|---|---|
| `"auto"` (default) | *Follow Windows* | Reads `AppsUseLightTheme` — the same value the scheduler writes — so the interface matches whatever the rest of Windows is using. |
| `"light"` | *Light* | Pinned light. A Windows theme change does not move it. |
| `"dark"` | *Dark* | Pinned dark. A Windows theme change does not move it. |

The dropdown is the *Interface theme* group, the last one in the dialog.

### Live preview, save and cancel

Changing the dropdown re-themes the open dialog immediately, so you can see what
you are choosing. That preview also pushes the choice into the module — which is
what lets the tray menu, built later on a right-click, pick the new scheme up.

* **Save** persists `uiTheme` and re-applies.
* **Cancel** puts the theme back to what `settings.json` holds, so leaving a
  preview on screen does not strand the process in a theme the user rejected.
* **Restore defaults** resets the dropdown to *Follow Windows* and moves the
  preview with it, rather than leaving the combo reading one thing and the
  window showing another.

### Following Windows live

While `uiTheme` is `"auto"`, both the dialog and the tray application react to
`WM_SETTINGCHANGE` with `lParam == "ImmersiveColorSet"` and to
`WM_THEMECHANGED`. Flip Windows between light and dark with the dialog open and
it changes under you; leave it pinned and it does not move at all.

### Why there are two extra modules

Windows ships dark resources for only a handful of controls, so most of the work
is self-drawing. Two layers take care of it:

* `src/dark/darkmode.{h,cpp}` — the **system layer**. DWM window chrome, the
  undocumented `uxtheme` ordinals, system-theme detection, the palette, and
  preference persistence.
* `src/dark/darkui.{h,cpp}` — the **interface layer**. Turns an existing control
  tree dark by class name: Static, Edit, ListBox, ComboBox, Button, CheckBox,
  RadioButton, GroupBox, ListView, TreeView, StatusBar, the menu bar and popup
  menus.

Those four files are copied **byte for byte** from the author's own
`win32-dark-mode` skill and are never edited (see *Provenance* below). Two small
project-side modules sit on top of them instead:

* `src/dark/DarkApp.{h,cpp}` — the **only** place that talks to the module. It
  exists because `dm::Init()` and `dm::OnApply(darkui::Apply)` must both run, in
  that order, before any window exists, and forgetting the second one fails
  *silently*: the title bar and palette go dark, every control stays light, and
  nothing is logged. One function that cannot be called half-way removes that
  class of bug. It also owns the "is the theme already right?" test, so the
  dialog and the tray menu cannot disagree about it.
* `src/dark/DarkControls.{h,cpp}` — self-drawn replacements for the two controls
  the interface layer does not cover. See below.

### The two controls that needed a takeover

`SysDateTimePick32` (the two time pickers) and `msctls_hotkey32` (the shortcut
box) have no dark theme class and ignore every `WM_CTLCOLOR*` message, so there
is nothing to switch on — their painting has to be taken over outright.
`DarkControls` subclasses them and intercepts **only** the painting messages
(`WM_ERASEBKGND`, `WM_PAINT`, `WM_NCPAINT`, focus/enable changes). Mouse
hit-testing, keyboard input, the spinner, and the `DTM_*` / `HKM_*` message
storage all stay with the control, so both keep working exactly as before —
only the pixels change.

One consequence is visible in the dialog template: **both date pickers carry
`DTS_UPDOWN`**. That replaces the drop-down calendar with an up/down spinner.
It is not cosmetic. The drop-down panel is a separate window that the system
populates and draws itself, and there is no way to darken it; removing it is the
only way to keep the dialog free of one bright white rectangle.

The other is that the shortcut box **does not show the Win key**. That was
already true before dark mode — `msctls_hotkey32` cannot represent `MOD_WIN` at
all, which is exactly why the dialog has a separate *Include Win key* checkbox.
The dark field simply draws what the control can actually store.

### What is deliberately not dark

| Thing | Why |
|---|---|
| The validation and confirmation `MessageBox`s (invalid coordinates, invalid offsets, "nothing to change", restart Explorer, `--help`) | `MessageBox` is drawn by the system on the application's behalf, in a way an application cannot reach. Only an owner-drawn replacement could change it, which is a much larger change than the feature is worth. |
| The tray icon's hover tooltip | Also system-drawn. |
| `lightswitch.log` | Unaffected — the log is a file, not a surface. |

### Provenance

`src/dark/darkmode.{h,cpp}` and `src/dark/darkui.{h,cpp}` are assets of the
author's `win32-dark-mode` skill, not upstream PowerToys code and not derived
from it. They are copied verbatim so that a hash can prove it:

```sh
sha256sum src/dark/darkmode.h src/dark/darkmode.cpp src/dark/darkui.h src/dark/darkui.cpp
```

Those four lines have to be stable, so `.gitattributes` pins them to `eol=lf`.
Without that pin, `core.autocrlf=true` — the Windows default — would check them
out with CRLF endings on a fresh clone and every hash would come out wrong, even
though the files were fine in the repository. If you copy the four files by hand
rather than through Git, make sure whatever tool you use does not rewrite their
line endings, or the check will fail for the same reason.

Both `.cpp` files are compiled at **warning level 3** rather than the project's
level 4, because that is what they were written and verified against. Raising
the level would mean editing them, which would destroy the only evidence that
they are unmodified. The `.vcxproj` sets the lower level per file, and nothing
else in the project is exempt.

The project has no other dark-mode dependency: no library, no NuGet package and
no redistributable. `dwmapi.dll` and `uxtheme.dll` are OS components, and the
`uxtheme` entry points used are resolved by **ordinal** at runtime, not linked by
name.

---

## Behaviour fidelity vs. upstream

### Kept identical

| Behaviour | Upstream | Verified by |
|---|---|---|
| Registry targets | `SystemUsesLightTheme`, `AppsUseLightTheme` under `HKCU\...\Themes\Personalize` | `vendor/ThemeHelper.cpp` is verbatim — but since 1.4.0 these are written only by the fallback path; normally the `.theme` pack sets them |
| Broadcast sequence | `WM_SETTINGCHANGE`+`ImmersiveColorSet`, then `WM_THEMECHANGED`, `SMTO_ABORTIFHUNG`/5000 ms | ditto (fallback path) |
| `ColorPrevalence` reset | Only when switching **to light** | ditto |
| Sunrise/sunset | NOAA algorithm, zenith 90.833, local-time conversion via `GetTimeZoneInformation` | `vendor/ThemeScheduler.cpp` is verbatim |
| Cross-midnight schedules | `ShouldBeLight` normalises into `[0,1439]` and handles wrap-around | `vendor/LightSwitchUtils.h` is verbatim |
| Decision order | `OnSettingsChanged` / `OnTick` / `OnManualOverride` / `OnNightLightChange` all funnel into the same `EvaluateAndApplyIfNeeded` | ported 1:1 |
| Manual override | Hotkey flips the theme and toggles the override flag; automatic switching resumes when a schedule boundary is crossed | ported 1:1 |
| Off mode | Early-out, does not touch the cached boundaries | ported 1:1 |
| Tick alignment | Wakes at the top of each wall-clock minute | ported 1:1 |
| External change detection | If the registry no longer matches the schedule, enter manual override instead of fighting the user | ported 1:1, but the comparison is on theme identity rather than two booleans — see *Themes* |
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
| Toggle shortcut only flips *enabled* targets | Upstream's module interface did the same, but silently. | Since 1.4.0 both sides are just theme paths: if either is empty the toggle logs a warning and does nothing, and the dialog refuses to save an incomplete pair while the schedule is on. |
| **Whole theme packs instead of two registry DWORDs** | The request that produced 1.4.0: "switch Windows themes, not light/dark". A pack is a superset, and `SystemUsesLightTheme` / `AppsUseLightTheme` end up set exactly as the pack says. | Wallpaper, cursor, sounds and accent colour now change too. The two "apply to" checkboxes are gone; `changeSystem` / `changeApps` stay in the file untouched so a downgrade still behaves. See *Themes*. |
| **A dedicated STA thread for `IThemeManager2`** | `OpenTheme` blocks for seconds while it copies the wallpaper and restarts the shell's theme providers. Doing that on the scheduler worker would stall the minute tick; doing it on the UI thread would freeze the tray. | One apply at a time, and a newer request replaces an older one instead of queueing. The thread is started on first use and joined in `Shutdown()`. |
| Everything that touches the theme runs off the UI thread | PowerToys' hotkey handler ran on the hook thread; here the UI thread must stay responsive. | Rapid toggling coalesces into one pending toggle. |
| Interface strings live in a compiled-in table, not in Win32 string resources | One source of truth per string, and no `.rc` code page to get wrong for non-ASCII. Upstream has no localisation at all. | Translating means editing `src/L10n.cpp` and rebuilding; there is no satellite DLL to drop in. See *Localisation*. |
| The dialog template carries no text | The same single-source-of-truth reason. Each label needs a real control id so it can be set at `WM_INITDIALOG`. | `LightSwitch.rc` is geometry + ids; the English wording survives only as comments. |
| The button row and over-long labels are laid out at runtime | Translated labels have different widths, and CJK labels are wider than they look. | No more `Restore defaults` overlapping `Save`, and a translated label is widened rather than clipped. |
| The dialog has an **Interface theme** group that upstream does not have | Upstream has no dark-mode support at all — it is a WinUI page and gets its theme from PowerToys. | One more dropdown and one more `settings.json` key. Nothing about the scheduling behaviour changes. |
| Both time pickers use `DTS_UPDOWN` instead of a drop-down calendar | The calendar panel cannot be darkened. | Up/down spinner instead of a calendar. The values, the `DTN_DATETIMECHANGE` notification and the stored format are unchanged. |
| The interface can be pinned light or dark regardless of the schedule | The schedule decides what Windows looks like; `uiTheme` decides what *this application's* windows look like. Conflating them would make the settings dialog change colour when you are trying to configure the schedule. | `uiTheme` is independent of `themeA` / `themeB` / `scheduleMode`. |
| The two theme dropdowns reuse the rectangle the "apply to" group had | The dialog is 397 dlu, already 1040 px at 125% scaling against a 1020 px work area. A new group would have pushed the button row behind the taskbar. | Two one-line combo boxes, one dlu apart, in 42 dlu of height. The labels are the shortest words that still read correctly in both languages. |

---

## Risks and mitigations

| Risk | Mitigation in this code |
|---|---|
| **`IThemeManager2` is an undocumented COM interface.** There is no SDK header for it, so the vtable is described by hand — and a wrong slot does not fail loudly, it calls a different method with the wrong signature. | The declaration is generated from a single X-macro list, and the slot of `OpenTheme` (19) plus the total method count (24 + `IUnknown`) are `static_assert`ed. A successful apply is then confirmed by polling `CurrentTheme` for two seconds, so "the call returned" is never mistaken for "the theme changed". If the class will not activate at all, the light/dark fallback runs and says so. |
| **Applying a pack is slow and not cancellable.** `OpenTheme` copies the wallpaper and restarts the shell's theme providers; a large 4K image takes noticeably longer than a solid colour. | It runs on its own STA thread with a one-slot queue, so the UI stays responsive and a second request replaces the first rather than queueing. The dialog's *Toggle now* returns immediately. |
| **A `.theme` file is not valid UTF-16.** On the machines tested it is single-byte CP936 with no BOM; some inbox themes are UTF-16LE with one. | It is never decoded by hand: `GetPrivateProfileStringW` / `GetPrivateProfileSectionW` handle all three encodings, which is exactly what `themeui.dll` itself uses. A `[Theme] DisplayName` starting with `@` is resolved with `SHLoadIndirectString`. |
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
| **A translation forgets a `%1` placeholder.** | `--dump-strings` makes this checkable: diff the two language sections and confirm every id carries the same set of placeholders. `Format()` leaves an unmatched placeholder verbatim rather than eating it, so the bug is visible in the UI instead of silent. |
| **The `uxtheme` entry points are undocumented ordinals.** `SetPreferredAppMode` (135), `AllowDarkModeForWindow` (133), `FlushMenuThemes` (136) and friends are exported by number only, so `GetProcAddress` must be given `MAKEINTRESOURCEA(ordinal)`. A future Windows could renumber them. | Every one of them is resolved at runtime and the result is checked; a failed lookup falls through to a light but fully working interface. `dm::Init()`'s return value reports whether the DWM dark title bar is available at all (Windows 10 1809 / build 17763 and later). Nothing crashes, and the log records what happened. |
| **A future control is added to the dialog and stays light.** The interface layer dispatches by *class name*, so a class nobody taught it about is simply left alone. | The classes the dialog uses are covered, and the two that were not have their own module. If you add a control in a new class, check it in dark mode: `WM_CTLCOLOR*` forwarding, subclassing, or a self-drawn takeover — in that order of preference. |
| **A third-party shell replacement or a classic (non-themed) visual style.** | The dark-mode module calls `SetWindowTheme(h, L"", L"")` on several controls and re-draws them itself. Under a non-default visual style the self-drawn parts still follow the app's palette; what changes is how the *system*-drawn parts (popup menus, title bar) look. |
| **Windows High Contrast is on.** | The module keeps drawing its own palette, so the interface stays dark or light as configured and is *not* overridden by High Contrast. If you rely on High Contrast for legibility, set `uiTheme` to `"light"` (or use the Light theme) rather than expecting the module to defer. |

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

一个独立运行的 Windows 托盘小工具，按时间计划或日出日落在你挑的**两个 Windows
主题包**（`.theme` 文件）之间自动互切。所谓主题包是整套外观：壁纸、配色、鼠标
指针、声音、强调色，浅色/深色模式本身就写在包里（`[VisualStyles]` 段）。
它从 Microsoft PowerToys 的 **Light Switch** 模块中抽取而来，但**不依赖 PowerToys**：
不需要 PowerToys 本体、Runner、设置界面，也不需要 vcpkg / NuGet / VC++ 运行库。

交付物就是一个 exe。**拷到任何地方直接运行，删掉目录就是卸载。**

> 1.3.x 及以前切的是两个注册表值（浅色/深色**模式**）。1.4.0 起改成切主题包，
> 模式是主题包的一部分，所以原来那两个勾选框已经没有意义，被换成了两个主题下拉框。

### 基本用法

双击 `LightSwitchStandalone.exe` 即常驻通知区域（托盘）。右键托盘图标是菜单，
双击托盘图标打开设置对话框。

命令行：

```
LightSwitchStandalone.exe               常驻通知区域运行
LightSwitchStandalone.exe --settings    运行并立即打开设置对话框
LightSwitchStandalone.exe --toggle      切到另一个主题包后退出
LightSwitchStandalone.exe --light       应用浅色一侧的主题包后退出
                                        （--theme-a 同义）
LightSwitchStandalone.exe --dark        应用深色一侧的主题包后退出
                                        （--theme-b 同义）
LightSwitchStandalone.exe --theme 文件  应用任意一个 .theme 文件后退出
LightSwitchStandalone.exe --lang 代码   仅在本次运行强制使用指定界面语言
                                        （auto | en-US | zh-CN）
LightSwitchStandalone.exe --dump-strings 文件
                                        把所有界面字符串导出到该文件后退出
LightSwitchStandalone.exe --dump-themes 文件
                                        把能看到的全部主题导出后退出
LightSwitchStandalone.exe --theme-check 文件
                                        探测 IThemeManager2 并写出结果后退出
LightSwitchStandalone.exe --help        显示以上说明
```

一次性命令的退出码：`0` 应用成功**或本来就是这个主题**，`2` 该侧没配主题，
`3` 主题文件不存在，`4` 应用失败 —— 脚本能分清「没事可做」和「坏了」。

### 两个主题怎么选

1. 打开设置对话框，第二个分组「主题对」里有 **浅色：** 和 **深色：** 两个下拉。
2. 下拉里列的是 `%LOCALAPPDATA%\Microsoft\Windows\Themes`（含一层子目录，
   Spotlight 壁纸包的主题就藏在子目录里）和 `%SystemRoot%\Resources\Themes`
   里的全部 `.theme`，再加上你已经配过的那个路径（即使它在别处）。
   最后一行是 **浏览…**，可以挑任意位置的 `.theme` 文件。
3. `Custom.theme` **不会**列出来：Windows 一改配色就把它重写一遍，里面的
   `ThemeId` 也跟着变，列出来等于给你一个永远在动的目标。
4. 下拉右边那两行是状态：**当前：** 是 Windows 此刻在用的主题，**目标：** 是按
   你现在的设置、这一刻该用的那个主题。改了模式、时间或经纬度，它会立刻跟着变。
5. 保存时的三条校验：
   - 计划开着（模式不是「已关闭」）却有一侧没选 → 拦住，要求两个都选；
   - 选中的文件已经不存在 → 只问一句「仍然保存吗」，不硬拦（可能只是那块盘没插）；
   - 两侧选成同一个主题 → 问一句，因为切换将没有任何可见变化。
6. 存在 `settings.json` 的 `themeA` / `themeB` 里，是**绝对路径**：

   ```json
   { "themeA": { "value": "C:\\Users\\你\\AppData\\Local\\Microsoft\\Windows\\Themes\\浅色1.theme" },
     "themeB": { "value": "C:\\Users\\你\\AppData\\Local\\Microsoft\\Windows\\Themes\\深色1.theme" } }
   ```

   空字符串表示「还没选」，这时自动切换会**什么都不做**，而不是猜一个。
7. 从 1.3.x 升级不需要手改文件：只要原来勾了任意一个「生效范围」，
   程序会把 `themeA` 预填成当前正在用的主题、`themeB` 留空，并在日志里写一行
   `Migrated from 1.3.x: …`。这个预填**只存在内存里**，你点「保存」才会落盘。

托盘菜单里也有 **浅色主题：名称** / **深色主题：名称** 两行，正在用的那个前面有
圆点，没配的那一侧是灰的。点一下就直接切过去，并进入手动覆盖状态
（计划不会在下一分钟把它改回去）。

### 界面语言怎么设

1. **默认跟随 Windows。** 程序读的是 Windows 显示语言（就是「设置 > 时间和语言 >
   语言」里选的那个），不是系统区域格式。
2. **想手动指定**，打开设置对话框，倒数第二个分组「界面语言」，下拉选
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

### 界面深色模式怎么设

1. **默认跟随系统。** 程序读的是 Windows 的**应用**主题
   （`AppsUseLightTheme`），也就是「设置 > 个性化 > 颜色」里的那个，
   和本程序自带的切换计划写的是同一个值。
2. **想手动指定**，打开设置对话框，最后一个分组「界面主题」，下拉选
   `跟随 Windows` / `浅色` / `深色`，然后点「保存」。
3. **下拉一改就立刻变**，不用保存也能先看效果：
   - 点「保存」→ 写入 `settings.json` 并重新应用；
   - 点「取消」→ 主题回到 `settings.json` 里的值，不会把预览留在界面上；
   - 点「恢复默认」→ 下拉回到「跟随 Windows」，界面也跟着一起回去。
4. 设置存在 `settings.json` 的 `uiTheme` 键里：

   ```json
   { "uiTheme": { "value": "auto" } }
   ```

   `"auto"` 跟随系统；`"light"` / `"dark"` 是**锁定**，之后系统主题再怎么变都不动。
   填了无法识别的值（比如打错字）会当作 `"auto"`，不会把界面锁死。
5. 处在 `auto` 时，运行期间把 Windows 在浅色/深色之间切换，**对话框会当场跟着变**，
   不用关掉重开；锁定了就不会动。
6. 深色是**全深色**：标题栏、分组框、复选框、下拉列表（展开的列表也是深的）、
   时间框和它的上下箭头、快捷键框都处理过，没有白块。
   只有系统自己弹的消息框（比如坐标填错的警告）着色改不了，那是系统画的。

### 单击托盘图标会不会切换主题

1. **默认不会。** 程序里**没有任何「长按」判断**——托盘图标只有单击、双击、右键
   三种手势。会和主题有关的是**单击**，而触摸屏上「轻点」和鼠标单击发的是同一条
   消息（`WM_LBUTTONUP`），所以手指擦过图标就会把整台机器的主题翻掉。为了挡住
   这个，默认把单击切换关掉了。
2. 想恢复单击切换：打开设置对话框，在「切换方式」分组里勾上
   **「单击托盘图标切换主题」**，再点「保存」。也可以直接写 `settings.json`：

   ```json
   { "trayClickTogglesTheme": { "value": true } }
   ```

3. 关掉后**另外三种切换方式不受影响**：全局快捷键、右键菜单里的「立即切换主题」、
   对话框里的「立即切换」。双击图标仍然打开设置窗口，右键仍然弹菜单。
4. 触摸屏上**长按等于右键**，弹出的是菜单，本来就从来不会切换主题。
5. 关掉时单击没反应是**故意的**，日志里会留一行
   `Left click ignored; "trayClickTogglesTheme" is off.`，免得你以为程序坏了。
   完整的手势对照表见英文部分的 *Trigger conditions*。
6. 旧版本（1.2.0 及更早）的 `settings.json` 没有这个键；读不到就用默认值
   `false`，所以升级不会悄悄把单击切换重新打开。

### 重启资源管理器后托盘图标还在吗

在。以前**不在**——这是个 bug，1.3.1 修掉了。

托盘区域（通知区）是 `explorer.exe` 的一部分。explorer 一重启，里面所有图标就都跟着
消失了，而 Windows **不记得谁注册过什么，也不会主动通知谁来重新注册**。所以程序必须
自己盯着 shell 广播的 `TaskbarCreated` 消息，收到就重新注册一次（连气泡提示一起）。

三种情况都覆盖：托盘菜单里的「重启资源管理器」、设置对话框里同名的按钮、以及
explorer 被外部杀掉或自己崩掉。

程序**自己**重启 explorer 时还多一层保险：它先杀掉 explorer、等 800 ms、再起一个新的，
这整段时间 UI 线程是停着的；而 Winlogon 的 `AutoRestartShell`（默认开启，位于
`HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon`）很可能就在这段等待里
把新 shell 拉起来了——那条广播于是发给一个没在取消息的线程，直接丢掉。所以除了广播，
还有一个定时重试在兜。真注册不上的话，日志里会明确报 `ERROR`，不会悄悄失败。

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
* **时间框怎么没有日历了？** 现在改成上下箭头（`DTS_UPDOWN`）。日历面板是系统
  自己开的一个独立窗口，颜色改不动，留着它深色界面里就会永远有一块白；去掉它才能
  做到全深色。取值、通知和存储格式都没变。
* **快捷键框里怎么看不到 Win 键？** `msctls_hotkey32` 这个控件**根本无法表示**
  Win 键，所以一直是用旁边那个「包含 Win 键」勾选框单独保存的——深色之前就是这样，
  不是深色模式改的。深色框只是照实画控件里真正存着的内容。
* **托盘图标没反应 / 快捷键无效？** 先看 `lightswitch.log`。涉及 Win 键的组合被系统
  保留，程序会退回键盘钩子；若连钩子也被安全软件拦了，程序照常运行，只是快捷键不可用。
* **任务栏颜色没变？** 部分外壳界面会缓存主题。用托盘菜单里的
  「重启资源管理器（刷新外壳）」——它会先发一个无破坏性的刷新通知，只有你确认后才真的
  重启 Explorer，绝不会静默杀掉它。
* **切了主题，但壁纸/鼠标指针没变？** 说明主题包没能通过 `IThemeManager2` 应用，
  程序退回了「只改浅色/深色两个注册表值」的降级路径，日志里会有一行
  `Pack could not be applied … falling back to the light/dark mode in the file.`。
  先跑 `--theme-check 结果.txt` 看看 COM 能不能起来。
* **我的主题没出现在下拉里？** 它可能不在那两个标准目录里，也可能是个文件夹
  （壁纸包）而不是 `.theme` 文件。用下拉最后一行的 **浏览…** 直接挑文件；
  挑过之后，即使文件在别处也会一直留在列表里。文件被删掉的话，会显示成
  `名称（缺失）`，提醒你换一个。
