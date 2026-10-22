// LightSwitch Standalone - .theme catalogue (declarations).
//
// Pure file layer: enumerates the two directories Windows keeps themes in,
// parses the .theme INI files and answers "is this the same theme?" questions.
// It deliberately contains no COM, no registry writes and no UI, so it is safe
// to call from any thread (including the settings dialog's UI thread).
//
// Why the files and not IThemeManager2: the COM interface only exposes a
// display name and a visual-style path, not the .theme file itself, and its
// index is unstable across theme installs. Everything this program needs to
// store, compare and display is in the file.
//
// Encoding note: .theme files are NOT UTF-16. On this machine they are plain
// ANSI (CP936) with no BOM; some Windows themes are UTF-16LE with a BOM and
// third-party packs may be UTF-8 with a BOM. GetPrivateProfileStringW handles
// all three, which is exactly what themeui.dll itself uses - so this module
// never decodes bytes by hand.
#pragma once

#include <string>
#include <vector>

namespace ls::theme
{

// One .theme file, as far as the rest of the program cares.
struct ThemeInfo
{
    std::wstring path;        // absolute, normalised (no \\?\, no short names)
    std::wstring displayName; // resolved from [Theme] DisplayName; falls back to the file name
    std::wstring themeId;     // [Theme] ThemeId, uppercased; empty when the file has none
    std::wstring systemMode;  // [VisualStyles] SystemMode: "Light", "Dark" or empty
    std::wstring appMode;     // [VisualStyles] AppMode:    "Light", "Dark" or empty
    bool fromUserDir = false;
    bool fileMissing = false; // synthesised for a configured path that no longer exists
};

// What makes two themes "the same": the ThemeId when both files have one,
// otherwise the path. Comparing by path alone breaks the moment the user
// copies a theme or Windows rewrites CurrentTheme to Custom.theme, and
// comparing by id alone breaks for older packs that never had one.
struct ThemeIdentity
{
    std::wstring themeId;
    std::wstring path;

    bool Empty() const { return themeId.empty() && path.empty(); }
};

bool SameIdentity(const ThemeIdentity& a, const ThemeIdentity& b);

// Absolute form of a user-supplied path: environment variables expanded,
// surrounding quotes and whitespace removed, relative segments resolved.
// Returns an empty string for an empty input. Does NOT require the file to
// exist - a configuration may legitimately point at a drive that is offline.
std::wstring NormalizePath(const std::wstring& path);

// Fully parsed description of one file. Never throws and never returns an
// error: a missing or unreadable file comes back with fileMissing=true and a
// display name derived from the file name.
ThemeInfo DescribePath(const std::wstring& path);

// Identity of a file, or of the path alone when the file is gone.
ThemeIdentity IdentifyPath(const std::wstring& path);

// Identity of the active Windows theme, read from
// HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\CurrentTheme.
// Empty when the value is absent (a fresh profile before any theme is chosen).
ThemeIdentity ReadCurrentIdentity();

// Display name of one file, cached. Used by the tooltip and the tray menu,
// both of which run on the UI thread and must not touch the disk repeatedly.
std::wstring DisplayNameOfPath(const std::wstring& path);

struct EnumerateOptions
{
    // Paths that are already configured but may live outside the two standard
    // directories (picked with "Browse..."). Missing ones are still listed so
    // the combo box can show - and the user can replace - a broken entry.
    std::vector<std::wstring> extraPaths;

    // Defensive ceiling; a directory full of themes is unusual, a runaway
    // wildcard hit is not worth an unbounded allocation.
    size_t maxThemes = 512;
};

// Both theme directories, user directory first, duplicates collapsed by
// identity, sorted by display name. Theme entries are files only: the same
// directories also contain wallpaper packs stored as folders, and
// Custom.theme is skipped because Windows rewrites it behind the user's back.
std::vector<ThemeInfo> Enumerate(const EnumerateOptions& options);

// Drop the per-path cache. The settings dialog calls this on every
// WM_INITDIALOG so a theme the user just imported shows up immediately.
void InvalidateCache();

// Directory helpers, exposed for the browse dialog's initial location and for
// diagnostics.
std::wstring UserThemesDirectory();
std::wstring SystemThemesDirectory();

} // namespace ls::theme
