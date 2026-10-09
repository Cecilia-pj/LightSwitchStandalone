// darkui.cpp - Win32 深色模式「界面层」实现（可移植模块）
//
// 每个控件为什么要单独处理（都实测过，不是照抄文档）：
//
//   控件            手段                                  为什么不能用更简单的办法
//   --------------  ------------------------------------  ----------------------------------
//   Static          WM_CTLCOLORSTATIC（父窗口收）          无深色资源
//   Edit            WM_CTLCOLOREDIT + 剥掉视觉样式         带视觉样式时拒绝颜色消息
//                   + WM_NCPAINT 重画非客户区滚动条         滚动条是 user32 画的，不过 uxtheme
//   ComboBox        WM_CTLCOLOR*（发给它自己！）            父窗口收不到这三个消息
//                   + WM_PAINT 覆盖下拉按钮/外框            按钮用 COLOR_BTNFACE 画，不吃颜色消息
//   Button          有资源，跟着走即可                        —
//   Button(BS_GROUPBOX)     子类化 WM_PAINT 自绘框+标题      无深色资源
//   CheckBox/Radio  子类化 WM_PAINT（保留 BS_AUTO* 行为）    不能用 BS_OWNERDRAW，见下方注释
//   ListView        主题类 ItemsView + 表头 NM_CUSTOMDRAW    表头文字不会自动跟随
//   TreeView        主题类 ItemsView + NM_CUSTOMDRAW         同上
//   StatusBar       整条自绘（子类化 WM_PAINT）              带视觉样式时 SB_SETBKCOLOR 被忽略
//   菜单栏本体      菜单项 MF_OWNERDRAW + SetMenuInfo        两层都没有深色资源
//
// 三处「系统色泄漏」是这套方案必须自己补的地方，且都是非客户区/系统绘制：
//   1. 菜单栏右侧空白条   -> SetMenuInfo(MIM_BACKGROUND)
//   2. Edit 的 WS_VSCROLL -> WM_NCPAINT 里 GetScrollBarInfo + GetWindowDC 重画
//   3. ComboBox 下拉按钮  -> WM_PAINT 之后用 GetWindowDC 覆盖

#include "darkui.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <string.h>
#include <memory>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace darkui {
namespace {

// 子类化标识：同一控件的不同接管点用不同 id
const UINT_PTR kSubGroupBox = 1;
const UINT_PTR kSubCheckBox = 2;
const UINT_PTR kSubCombo    = 3;
const UINT_PTR kSubStatus   = 4;
const UINT_PTR kSubEditNc   = 5;

int g_btnRadiusPx = 8;

// ---- DPI -------------------------------------------------------------------
// 按窗口取 DPI（GetDpiForWindow 动态解析，老系统退回 96），比全局常量稳。
int DpiOf(HWND h) {
    using FnGetDpiForWindow = UINT (WINAPI*)(HWND);
    static FnGetDpiForWindow fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (HMODULE u = GetModuleHandleW(L"user32.dll"))
            fn = reinterpret_cast<FnGetDpiForWindow>(GetProcAddress(u, "GetDpiForWindow"));
    }
    if (fn && h) {
        UINT d = fn(h);
        if (d >= 72) return static_cast<int>(d);
    }
    return 96;
}

int S(HWND h, int v) { return MulDiv(v, DpiOf(h), 96); }

COLORREF ContrastText(COLORREF bg) {
    int lum = (GetRValue(bg) * 299 + GetGValue(bg) * 587 + GetBValue(bg) * 114) / 1000;
    return lum > 140 ? RGB(0, 0, 0) : RGB(255, 255, 255);
}

bool AlreadySubclassed(HWND h, SUBCLASSPROC p, UINT_PTR id) {
    DWORD_PTR ref = 0;
    return GetWindowSubclass(h, p, id, &ref) != FALSE;
}

void SubOnce(HWND h, SUBCLASSPROC p, UINT_PTR id) {
    if (!AlreadySubclassed(h, p, id)) SetWindowSubclass(h, p, id, 0);
}

// ---- 菜单栏 -----------------------------------------------------------------
// 菜单项标签的生命周期必须 >= 菜单的生命周期，所以放在一个进程级容器里。
// 每次对同一个菜单栏重复调用 OwnerdrawMenuBar 是幂等的（owner-draw 项取不到文本会跳过）。
struct OwnedLabel { std::vector<std::unique_ptr<wchar_t[]>> storage; };
OwnedLabel g_labels;
HBRUSH     g_menuBrush = nullptr;

