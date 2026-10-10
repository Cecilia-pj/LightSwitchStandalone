// LightSwitch Standalone - configuration store (implementation).
#include "Config.h"

#include "Json.h"
#include "L10n.h"
#include "Logging.h"
#include "Text.h"

#include <shlobj.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

namespace ls
{

namespace
{

constexpr wchar_t kConfigFileName[] = L"settings.json";
constexpr wchar_t kTempSuffix[] = L".tmp";

std::string ReadFileUtf8(const std::wstring& path, bool& ok)
{
    ok = false;
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return {};

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart < 0)
    {
        CloseHandle(hFile);
        return {};
    }

    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    DWORD total = 0;
    if (!data.empty())
    {
        if (!ReadFile(hFile, data.data(), static_cast<DWORD>(data.size()), &total, nullptr))
        {
            CloseHandle(hFile);
            return {};
        }
        data.resize(total);
    }

    CloseHandle(hFile);
    ok = true;

    // Strip a UTF-8 BOM if the file was edited by a tool that adds one.
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF)
        data.erase(0, 3);

    return data;
}

bool WriteFileUtf8Atomic(const std::wstring& path, const std::string& utf8)
{
    const std::wstring tempPath = path + kTempSuffix;

    HANDLE hFile = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(hFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    FlushFileBuffers(hFile);
    CloseHandle(hFile);

    if (!ok || written != utf8.size())
    {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    return true;
}

std::wstring GetLocalAppDataDirectory()
{
    PWSTR raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw)
        result.assign(raw);
    if (raw)
        CoTaskMemFree(raw);
    return result;
}

bool DirectoryExists(const std::wstring& dir)
{
    const DWORD attrs = GetFileAttributesW(dir.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectory(const std::wstring& dir)
{
    if (DirectoryExists(dir))
        return true;
    return CreateDirectoryW(dir.c_str(), nullptr) != FALSE;
}

bool CanCreateFileIn(const std::wstring& dir)
{
    const std::wstring probe = dir + L"\\._ls_write_probe";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(h);
    return true;
}

int JsonGetIntWrapped(const json::Value& root, const char* key, int fallback)
{
    const json::Value* entry = root.find(key);
    if (!entry || !entry->isObject())
        return fallback;
    const json::Value* value = entry->find("value");
    if (!value || !value->isNumber())
        return fallback;
    return value->asInt(fallback);
}

bool JsonGetBoolWrapped(const json::Value& root, const char* key, bool fallback)
{
    const json::Value* entry = root.find(key);
    if (!entry || !entry->isObject())
        return fallback;
    const json::Value* value = entry->find("value");
    if (!value || !value->isBool())
        return fallback;
    return value->asBool(fallback);
}

std::wstring JsonGetStringWrapped(const json::Value& root, const char* key, const std::wstring& fallback)
{
    const json::Value* entry = root.find(key);
    if (!entry || !entry->isObject())
        return fallback;
    const json::Value* value = entry->find("value");
    if (!value || !value->isString())
        return fallback;
    return Utf8ToWide(value->asString());
}

json::Value Wrap(const json::Value& inner)
{
    json::Value wrapper = json::Value::MakeObject();
    wrapper.set("value", inner);
    return wrapper;
}

} // namespace

std::wstring ScheduleModeToString(ScheduleMode mode)
{
    switch (mode)
    {
    case ScheduleMode::FixedHours:
        return L"FixedHours";
    case ScheduleMode::SunsetToSunrise:
        return L"SunsetToSunrise";
    case ScheduleMode::FollowNightLight:
        return L"FollowNightLight";
    default:
        return L"Off";
    }
}

ScheduleMode ScheduleModeFromString(const std::wstring& value)
{
    if (value == L"FixedHours")
        return ScheduleMode::FixedHours;
    if (value == L"SunsetToSunrise")
        return ScheduleMode::SunsetToSunrise;
    if (value == L"FollowNightLight")
        return ScheduleMode::FollowNightLight;
    return ScheduleMode::Off;
}

std::wstring NormalizeUiTheme(const std::wstring& value)
{
    // Trim first: hand-edited JSON easily picks up a stray space, and the exact
    // string is what ends up back in the file.
    const size_t begin = value.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos)
        return L"auto";

    const size_t end = value.find_last_not_of(L" \t\r\n");
    const std::wstring trimmed = value.substr(begin, end - begin + 1);

    if (_wcsicmp(trimmed.c_str(), L"light") == 0)
        return L"light";
    if (_wcsicmp(trimmed.c_str(), L"dark") == 0)
        return L"dark";

    // "auto" and everything unrecognised. Degrading to "auto" rather than to a
    // pinned theme means a typo cannot leave the user stuck in the wrong theme
    // with no obvious way back.
    return L"auto";
}

SettingsStore& SettingsStore::Instance()
{
    static SettingsStore instance;
    return instance;
}

SettingsStore::SettingsStore()
{
    InitializeCriticalSection(&m_lock);
    m_changedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Load();
}

SettingsStore::~SettingsStore()
{
    StopWatching();
    if (m_changedEvent)
        CloseHandle(m_changedEvent);
    if (m_cancelEvent)
        CloseHandle(m_cancelEvent);
    DeleteCriticalSection(&m_lock);
}

std::wstring SettingsStore::ResolveConfigPath()
{
    const std::wstring exeDir = ExeDirectory();

    // Green/portable first choice: right next to the executable.
    if (CanCreateFileIn(exeDir))
        return exeDir + L"\\" + kConfigFileName;

    // Fallback for the (unusual) case of running from a read-only location.
    const std::wstring localAppData = GetLocalAppDataDirectory();
    if (!localAppData.empty())
    {
        const std::wstring dir = localAppData + L"\\LightSwitchStandalone";
        if (EnsureDirectory(dir) && CanCreateFileIn(dir))
        {
            LOG_WARN(L"[Config] Executable directory is not writable; falling back to " << dir);
            return dir + L"\\" + kConfigFileName;
        }
    }

    LOG_ERROR(L"[Config] No writable location found; configuration will not persist.");
    return exeDir + L"\\" + kConfigFileName;
}

std::wstring SettingsStore::ConfigFilePath() const
{
    return m_path;
}

Config SettingsStore::Get() const
{
    EnterCriticalSection(&m_lock);
    const Config copy = m_config;
    LeaveCriticalSection(&m_lock);
    return copy;
}

std::string SettingsStore::Serialize(const Config& config) const
{
    json::Value root = json::Value::MakeObject();

    root.set("changeSystem", Wrap(json::Value(config.changeSystem)));
    root.set("changeApps", Wrap(json::Value(config.changeApps)));
    root.set("scheduleMode", Wrap(json::Value(WideToUtf8(ScheduleModeToString(config.scheduleMode)))));
    root.set("lightTime", Wrap(json::Value(config.lightTime)));
    root.set("darkTime", Wrap(json::Value(config.darkTime)));
    root.set("sunrise_offset", Wrap(json::Value(config.sunriseOffset)));
    root.set("sunset_offset", Wrap(json::Value(config.sunsetOffset)));
    root.set("latitude", Wrap(json::Value(WideToUtf8(config.latitude))));
    root.set("longitude", Wrap(json::Value(WideToUtf8(config.longitude))));

    json::Value hotkey = json::Value::MakeObject();
    hotkey.set("win", json::Value(config.hotkey.win));
    hotkey.set("ctrl", json::Value(config.hotkey.ctrl));
    hotkey.set("alt", json::Value(config.hotkey.alt));
    hotkey.set("shift", json::Value(config.hotkey.shift));
    hotkey.set("code", json::Value(static_cast<int>(config.hotkey.vk)));
    root.set("toggle-theme-hotkey", Wrap(hotkey));

    root.set("startOnLogin", Wrap(json::Value(config.startOnLogin)));
    root.set("language", Wrap(json::Value(WideToUtf8(config.language))));
    root.set("uiTheme", Wrap(json::Value(WideToUtf8(config.uiTheme))));
    root.set("trayClickTogglesTheme", Wrap(json::Value(config.trayClickTogglesTheme)));

    // Key order here is irrelevant: json::Value stores objects in a std::map, so
    // the file always comes out alphabetically sorted regardless of the order
    // these set() calls run in.
    return root.dump(2) + "\n";
}

bool SettingsStore::Deserialize(const std::string& utf8, Config& config, std::string& error) const
{
    bool parsed = false;
    const json::Value root = json::Parse(utf8, &parsed, &error);
    if (!parsed || !root.isObject())
    {
        if (error.empty())
            error = "top-level value is not an object";
        return false;
    }

    Config loaded;
    loaded.changeSystem = JsonGetBoolWrapped(root, "changeSystem", loaded.changeSystem);
    loaded.changeApps = JsonGetBoolWrapped(root, "changeApps", loaded.changeApps);
    loaded.scheduleMode = ScheduleModeFromString(JsonGetStringWrapped(root, "scheduleMode", ScheduleModeToString(loaded.scheduleMode)));
    loaded.lightTime = JsonGetIntWrapped(root, "lightTime", loaded.lightTime);
    loaded.darkTime = JsonGetIntWrapped(root, "darkTime", loaded.darkTime);
    loaded.sunriseOffset = JsonGetIntWrapped(root, "sunrise_offset", loaded.sunriseOffset);
    loaded.sunsetOffset = JsonGetIntWrapped(root, "sunset_offset", loaded.sunsetOffset);
    loaded.latitude = JsonGetStringWrapped(root, "latitude", loaded.latitude);
    loaded.longitude = JsonGetStringWrapped(root, "longitude", loaded.longitude);
    loaded.startOnLogin = JsonGetBoolWrapped(root, "startOnLogin", loaded.startOnLogin);

    // Standalone extension. Canonicalised here so "ZH-cn" and "zh" both end up
    // as "zh-CN"; an unrecognised code degrades to "auto" rather than pinning
    // whatever the system happens to be today.
    loaded.language = L10n::Normalize(JsonGetStringWrapped(root, "language", loaded.language));

    // Standalone extension, same reasoning: only "auto" / "light" / "dark" ever
    // reach the rest of the program.
    loaded.uiTheme = NormalizeUiTheme(JsonGetStringWrapped(root, "uiTheme", loaded.uiTheme));

    // Standalone extension. A settings.json written by 1.2.0 or earlier has no
    // such key, and the fallback is the struct default - which is false, i.e.
    // the safe value. An upgrade therefore never silently turns the touch-unfriendly
    // gesture back on.
    loaded.trayClickTogglesTheme = JsonGetBoolWrapped(root, "trayClickTogglesTheme", loaded.trayClickTogglesTheme);

    if (const json::Value* hotkeyEntry = root.find("toggle-theme-hotkey"); hotkeyEntry && hotkeyEntry->isObject())
    {
        if (const json::Value* hotkey = hotkeyEntry->find("value"); hotkey && hotkey->isObject())
        {
            const auto readBool = [hotkey](const char* key, bool fallback) {
                const json::Value* v = hotkey->find(key);
                return (v && v->isBool()) ? v->asBool(fallback) : fallback;
            };
            loaded.hotkey.win = readBool("win", loaded.hotkey.win);
            loaded.hotkey.ctrl = readBool("ctrl", loaded.hotkey.ctrl);
            loaded.hotkey.alt = readBool("alt", loaded.hotkey.alt);
            loaded.hotkey.shift = readBool("shift", loaded.hotkey.shift);

            if (const json::Value* code = hotkey->find("code"); code && code->isNumber())
            {
                const int vk = code->asInt(static_cast<int>(loaded.hotkey.vk));
                if (vk > 0 && vk <= 0xFF)
                    loaded.hotkey.vk = static_cast<unsigned int>(vk);
            }
        }
    }

    // Sanitise ranges so a hand-edited file can never wedge the scheduler.
    loaded.lightTime = ((loaded.lightTime % 1440) + 1440) % 1440;
    loaded.darkTime = ((loaded.darkTime % 1440) + 1440) % 1440;

    config = loaded;
    return true;
}

bool SettingsStore::Load()
{
    EnterCriticalSection(&m_lock);
    if (m_path.empty())
        m_path = ResolveConfigPath();
    const std::wstring path = m_path;
    LeaveCriticalSection(&m_lock);

    bool readOk = false;
    std::string raw = ReadFileUtf8(path, readOk);

    if (!readOk)
    {
        LOG_INFO(L"[Config] No settings file at " << path << L"; writing defaults.");
        Config defaults;
        EnterCriticalSection(&m_lock);
        m_config = defaults;
        LeaveCriticalSection(&m_lock);
        Save();
        return true;
    }

    Config loaded;
    std::string error;
    if (!Deserialize(raw, loaded, error))
    {
        // Corrupt file: keep the in-memory config untouched and report loudly.
        // We deliberately do NOT overwrite the file - the user may want to fix it.
        LOG_ERROR(L"[Config] Failed to parse " << path << L": " << Utf8ToWide(error) << L". Keeping previous settings.");
        return false;
    }

    EnterCriticalSection(&m_lock);
    m_config = loaded;
    m_lastRawContent = raw;
    LeaveCriticalSection(&m_lock);

    LOG_INFO(L"[Config] Loaded settings from " << path);
    return true;
}

bool SettingsStore::Save()
{
    Config snapshot;
    std::wstring path;

    EnterCriticalSection(&m_lock);
    snapshot = m_config;
    if (m_path.empty())
        m_path = ResolveConfigPath();
    path = m_path;
    LeaveCriticalSection(&m_lock);

    const std::string payload = Serialize(snapshot);
    if (!WriteFileUtf8Atomic(path, payload))
    {
        LOG_ERROR(L"[Config] Failed to write " << path << L" (error " << GetLastError() << L").");
        return false;
    }

    EnterCriticalSection(&m_lock);
    m_lastRawContent = payload;
    LeaveCriticalSection(&m_lock);
    return true;
}

bool SettingsStore::SetAndSave(const Config& config)
{
    EnterCriticalSection(&m_lock);
    m_config = config;
    LeaveCriticalSection(&m_lock);

    const bool saved = Save();
    if (m_changedEvent)
        SetEvent(m_changedEvent);
    return saved;
}

void SettingsStore::StartWatching()
{
    if (m_watchThread)
        return;

    ResetEvent(m_cancelEvent);
    m_watchThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
        static_cast<SettingsStore*>(param)->WatchLoop();
        return 0;
    }, this, 0, nullptr);

    if (!m_watchThread)
        LOG_WARN(L"[Config] Could not start settings file watcher.");
}

