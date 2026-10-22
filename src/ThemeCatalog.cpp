// LightSwitch Standalone - .theme catalogue (implementation).
#include "ThemeCatalog.h"

#include "Logging.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <map>
#include <mutex>

namespace ls::theme
{

namespace
{

constexpr wchar_t kThemesRegistryPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes";
constexpr wchar_t kCurrentThemeValue[] = L"CurrentTheme";

// Windows rewrites Custom.theme whenever the user tweaks a setting, and its
// ThemeId changes with it. Listing it would give the user a moving target.
constexpr wchar_t kCustomThemeFileName[] = L"Custom.theme";

constexpr ULONGLONG kCacheTtlMs = 30000;

std::wstring TrimCopy(const std::wstring& value)
{
    size_t first = 0;
    size_t last = value.size();
    while (first < last && (value[first] == L' ' || value[first] == L'\t' || value[first] == L'\r' || value[first] == L'\n'))
        ++first;
    while (last > first && (value[last - 1] == L' ' || value[last - 1] == L'\t' || value[last - 1] == L'\r' || value[last - 1] == L'\n'))
        --last;
    return value.substr(first, last - first);
}

std::wstring UnquoteCopy(const std::wstring& value)
{
    std::wstring trimmed = TrimCopy(value);
    if (trimmed.size() >= 2 && trimmed.front() == L'"' && trimmed.back() == L'"')
        return TrimCopy(trimmed.substr(1, trimmed.size() - 2));
    return trimmed;
}

bool FileExists(const std::wstring& path)
{
    if (path.empty())
        return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// GetPrivateProfileSectionW returns nSize-2 when the buffer was too small.
// Grow until the whole section fits; give up after a few doublings rather than
// looping forever on a pathological file.
std::wstring ReadSection(const std::wstring& path, const wchar_t* section)
{
    std::vector<wchar_t> buffer(4096);
    for (int attempt = 0; attempt < 6; ++attempt)
    {
        const DWORD copied = GetPrivateProfileSectionW(section, buffer.data(),
                                                       static_cast<DWORD>(buffer.size()), path.c_str());
        if (copied == 0)
            return std::wstring();
        if (copied < buffer.size() - 2)
            return std::wstring(buffer.data(), copied);
        buffer.resize(buffer.size() * 4);
    }
    return std::wstring();
}

// Section data is "key=value\0key=value\0\0". Keys are case insensitive, the
// way Windows INI files are.
std::wstring SectionValue(const std::wstring& sectionData, const wchar_t* key)
{
    const wchar_t* cursor = sectionData.c_str();
    while (*cursor)
    {
        const std::wstring entry(cursor);
        cursor += entry.size() + 1;

        const size_t equals = entry.find(L'=');
        if (equals == std::wstring::npos)
            continue;

        if (_wcsicmp(entry.substr(0, equals).c_str(), key) == 0)
            return UnquoteCopy(entry.substr(equals + 1));
    }
    return std::wstring();
}

std::wstring FileNameWithoutExtension(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/:");
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0)
        name.erase(dot);
    return name;
}

bool EqualsIgnoreCase(const std::wstring& a, const wchar_t* b)
{
    return _wcsicmp(a.c_str(), b) == 0;
}

// A .theme file's DisplayName is either literal text or an indirect string
// such as "@%SystemRoot%\System32\themeui.dll,-2060", which is what every
// inbox Windows theme uses. SHLoadIndirectString is the same resolver the
// shell uses, so localisation comes for free.
std::wstring ResolveDisplayName(const std::wstring& raw, const std::wstring& path)
{
    if (raw.empty())
        return FileNameWithoutExtension(path);

    if (raw.front() != L'@')
        return raw;

    wchar_t expanded[32768];
    std::wstring source = raw;
    if (ExpandEnvironmentStringsW(raw.c_str(), expanded, ARRAYSIZE(expanded)) > 0)
        source = expanded;

    wchar_t resolved[1024];
    resolved[0] = L'\0';
    if (SUCCEEDED(SHLoadIndirectString(source.c_str(), resolved, ARRAYSIZE(resolved), nullptr)))
    {
        const std::wstring name = UnquoteCopy(resolved);
        if (!name.empty())
            return name;
    }

    LOG_DEBUG(L"[ThemeCatalog] Could not resolve \"" << raw << L"\" from " << path << L"; using the file name.");
    return FileNameWithoutExtension(path);
}

std::wstring NormalizeThemeId(const std::wstring& raw)
{
    std::wstring id = TrimCopy(raw);
    if (id.empty())
        return std::wstring();

    if (id.front() != L'{')
        id.insert(id.begin(), L'{');
    if (id.back() != L'}')
        id.push_back(L'}');

    CharUpperBuffW(id.data(), static_cast<DWORD>(id.size()));
    return id;
}

struct CacheEntry
{
    ThemeInfo info;
    ULONGLONG stamp = 0;
};

SRWLOCK g_cacheLock = SRWLOCK_INIT;
std::map<std::wstring, CacheEntry> g_cache;

std::wstring CacheKey(const std::wstring& path)
{
    std::wstring key = path;
    CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
    return key;
}

ThemeInfo DescribePathUncached(const std::wstring& path)
{
    ThemeInfo info;
    info.path = NormalizePath(path);

    if (info.path.empty() || !FileExists(info.path))
    {
        info.fileMissing = true;
        info.displayName = FileNameWithoutExtension(info.path);
        return info;
    }

    const std::wstring themeSection = ReadSection(info.path, L"Theme");
    const std::wstring visualStyles = ReadSection(info.path, L"VisualStyles");

    info.themeId = NormalizeThemeId(SectionValue(themeSection, L"ThemeId"));
    info.displayName = ResolveDisplayName(SectionValue(themeSection, L"DisplayName"), info.path);
    info.systemMode = SectionValue(visualStyles, L"SystemMode");
    info.appMode = SectionValue(visualStyles, L"AppMode");

    return info;
}

int CompareNames(const std::wstring& a, const std::wstring& b)
{
    const int result = CompareStringEx(LOCALE_NAME_USER_DEFAULT,
                                       LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
                                       a.c_str(), -1, b.c_str(), -1, nullptr, nullptr, 0);
    if (result == CSTR_LESS_THAN)
        return -1;
    if (result == CSTR_GREATER_THAN)
        return 1;
    return 0;
}

void CollectFromDirectory(const std::wstring& directory, bool fromUserDir,
                          std::vector<ThemeInfo>& out, size_t maxThemes, int depth);

void AddThemeFile(const std::wstring& filePath, bool fromUserDir,
                  std::vector<ThemeInfo>& out, size_t maxThemes)
{
    if (out.size() >= maxThemes)
        return;

    ThemeInfo info = DescribePathUncached(filePath);
    info.fromUserDir = fromUserDir;
    out.push_back(std::move(info));
}

// Windows Spotlight's wallpaper packs live one level down, in a folder named
// after the pack, and hold a real .theme each ("Flower (d\Flower (d.theme").
// They are ordinary themes with a ThemeId and a SystemMode, and the Settings
// app lists them, so skipping the subdirectories would leave visible themes
// unreachable from this program. One level is all Windows uses.
void CollectFromDirectory(const std::wstring& directory, bool fromUserDir,
                          std::vector<ThemeInfo>& out, size_t maxThemes, int depth)
{
    if (directory.empty())
        return;

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = directory + L"\\*.theme";
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                   FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        LOG_DEBUG(L"[ThemeCatalog] No .theme files in " << directory
                                                        << L" (error " << GetLastError() << L").");
    }
    else
    {
        do
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                continue;
            if (EqualsIgnoreCase(data.cFileName, kCustomThemeFileName))
            {
                LOG_DEBUG(L"[ThemeCatalog] Skipping " << data.cFileName << L": Windows rewrites it behind our back.");
                continue;
            }

            if (out.size() >= maxThemes)
            {
                LOG_WARN(L"[ThemeCatalog] Listing capped at " << maxThemes << L" themes; some were skipped.");
                break;
            }

            AddThemeFile(directory + L"\\" + data.cFileName, fromUserDir, out, maxThemes);
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    if (depth <= 1)
        return;

    // Subdirectories of the user folder only: the system folder keeps visual
    // styles in subdirectories, never .theme files, so scanning it again would
    // be pure I/O for nothing.
    if (!fromUserDir)
        return;

    const std::wstring childPattern = directory + L"\\*";
    find = FindFirstFileExW(childPattern.c_str(), FindExInfoBasic, &data,
                            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        if (data.cFileName[0] == L'.')
            continue;
        if ((data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0)
            continue;

        CollectFromDirectory(directory + L"\\" + data.cFileName, fromUserDir, out, maxThemes, depth - 1);
    } while (FindNextFileW(find, &data));

    FindClose(find);
}

bool ContainsIdentity(const std::vector<ThemeInfo>& list, const ThemeIdentity& identity)
{
    for (const ThemeInfo& entry : list)
    {
        if (SameIdentity(ThemeIdentity{ entry.themeId, entry.path }, identity))
            return true;
    }
    return false;
}

} // namespace

bool SameIdentity(const ThemeIdentity& a, const ThemeIdentity& b)
{
    if (!a.themeId.empty() && !b.themeId.empty())
        return CompareStringOrdinal(a.themeId.c_str(), -1, b.themeId.c_str(), -1, TRUE) == CSTR_EQUAL;

    if (a.path.empty() || b.path.empty())
        return false;
    return CompareStringOrdinal(a.path.c_str(), -1, b.path.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring NormalizePath(const std::wstring& path)
{
    std::wstring value = UnquoteCopy(path);
    if (value.empty())
        return std::wstring();

    std::vector<wchar_t> buffer(32768);
    const DWORD expanded = ExpandEnvironmentStringsW(value.c_str(), buffer.data(),
                                                     static_cast<DWORD>(buffer.size()));
    if (expanded > 0 && expanded <= buffer.size())
        value.assign(buffer.data());

    const DWORD full = GetFullPathNameW(value.c_str(), static_cast<DWORD>(buffer.size()),
                                        buffer.data(), nullptr);
    if (full > 0 && full < buffer.size())
        value.assign(buffer.data());

    // GetFullPathNameW keeps the \\?\ prefix of an already-extended path;
    // strip it so identities compare equal in both spellings.
    if (value.rfind(L"\\\\?\\", 0) == 0)
        value.erase(0, 4);

    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t'))
        value.pop_back();

    return value;
}

ThemeInfo DescribePath(const std::wstring& path)
{
    const std::wstring normalized = NormalizePath(path);
    if (normalized.empty())
        return ThemeInfo{};

    const std::wstring key = CacheKey(normalized);
    const ULONGLONG now = GetTickCount64();

    {
        AcquireSRWLockShared(&g_cacheLock);
        const auto it = g_cache.find(key);
        if (it != g_cache.end() && now - it->second.stamp < kCacheTtlMs)
        {
            const ThemeInfo cached = it->second.info;
            ReleaseSRWLockShared(&g_cacheLock);
            return cached;
        }
        ReleaseSRWLockShared(&g_cacheLock);
    }

    ThemeInfo info = DescribePathUncached(normalized);

    AcquireSRWLockExclusive(&g_cacheLock);
    g_cache[key] = CacheEntry{ info, now };
    ReleaseSRWLockExclusive(&g_cacheLock);

    return info;
}

ThemeIdentity IdentifyPath(const std::wstring& path)
{
    const ThemeInfo info = DescribePath(path);
    return ThemeIdentity{ info.themeId, info.path };
}

ThemeIdentity ReadCurrentIdentity()
{
    std::vector<wchar_t> buffer(32768);
    DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kThemesRegistryPath, kCurrentThemeValue,
                                        RRF_RT_REG_SZ, nullptr, buffer.data(), &size);
    if (status != ERROR_SUCCESS)
    {
        LOG_DEBUG(L"[ThemeCatalog] CurrentTheme is unreadable (status " << status << L").");
        return ThemeIdentity{};
    }

    const std::wstring raw(buffer.data());
    if (TrimCopy(raw).empty())
        return ThemeIdentity{};

    return IdentifyPath(raw);
}

std::wstring DisplayNameOfPath(const std::wstring& path)
{
    return DescribePath(path).displayName;
}

std::vector<ThemeInfo> Enumerate(const EnumerateOptions& options)
{
    std::vector<ThemeInfo> found;
    found.reserve(32);

    CollectFromDirectory(UserThemesDirectory(), true, found, options.maxThemes, 2);
    CollectFromDirectory(SystemThemesDirectory(), false, found, options.maxThemes, 1);

    std::vector<ThemeInfo> unique;
    unique.reserve(found.size());
    for (ThemeInfo& entry : found)
    {
        if (ContainsIdentity(unique, ThemeIdentity{ entry.themeId, entry.path }))
            continue;
        unique.push_back(std::move(entry));
    }

    // Configured paths outside the two directories, including ones whose file
    // has since been deleted, so the combo box can show the broken entry.
    for (const std::wstring& extra : options.extraPaths)
    {
        const ThemeInfo info = DescribePath(extra);
        if (info.path.empty())
            continue;
        if (ContainsIdentity(unique, ThemeIdentity{ info.themeId, info.path }))
            continue;
        unique.push_back(info);
    }

    std::sort(unique.begin(), unique.end(), [](const ThemeInfo& a, const ThemeInfo& b) {
        const int byName = CompareNames(a.displayName, b.displayName);
        if (byName != 0)
            return byName < 0;
        return CompareStringOrdinal(a.path.c_str(), -1, b.path.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });

    return unique;
}

void InvalidateCache()
{
    AcquireSRWLockExclusive(&g_cacheLock);
    g_cache.clear();
    ReleaseSRWLockExclusive(&g_cacheLock);
}

std::wstring UserThemesDirectory()
{
    PWSTR base = nullptr;
    std::wstring directory;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base != nullptr)
        directory = std::wstring(base) + L"\\Microsoft\\Windows\\Themes";
    if (base != nullptr)
        CoTaskMemFree(base);

    if (directory.empty())
    {
        std::wstring localAppData(32768, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(),
                                                     static_cast<DWORD>(localAppData.size()));
        if (length > 0 && length < localAppData.size())
        {
            localAppData.resize(length);
            directory = localAppData + L"\\Microsoft\\Windows\\Themes";
        }
    }

    return directory;
}

std::wstring SystemThemesDirectory()
{
    std::wstring windows(32768, L'\0');
    const UINT length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (length == 0 || length >= windows.size())
        return std::wstring();

    windows.resize(length);
    return windows + L"\\Resources\\Themes";
}

} // namespace ls::theme