void DrawMenuBarItem(const DRAWITEMSTRUCT* dis, const wchar_t* text) {
    if (!dis || !text) return;
    const dm::Palette& c = dm::Colors();
    bool hot      = (dis->itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
    bool disabled = (dis->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;

    HBRUSH br = CreateSolidBrush(hot ? c.hover : c.bg);
    FillRect(dis->hDC, &dis->rcItem, br);
    DeleteObject(br);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, disabled ? c.textDim : c.text);
    HFONT old = static_cast<HFONT>(SelectObject(dis->hDC, dm::UiFont()));
    RECT rc = dis->rcItem;
    DrawTextW(dis->hDC, text, -1, &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dis->hDC, old);
}

// 自绘的圆角按钮（宿主 set 了 BS_OWNERDRAW 的按钮会把 WM_DRAWITEM 转过来）
void DrawOwnedButton(const DRAWITEMSTRUCT* dis) {
    const dm::Palette& c = dm::Colors();
    HDC  dc = dis->hDC;
    RECT rc = dis->rcItem;
    HWND hw  = dis->hwndItem;

    bool pressed   = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled  = (dis->itemState & ODS_DISABLED) != 0;
    bool focused   = (dis->itemState & ODS_FOCUS) != 0;
    bool isDefault = (dis->itemState & ODS_DEFAULT) != 0;

    COLORREF fill = disabled ? c.bg : (pressed ? c.pressed : c.field);
    COLORREF edge = disabled ? c.textDim : ((isDefault || focused) ? c.accent : c.border);

    HBRUSH br = CreateSolidBrush(fill);
    HPEN   pn = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pn);
    int r = S(hw, g_btnRadiusPx);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pn);

    wchar_t text[128] = { 0 };
    GetWindowTextW(hw, text, 128);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? c.textDim : c.text);
    HFONT old = static_cast<HFONT>(SelectObject(dc, dm::UiFont()));
    RECT tr = rc;
    if (pressed) OffsetRect(&tr, 0, S(hw, 1));
    DrawTextW(dc, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

// ---- GroupBox ---------------------------------------------------------------

LRESULT CALLBACK GroupBoxProc(HWND h, UINT msg, WPARAM w, LPARAM l,
                              UINT_PTR id, DWORD_PTR) {
    const dm::Palette& c = dm::Colors();
    switch (msg) {
    case WM_ERASEBKGND: {
        // 父窗口通常带 WS_CLIPCHILDREN，擦背景时会【跳过子窗口区域】；
        // 而 GroupBox 自身声明「不擦背景」，那块区域于是从未被绘制过 -> 露白。
        // 所以自绘子控件必须自己铺底。
        HBRUSH bg = dm::BgBrush();
        if (!bg) return 1;
        HDC dc = reinterpret_cast<HDC>(w);
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, bg);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (HBRUSH bg = dm::BgBrush()) FillRect(dc, &rc, bg);

        int top = rc.top + S(h, 8);
        HPEN pen = CreatePen(PS_SOLID, 1, c.border);
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        int r = S(h, 8);
        RoundRect(dc, rc.left + 1, top, rc.right - 1, rc.bottom - 1, r, r);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);

        wchar_t txt[128] = { 0 };
        GetWindowTextW(h, txt, 128);
        if (txt[0]) {
            SIZE sz{ 0, 0 };
            HFONT old = static_cast<HFONT>(SelectObject(dc, dm::UiFont()));
            GetTextExtentPoint32W(dc, txt, static_cast<int>(wcslen(txt)), &sz);
            // 用底色不透明地写标题，视觉上「切断」边框线
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, c.bg);
            SetTextColor(dc, c.text);
            RECT tr = { rc.left + S(h, 12), rc.top,
                        rc.left + S(h, 12) + sz.cx + S(h, 4), top + S(h, 8) };
            DrawTextW(dc, txt, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            SelectObject(dc, old);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, GroupBoxProc, id);
        break;
    }
    return DefSubclassProc(h, msg, w, l);
}

// ---- CheckBox / RadioButton ------------------------------------------------
// ⚠️ 不能用 BS_OWNERDRAW 做深色复选框：
//    BS_OWNERDRAW(0xB) 和 BS_AUTOCHECKBOX(0x3) 占同一个 style 位段，加了就丢失自动勾选。
//    正确做法：保留 BS_AUTO* 行为，只子类化接管 WM_PAINT。