void SettingsStore::StopWatching()
{
    if (!m_watchThread)
        return;

    SetEvent(m_cancelEvent);
    WaitForSingleObject(m_watchThread, 5000);
    CloseHandle(m_watchThread);
    m_watchThread = nullptr;
}

void SettingsStore::WatchLoop()
{
    std::wstring directory;
    std::wstring fileName;

    {
        EnterCriticalSection(&m_lock);
        if (m_path.empty())
            m_path = ResolveConfigPath();
        const std::wstring path = m_path;
        LeaveCriticalSection(&m_lock);

        const auto pos = path.find_last_of(L"\\/");
        if (pos == std::wstring::npos)
        {
            LOG_WARN(L"[Config] Settings path has no directory component; watcher disabled.");
            return;
        }
        directory = path.substr(0, pos);
        fileName = path.substr(pos + 1);
    }

    HANDLE hDir = CreateFileW(directory.c_str(),
                              FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                              nullptr);
    if (hDir == INVALID_HANDLE_VALUE)
    {
        LOG_WARN(L"[Config] Cannot watch directory " << directory << L" (error " << GetLastError() << L").");
        return;
    }

    HANDLE ovEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::vector<BYTE> buffer(16 * 1024);

    LOG_INFO(L"[Config] Watching " << directory << L" for changes to " << fileName);

    bool running = true;
    while (running)
    {
        ResetEvent(ovEvent);

        OVERLAPPED ov{};
        ov.hEvent = ovEvent;

        if (!ReadDirectoryChangesW(hDir,
                                   buffer.data(),
                                   static_cast<DWORD>(buffer.size()),
                                   FALSE,
                                   FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME,
                                   nullptr,
                                   &ov,
                                   nullptr))
        {
            LOG_WARN(L"[Config] ReadDirectoryChangesW failed (error " << GetLastError() << L").");
            break;
        }

        HANDLE waits[2] = { m_cancelEvent, ovEvent };
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);

        if (wait == WAIT_OBJECT_0)
        {
            CancelIo(hDir);
            running = false;
            break;
        }

        if (wait != WAIT_OBJECT_0 + 1)
            break;

        DWORD bytes = 0;
        if (!GetOverlappedResult(hDir, &ov, &bytes, FALSE) || bytes == 0)
            continue;

        bool relevant = false;
        const BYTE* cursor = buffer.data();
        for (;;)
        {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
            const std::wstring changed(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (_wcsicmp(changed.c_str(), fileName.c_str()) == 0)
            {
                relevant = true;
                break;
            }
            if (info->NextEntryOffset == 0)
                break;
            cursor += info->NextEntryOffset;
        }

        if (!relevant)
            continue;

        // Debounce: wait until size + mtime stop moving, then compare content.
        // Content comparison is what makes our own writes self-suppressing.
        std::string stableContent;
        bool haveContent = false;

        for (int attempt = 0; attempt < 25; ++attempt)
        {
            if (WaitForSingleObject(m_cancelEvent, 200) == WAIT_OBJECT_0)
            {
                running = false;
                break;
            }

            bool ok = false;
            const std::string first = ReadFileUtf8(m_path, ok);
            if (!ok)
                continue;

            if (WaitForSingleObject(m_cancelEvent, 200) == WAIT_OBJECT_0)
            {
                running = false;
                break;
            }

            bool ok2 = false;
            const std::string second = ReadFileUtf8(m_path, ok2);
            if (ok2 && second == first)
            {
                stableContent = second;
                haveContent = true;
                break;
            }
        }

        if (!running)
            break;
        if (!haveContent)
        {
            LOG_WARN(L"[Config] Settings file did not settle; skipping reload.");
            continue;
        }

        bool isOwnWrite = false;
        {
            EnterCriticalSection(&m_lock);
            isOwnWrite = (stableContent == m_lastRawContent);
            LeaveCriticalSection(&m_lock);
        }
        if (isOwnWrite)
            continue;

        Config loaded;
        std::string error;
        if (!Deserialize(stableContent, loaded, error))
        {
            LOG_ERROR(L"[Config] Ignoring invalid settings.json: " << Utf8ToWide(error));
            continue;
        }

        EnterCriticalSection(&m_lock);
        m_config = loaded;
        m_lastRawContent = stableContent;
        LeaveCriticalSection(&m_lock);

        LOG_INFO(L"[Config] External settings change detected; notifying scheduler.");
        SetEvent(m_changedEvent);
    }

    CancelIo(hDir);
    CloseHandle(ovEvent);
    CloseHandle(hDir);
    LOG_INFO(L"[Config] Settings file watcher stopped.");
}

} // namespace ls
