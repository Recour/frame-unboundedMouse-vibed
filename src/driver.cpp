// SPDX-License-Identifier: MIT
// mouselaser: a virtual SteamVR controller whose pose is aimed by a physical mouse,
// so the SteamVR laser pointer can be driven by the mouse across every overlay.
//
// Toggle with the mouse's forward side button (BTN_EXTRA by default). While active the
// mouse is grabbed (see mouse_reader_*.cpp) so the desktop stops seeing it; while inactive the device
// stays connected (so the compositor keeps it as a pointer source), but parks its ray
// pointing at the sky and releases every button.
// It is a stylus while off, so it never takes a real controller's hand slot, and switches to
// a hand role while on, because the compositor only drags overlays with hands (or the head).
//
// EXPERIMENTAL and AI-generated ("vibecoded"); see README.md before relying on it.

#define _USE_MATH_DEFINES // M_PI on MSVC
#include <openvr_driver.h>

#include "mouse_reader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#if defined(_WIN32)
#define MOUSELASER_EXPORT extern "C" __declspec(dllexport)
#else
#define MOUSELASER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace vr;

static const char *k_section = "driver_mouselaser";
static const char *k_version = "0.7.0-experimental";

void Log(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (VRDriverLog())
        VRDriverLog()->Log(buf);
}

struct Settings
{
    float sensitivityDeg = 0.05f; // degrees of ray rotation per mouse count
    int toggleButton = kBtnExtra;
    int backButton = kBtnSide;    // sends the compositor's laser "back"; -1 = none
    // Stylus (5) gets its own /user/stylus path. A hand role (1/2) shares the slot with the
    // real controller of that hand, so the device disconnects while off (pre-0.5.0 behaviour).
    int role = TrackedControllerRole_Stylus;
    // Role while laser mode is on (0 = keep `role`). The compositor attaches dragged overlays
    // to /user/hand/{left,right} or /user/head only, so a stylus drag follows the head.
    int activeRole = TrackedControllerRole_LeftHand;
    std::string nameFilter;      // substring of the mouse device name; empty = first mouse found
    // Where the ray starts, relative to the head, in the head's horizontal frame (metres).
    // The defaults put it roughly where a hand would hold a controller.
    float originOffsetX = 0.15f;  // right
    float originOffsetY = -0.35f; // up (negative = below the head)
    float originOffsetZ = 0.30f;  // forward
    // true: the origin is fixed in the room when laser mode turns on, so moving your head
    // can't move the pointer. false: it follows the head (old behaviour).
    bool originLatched = true;
    // On activation the ray aims at the point this far along your gaze, so the pointer starts
    // where you are looking even though the origin is below your head.
    float aimDistance = 2.0f;
    bool invertY = false;
    // How wheel notches drive the virtual thumbstick:
    //  smooth: each notch bumps the stick, which eases back to centre; spinning holds it.
    //  step:   each notch is one flick, pushed for wheelPressMs then centred for wheelReleaseMs.
    bool wheelSmooth = true;
    float wheelSmoothMin = 0.7f;      // deflection right after a single notch
    float wheelSmoothImpulse = 0.3f;  // extra deflection per further notch
    int wheelSmoothHoldMs = 80;       // no decay this long after a notch
    int wheelSmoothDecayMs = 150;     // exponential decay time constant
    int wheelPressMs = 90;
    int wheelReleaseMs = 60;
    float wheelDeflection = 1.0f;     // maximum deflection (both modes)
    int wheelMaxQueued = 10; // notches buffered while flicks are still playing out

