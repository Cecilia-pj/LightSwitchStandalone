// darkmode.h - Win32 深色模式「系统层」（可移植模块）
//
// 职责边界：只管系统层 —— DWM 窗口外壳、uxtheme 未文档化开关、系统主题侦测、
//          调色板、偏好持久化。**不碰任何控件**；控件的具体画法在 darkui.h/.cpp。
//
// 依赖：dwmapi.lib / uxtheme.lib / advapi32.lib（user32.lib 由 windows.h 隐含）
// 适用：Windows 10 1809 (build 17763) 及以上，含 Windows 11。
//
// 实测依据（Windows 11 build 26300，用 dumpbin 读导出表 + 探针实调验证）：
//   * uxtheme 的深色 API 仍然【只按序号导出】，按名字 GetProcAddress 一定失败。
//     序号表与 Win10 1809/1903 完全一致：132/133/135/136/137/138/139/49/104/106。
//   * 签名必须用 bool(1 字节)，不能写 BOOL(4 字节)；否则返回值高字节是垃圾。
//   * SetPreferredAppMode(AllowDark) 只是「允许」，此时系统自己绘制的那部分
//     （弹出菜单、消息框外壳、非客户区）仍跟随【系统】主题，不跟随应用选择。
//     要让应用的选择说话，必须每次 Apply 时显式 ForceDark(2)/ForceLight(3)。
//   * 切模式后必须调 FlushMenuThemes(序号 136) 清 uxtheme 缓存的菜单主题句柄，
//     否则「先深色弹出过菜单 → 再切浅色」时菜单会命中旧缓存仍是深色。
//   * DarkMode_* 主题类（DarkMode_Explorer / DarkMode_CFD / DarkMode_ItemsView /
//     DarkMode_ScrollBar）【全部不存在】；唯一真实存在的深色主题类是 ItemsView。

#pragma once
#include <windows.h>

namespace dm {

// 用户选择：强制深色 / 强制浅色 / 跟随系统
enum AppTheme { ThemeDark = 0, ThemeLight = 1, ThemeSystem = 2 };

// 调色板。要换成自己的品牌色，用 OnPalette() 挂个钩子改，别改这份源码。
struct Palette {
    COLORREF bg;        // 窗口/对话框底
    COLORREF field;     // 输入类控件填充
    COLORREF text;      // 主文字
    COLORREF textDim;   // 禁用/次要文字
    COLORREF border;    // 描边
    COLORREF accent;    // 选中/勾选/强调
    COLORREF hover;     // 悬停
    COLORREF pressed;   // 按下
};

// ---- 生命周期 --------------------------------------------------------------

// 进程启动时调一次，越早越好，【必须在创建任何窗口之前】。
// 内部会做：探测系统版本 -> 解析 uxtheme 序号 -> 先置 AllowDark 防初始闪白 ->
//          读注册表偏好。返回是否支持 DWM 深色标题栏。
bool Init();

// 可选：让滚动条也变深。做法是 IAT hook comctl32 延迟加载表里的
// uxtheme!OpenNcThemeData(序号 49)，把 "ScrollBar" 重定向到 "Explorer::ScrollBar"。
// 因为是跨模块写内存，【可能被 Windows Defender 拦截】；失败会静默跳过。
// 注意：它只影响走 uxtheme 的滚动条；WS_VSCROLL 非客户区滚动条由 user32 画，
//      不受影响（那个要 darkui 自己重画）。需要 INITCOMMONCONTROLSEX 之后再调。
void PatchScrollBar();

// ---- 主题解析与应用 --------------------------------------------------------

using ApplyFn   = void (*)(HWND top, bool dark, const Palette& pal);
using PaletteFn = void (*)(bool dark, Palette& pal);

// 注册界面层回调。**漏调这一行的后果是完全静默的**：系统层一切正常
// （标题栏变深、调色板变深），但控件毫无变化，且不报任何错。
void OnApply(ApplyFn fn);

// 注册调色板微调钩子（可选）。在 RebuildPalette 之后调用，可覆盖任意字段。
void OnPalette(PaletteFn fn);

// 解析当前应有的深浅 -> 重建调色板 -> 设应用模式 -> 刷系统策略 -> 调回调 -> 重绘。
// 切换主题、收到 WM_SETTINGCHANGE/WM_THEMECHANGED 时都调它。
void Apply(HWND top);

// ---- 查询 ------------------------------------------------------------------

bool  ChromeSupported();   // 系统是否支持 DWM 深色标题栏（build >= 17763）
bool  UxDarkActive();      // 是否成功设置过 uxtheme 应用模式
bool  IsDark();            // 当前生效的深浅
bool  IsSystemDark();      // 读注册表 AppsUseLightTheme（0 = 系统是深色）
AppTheme UserTheme();      // 用户选择

// ---- 设置 ------------------------------------------------------------------

void SetUserTheme(AppTheme t);   // 会立即写入注册表偏好
void LoadPref();
void SavePref();

// 偏好注册表位置，默认 L"Software\\Win32DarkMode"（HKCU 下）。
// 多程序共存时换成自己的子键。
void SetPrefKey(const wchar_t* subkey);

// 单窗口放行：AllowDarkModeForWindow(序号 133)。每个想变深的 HWND 都要单独放行。
// 顶层窗口由 Apply() 自动处理；子控件由 darkui 负责调用。
void AllowDarkWindow(HWND hwnd, bool allow);

// 判断 WM_SETTINGCHANGE 的 lParam 是否为换配色（"ImmersiveColorSet"）
bool IsColorSchemeChange(LPARAM lParam);

// ---- 资源 ------------------------------------------------------------------

HFONT          UiFont();      // 系统消息字体（惰性创建，进程级共享）
const Palette& Colors();
HBRUSH         BgBrush();     // Colors().bg 对应的刷子，随主题重建
HBRUSH         FieldBrush();  // Colors().field 对应的刷子

} // namespace dm
