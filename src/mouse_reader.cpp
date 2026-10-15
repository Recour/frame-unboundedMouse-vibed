// SPDX-License-Identifier: MIT
#include "mouse_reader.h"

MouseReader::MouseReader() = default;

void MouseReader::Start(const MouseReaderConfig &cfg)
{
    m_cfg = cfg;
    m_run = true;
    m_thread = std::thread(&MouseReader::Loop, this);
}

void MouseReader::Stop()
{
    m_run = false;
    if (m_thread.joinable())
        m_thread.join();
}

void MouseReader::SetActive(bool on)
{
    if (active == on) return;
    if (on && !Grab(true))
        return; // Grab() logged why
    if (!on)
        Grab(false);
    left = right = middle = back = false;
    dx = dy = wheel = hwheel = 0;
    active = on;
    toggled = true;
    Log("%s\n", on ? "ACTIVE (mouse drives the laser)" : "inactive (mouse back to desktop)");
}

void MouseReader::Press(int code, bool down)
{
    if (code == m_cfg.toggleButton)
    {
        if (down) SetActive(!active);
        return;
    }
    if (!active) return;
    if (code == kBtnLeft) left = down;
    else if (code == kBtnRight) right = down;
    else if (code == kBtnMiddle) middle = down;
    else if (code == m_cfg.backButton) back = down;
}

void MouseReader::Move(int x, int y, int wheelTicks, int hwheelTicks)
{
    if (!active) return;
    dx += x;
    dy += y;
    wheel += wheelTicks;
    hwheel += hwheelTicks;
}
