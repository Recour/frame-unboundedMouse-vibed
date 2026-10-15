// SPDX-License-Identifier: MIT
// Linux mouse reader: evdev, with EVIOCGRAB while laser mode is on.
#include "mouse_reader.h"

#include <cerrno>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct MouseReader::Platform
{
    int fd = -1;
};

static bool TestBit(const unsigned long *bits, int bit)
{
    const int bpl = sizeof(unsigned long) * 8;
    return (bits[bit / bpl] >> (bit % bpl)) & 1;
}

static bool IsMouse(int fd, const std::string &nameFilter, std::string &name)
{
    unsigned long evbits[(EV_MAX + 1) / (8 * sizeof(long)) + 1] = {};
    unsigned long relbits[(REL_MAX + 1) / (8 * sizeof(long)) + 1] = {};
    unsigned long keybits[(KEY_MAX + 1) / (8 * sizeof(long)) + 1] = {};
    char n[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(n)), n) < 0) return false;
    name = n;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) return false;
    if (!TestBit(evbits, EV_REL) || !TestBit(evbits, EV_KEY)) return false;
    ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits);
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
    if (!TestBit(relbits, REL_X) || !TestBit(relbits, REL_Y) || !TestBit(keybits, BTN_LEFT))
        return false;
    return nameFilter.empty() || name.find(nameFilter) != std::string::npos;
}

static int OpenMouse(const std::string &nameFilter)
{
    DIR *dir = opendir("/dev/input");
    if (!dir) return -1;
    while (dirent *e = readdir(dir))
    {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + e->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        std::string name;
        if (IsMouse(fd, nameFilter, name))
        {
            Log("using %s (%s)\n", path.c_str(), name.c_str());
            closedir(dir);
            return fd;
        }
        close(fd);
    }
    closedir(dir);
    return -1;
}

bool MouseReader::Grab(bool on)
{
    int fd = m_platform->fd;
    if (fd < 0) return true;
    if (ioctl(fd, EVIOCGRAB, on ? 1 : 0) < 0 && on)
    {
        Log("EVIOCGRAB failed (%s), staying inactive\n", strerror(errno));
        return false;
    }
    return true;
}

void MouseReader::Loop()
{
    Platform p;
    m_platform = &p;

    auto closeMouse = [&]
    {
        SetActive(false); // releases the grab while the fd is still open
        if (p.fd >= 0)
        {
            close(p.fd);
            p.fd = -1;
        }
    };

    while (m_run)
    {
        if (p.fd < 0)
        {
            p.fd = OpenMouse(m_cfg.nameFilter);
            if (p.fd < 0)
            {
                for (int i = 0; i < 20 && m_run; i++) usleep(100 * 1000);
                continue;
            }
        }
        pollfd pfd = {p.fd, POLLIN, 0};
        int r = poll(&pfd, 1, 200);
        if (r <= 0) continue;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            Log("mouse disappeared\n");
            closeMouse();
            continue;
        }
        input_event ev;
        ssize_t n;
        int relX = 0, relY = 0, relWheel = 0, relHWheel = 0;
        while ((n = read(p.fd, &ev, sizeof(ev))) == sizeof(ev))
        {
            if (ev.type == EV_REL)
            {
                if (ev.code == REL_X) relX += ev.value;
                else if (ev.code == REL_Y) relY += ev.value;
                else if (ev.code == REL_WHEEL) relWheel += ev.value;
                else if (ev.code == REL_HWHEEL) relHWheel += ev.value;
            }
            else if (ev.type == EV_KEY && ev.value != 2) // 2 = autorepeat
            {
                Move(relX, relY, relWheel, relHWheel); // keep motion ordered before the click
                relX = relY = relWheel = relHWheel = 0;
                Press(ev.code, ev.value != 0);
            }
        }
        Move(relX, relY, relWheel, relHWheel);
        if (n < 0 && errno == ENODEV)
        {
            Log("mouse disappeared\n");
            closeMouse();
        }
    }
    closeMouse();
    m_platform = nullptr;
}
