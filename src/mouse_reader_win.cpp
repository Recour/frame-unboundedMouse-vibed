// SPDX-License-Identifier: MIT
// Windows mouse reader.
//  - Raw Input (RIDEV_INPUTSINK) delivers the physical mouse's deltas, buttons and wheel while
//    vrserver is in the background.
//  - While laser mode is on, a low-level mouse hook swallows the physical mouse so the desktop
//    and most windows stop seeing it. (Games that read Raw Input or DirectInput directly still
//    do, a hook can't stop that.) Injected events (other tools, remote desktop) pass through.
//  - The toggle button is swallowed even while laser mode is off, otherwise pressing the forward
//    side button would also trigger "browser forward".
// Windows removes a low-level hook on its own if its thread stops responding, so a stuck driver
// can't leave the mouse dead.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "mouse_reader.h"

#include <cctype>
#include <unordered_map>
#include <vector>

struct MouseReader::Platform
{
    HWND hwnd = nullptr;
    HHOOK hook = nullptr;
    int wheelAcc = 0, hwheelAcc = 0; // leftover of high-resolution wheel deltas
    std::unordered_map<HANDLE, bool> deviceAllowed;
};

// The hook callback has no user data, so it reads these. There is one MouseReader per driver.
static std::atomic<bool> *g_hookActive = nullptr;
static int g_hookToggleX = 0; // 1 = XBUTTON1 (back), 2 = XBUTTON2 (forward), 0 = not an X button

static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION)
    {
        const MSLLHOOKSTRUCT *ms = reinterpret_cast<const MSLLHOOKSTRUCT *>(lp);
        if (!(ms->flags & LLMHF_INJECTED))
        {
            if (g_hookActive && g_hookActive->load())
                return 1;
            if (g_hookToggleX && (wp == WM_XBUTTONDOWN || wp == WM_XBUTTONUP) && HIWORD(ms->mouseData) == g_hookToggleX)
                return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static std::string Lower(std::string s)
{
    for (char &c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

static bool DeviceAllowed(MouseReader::Platform &p, HANDLE device, const std::string &filter)
{
    auto it = p.deviceAllowed.find(device);
    if (it != p.deviceAllowed.end()) return it->second;

    std::string name;
    UINT len = 0;
    if (device && GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, nullptr, &len) == 0 && len > 0)
    {
        std::vector<char> buf(len);
        if (GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, buf.data(), &len) != UINT(-1))
            name = buf.data();
    }
    bool ok = filter.empty() || Lower(name).find(Lower(filter)) != std::string::npos;
    Log("mouse device %s%s\n", name.empty() ? "(unnamed)" : name.c_str(), ok ? "" : " (ignored by deviceNameFilter)");
    p.deviceAllowed[device] = ok;
    return ok;
}

bool MouseReader::Grab(bool on)
{
    // The hook is installed for the lifetime of the thread and only looks at `active`.
    if (on && !m_platform->hook)
    {
        Log("mouse hook not installed, staying inactive\n");
        return false;
    }
    return true;
}

void MouseReader::Loop()
{
    Platform p;
    m_platform = &p;

    // Message-only window: only needs to exist so Raw Input has a target.
    p.hwnd = CreateWindowExA(0, "STATIC", "mouselaser", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                             GetModuleHandleA(nullptr), nullptr);
    if (!p.hwnd)
    {
        Log("CreateWindowEx failed (%lu)\n", GetLastError());
        m_platform = nullptr;
        return;
    }

    RAWINPUTDEVICE rid = {0x01, 0x02, RIDEV_INPUTSINK, p.hwnd}; // generic desktop / mouse
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
    {
        Log("RegisterRawInputDevices failed (%lu)\n", GetLastError());
        DestroyWindow(p.hwnd);
        m_platform = nullptr;
        return;
    }

    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&LowLevelMouseProc), &self);
    g_hookActive = &active;
    g_hookToggleX = m_cfg.toggleButton == kBtnSide ? 1 : m_cfg.toggleButton == kBtnExtra ? 2 : 0;
    p.hook = SetWindowsHookExA(WH_MOUSE_LL, LowLevelMouseProc, self, 0);
    if (!p.hook)
        Log("SetWindowsHookEx failed (%lu), the mouse cannot be grabbed\n", GetLastError());
    Log("mouse reader running\n");

    while (m_run)
    {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_INPUT)
            {
                HRAWINPUT h = reinterpret_cast<HRAWINPUT>(msg.lParam);
                UINT size = 0;
                GetRawInputData(h, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
                std::vector<BYTE> buf(size);
                if (size && GetRawInputData(h, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) == size)
                {
                    const RAWINPUT *ri = reinterpret_cast<const RAWINPUT *>(buf.data());
                    if (ri->header.dwType == RIM_TYPEMOUSE && DeviceAllowed(p, ri->header.hDevice, m_cfg.nameFilter))
                    {
                        const RAWMOUSE &m = ri->data.mouse;
                        const USHORT f = m.usButtonFlags;
                        int wheelTicks = 0, hwheelTicks = 0;
                        if (f & RI_MOUSE_WHEEL)
                        {
                            p.wheelAcc += static_cast<SHORT>(m.usButtonData);
                            wheelTicks = p.wheelAcc / WHEEL_DELTA;
                            p.wheelAcc -= wheelTicks * WHEEL_DELTA;
                        }
                        if (f & RI_MOUSE_HWHEEL)
                        {
                            p.hwheelAcc += static_cast<SHORT>(m.usButtonData);
                            hwheelTicks = p.hwheelAcc / WHEEL_DELTA;
                            p.hwheelAcc -= hwheelTicks * WHEEL_DELTA;
                        }
                        // Absolute devices (tablets, remote desktop) have no deltas.
                        const bool relative = !(m.usFlags & MOUSE_MOVE_ABSOLUTE);
                        Move(relative ? m.lLastX : 0, relative ? m.lLastY : 0, wheelTicks, hwheelTicks);

                        static const struct { USHORT down, up; int code; } k_buttons[] = {
                            {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, kBtnLeft},
                            {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, kBtnRight},
                            {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, kBtnMiddle},
                            {RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, kBtnSide},   // XBUTTON1, back
                            {RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, kBtnExtra},  // XBUTTON2, forward
                        };
                        for (const auto &b : k_buttons)
                        {
                            if (f & b.down) Press(b.code, true);
                            if (f & b.up) Press(b.code, false);
                        }
                    }
                }
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg); // also lets the system clean up the raw input
        }
    }

    SetActive(false);
    if (p.hook) UnhookWindowsHookEx(p.hook);
    g_hookActive = nullptr;
    rid.dwFlags = RIDEV_REMOVE;
    rid.hwndTarget = nullptr;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    DestroyWindow(p.hwnd);
    m_platform = nullptr;
}
