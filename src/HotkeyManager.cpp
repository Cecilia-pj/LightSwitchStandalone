// LightSwitch Standalone - global hotkey support (implementation).
#include "HotkeyManager.h"

#include "Config.h"
#include "Logging.h"

namespace ls
{

namespace
{

constexpr int kHotkeyId = 0xA511;

bool IsKeyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void AppendKeyName(std::wstring& out, unsigned int vk)
{
    if (vk >= 'A' && vk <= 'Z')
    {
        out.push_back(static_cast<wchar_t>(vk));
        return;
    }
    if (vk >= '0' && vk <= '9')
    {
        out.push_back(static_cast<wchar_t>(vk));
        return;
    }
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        out += L"F";
        out += std::to_wstring(vk - VK_F1 + 1);
        return;
    }

    switch (vk)
    {
    case VK_SPACE:
        out += L"Space";
        return;
    case VK_TAB:
        out += L"Tab";
        return;
    case VK_RETURN:
        out += L"Enter";
        return;
    case VK_ESCAPE:
        out += L"Esc";
        return;
    case VK_OEM_3:
        out += L"`";
        return;
    case VK_OEM_MINUS:
        out += L"-";
        return;
    case VK_OEM_PLUS:
        out += L"=";
        return;
    case VK_OEM_4:
        out += L"[";
        return;
    case VK_OEM_5:
        out += L"\\";
        return;
    case VK_OEM_6:
        out += L"]";
        return;
    case VK_OEM_1:
        out += L";";
        return;
    case VK_OEM_7:
        out += L"'";
        return;
    case VK_OEM_COMMA:
        out += L",";
        return;
    case VK_OEM_PERIOD:
        out += L".";
        return;
    case VK_OEM_2:
        out += L"/";
        return;
    default:
        break;
    }

    wchar_t buffer[32];
    swprintf_s(buffer, L"0x%02X", vk);
    out += buffer;
}

} // namespace

HotkeyManager* HotkeyManager::s_instance = nullptr;

std::wstring FormatHotkey(const HotkeyConfig& hotkey)
{
    std::wstring text;
    if (hotkey.ctrl)
        text += L"Ctrl+";
    if (hotkey.win)
        text += L"Win+";
    if (hotkey.alt)
        text += L"Alt+";
    if (hotkey.shift)
        text += L"Shift+";
    AppendKeyName(text, hotkey.vk);
    return text;
}

HotkeyManager::~HotkeyManager()
{
    Unregister();
}

bool HotkeyManager::Register(HWND targetWindow, const HotkeyConfig& hotkey)
{
    Unregister();

    m_window = targetWindow;
    m_hotkey = hotkey;

    UINT modifiers = 0;
    if (hotkey.ctrl)
        modifiers |= MOD_CONTROL;
    if (hotkey.alt)
        modifiers |= MOD_ALT;
    if (hotkey.shift)
        modifiers |= MOD_SHIFT;
    if (hotkey.win)
        modifiers |= MOD_WIN;

    if (RegisterHotKey(m_window, kHotkeyId, modifiers, hotkey.vk))
    {
        m_registered = true;
        m_usingHook = false;
        LOG_INFO(L"[Hotkey] Registered " << FormatHotkey(hotkey) << L" via RegisterHotKey.");
        return true;
    }

    const DWORD error = GetLastError();
    LOG_WARN(L"[Hotkey] RegisterHotKey failed for " << FormatHotkey(hotkey) << L" (error " << error
                                                    << L"). Falling back to a low-level keyboard hook.");

    if (InstallHook())
    {
        m_registered = true;
        m_usingHook = true;
        LOG_INFO(L"[Hotkey] Low-level keyboard hook active for " << FormatHotkey(hotkey) << L".");
        return true;
    }

    m_window = nullptr;
    LOG_ERROR(L"[Hotkey] No hotkey mechanism available; the toggle shortcut will not work.");
    return false;
}

void HotkeyManager::Unregister()
{
    if (m_registered && !m_usingHook)
        UnregisterHotKey(m_window, kHotkeyId);

    RemoveHook();

    m_registered = false;
    m_usingHook = false;
    m_window = nullptr;
}

bool HotkeyManager::InstallHook()
{
    if (m_hook)
        return true;

    s_instance = this;
    m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &HotkeyManager::LowLevelKeyboardProc, nullptr, 0);
    if (!m_hook)
    {
        s_instance = nullptr;
        LOG_ERROR(L"[Hotkey] SetWindowsHookEx(WH_KEYBOARD_LL) failed (error " << GetLastError() << L").");
        return false;
    }
    return true;
}

void HotkeyManager::RemoveHook()
{
    if (m_hook)
    {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
    }
    if (s_instance == this)
        s_instance = nullptr;
}

bool HotkeyManager::CurrentSystemMatches() const
{
    const bool ctrlDown = IsKeyDown(VK_CONTROL);
    const bool shiftDown = IsKeyDown(VK_SHIFT);
    const bool altDown = IsKeyDown(VK_MENU);
    const bool winDown = IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN);

    return ctrlDown == m_hotkey.ctrl &&
           shiftDown == m_hotkey.shift &&
           altDown == m_hotkey.alt &&
           winDown == m_hotkey.win;
}

LRESULT CALLBACK HotkeyManager::LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && s_instance && s_instance->m_window)
    {
        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool injected = (info->flags & LLKHF_INJECTED) != 0;

        if (isKeyDown && !injected && info->vkCode == s_instance->m_hotkey.vk && s_instance->CurrentSystemMatches())
        {
            // Deliberately minimal: no blocking calls, no SendMessage.
            PostMessageW(s_instance->m_window, kHotkeyMessage, 0, 0);
            return 1; // swallow so the shell does not also act on it
        }
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace ls