LRESULT CALLBACK CheckRadioProc(HWND h, UINT msg, WPARAM w, LPARAM l,
                                UINT_PTR id, DWORD_PTR) {
    const dm::Palette& c = dm::Colors();
    switch (msg) {
    case WM_ERASEBKGND: {
        HBRUSH bg = dm::BgBrush();
        if (!bg) return 1;
        HDC dc = reinterpret_cast<HDC>(w);
        RECT rc;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, bg);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (HBRUSH bg = dm::BgBrush()) FillRect(dc, &rc, bg);

        bool disabled = !IsWindowEnabled(h);
        bool checked  = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
        bool focused  = GetFocus() == h;
        LONG st = GetWindowLongW(h, GWL_STYLE) & 0xF;
        bool isRadio = (st == BS_AUTORADIOBUTTON || st == BS_RADIOBUTTON);

        int box = S(h, 14);
        int cy  = rc.top + (rc.bottom - rc.top) / 2;
        int x0  = rc.left + S(h, 1);
        RECT g  = { x0, cy - box / 2, x0 + box, cy + box / 2 };

        HBRUSH gb = CreateSolidBrush(checked ? c.accent : c.field);
        HPEN   gp = CreatePen(PS_SOLID, 1, disabled ? c.textDim : c.border);
        HGDIOBJ ob = SelectObject(dc, gb);
        HGDIOBJ op = SelectObject(dc, gp);
        int rr = S(h, 4);
        if (isRadio) Ellipse(dc, g.left, g.top, g.right, g.bottom);
        else         RoundRect(dc, g.left, g.top, g.right, g.bottom, rr, rr);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(gb);
        DeleteObject(gp);

        if (checked) {
            COLORREF mk = ContrastText(c.accent);
            HBRUSH mb = CreateSolidBrush(mk);
            HPEN   mp = CreatePen(PS_SOLID, S(h, 2), mk);
            HGDIOBJ ob2 = SelectObject(dc, mb);
            HGDIOBJ op2 = SelectObject(dc, mp);
            if (isRadio) {
                int in = (g.right - g.left) / 4;
                Ellipse(dc, g.left + in, g.top + in, g.right - in, g.bottom - in);
            } else {
                HGDIOBJ nb = SelectObject(dc, GetStockObject(NULL_BRUSH));
                int w2 = g.right - g.left, h2 = g.bottom - g.top;
                POINT pts[3] = {
                    { g.left + w2 * 20 / 100, g.top + h2 * 52 / 100 },
                    { g.left + w2 * 42 / 100, g.top + h2 * 74 / 100 },
                    { g.left + w2 * 80 / 100, g.top + h2 * 28 / 100 }
                };
                Polyline(dc, pts, 3);
                SelectObject(dc, nb);
            }
            SelectObject(dc, ob2);
            SelectObject(dc, op2);
            DeleteObject(mb);
            DeleteObject(mp);
        }

        wchar_t txt[256] = { 0 };
        GetWindowTextW(h, txt, 256);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, disabled ? c.textDim : c.text);
        HFONT old = static_cast<HFONT>(SelectObject(dc, dm::UiFont()));
        RECT tr = rc;
        tr.left = g.right + S(h, 6);
        DrawTextW(dc, txt, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(dc, old);

        if (focused) {
            RECT fr = rc;
            InflateRect(&fr, -S(h, 1), -S(h, 1));
            DrawFocusRect(dc, &fr);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, CheckRadioProc, id);
        break;
    }
    LRESULT r = DefSubclassProc(h, msg, w, l);
    if (msg == WM_LBUTTONUP || msg == WM_KEYUP)
        InvalidateRect(h, nullptr, FALSE);
    return r;
}

// ---- ComboBox ---------------------------------------------------------------
// 关键：WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX / WM_CTLCOLORSTATIC 是发给
// ComboBox【自己】的，不会转发给父窗口。所以必须在 ComboBox 上拦。
//
// 另一个实测结论：ComboBox【必须 SetWindowTheme(h, L"", L"") 剥掉主题】才会变深；
// 带主题时它拒绝颜色消息，维持纯白。剥掉之后左侧文本区可上色，但
// 【右侧下拉按钮 + 1px 外框】仍是控件自己用系统色画的，得在默认绘制后覆盖。

