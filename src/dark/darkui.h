// darkui.h - Win32 深色模式「界面层」（可移植模块）
//
// 职责边界：把一棵已有的控件树变成深色。它【不创建、不布局、不改 ID】，
//          只按类名识别控件并接管它们的绘制。
//
// 与 darkmode.h 的分工：
//   darkmode.h -> 系统层（DWM 外壳 / uxtheme 开关 / 调色板 / 偏好）
//   darkui.h   -> 界面层（每个控件到底怎么画）
//
// 为什么需要这一层：Windows 只为一小部分控件提供了深色资源。
//   有深色资源：非客户区标题栏、系统弹出菜单、ListView（ItemsView 主题类）
//   没有深色资源（必须自己画）：Static / Edit / ComboBox / Button /
//       CheckBox / Radio / GroupBox / 菜单栏本体 / 状态栏 / 各类滚动条 / 下拉按钮
//
// 三行接入（详见 assets/templates/integration-snippet.cpp）：
//   dm::Init();                        // 建窗口前
//   dm::OnApply(darkui::Apply);        // 注册界面层
//   darkui::Install(hwnd);             // 控件建好后；之后新建控件再调一次
// 再把 WM_DRAWITEM / WM_MEASUREITEM / WM_NOTIFY / WM_CTLCOLOR* 转发给本模块。

#pragma once
#include <windows.h>
#include "darkmode.h"

namespace darkui {

// ---- 安装 ------------------------------------------------------------------

// 遍历 top 的整棵控件树，按类名给需要自绘的控件挂子类过程（subclass）。
// 幂等：已安装过的不会重复挂。控件建好之后调；之后动态新建了控件（或者
// 对话框刚创建完），对相应父窗口再调一次即可。
void Install(HWND top);

// ---- 应用主题 --------------------------------------------------------------

// 把所有控件刷成 dark 形态并请求重绘。
// 签名与 dm::ApplyFn 一致，可直接 dm::OnApply(darkui::Apply)。
void Apply(HWND top, bool dark, const dm::Palette& pal);

// ---- 宿主窗口过程需要转发的消息 --------------------------------------------
// 这几个函数都返回 true 表示「已处理」，宿主应立刻返回 *result。
// 参数就是宿主收到的对应消息的 wParam / lParam。

// WM_DRAWITEM —— 菜单栏自绘 + 宿主自建的 BS_OWNERDRAW 按钮
bool HandleDrawItem(LPARAM lParam, LRESULT* result);

// WM_MEASUREITEM —— 菜单栏项尺寸测量
bool HandleMeasureItem(LPARAM lParam, LRESULT* result);

// WM_NOTIFY —— ListView 表头 NM_CUSTOMDRAW、TreeView / StatusBar 文字色
bool HandleNotify(LPARAM lParam, LRESULT* result);

// WM_CTLCOLORSTATIC / DLG / BTN / EDIT / LISTBOX —— Static 与 Edit 的配色来源。
// 注意：ComboBox 的这三个消息是发给【它自己】的、不会转发到父窗口，
//      本模块已经在 ComboBox 自己的子类过程里拦掉了。
bool HandleCtlColor(UINT msg, WPARAM wParam, LRESULT* result);

// ---- 菜单栏 -----------------------------------------------------------------
// 菜单栏本体（那条横向的条 + 右侧空白区）没有深色资源，要分两块处理：
//   1) 菜单【项】      -> 转成 MF_OWNERDRAW，由本模块自绘
//   2) 菜单项右侧空白条 -> 系统用 COLOR_MENU 画，必须用 SetMenuInfo 指定背景刷
// 两步缺一就得到「深色菜单项 + 一条白色长条」的割裂效果。

// 把 bar 的所有顶层项转成 MF_OWNERDRAW（用它们当前的文本做标签）。
// 建完菜单、SetMenu(hwnd, bar) 之后调一次。
void OwnerdrawMenuBar(HWND top);

// 刷新菜单栏空白条的背景刷。dark 变化时调（dm::Apply 的时机就够）。
void ApplyMenuBar(HWND top, bool dark, const dm::Palette& pal);

// ---- 可选：宿主自建的 BS_OWNERDRAW 按钮 ------------------------------------
// 自己画一个圆角深色按钮（框架里 set 了 BS_OWNERDRAW 的按钮由父窗口收
// WM_DRAWITEM，本模块代为绘制）。不需要就别用。
void SetOwnedButtonCornerRadius(int px);

} // namespace darkui