    void Load()
    {
        EVRSettingsError err;
        IVRSettings *s = VRSettings();
        float f = s->GetFloat(k_section, "sensitivity", &err);
        if (err == VRSettingsError_None) sensitivityDeg = f;
        int32_t i = s->GetInt32(k_section, "toggleButton", &err);
        if (err == VRSettingsError_None) toggleButton = i;
        i = s->GetInt32(k_section, "backButton", &err);
        if (err == VRSettingsError_None) backButton = i;
        i = s->GetInt32(k_section, "role", &err);
        if (err == VRSettingsError_None) role = i;
        i = s->GetInt32(k_section, "activeRole", &err);
        if (err == VRSettingsError_None) activeRole = i;
        char buf[256] = {};
        s->GetString(k_section, "deviceNameFilter", buf, sizeof(buf), &err);
        if (err == VRSettingsError_None) nameFilter = buf;
        f = s->GetFloat(k_section, "originOffsetY", &err);
        if (err == VRSettingsError_None) originOffsetY = f;
        f = s->GetFloat(k_section, "originOffsetX", &err);
        if (err == VRSettingsError_None) originOffsetX = f;
        f = s->GetFloat(k_section, "originOffsetZ", &err);
        if (err == VRSettingsError_None) originOffsetZ = f;
        f = s->GetFloat(k_section, "aimDistance", &err);
        if (err == VRSettingsError_None) aimDistance = f;
        bool latched = s->GetBool(k_section, "originLatched", &err);
        if (err == VRSettingsError_None) originLatched = latched;
        bool b = s->GetBool(k_section, "invertY", &err);
        if (err == VRSettingsError_None) invertY = b;
        i = s->GetInt32(k_section, "wheelPressMs", &err);
        if (err == VRSettingsError_None) wheelPressMs = i;
        i = s->GetInt32(k_section, "wheelReleaseMs", &err);
        if (err == VRSettingsError_None) wheelReleaseMs = i;
        f = s->GetFloat(k_section, "wheelDeflection", &err);
        if (err == VRSettingsError_None) wheelDeflection = f;
        i = s->GetInt32(k_section, "wheelMaxQueued", &err);
        if (err == VRSettingsError_None) wheelMaxQueued = i;
        s->GetString(k_section, "wheelMode", buf, sizeof(buf), &err);
        if (err == VRSettingsError_None) wheelSmooth = strcmp(buf, "step") != 0;
        f = s->GetFloat(k_section, "wheelSmoothMin", &err);
        if (err == VRSettingsError_None) wheelSmoothMin = f;
        f = s->GetFloat(k_section, "wheelSmoothImpulse", &err);
        if (err == VRSettingsError_None) wheelSmoothImpulse = f;
        i = s->GetInt32(k_section, "wheelSmoothHoldMs", &err);
        if (err == VRSettingsError_None) wheelSmoothHoldMs = i;
        i = s->GetInt32(k_section, "wheelSmoothDecayMs", &err);
        if (err == VRSettingsError_None) wheelSmoothDecayMs = i;
    }
};

// Turns wheel notches on one axis into thumbstick deflection, in "smooth" or "step" mode
// (see Settings). A change of direction drops whatever was still pending the other way.
class StickStepper
{
public:
    float Update(int ticks, const Settings &s)
    {
        using clock = std::chrono::steady_clock;
        clock::time_point now = clock::now();
        return s.wheelSmooth ? UpdateSmooth(ticks, s, now) : UpdateStep(ticks, s, now);
    }

    void Reset() { m_queued = 0, m_pressed = false, m_mag = 0; }

private:
    float UpdateSmooth(int ticks, const Settings &s, std::chrono::steady_clock::time_point now)
    {
        float dt = std::chrono::duration<float>(now - m_last).count();
        m_last = now;
        if (ticks != 0)
        {
            float d = ticks > 0 ? 1.f : -1.f;
            if (d != m_dir) m_dir = d, m_mag = 0;
            float bumped = m_mag > 0 ? m_mag + s.wheelSmoothImpulse * std::abs(ticks)
                                     : s.wheelSmoothMin + s.wheelSmoothImpulse * (std::abs(ticks) - 1);
            m_mag = std::min(s.wheelDeflection, std::max(bumped, s.wheelSmoothMin));
            m_lastNotch = now;
        }
        else if (m_mag > 0 && now - m_lastNotch > std::chrono::milliseconds(s.wheelSmoothHoldMs))
        {
            m_mag *= expf(-dt * 1000.f / float(std::max(1, s.wheelSmoothDecayMs)));
            if (m_mag < 0.1f) m_mag = 0;
        }
        return m_dir * m_mag;
    }

    float UpdateStep(int ticks, const Settings &s, std::chrono::steady_clock::time_point now)
    {
        if (ticks != 0)
        {
            float d = ticks > 0 ? 1.f : -1.f;
            if (d != m_dir)
            {
                m_dir = d;
                m_queued = 0;
                m_pressed = false;
                m_until = now;
            }
            m_queued = std::min(m_queued + std::abs(ticks), s.wheelMaxQueued);
        }
        if (now >= m_until)
        {
            if (m_pressed)
            {
                m_pressed = false;
                m_until = now + std::chrono::milliseconds(s.wheelReleaseMs);
            }
            else if (m_queued > 0)
            {
                m_queued--;
                m_pressed = true;
                m_until = now + std::chrono::milliseconds(s.wheelPressMs);
            }
        }
        return m_pressed ? m_dir * s.wheelDeflection : 0.f;
    }