void PaintComboChrome(HWND h) {
    RECT wr;
    GetWindowRect(h, &wr);
    int w  = wr.right - wr.left;
    int hh = wr.bottom - wr.top;
    if (w <= 0 || hh <= 0) return;

    const dm::Palette& c = dm::Colors();
    bool dark = dm::IsDark();
    int bw = GetSystemMetrics(SM_CXVSCROLL);

    HDC dc = GetWindowDC(h);
    COLORREF face = dark ? c.bg     : GetSysColor(COLOR_BTNFACE);
    COLORREF line = dark ? c.border : GetSysColor(COLOR_WINDOWFRAME);
    COLORREF mark = dark ? c.text   : GetSysColor(COLOR_BTNTEXT);

    RECT ar = { w - bw - 1, 1, w - 1, hh - 1 };     // 下拉按钮
    HBRUSH ab = CreateSolidBrush(face);
    FillRect(dc, &ar, ab);
    DeleteObject(ab);

    RECT dv = { ar.left - 1, ar.top, ar.left, ar.bottom };   // 按钮分隔线
    HBRUSH db = CreateSolidBrush(line);
    FillRect(dc, &dv, db);
    DeleteObject(db);

    int mx = (ar.left + ar.right) / 2;
    int my = (ar.top + ar.bottom) / 2;
    int s  = S(h, 4) < 3 ? 3 : S(h, 4);
    POINT tri[3] = { { mx - s, my - s / 2 }, { mx + s, my - s / 2 }, { mx, my + s / 2 } };
    HBRUSH pb = CreateSolidBrush(mark);
    HPEN   pp = CreatePen(PS_SOLID, 1, mark);
    HGDIOBJ ob = SelectObject(dc, pb), op = SelectObject(dc, pp);
    Polygon(dc, tri, 3);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(pb); DeleteObject(pp);

    RECT fr = { 0, 0, w, hh };                       // 1px 外框
    HBRUSH fb = CreateSolidBrush(line);
    FrameRect(dc, &fr, fb);
    DeleteObject(fb);

    ReleaseDC(h, dc);
}

LRESULT CALLBACK ComboProc(HWND h, UINT msg, WPARAM w, LPARAM l,
                           UINT_PTR id, DWORD_PTR) {
    switch (msg) {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w);
        SetBkColor(dc, dm::Colors().field);
        SetTextColor(dc, dm::Colors().text);
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(dm::FieldBrush());
    }
    case WM_PAINT:
    case WM_NCPAINT: {
        LRESULT r = DefSubclassProc(h, msg, w, l);
        PaintComboChrome(h);
        return r;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, ComboProc, id);
        break;
    }
    return DefSubclassProc(h, msg, w, l);
}

// ---- StatusBar --------------------------------------------------------------
// 实测：带视觉样式的 StatusBar 会【忽略 SB_SETBKCOLOR】，底色永远是主题灰。
// 所以不能靠改色消息，只能整条自己画：背景 + 顶部分隔线 + 分区文本 + 尺寸抓手。

LRESULT CALLBACK StatusProc(HWND h, UINT msg, WPARAM w, LPARAM l,
                            UINT_PTR id, DWORD_PTR) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;                       // 全部在 WM_PAINT 里画
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        const dm::Palette& c = dm::Colors();

        HBRUSH bg = CreateSolidBrush(c.bg);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        RECT ln = { rc.left, rc.top, rc.right, rc.top + 1 };    // 顶部分隔线
        HBRUSH lb = CreateSolidBrush(c.border);
        FillRect(dc, &ln, lb);
        DeleteObject(lb);

        // 右下角尺寸抓手（3 个斜点），替代被覆盖掉的主题抓手
        if (GetWindowLongW(GetParent(h), GWL_STYLE) & WS_THICKFRAME) {
            for (int i = 0; i < 3; ++i) {
                int x = rc.right - S(h, 4) - i * S(h, 4);
                int y = rc.bottom - S(h, 4) - i * S(h, 4);
                for (int d = 0; d < 2; ++d)
                    SetPixelV(dc, x + d, y - d, c.textDim);
            }
        }

        // 文本：这里只画第 0 个分区
        wchar_t buf[1024] = { 0 };
        LRESULT r = SendMessageW(h, SB_GETTEXTW, 0, reinterpret_cast<LPARAM>(buf));
        if (buf[0]) {
            int len = static_cast<int>(r & 0xFFFF);
            SIZE sz{ 0, 0 };
            HFONT old = static_cast<HFONT>(SelectObject(dc, dm::UiFont()));
            GetTextExtentPoint32W(dc, buf, len, &sz);
            int ty = rc.top + ((rc.bottom - rc.top) - sz.cy) / 2;
            int tx = rc.left + S(h, 12);
            RECT tr = { tx, ty, rc.right - S(h, 24), ty + sz.cy };
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, c.text);
            DrawTextW(dc, buf, len, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, old);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, StatusProc, id);
        break;
    }
    return DefSubclassProc(h, msg, w, l);
}

// ---- 非客户区竖直滚动条 -----------------------------------------------------
// WS_VSCROLL 是【非客户区】滚动条，由 user32 用系统颜色（COLOR_SCROLLBAR 等）绘制，
// 完全不经过 uxtheme —— 所以 PatchScrollBar() 的 IAT hook 对它无效，
// 剥掉视觉样式后必然是经典浅色。这里在默认 NC 绘制之后自己重画一遍。

