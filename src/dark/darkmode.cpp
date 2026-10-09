// darkmode.cpp - Win32 深色模式「系统层」实现（可移植模块）
#include "darkmode.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <string.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "advapi32.lib")

namespace dm {
namespace {

constexpr DWORD kDwmImmersiveDark = 20;   // DWMWA_USE_IMMERSIVE_DARK_MODE

const wchar_t* kPersonalizeKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
const wchar_t* kDwmKey = L"Software\\Microsoft\\Windows\\DWM";

const wchar_t* g_prefKey = L"Software\\Win32DarkMode";

// SetPreferredAppMode 的枚举值（未文档化，序号 135）
enum PreferredAppMode {
    PAM_Default = 0, PAM_AllowDark = 1, PAM_ForceDark = 2, PAM_ForceLight = 3, PAM_Max = 4
};

// 未文档化 API 的函数类型。全部按序号解析。
// ⚠️ 返回 bool 的必须写 bool 而不是 BOOL：BOOL 是 4 字节，读 1 字节函数的高位是垃圾。
using FnSetPreferredAppMode    = PreferredAppMode (WINAPI*)(PreferredAppMode);
using FnAllowDarkModeForWindow = bool (WINAPI*)(HWND, bool);
using FnRefreshImmersiveColor  = void (WINAPI*)(void);
using FnFlushMenuThemes        = void (WINAPI*)(void);
using FnOpenNcThemeData        = HTHEME (WINAPI*)(HWND, LPCWSTR);

FnSetPreferredAppMode    g_setPreferredAppMode    = nullptr;
FnAllowDarkModeForWindow g_allowDarkModeForWindow = nullptr;
FnRefreshImmersiveColor  g_refreshImmersiveColor  = nullptr;
FnFlushMenuThemes        g_flushMenuThemes        = nullptr;
FnOpenNcThemeData        g_realOpenNcThemeData    = nullptr;

bool      g_inited       = false;
bool      g_chromeOk     = false;
bool      g_uxDarkActive = false;
AppTheme  g_userTheme    = ThemeSystem;
bool      g_dark         = false;
Palette   g_pal{};
HBRUSH    g_bgBrush      = nullptr;
HBRUSH    g_fieldBrush   = nullptr;
HFONT     g_font         = nullptr;
ApplyFn   g_applyFn      = nullptr;
PaletteFn g_paletteFn    = nullptr;

// ---- helpers ---------------------------------------------------------------

// 先按名字试，失败再按序号。Win10/11 上这些 API 都是 NONAME，实际只会走序号分支。
FARPROC ByNameOrOrdinal(HMODULE m, const char* name, WORD ord) {
    FARPROC p = GetProcAddress(m, name);
    if (!p) p = GetProcAddress(m, MAKEINTRESOURCEA(ord));
    return p;
}

DWORD ReadDword(HKEY root, const wchar_t* sub, const wchar_t* val, DWORD def) {
    HKEY k = nullptr;
    DWORD out = def, size = sizeof(out), type = 0;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return def;
    if (RegQueryValueExW(k, val, nullptr, &type,
                         reinterpret_cast<LPBYTE>(&out), &size) != ERROR_SUCCESS
        || type != REG_DWORD) {
        out = def;
    }
    RegCloseKey(k);
    return out;
}

void WriteDword(HKEY root, const wchar_t* sub, const wchar_t* val, DWORD v) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr)
        == ERROR_SUCCESS) {
        RegSetValueExW(k, val, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
        RegCloseKey(k);
    }
}

// 进程级「首选应用模式」——决定【系统自己绘制】的那部分走哪套配色：
// 弹出菜单、消息框外壳、非客户区、以及部分控件的默认主题。
//
// AllowDark(1) 只是「允许」，此时这些系统部分仍然跟随【系统】主题，
// 不跟随应用自己的深/浅选择 —— 这正是「手动切浅色、弹出菜单依旧深色」的根因。
// 想让应用的选择说话，必须显式 ForceDark(2) / ForceLight(3)。
void SetAppMode(bool dark) {
    if (!g_setPreferredAppMode) return;
    g_setPreferredAppMode(dark ? PAM_ForceDark : PAM_ForceLight);
    g_uxDarkActive = true;
}