    // step mode
    int m_queued = 0;
    bool m_pressed = false;
    float m_dir = 0;
    std::chrono::steady_clock::time_point m_until;
    // smooth mode
    float m_mag = 0;
    std::chrono::steady_clock::time_point m_last, m_lastNotch;
};

class MouseLaserDevice final : public ITrackedDeviceServerDriver
{
public:
    explicit MouseLaserDevice(const Settings &s) : m_settings(s) {}

    EVRInitError Activate(uint32_t id) override
    {
        m_id = id;
        PropertyContainerHandle_t c = VRProperties()->TrackedDeviceToPropertyContainer(id);
        m_container = c;
        VRProperties()->SetStringProperty(c, Prop_ModelNumber_String, "Mouse Laser (virtual)");
        VRProperties()->SetStringProperty(c, Prop_ManufacturerName_String, "frame-unboundedMouse-vibed");
        VRProperties()->SetStringProperty(c, Prop_ControllerType_String, "mouselaser");
        VRProperties()->SetStringProperty(c, Prop_InputProfilePath_String, "{mouselaser}/input/mouselaser_profile.json");
        VRProperties()->SetStringProperty(c, Prop_RenderModelName_String, "{mouselaser}mouselaser_none");
        VRProperties()->SetInt32Property(c, Prop_ControllerRoleHint_Int32, m_settings.role);
        m_currentRole = m_settings.role;
        VRProperties()->SetBoolProperty(c, Prop_DeviceProvidesBatteryStatus_Bool, false);

        VRDriverInput()->CreateBooleanComponent(c, "/input/trigger/click", &m_trigger);
        VRDriverInput()->CreateScalarComponent(c, "/input/trigger/value", &m_triggerValue,
                                               VRScalarType_Absolute, VRScalarUnits_NormalizedOneSided);
        VRDriverInput()->CreateBooleanComponent(c, "/input/a/click", &m_a);
        VRDriverInput()->CreateBooleanComponent(c, "/input/b/click", &m_b);
        VRDriverInput()->CreateBooleanComponent(c, "/input/back/click", &m_back);
        VRDriverInput()->CreateBooleanComponent(c, "/input/grip/click", &m_grip);
        VRDriverInput()->CreateBooleanComponent(c, "/input/thumbstick/touch", &m_stickTouch);
        VRDriverInput()->CreateScalarComponent(c, "/input/thumbstick/x", &m_stickX,
                                               VRScalarType_Absolute, VRScalarUnits_NormalizedTwoSided);
        VRDriverInput()->CreateScalarComponent(c, "/input/thumbstick/y", &m_stickY,
                                               VRScalarType_Absolute, VRScalarUnits_NormalizedTwoSided);

        MouseReaderConfig cfg;
        cfg.toggleButton = m_settings.toggleButton;
        cfg.backButton = m_settings.backButton;
        cfg.nameFilter = m_settings.nameFilter;
        m_mouse.Start(cfg);
        Log("activated as device %u, role %d\n", id, m_settings.role);
        return VRInitError_None;
    }

    void Deactivate() override
    {
        m_mouse.Stop();
        m_id = k_unTrackedDeviceIndexInvalid;
    }

    void EnterStandby() override {}
    void *GetComponent(const char *) override { return nullptr; }
    void DebugRequest(const char *, char *resp, uint32_t size) override
    {
        if (size) resp[0] = 0;
    }

    DriverPose_t GetPose() override
    {
        std::lock_guard<std::mutex> lock(m_poseMutex);
        return m_pose;
    }

    uint32_t Id() const { return m_id; }
    bool Active() const { return m_mouse.active; }