void PaintDarkVScroll(HWND h) {
    if (!dm::IsDark()) return;
    if (!(GetWindowLongW(h, GWL_STYLE) & WS_VSCROLL)) return;

    SCROLLBARINFO sbi{};
    sbi.cbSize = sizeof(sbi);
    if (!GetScrollBarInfo(h, OBJID_VSCROLL, &sbi)) return;
    if (sbi.rgstate[0] & STATE_SYSTEM_INVISIBLE) return;

    // rcScrollBar 是屏幕坐标；GetWindowDC 的原点在窗口左上角，需要映射到窗口坐标
    POINT pts[2] = { { sbi.rcScrollBar.left,  sbi.rcScrollBar.top },
                     { sbi.rcScrollBar.right, sbi.rcScrollBar.bottom } };
    MapWindowPoints(HWND_DESKTOP, h, pts, 2);
    RECT sr = { pts[0].x, pts[0].y, pts[1].x, pts[1].y };
    if (sr.right <= sr.left || sr.bottom <= sr.top) return;

    const dm::Palette& c = dm::Colors();
    bool dis = (sbi.rgstate[0] & STATE_SYSTEM_UNAVAILABLE) != 0;   // 本机 SDK 无 STATE_SYSTEM_DISABLED

    HDC dc = GetWindowDC(h);

    HBRUSH tb = CreateSolidBrush(c.field);          // 轨道
    FillRect(dc, &sr, tb);
    DeleteObject(tb);

    int w   = sr.right - sr.left;
    int box = w < GetSystemMetrics(SM_CXVSCROLL) ? w : GetSystemMetrics(SM_CXVSCROLL);

    auto arrow = [&](int yTop, bool up) {
        RECT ar = { sr.left, yTop, sr.right, yTop + box };
        HBRUSH ab = CreateSolidBrush(dis ? c.field : c.bg);
        FillRect(dc, &ar, ab);
        DeleteObject(ab);
        int m = box / 4;
        POINT tri[3];
        if (up) {
            tri[0] = { sr.left + box / 2, yTop + m };
            tri[1] = { sr.left + m,       yTop + box - m };
            tri[2] = { sr.right - m,      yTop + box - m };
        } else {
            tri[0] = { sr.left + box / 2, yTop + box - m };
            tri[1] = { sr.left + m,       yTop + m };
            tri[2] = { sr.right - m,      yTop + m };
        }
        COLORREF ac = dis ? c.textDim : c.text;
        HBRUSH pb = CreateSolidBrush(ac);
        HPEN   pp = CreatePen(PS_SOLID, 1, ac);
        HGDIOBJ ob = SelectObject(dc, pb), op = SelectObject(dc, pp);
        Polygon(dc, tri, 3);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(pb); DeleteObject(pp);
    };
    arrow(sr.top, true);
    arrow(sr.bottom - box, false);

    // 滑块：用 SCROLLINFO 自己算位置，语义比 SCROLLBARINFO.xyThumb* 明确
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_ALL;
    if (GetScrollInfo(h, SB_VERT, &si)) {
        int trackTop = sr.top + box;
        int trackLen = (sr.bottom - box) - trackTop;
        if (trackLen > 0) {
            int range = si.nMax - si.nMin + 1;
            int page  = si.nPage ? static_cast<int>(si.nPage) : 1;
            int thumb = range > 0 ? MulDiv(trackLen, page, range) : trackLen;
            if (thumb < S(h, 28)) thumb = S(h, 28);
            if (thumb > trackLen) thumb = trackLen;
            int maxPos = range - page;
            int pos    = si.nPos - si.nMin;
            int top    = maxPos > 0 ? trackTop + MulDiv(trackLen - thumb, pos, maxPos)
                                    : trackTop;
            RECT tr = { sr.left + 1, top, sr.right - 1, top + thumb };
            HBRUSH hb = CreateSolidBrush(c.border);
            FillRect(dc, &tr, hb);
            DeleteObject(hb);
        }
    }
    ReleaseDC(h, dc);
}

