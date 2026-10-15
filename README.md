# frame-unboundedMouse-vibed

An experimental SteamVR driver that lets you control the **SteamVR laser pointer with a regular mouse**, across every VR overlay (the SteamVR dashboard, Steam UI, desktop overlays, popups), instead of the mouse being trapped inside the desktop window. It started on the Steam Frame (Linux) and now has a **Windows port** for PC SteamVR headsets (for example a Bigscreen Beyond 2), see [Windows](#windows).

<p align="center">
  <img src="docs/media/demo.webp" alt="Demo: the mouse aims the Steam Frame's laser pointer across the KDE desktop, the Steam UI and other VR overlays" width="800">
</p>

> [!WARNING]
> ## ⚠️ This project is vibecoded. Read this first.
>
> This driver was written almost entirely by an AI coding assistant (Claude) during one exploratory session on a Steam Frame, with a human steering, testing and approving each step. It has **not** been reviewed the way hand-written, production code would be.
>
> - **Experimental.** The Linux build works on one headset, one SteamOS build and one mouse (see [Tested on](#tested-on)). **The Windows port has been cross-compiled but not yet run on a headset.** It may not work on yours.
> - **It runs inside `vrserver`.** A bug in the driver can crash SteamVR. On the Frame, SteamVR is your whole session, so the headset can end up showing nothing until you remove the driver (see [Recovery](#recovery)).
> - **It grabs your mouse.** While the laser mode is on, the driver takes control of the mouse (Linux: `EVIOCGRAB`, exclusive; Windows: a low-level mouse hook, see [Windows](#windows)).
> - **It relies on undocumented behaviour.** It depends on SteamVR internals that Valve can change in any update: the compositor's `lasermouse` action set and Frame-specific bindings.
> - **No warranty.** Use it at your own risk. Read the code first; it's a single ~580-line file: [`src/driver.cpp`](src/driver.cpp).

## What it does

The Frame's laser pointer is drawn by SteamVR's compositor. The compositor aims it with whatever tracked device supplies a *pose*, normally a controller. A physical mouse never reaches that system: gamescope consumes it and clamps it to whichever window has focus.

This driver adds a **virtual controller** to SteamVR:
- **Position:** follows your head.
- **Direction:** aimed by mouse movement.
- **Buttons:** your mouse buttons.

The compositor then treats it like any other laser-pointing controller.

| Mouse | Laser mode OFF (default) | Laser mode ON |
|---|---|---|
| Forward side button (BTN_EXTRA) | **Turns laser mode on** | **Turns laser mode off** |
| Movement | Normal desktop cursor | Aims the laser (world-locked: turning your head doesn't move it) |
| Left / right / middle button | Normal | Laser click / right click / middle click |
| Back side button (BTN_SIDE) | Normal | SteamVR's laser **Back**, the same action as the right Frame controller's B button. Works in Steam UI menus; it does not reach the KDE desktop (see Known limitations). |
| Wheel (and tilt wheel) | Normal | Acts like the controller **thumbstick**. One notch moves one item in Steam UI lists, and spinning the wheel scrolls continuously. It also scrolls the overlay under the laser. |

When laser mode turns on, the ray starts where you're looking. When it's off, the virtual device stays connected but parks its ray pointing at the sky with every button released, so your real controllers keep the laser.

Since 0.5.1 the virtual device is a **stylus** (`/user/stylus`) while laser mode is off, so it doesn't take your real controllers' slots. While laser mode is on it switches to the **left hand** (`activeRole`), because SteamVR only lets hands drag overlays. The owner tested this switch with 0.5.1 and reports it works (see the development log).

## Windows

Windows port for PC SteamVR (tested target: Bigscreen Beyond 2 with Valve Index controllers). **Status: it builds, but it has not been run on a headset yet.** Expect to debug it.

How it differs from Linux:
- **Input:** Raw Input (`RIDEV_INPUTSINK`) delivers the mouse's deltas, buttons and wheel even though `vrserver` is in the background. While laser mode is on, a low-level mouse hook (`WH_MOUSE_LL`) swallows the physical mouse so the desktop and most windows don't see it. Windows removes such a hook by itself if the thread stops responding, and `vrserver` exiting removes it too, so a stuck driver can't leave your mouse dead.
- **Games can still see the mouse.** A hook can't hide the mouse from games that read Raw Input or DirectInput. If you point at the dashboard while a game is running, the game may still turn the camera. Pause the game or open the SteamVR dashboard first.
- **The toggle button is always swallowed**, also while laser mode is off. Otherwise the forward side button would also trigger "browser forward". Pick a different `toggleButton` if you still need that button.
- **Button codes are the same numbers as on Linux** (evdev): 272 left, 273 right, 274 middle, 275 back side button, 276 forward side button. The defaults are unchanged.
- **`deviceNameFilter`** is matched case-insensitively against the Raw Input device path (for example `VID_046D`). The driver logs every mouse it sees, so you can copy a fragment from `vrserver.txt`.
- **Not Frame-specific:** the pose comes from the HMD, and the bindings target SteamVR's own compositor (`vrcompositor`) laser actions. Whether PC SteamVR accepts the `/user/stylus` and hand-role pointer the way the Frame does is the main thing to confirm on the first run.

### Requirements (Windows)
- SteamVR with a headset (tested target: Bigscreen Beyond 2). The Index controllers can stay on.
- Visual Studio 2022 (Desktop development with C++) and CMake 3.20 or newer. Other Windows compilers may work but aren't what the CMake file targets.
- Internet access for the first CMake run: it downloads the single header `openvr_driver.h` from the public OpenVR SDK (tag `v2.5.1`, BSD-3-Clause). Use `-DOPENVR_HEADERS=<dir>` to point to your own copy.

### Build and install (Windows)
From a *Developer PowerShell for VS 2022*:
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\install.ps1      # registers driver\mouselaser with vrpathreg (backs up openvrpaths.vrpath first)
# then restart SteamVR
```
Check that it loaded: open `C:\Program Files (x86)\Steam\logs\vrserver.txt` (or `<Steam>\logs\vrserver.txt`) and look for lines starting with `mouselaser:`:
```
version 0.6.0-experimental
activated as device N, role 5
mouse reader running
mouse device \\?\HID#VID_...
```
Then press the forward side button: the log should say `ACTIVE (mouse drives the laser)`.

Settings go in `<Steam>\config\steamvr.vrsettings` under a `"driver_mouselaser"` section (same keys as below).

### Uninstall and recovery (Windows)
- `.\uninstall.ps1`, then restart SteamVR.
- Or in VR / the SteamVR window: *Settings → Developer → Manage Add-ons*, switch *mouselaser* off.
- Or set `"driver_mouselaser": { "enable": false }` in `steamvr.vrsettings`.
- Or restore the backup: copy `%LOCALAPPDATA%\openvr\openvrpaths.vrpath.bak-mouselaser` over `openvrpaths.vrpath`.

## Requirements (Linux, Steam Frame)
- A Steam Frame (aarch64, SteamOS VR variant) with SteamVR at `/opt/steamvr`.
- A USB or Bluetooth mouse. Your user must be able to read `/dev/input/event*`; the default `steamos` user is in the `input` group.
- `gcc` or `clang`, `cmake` and `ninja`. They are not guaranteed to be on a stock image; installing them may require disabling SteamOS read-only mode.
- The OpenVR driver header is **not vendored**. The build uses the copy SteamVR ships at `/opt/steamvr/tools/hellovr_vulkan_linux/src/openvr/headers`. Override it with `-DOPENVR_HEADERS=<dir>`.

## Build and install (Linux)
```sh
cmake -S . -B build -G Ninja && cmake --build build
./install.sh      # registers driver/mouselaser with vrpathreg (backs up openvrpaths.vrpath first)
# then REBOOT the headset
```

`install.sh` refuses to run if another driver named `mouselaser` is already registered from a different folder.

Check that it loaded:
```sh
grep -a 'mouselaser:' ~/.local/share/Steam/logs/vrserver.txt | tail
# expect: "version 0.5.1-experimental", "activated as device N, role 5", "using /dev/input/eventX (<your mouse>)"
```

### Optional: offline wheel test
```sh
cmake -S . -B build -DMOUSELASER_TESTS=ON && cmake --build build && ./build/wheel_sim
```
This simulates the wheel → thumbstick logic at 120 Hz, without SteamVR, and exits non-zero if a single notch or a smooth spin misbehaves. It is handy when changing the `wheel*` defaults.

## Uninstall (Linux)
```sh
./uninstall.sh    # then reboot
```

## Settings
Add any of these to `~/.config/openvr/config/steamvr.vrsettings` (Windows: `<Steam>\config\steamvr.vrsettings`) under a `"driver_mouselaser"` section. They are read once, when SteamVR starts.

| key | default | meaning |
|---|---|---|
| `enable` | `true` | `false` loads the driver but adds no device. This is the kill switch. |
| `sensitivity` | `0.05` | Degrees of ray rotation per mouse count. |
| `toggleButton` | `276` | evdev key code of the toggle: 276 = BTN_EXTRA (forward), 275 = BTN_SIDE (back). On Windows the same numbers are used (272 to 276 are supported). |
| `backButton` | `275` | evdev key code that sends the laser Back action while laser mode is on (275 = BTN_SIDE). `-1` disables it. |
| `role` | `5` | Role while laser mode is off. 5 = stylus (its own `/user/stylus` path, stays connected, doesn't collide with real controllers). 1 = left hand, 2 = right hand: these share the slot with that real controller and disconnect while off (pre-0.5.0 behaviour). |
| `activeRole` | `1` | Role while laser mode is on. 1 = left hand, 2 = right hand: needed to drag overlays (a stylus drag follows your head). `0` keeps `role`. While on, the real controller of that hand loses its laser. |
| `deviceNameFilter` | `""` | Substring of the mouse name. Linux: the evdev name, empty means the first device with REL_X/REL_Y and BTN_LEFT. Windows: the Raw Input device path, case-insensitive, empty means every mouse. |
| `originOffsetX` / `originOffsetY` / `originOffsetZ` | `0` / `-0.08` / `0` | Where the ray starts relative to your head, in metres: right, up, forward (head yaw only, so tilting your head doesn't move it). The defaults keep it at the head, so Desktop+ places new windows facing you. Try `0.15` / `-0.35` / `0.3` for a hand-like origin. |
| `originLatched` | `true` | Fix the ray origin in the room when laser mode turns on, so head movement can't move the pointer. `false` makes the origin follow the head. |
| `aimDistance` | `2.0` | On activation the ray aims at the point this many metres along your gaze, so the pointer starts where you are looking. |
| `invertY` | `false` | Invert vertical aim. |
| `wheelMode` | `"smooth"` | `"smooth"`: each notch bumps the virtual stick, which then eases back, and spinning holds it. `"step"`: each notch is one fixed flick (the 0.2.0 behaviour). |
| `wheelDeflection` | `1.0` | Maximum stick deflection (0–1), in both modes. |
| `wheelSmoothMin` | `0.7` | *smooth:* deflection after a single notch. Keep it above the UI's step threshold (about 0.5). |
| `wheelSmoothImpulse` | `0.3` | *smooth:* extra deflection added by each further notch, so faster spinning pushes harder. |
| `wheelSmoothHoldMs` | `80` | *smooth:* how long after a notch the stick holds before easing back. |
| `wheelSmoothDecayMs` | `150` | *smooth:* how fast it eases back (exponential time constant). Lower is snappier, higher glides longer. |
| `wheelPressMs` | `90` | *step:* how long each notch holds the stick pushed. |
| `wheelReleaseMs` | `60` | *step:* gap at centre between queued notches. |
| `wheelMaxQueued` | `10` | *step:* maximum notches buffered. |

**Tuning the wheel:**
- One notch skips two list items: lower `wheelSmoothHoldMs`.
- Scrolling stops abruptly: raise `wheelSmoothDecayMs`.
- Fast spins aren't fast enough: raise `wheelSmoothImpulse`.

## Recovery (Linux)
If SteamVR won't come up properly after installing, there are four options:
0. In VR, open **Manage Add-ons**, switch *mouselaser* off, then reboot.
1. **SSH or RDP in** (`xrdp` runs on the Frame), run `./uninstall.sh` or `vrpathreg removedriver <path>/driver/mouselaser`, and reboot.
2. Set `"driver_mouselaser": { "enable": false }` in `steamvr.vrsettings`, then reboot.
3. Restore the backup: `cp ~/.config/openvr/openvrpaths.vrpath.bak-mouselaser ~/.config/openvr/openvrpaths.vrpath`.

## Tested on
| | |
|---|---|
| Device | Steam Frame (Snapdragon SM8650) |
| SteamOS | `VERSION_ID=0.4.2`, `VARIANT_ID=vr`, build `20260928.6175029` |
| Kernel | `6.18.0-gfbdbca41fd45` |
| SteamVR | as installed at `/opt/steamvr` on 2026-10-01 |
| Mouse | MCHOSE G3 A (2.4 GHz, has BTN_SIDE/BTN_EXTRA) |
| Result (Linux) | Loads, toggles, aims and clicks across overlays. Reconnects after the mouse sleeps or replugs. The wheel scrolls Steam UI lists like the thumbstick: 0.2.0 worked but felt a bit choppy. 0.3.0's smooth mode was confirmed working by the owner. 0.5.1 (stylus while off, left hand while on) reported working by the owner, including quick off/on toggles. |

## Windows test status
| | |
|---|---|
| Builds | Cross-compiled with mingw-w64 against `openvr_driver.h` v2.5.1: compiles and exports `HmdDriverFactory`. The MSVC/CMake path is untested. |
| Runs | **Not yet run on a headset.** |
| Open questions | Does PC SteamVR's compositor pick the virtual stylus/hand as the laser? Does Raw Input keep arriving while the hook swallows the mouse? Does the role switch to left hand apply live with Index controllers connected? |

## Known limitations
- Before 0.7.0 the ray started just below your head and followed it, so the beam was seen end-on and head movement shifted the pointer. Since 0.7.0 the origin is latched when laser mode turns on, so head movement no longer shifts the pointer (see `origin*` settings).
- While laser mode is on, the **real controller of the `activeRole` hand loses its laser** (the left one by default; confirmed by the owner). It gets it back when laser mode is switched off. Set `activeRole: 2` to borrow the right hand instead. With `role` 1 or 2 this happens whenever the device is connected.
- With a stylus role active (`activeRole: 0`), the mouse laser can aim and click but **can't drag overlays**: they follow your head. SteamVR's compositor only drags with hands or the head.
- The driver logs some SteamVR events (`event N device M`) as diagnostics, including a burst of `event 111` lines at startup.
- **Wheel feel is approximate.** A wheel isn't a stick: it only sends notches. Smooth mode only *simulates* a held stick, so it still won't feel exactly like a real thumbstick. Tune it with the `wheel*` settings.
- Settings are only read when SteamVR starts.
- **Back doesn't reach the KDE desktop.** SteamVR's laser Back goes to SteamVR overlays. Steam's UI handles it, but gamescope doesn't pass it on to the windows it hosts (a real controller's B behaves the same). With laser mode off, the side buttons don't work in KDE either, because the nested KWin's X11 backend drops X buttons 8 and up. See [docs/steam-frame-background.md](docs/steam-frame-background.md).
- Two harmless log lines:
  - `steam.client (mouselaser) has no configured binding`: only the compositor bindings are provided, and Steam's own binding for the Frame controllers is haptics-only anyway.
  - `Driver mouselaser has no suitable devices`: logged at load time, presumably because the driver provides no HMD. The device is added right after.

## Docs
- [docs/steamvr-primer.md](docs/steamvr-primer.md): **start here if SteamVR is new to you.** SteamVR internals (drivers, devices, roles, input bindings, the compositor's laser) and the Steam Frame specifics this project touches.
- [docs/how-it-works.md](docs/how-it-works.md): the driver internals, the pose math and the bindings.
- [docs/steam-frame-background.md](docs/steam-frame-background.md): what we learned about the Frame's display and input stack that led here.
- [docs/development-log.md](docs/development-log.md): where this came from, what was verified, and what is next.
- [CLAUDE.md](CLAUDE.md): context and ground rules for AI coding sessions on this repo.


## License
[MIT](LICENSE) © 2026 Andalu30 and contributors.

Third-party notes:
- **OpenVR SDK header** (`openvr_driver.h`, © Valve Corporation, BSD-3-Clause in the public [OpenVR SDK](https://github.com/ValveSoftware/openvr)). It is **not included** in this repo. The build uses the copy SteamVR ships on the headset.
- **SteamVR** is proprietary Valve software. This project doesn't contain or redistribute any of it. The input profile and bindings files follow SteamVR's documented JSON formats so the driver can interoperate.
- Not affiliated with or endorsed by Valve. "Steam", "SteamVR" and "Steam Frame" are trademarks of Valve Corporation.