void RebuildPalette() {
    if (g_dark) {
        g_pal.bg      = RGB(0x20, 0x20, 0x20);
        g_pal.field   = RGB(0x2B, 0x2B, 0x2B);
        g_pal.text    = RGB(0xEE, 0xEE, 0xEE);
        g_pal.textDim = RGB(0x7A, 0x7A, 0x7A);
        g_pal.border  = RGB(0x48, 0x48, 0x48);
        g_pal.hover   = RGB(0x3A, 0x3A, 0x3A);
        g_pal.pressed = RGB(0x16, 0x16, 0x16);
    } else {
        g_pal.bg      = RGB(0xF3, 0xF3, 0xF3);
        g_pal.field   = RGB(0xFF, 0xFF, 0xFF);
        g_pal.text    = RGB(0x1A, 0x1A, 0x1A);
        g_pal.textDim = RGB(0x8A, 0x8A, 0x8A);
        g_pal.border  = RGB(0xC6, 0xC6, 0xC6);
        g_pal.hover   = RGB(0xE4, 0xE4, 0xE4);
        g_pal.pressed = RGB(0xCF, 0xCF, 0xCF);
    }

    // 强调色尽量取系统实际值：HKCU\...\DWM\AccentColor 是 0xAABBGGRR，
    // 低 24 位恰好就是 COLORREF 的 0x00BBGGRR 布局。
    DWORD accent = ReadDword(HKEY_CURRENT_USER, kDwmKey, L"AccentColor", 0);
    g_pal.accent = (accent & 0x00FFFFFF) ? (accent & 0x00FFFFFF) : RGB(0x00, 0x78, 0xD7);

    if (g_paletteFn) g_paletteFn(g_dark, g_pal);   // 允许业务侧覆盖任意字段

    if (g_bgBrush)    { DeleteObject(g_bgBrush);    g_bgBrush = nullptr; }
    if (g_fieldBrush) { DeleteObject(g_fieldBrush); g_fieldBrush = nullptr; }
    g_bgBrush    = CreateSolidBrush(g_pal.bg);
    g_fieldBrush = CreateSolidBrush(g_pal.field);
}

// ---- DWM 外壳 --------------------------------------------------------------
// DWM 会缓存标题栏渲染结果，改完属性不会立刻重画，必须逼它重新合成。
// 两个手法（非最大化 / 最大化）缺一不可，这是实测出来的，不是理论推导：
//   非最大化：把 DwmExtendFrameIntoClientArea 的 top margin 抖一下 0->1->0
//   最大化：  把 WS_CAPTION 摘掉再装回去，各带一次 SWP_FRAMECHANGED
void ApplyChrome(HWND hwnd, bool dark) {
    if (!g_chromeOk || !hwnd) return;

    BOOL bDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kDwmImmersiveDark, &bDark, sizeof(bDark));

    MARGINS zero = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(hwnd, &zero);

    if (IsZoomed(hwnd)) {
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~WS_CAPTION);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_DRAWFRAME);
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_DRAWFRAME);
    } else {
        MARGINS nudge = { 0, 0, 1, 0 };
        DwmExtendFrameIntoClientArea(hwnd, &nudge);
        DwmExtendFrameIntoClientArea(hwnd, &zero);
    }
}

// ---- 滚动条 IAT hook（可选，见 darkmode.h 的说明）-------------------------
// comctl32 通过【延迟加载表】导入 uxtheme!OpenNcThemeData(序号 49)，
// 把它换成自己的实现，就能将 classList "ScrollBar" 重定向为
// "Explorer::ScrollBar"（系统内部的深色滚动条主题）。
// 注意：这是跨模块写内存，可能被 Windows Defender 拦截；失败会静默跳过。

HTHEME WINAPI HookOpenNcThemeData(HWND hWnd, LPCWSTR classList) {
    if (g_dark && classList && wcscmp(classList, L"ScrollBar") == 0) {
        hWnd = nullptr;
        classList = L"Explorer::ScrollBar";
    }
    return g_realOpenNcThemeData ? g_realOpenNcThemeData(hWnd, classList) : nullptr;
}

template <typename T, typename T1, typename T2>
T RvaToVa(T1 base, T2 rva) {
    return reinterpret_cast<T>(reinterpret_cast<ULONG_PTR>(base) + rva);
}

template <typename T>
T DirFromBase(void* moduleBase, size_t entry) {
    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
    auto* nt  = RvaToVa<PIMAGE_NT_HEADERS>(moduleBase, dos->e_lfanew);
    return RvaToVa<T>(moduleBase, nt->OptionalHeader.DataDirectory[entry].VirtualAddress);
}

} // namespace

// ---- 公开 API --------------------------------------------------------------