    void RunFrame()
    {
        if (m_id == k_unTrackedDeviceIndexInvalid) return;

        TrackedDevicePose_t hmd = {};
        VRServerDriverHost()->GetRawTrackedDevicePoses(0, &hmd, 1);
        const HmdMatrix34_t &m = hmd.mDeviceToAbsoluteTracking;

        bool active = m_mouse.active;
        // A hand role would steal the real controller's slot while connected, so it only
        // connects while active. Other roles stay connected: a device that disappears or goes
        // invalid is dropped as the laser pointer until a new "user interaction" (about 10 s of
        // quiet), so a quick off/on would grab the mouse with no laser.
        bool present = active || IsRoleAllowedAsHand(ETrackedControllerRole(m_settings.role));
        if (active != m_wasActive)
        {
            m_wasActive = active;
            int role = active && m_settings.activeRole > 0 ? m_settings.activeRole : m_settings.role;
            if (role != m_currentRole)
            {
                m_currentRole = role;
                VRProperties()->SetInt32Property(m_container, Prop_ControllerRoleHint_Int32, role);
                Log("role %d\n", role);
            }
        }
        // The ray origin sits below and in front of the head, like a held controller. It is
        // computed in the head's horizontal frame so head roll and pitch don't move it.
        auto computeOrigin = [&](float out[3], float hf[3])
        {
            float rx = m.m[0][0], rz = m.m[2][0];
            float len = sqrtf(rx * rx + rz * rz);
            if (len < 1e-4f) rx = 1, rz = 0, len = 1;
            rx /= len, rz /= len;
            hf[0] = rz, hf[1] = 0, hf[2] = -rx; // horizontal forward
            out[0] = m.m[0][3] + rx * m_settings.originOffsetX + hf[0] * m_settings.originOffsetZ;
            out[1] = m.m[1][3] + m_settings.originOffsetY;
            out[2] = m.m[2][3] + rz * m_settings.originOffsetX + hf[2] * m_settings.originOffsetZ;
        };

        if (m_mouse.toggled.exchange(false) && active && hmd.bPoseIsValid)
        {
            float hf[3];
            computeOrigin(m_origin, hf);
            // Aim at the point aimDistance metres along the gaze, so the pointer starts where
            // you are looking.
            float fx = -m.m[0][2], fy = -m.m[1][2], fz = -m.m[2][2];
            float dx = m.m[0][3] + fx * m_settings.aimDistance - m_origin[0];
            float dy = m.m[1][3] + fy * m_settings.aimDistance - m_origin[1];
            float dz = m.m[2][3] + fz * m_settings.aimDistance - m_origin[2];
            float dl = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dl < 1e-4f) dx = fx, dy = fy, dz = fz, dl = 1;
            m_yaw = atan2f(-dx, -dz);
            m_pitch = asinf(fmaxf(-1.f, fminf(1.f, dy / dl)));
        }

        const float k = m_settings.sensitivityDeg * float(M_PI) / 180.f;
        m_yaw -= m_mouse.dx.exchange(0) * k;
        m_pitch -= m_mouse.dy.exchange(0) * k * (m_settings.invertY ? -1.f : 1.f);
        const float limit = 89.f * float(M_PI) / 180.f;
        m_pitch = fmaxf(-limit, fminf(limit, m_pitch));

        DriverPose_t pose = {};
        pose.qWorldFromDriverRotation.w = 1;
        pose.qDriverFromHeadRotation.w = 1;
        pose.deviceIsConnected = present;
        pose.poseIsValid = present && hmd.bPoseIsValid;
        pose.result = pose.poseIsValid ? TrackingResult_Running_OK : TrackingResult_Running_OutOfRange;
        float origin[3], hfUnused[3];
        if (active && m_settings.originLatched)
            std::copy(m_origin, m_origin + 3, origin);
        else
            computeOrigin(origin, hfUnused);
        pose.vecPosition[0] = origin[0];
        pose.vecPosition[1] = origin[1];
        pose.vecPosition[2] = origin[2];
        // q = yaw(Y) * pitch(X). While off the ray is parked straight up, where it hits nothing.
        float yaw = active ? m_yaw : 0.f, pitch = active ? m_pitch : float(M_PI) / 2;
        float cy = cosf(yaw / 2), sy = sinf(yaw / 2);
        float cp = cosf(pitch / 2), sp = sinf(pitch / 2);
        pose.qRotation.w = cy * cp;
        pose.qRotation.x = cy * sp;
        pose.qRotation.y = sy * cp;
        pose.qRotation.z = -sy * sp;
        {
            std::lock_guard<std::mutex> lock(m_poseMutex);
            m_pose = pose;
        }
        VRServerDriverHost()->TrackedDevicePoseUpdated(m_id, pose, sizeof(DriverPose_t));

