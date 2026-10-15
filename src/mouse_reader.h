// SPDX-License-Identifier: MIT
// Platform-neutral half of the mouse reader. The platform half (mouse_reader_linux.cpp,
// mouse_reader_win.cpp) finds the physical mouse, feeds Press()/Move() and implements Grab().
#pragma once

#include <atomic>
#include <string>
#include <thread>

// Button codes are the Linux evdev ones on every platform, so the `toggleButton` and
// `backButton` settings mean the same thing everywhere.
enum MouseButtonCode
{
    kBtnLeft = 0x110,   // BTN_LEFT
    kBtnRight = 0x111,  // BTN_RIGHT
    kBtnMiddle = 0x112, // BTN_MIDDLE
    kBtnSide = 0x113,   // BTN_SIDE  (back side button)
    kBtnExtra = 0x114,  // BTN_EXTRA (forward side button)
};

struct MouseReaderConfig
{
    int toggleButton = kBtnExtra;
    int backButton = kBtnSide; // -1 = none
    std::string nameFilter;    // substring of the device name; empty = any mouse
};

// Implemented in driver.cpp.
void Log(const char *fmt, ...);

class MouseReader
{
public:
    std::atomic<bool> active{false};
    std::atomic<bool> toggled{false}; // set on every toggle; RunFrame clears it
    std::atomic<int> dx{0}, dy{0};
    std::atomic<int> wheel{0}, hwheel{0};
    std::atomic<bool> left{false}, right{false}, middle{false}, back{false};

    MouseReader();
    // Destroying a joinable std::thread calls std::terminate (taking vrserver down),
    // so make sure the thread is stopped even if Deactivate() was never called.
    ~MouseReader() { Stop(); }

    void Start(const MouseReaderConfig &cfg);
    void Stop();

    struct Platform; // defined by the platform half

private:
    MouseReaderConfig m_cfg;
    std::atomic<bool> m_run{false};
    std::thread m_thread;
    Platform *m_platform = nullptr;

    // Platform half
    void Loop();
    bool Grab(bool on); // take the mouse away from the desktop (or give it back); false = failed

    // Shared logic, called from Loop()
    void SetActive(bool on);
    void Press(int code, bool down);
    void Move(int x, int y, int wheelTicks, int hwheelTicks);
};