LRESULT CALLBACK EditNcProc(HWND h, UINT msg, WPARAM w, LPARAM l,
                            UINT_PTR id, DWORD_PTR) {
    switch (msg) {
    case WM_NCPAINT: {
        LRESULT r = DefSubclassProc(h, msg, w, l);
        PaintDarkVScroll(h);
        return r;
    }
    case WM_VSCROLL:
    case WM_MOUSEWHEEL:
    case WM_KEYUP:
    case WM_LBUTTONUP:
    case WM_SIZE: {
        LRESULT r = DefSubclassProc(h, msg, w, l);
        RedrawWindow(h, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        return r;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, EditNcProc, id);
        break;
    }
    return DefSubclassProc(h, msg, w, l);
}

// ---- ListView / TreeView ----------------------------------------------------

void ThemeListView(HWND h, bool dark, const dm::Palette& c) {
    HWND hdr = ListView_GetHeader(h);

    // ItemsView 是【唯一】真实存在的深色主题类（已用 OpenThemeData 逐类实测）。
    // 浅色时【必须传 nullptr 复位】，不能传 L"Explorer" —— "Explorer" 主题类会让
    // 滚动条用内部的 Explorer::ScrollBar（正是深色那套，也是 IAT hook 的重定向目标），
    // 结果浅色界面里留下一条纯黑滚动条。
    SetWindowTheme(h, dark ? L"ItemsView" : nullptr, nullptr);
    if (hdr) SetWindowTheme(hdr, dark ? L"ItemsView" : nullptr, nullptr);
    dm::AllowDarkWindow(h, dark);
    if (hdr) dm::AllowDarkWindow(hdr, dark);

    ListView_SetBkColor(h, dark ? c.field : RGB(255, 255, 255));
    ListView_SetTextBkColor(h, dark ? c.field : RGB(255, 255, 255));
    ListView_SetTextColor(h, dark ? c.text : RGB(0, 0, 0));

    InvalidateRect(h, nullptr, TRUE);
    if (hdr) InvalidateRect(hdr, nullptr, TRUE);
}

void ThemeTreeView(HWND h, bool dark, const dm::Palette& c) {
    SetWindowTheme(h, dark ? L"ItemsView" : nullptr, nullptr);   // 同上：浅色用 nullptr 复位
    dm::AllowDarkWindow(h, dark);
    TreeView_SetBkColor(h, dark ? c.field : RGB(255, 255, 255));
    TreeView_SetTextColor(h, dark ? c.text : RGB(0, 0, 0));
    TreeView_SetLineColor(h, dark ? c.border : RGB(0xA0, 0xA0, 0xA0));
    InvalidateRect(h, nullptr, TRUE);
}

// ListView 表头：ItemsView 下底色是深的，但表头文字仍按浅色主题画成黑色，必须接管。
// 这里连底色一起自己画，避免主题色和我们的深色打架。
LRESULT HeaderCustomDraw(LPNMCUSTOMDRAW cd) {
    if (!dm::IsDark()) return CDRF_DODEFAULT;
    const dm::Palette& c = dm::Colors();

    if (cd->dwDrawStage == CDDS_PREPAINT) {
        FillRect(cd->hdc, &cd->rc, dm::FieldBrush());
        return CDRF_NOTIFYITEMDRAW;
    }
    if (cd->dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;

    HBRUSH bg = CreateSolidBrush(c.field);
    FillRect(cd->hdc, &cd->rc, bg);
    DeleteObject(bg);

    wchar_t buf[128] = { 0 };
    HDITEMW hdi{};
    hdi.mask = HDI_TEXT;
    hdi.pszText = buf;
    hdi.cchTextMax = 128;
    SendMessageW(cd->hdr.hwndFrom, HDM_GETITEMW, cd->dwItemSpec,
                 reinterpret_cast<LPARAM>(&hdi));

    SetBkMode(cd->hdc, TRANSPARENT);
    SetTextColor(cd->hdc, c.text);
    HFONT old = static_cast<HFONT>(SelectObject(cd->hdc, dm::UiFont()));
    RECT tr = cd->rc;
    tr.left += S(cd->hdr.hwndFrom, 6);
    DrawTextW(cd->hdc, buf, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(cd->hdc, old);

    RECT ln = { cd->rc.left, cd->rc.bottom - 1, cd->rc.right, cd->rc.bottom };
    HBRUSH lb = CreateSolidBrush(c.border);
    FillRect(cd->hdc, &ln, lb);
    DeleteObject(lb);
    return CDRF_SKIPDEFAULT;
}

// ---- 主题铺开 --------------------------------------------------------------

void ThemeOneControl(HWND h, bool dark, const dm::Palette& c) {
    wchar_t cls[64] = { 0 };
    if (!GetClassNameW(h, cls, 64)) return;

    if (_wcsicmp(cls, L"SysListView32") == 0) {
        ThemeListView(h, dark, c);
    } else if (_wcsicmp(cls, L"SysHeader32") == 0) {
        dm::AllowDarkWindow(h, dark);
        InvalidateRect(h, nullptr, TRUE);
    } else if (_wcsicmp(cls, L"SysTreeView32") == 0) {
        ThemeTreeView(h, dark, c);
    } else if (_wcsicmp(cls, L"msctls_statusbar32") == 0) {
        // 带视觉样式时 SB_SETBKCOLOR 会被忽略 -> 剥掉主题，再让子类全自绘
        SendMessageW(h, SB_SETBKCOLOR, 0, static_cast<LPARAM>(dark ? c.bg : c.field));
        SetWindowTheme(h, L"", L"");
        SendMessageW(h, WM_SIZE, 0, 0);
        InvalidateRect(h, nullptr, TRUE);
    } else if (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ListBox") == 0) {
        // 没有深色主题可用 -> 剥掉视觉样式，配色全交给父窗口的 WM_CTLCOLOR*
        SetWindowTheme(h, L"", L"");
        dm::AllowDarkWindow(h, dark);
        InvalidateRect(h, nullptr, TRUE);
    } else if (_wcsicmp(cls, L"ComboBox") == 0) {
        SetWindowTheme(h, L"", L"");
        dm::AllowDarkWindow(h, dark);
        InvalidateRect(h, nullptr, TRUE);
    } else if (_wcsicmp(cls, L"Button") == 0) {
        dm::AllowDarkWindow(h, dark);
        InvalidateRect(h, nullptr, TRUE);
    } else if (_wcsicmp(cls, L"Static") == 0) {
        InvalidateRect(h, nullptr, TRUE);
    }
}

void ThemeTree(HWND parent, bool dark, const dm::Palette& c) {
    for (HWND h = GetWindow(parent, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        ThemeOneControl(h, dark, c);
        wchar_t cls[64] = { 0 };
        GetClassNameW(h, cls, 64);
        // 只有这几类有需要单独处理的子窗口
        if (_wcsicmp(cls, L"ComboBox") == 0
            || _wcsicmp(cls, L"SysListView32") == 0
            || _wcsicmp(cls, L"SysTreeView32") == 0) {
            ThemeTree(h, dark, c);
        }
    }
}

} // namespace

// ---- 公开 API --------------------------------------------------------------

void Install(HWND top) {
    if (!top) return;
    for (HWND h = GetWindow(top, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        wchar_t cls[64] = { 0 };
        if (!GetClassNameW(h, cls, 64)) continue;

        if (_wcsicmp(cls, L"Button") == 0) {
            LONG st = GetWindowLongW(h, GWL_STYLE) & 0xF;
            if (st == BS_GROUPBOX) {
                SubOnce(h, GroupBoxProc, kSubGroupBox);
            } else if (st == BS_AUTOCHECKBOX || st == BS_CHECKBOX
                    || st == BS_AUTORADIOBUTTON || st == BS_RADIOBUTTON
                    || st == BS_3STATE || st == BS_AUTO3STATE) {
                SubOnce(h, CheckRadioProc, kSubCheckBox);
            }
            // BS_OWNERDRAW 按钮由父窗口的 WM_DRAWITEM 处理，这里不管
        } else if (_wcsicmp(cls, L"ComboBox") == 0) {
            SubOnce(h, ComboProc, kSubCombo);
        } else if (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ListBox") == 0) {
            if (GetWindowLongW(h, GWL_STYLE) & WS_VSCROLL)
                SubOnce(h, EditNcProc, kSubEditNc);
        } else if (_wcsicmp(cls, L"msctls_statusbar32") == 0) {
            SubOnce(h, StatusProc, kSubStatus);
        }

        // 递归进容器类（ComboBox 的下拉列表 / ListView 的表头 / TreeView 的子窗口）
        if (_wcsicmp(cls, L"ComboBox") == 0
            || _wcsicmp(cls, L"SysListView32") == 0
            || _wcsicmp(cls, L"SysTreeView32") == 0) {
            Install(h);
        }
    }
}

void Apply(HWND top, bool dark, const dm::Palette& pal) {
    ApplyMenuBar(top, dark, pal);
    ThemeTree(top, dark, pal);
}

bool HandleDrawItem(LPARAM lParam, LRESULT* result) {
    auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    if (!dis) return false;
    if (dis->CtlType == ODT_MENU) {
        DrawMenuBarItem(dis, reinterpret_cast<const wchar_t*>(dis->itemData));
        if (result) *result = TRUE;
        return true;
    }
    if (dis->CtlType == ODT_BUTTON) {
        DrawOwnedButton(dis);
        if (result) *result = TRUE;
        return true;
    }
    return false;
}

bool HandleMeasureItem(LPARAM lParam, LRESULT* result) {
    auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
    if (!mis || mis->CtlType != ODT_MENU) return false;

    const wchar_t* text = reinterpret_cast<const wchar_t*>(mis->itemData);
    HDC dc = GetDC(nullptr);
    HFONT old = static_cast<HFONT>(SelectObject(dc, dm::UiFont()));
    SIZE sz{ 48, 0 };
    if (text) GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &sz);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);

    mis->itemWidth  = static_cast<UINT>(sz.cx) + 28;
    mis->itemHeight = GetSystemMetrics(SM_CYMENU);
    if (result) *result = TRUE;
    return true;
}

bool HandleNotify(LPARAM lParam, LRESULT* result) {
    auto* hdr = reinterpret_cast<LPNMHDR>(lParam);
    if (!hdr || hdr->code != NM_CUSTOMDRAW) return false;
    auto* cd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
    wchar_t cls[64] = { 0 };
    GetClassNameW(hdr->hwndFrom, cls, 64);
    const dm::Palette& c = dm::Colors();

    if (_wcsicmp(cls, L"SysHeader32") == 0) {
        if (result) *result = HeaderCustomDraw(cd);
        return true;
    }
    if (_wcsicmp(cls, L"msctls_statusbar32") == 0) {
        if (cd->dwDrawStage == CDDS_PREPAINT) {
            SetTextColor(cd->hdc, dm::IsDark() ? c.text : RGB(0, 0, 0));
            SetBkMode(cd->hdc, TRANSPARENT);
        }
        if (result) *result = CDRF_DODEFAULT;
        return true;
    }
    if (_wcsicmp(cls, L"SysTreeView32") == 0) {
        if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
            SetTextColor(cd->hdc, dm::IsDark() ? c.text : RGB(0, 0, 0));
            SetBkMode(cd->hdc, TRANSPARENT);
        }
        if (result) *result = CDRF_DODEFAULT;
        return true;
    }
    return false;
}

bool HandleCtlColor(UINT msg, WPARAM wParam, LRESULT* result) {
    HDC dc = reinterpret_cast<HDC>(wParam);
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORBTN:
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dm::Colors().text);
        if (result) *result = reinterpret_cast<LRESULT>(dm::BgBrush());
        return true;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetBkColor(dc, dm::Colors().field);
        SetTextColor(dc, dm::Colors().text);
        if (result) *result = reinterpret_cast<LRESULT>(dm::FieldBrush());
        return true;
    default:
        return false;
    }
}