        // Wheel notches become thumbstick flicks: vertical wheel -> Y, tilt wheel -> X.
        if (!active) m_stepY.Reset(), m_stepX.Reset();
        float stickY = m_stepY.Update(m_mouse.wheel.exchange(0), m_settings);
        float stickX = m_stepX.Update(m_mouse.hwheel.exchange(0), m_settings);

        bool l = active && m_mouse.left;
        VRDriverInput()->UpdateBooleanComponent(m_trigger, l, 0);
        VRDriverInput()->UpdateScalarComponent(m_triggerValue, l ? 1.f : 0.f, 0);
        VRDriverInput()->UpdateBooleanComponent(m_a, active && m_mouse.right, 0);
        VRDriverInput()->UpdateBooleanComponent(m_b, active && m_mouse.middle, 0);
        VRDriverInput()->UpdateBooleanComponent(m_back, active && m_mouse.back, 0);
        VRDriverInput()->UpdateBooleanComponent(m_grip, active, 0); // holds "quick mouse" on
        VRDriverInput()->UpdateBooleanComponent(m_stickTouch, stickX != 0.f || stickY != 0.f, 0);
        VRDriverInput()->UpdateScalarComponent(m_stickX, stickX, 0);
        VRDriverInput()->UpdateScalarComponent(m_stickY, stickY, 0);
    }

private:
    Settings m_settings;
    MouseReader m_mouse;
    uint32_t m_id = k_unTrackedDeviceIndexInvalid;
    PropertyContainerHandle_t m_container = k_ulInvalidPropertyContainer;
    bool m_wasActive = false;
    int m_currentRole = 0;
    std::mutex m_poseMutex;
    DriverPose_t m_pose = {};
    float m_yaw = 0, m_pitch = 0;
    float m_origin[3] = {0, 0, 0}; // ray origin latched on activation
    StickStepper m_stepX, m_stepY;
    VRInputComponentHandle_t m_trigger = 0, m_triggerValue = 0, m_a = 0, m_b = 0, m_back = 0, m_grip = 0;
    VRInputComponentHandle_t m_stickTouch = 0, m_stickX = 0, m_stickY = 0;
};

class MouseLaserProvider : public IServerTrackedDeviceProvider
{
public:
    EVRInitError Init(IVRDriverContext *ctx) override
    {
        VR_INIT_SERVER_DRIVER_CONTEXT(ctx);
        Log("version %s\n", k_version);
        Settings s;
        s.Load();
        EVRSettingsError err;
        bool enable = VRSettings()->GetBool(k_section, "enable", &err);
        if (err == VRSettingsError_None && !enable)
        {
            Log("disabled by setting\n");
            return VRInitError_None;
        }
        m_device = new MouseLaserDevice(s);
        if (!VRServerDriverHost()->TrackedDeviceAdded("mouselaser-0", TrackedDeviceClass_Controller, m_device))
            Log("TrackedDeviceAdded failed\n");
        return VRInitError_None;
    }

    void Cleanup() override
    {
        delete m_device;
        m_device = nullptr;
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }

    const char *const *GetInterfaceVersions() override { return k_InterfaceVersions; }

    void RunFrame() override
    {
        if (m_device) m_device->RunFrame();
        // Diagnostics: events about our device, and the global ones that decide which device
        // the compositor treats as the laser. There is no direct "laser is up" signal.
        VREvent_t ev;
        while (VRServerDriverHost()->PollNextEvent(&ev, sizeof(ev)))
        {
            bool ours = m_device && ev.trackedDeviceIndex == m_device->Id();
            bool global = ev.eventType == VREvent_TrackedDeviceUserInteractionStarted ||
                          ev.eventType == VREvent_TrackedDeviceUserInteractionEnded ||
                          ev.eventType == VREvent_TrackedDeviceRoleChanged ||
                          ev.eventType == VREvent_DashboardActivated ||
                          ev.eventType == VREvent_DashboardDeactivated;
            if (ours || global)
                Log("event %u device %u (%s)\n", ev.eventType, ev.trackedDeviceIndex,
                    m_device && m_device->Active() ? "active" : "off");
        }
    }

    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {}
    void LeaveStandby() override {}

private:
    MouseLaserDevice *m_device = nullptr;
};

static MouseLaserProvider g_provider;

MOUSELASER_EXPORT void *HmdDriverFactory(const char *iface, int *ret)
{
    if (strcmp(iface, IServerTrackedDeviceProvider_Version) == 0)
        return &g_provider;
    if (ret) *ret = VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