void PatchScrollBar() {
    if (!g_realOpenNcThemeData) return;

    HMODULE comctl = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!comctl) return;

    auto* imports = DirFromBase<PIMAGE_DELAYLOAD_DESCRIPTOR>(
        comctl, IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
    if (!imports) return;

    for (; imports->DllNameRVA; ++imports) {
        if (_stricmp(RvaToVa<LPCSTR>(comctl, imports->DllNameRVA), "uxtheme.dll") != 0)
            continue;

        auto* nameThunk = RvaToVa<PIMAGE_THUNK_DATA>(comctl, imports->ImportNameTableRVA);
        auto* addrThunk = RvaToVa<PIMAGE_THUNK_DATA>(comctl, imports->ImportAddressTableRVA);
        for (; nameThunk->u1.Ordinal; ++nameThunk, ++addrThunk) {
            if (!IMAGE_SNAP_BY_ORDINAL(nameThunk->u1.Ordinal)) continue;
            if (IMAGE_ORDINAL(nameThunk->u1.Ordinal) != 49) continue;

            DWORD oldProtect = 0;
            if (VirtualProtect(addrThunk, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                addrThunk->u1.Function =
                    reinterpret_cast<ULONG_PTR>(static_cast<FnOpenNcThemeData>(HookOpenNcThemeData));
                VirtualProtect(addrThunk, sizeof(void*), oldProtect, &oldProtect);
            }
            return;   // 只打补丁一次
        }
        return;
    }
}

bool Init() {
    if (g_inited) return g_chromeOk;
    g_inited = true;

    // 版本判定走 RtlGetNtVersionNumbers（GetVersionEx 会被 manifest 撒谎）。
    // 不做 build 白名单：17763 是深色标题栏的下限，往上都按支持处理，
    // 具体的 API 是否存在一律以 GetProcAddress 结果为准。
    DWORD major = 0, minor = 0, build = 0;
    using FnRtl = void (WINAPI*)(DWORD*, DWORD*, DWORD*);
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (auto f = reinterpret_cast<FnRtl>(GetProcAddress(nt, "RtlGetNtVersionNumbers")))
            f(&major, &minor, &build);
    }
    build &= 0x0FFFFFFF;
    g_chromeOk = (major > 10) || (major == 10 && build >= 17763);

    if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        g_setPreferredAppMode = reinterpret_cast<FnSetPreferredAppMode>(
            ByNameOrOrdinal(ux, "SetPreferredAppMode", 135));
        g_allowDarkModeForWindow = reinterpret_cast<FnAllowDarkModeForWindow>(
            ByNameOrOrdinal(ux, "AllowDarkModeForWindow", 133));
        g_refreshImmersiveColor = reinterpret_cast<FnRefreshImmersiveColor>(
            ByNameOrOrdinal(ux, "RefreshImmersiveColorPolicyState", 104));
        g_realOpenNcThemeData = reinterpret_cast<FnOpenNcThemeData>(
            ByNameOrOrdinal(ux, "OpenNcThemeData", 49));
        // 序号 136 FlushMenuThemes：清掉 uxtheme 缓存的「菜单主题句柄」。
        // 不做这一步时，只要曾在深色状态下弹出过菜单，之后再切浅色，
        // 菜单仍会命中深色缓存 —— 这是「启动即浅色正常、深色切浅色异常」的根因。
        g_flushMenuThemes = reinterpret_cast<FnFlushMenuThemes>(
            ByNameOrOrdinal(ux, "FlushMenuThemes", 136));

        // 先置 AllowDark（防建窗口时闪白），真正的模式在每次 Apply 里定。
        if (g_setPreferredAppMode) {
            g_setPreferredAppMode(PAM_AllowDark);
            g_uxDarkActive = true;
        }
        if (g_refreshImmersiveColor) g_refreshImmersiveColor();
    }

    LoadPref();
    return g_chromeOk;
}

bool ChromeSupported() { return g_chromeOk; }
bool UxDarkActive()    { return g_uxDarkActive; }
bool IsDark()          { return g_dark; }

void AllowDarkWindow(HWND hwnd, bool allow) {
    if (g_allowDarkModeForWindow && hwnd) g_allowDarkModeForWindow(hwnd, allow);
}

bool IsSystemDark() {
    return ReadDword(HKEY_CURRENT_USER, kPersonalizeKey, L"AppsUseLightTheme", 1) == 0;
}

AppTheme UserTheme() { return g_userTheme; }

void SetPrefKey(const wchar_t* subkey) {
    if (subkey && *subkey) g_prefKey = subkey;
}

void SetUserTheme(AppTheme t) {
    g_userTheme = t;
    SavePref();
}

void LoadPref() {
    DWORD v = ReadDword(HKEY_CURRENT_USER, g_prefKey, L"Theme", ThemeSystem);
    g_userTheme = (v <= ThemeSystem) ? static_cast<AppTheme>(v) : ThemeSystem;
}

void SavePref() {
    WriteDword(HKEY_CURRENT_USER, g_prefKey, L"Theme", static_cast<DWORD>(g_userTheme));
}

bool IsColorSchemeChange(LPARAM lParam) {
    return lParam != 0
        && CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam), -1,
                                L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL;
}

HFONT UiFont() {
    if (!g_font) {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        else
            g_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    return g_font;
}

const Palette& Colors() { return g_pal; }
HBRUSH BgBrush()        { return g_bgBrush; }
HBRUSH FieldBrush()     { return g_fieldBrush; }

void OnApply(ApplyFn fn)      { g_applyFn = fn; }
void OnPalette(PaletteFn fn)  { g_paletteFn = fn; }

void Apply(HWND top) {
    bool dark = (g_userTheme == ThemeDark) ? true
              : (g_userTheme == ThemeLight) ? false
              : IsSystemDark();
    g_dark = dark;
    RebuildPalette();
    SetAppMode(dark);                 // 系统绘制的部分（弹出菜单等）跟着应用选择走

    if (g_refreshImmersiveColor) g_refreshImmersiveColor();
    if (g_flushMenuThemes) g_flushMenuThemes();   // 清菜单主题缓存，顺序必须在刷新策略之后
    if (g_allowDarkModeForWindow && top) g_allowDarkModeForWindow(top, dark);

    ApplyChrome(top, dark);
    if (g_applyFn) g_applyFn(top, dark, g_pal);   // 界面层在这里铺开控件

    if (top) {
        RedrawWindow(top, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        DrawMenuBar(top);
    }
}

} // namespace dm