void OwnerdrawMenuBar(HWND top) {
    HMENU bar = top ? GetMenu(top) : nullptr;
    if (!bar) return;

    int n = GetMenuItemCount(bar);
    for (int i = 0; i < n; ++i) {
        wchar_t text[128] = { 0 };
        // owner-draw 项取不到文本，所以重复调用会自然跳过（幂等）
        if (GetMenuStringW(bar, i, text, 128, MF_BYPOSITION) <= 0) continue;
        HMENU sub = GetSubMenu(bar, i);
        if (!sub) continue;

        size_t len = wcslen(text) + 1;
        auto buf = std::unique_ptr<wchar_t[]>(new wchar_t[len]);
        wcscpy_s(buf.get(), len, text);
        wchar_t* raw = buf.get();
        g_labels.storage.push_back(std::move(buf));

        ModifyMenuW(bar, i, MF_BYPOSITION | MF_POPUP | MF_OWNERDRAW,
                    reinterpret_cast<UINT_PTR>(sub),
                    reinterpret_cast<LPCWSTR>(raw));
    }
    DrawMenuBar(top);
}

void ApplyMenuBar(HWND top, bool dark, const dm::Palette& pal) {
    HMENU bar = top ? GetMenu(top) : nullptr;
    if (!bar) return;

    HBRUSH fresh = CreateSolidBrush(dark ? pal.bg : GetSysColor(COLOR_MENU));
    HBRUSH old   = g_menuBrush;
    g_menuBrush  = fresh;

    MENUINFO mi{};
    mi.cbSize  = sizeof(mi);
    // ⚠️ fMask 只给 MIM_BACKGROUND，绝不能加 MIM_APPLYTOSUBMENUS：
    //    加了会把系统按深色主题绘制的【下拉弹出菜单】一起涂成我们的底色。
    mi.fMask   = MIM_BACKGROUND;
    mi.hbrBack = fresh;
    SetMenuInfo(bar, &mi);

    // 先装新的再删旧的，避免菜单短暂引用已释放的刷子
    if (old) DeleteObject(old);

    RedrawWindow(top, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
    DrawMenuBar(top);
}

void SetOwnedButtonCornerRadius(int px) {
    if (px >= 0) g_btnRadiusPx = px;
}

} // namespace darkui
