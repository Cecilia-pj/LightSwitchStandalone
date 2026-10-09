// LightSwitch Standalone - global hotkey support.
//
// Upstream PowerToys does NOT call RegisterHotKey: modules only *declare* their
// hotkeys and the Runner's centralised low-level keyboard hook does the capture.
// A standalone component has to own this layer, so we implement two strategies:
//
//   1. RegisterHotKey() bound to our message-only window. This is the clean
//      route and works for Ctrl/Alt/Shift combinations.
//   2. Fallback WH_KEYBOARD_LL hook. The default Light Switch shortcut is
//      Ctrl+Win+Shift+D, and MS Learn explicitly documents that "keyboard
//      shortcuts that involve the Windows key are reserved for use by the
//      operating system", which in practice makes RegisterHotKey fail with
//      MOD_WIN. The hook reproduces what the PowerToys Runner did.
//
// The hook callback must stay trivial: it only compares modifiers and posts a
// message; all real work happens on the scheduler / UI thread.
#pragma once

#include <windows.h>

#include <string>

#include "Config.h"

namespace ls
{

class HotkeyManager
{
public:
    // Posted to the target window when the hotkey fires.
    static constexpr UINT kHotkeyMessage = WM_APP + 0x101;

    HotkeyManager() = default;
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    // (Re)registers the hotkey. Returns true if either strategy succeeded.
    bool Register(HWND targetWindow, const HotkeyConfig& hotkey);

    void Unregister();

    bool IsActive() const { return m_registered; }
    bool UsesLowLevelHook() const { return m_usingHook; }

private:
    static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam);
    static HotkeyManager* s_instance;

    bool InstallHook();
    void RemoveHook();
    bool CurrentSystemMatches() const;

    HWND m_window = nullptr;
    HotkeyConfig m_hotkey;
    bool m_registered = false;
    bool m_usingHook = false;
    HHOOK m_hook = nullptr;
};

std::wstring FormatHotkey(const HotkeyConfig& hotkey);

} // namespace ls
