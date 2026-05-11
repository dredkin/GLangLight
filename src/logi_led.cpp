#include "logi_led.h"
#include "dbglog.h"
#include "../include/LogitechLEDLib.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // Sleep(), COLORREF

namespace LogiLedWrapper
{
    static bool s_ready = false;   // true only when SDK + at least one device is live

    // ── Internal helpers ──────────────────────────────────────────────────

    // After a successful LogiLedInit, probe the SDK version and try a test
    // SetLighting call to confirm a device is actually responding.
    static InitStatus ProbeDevice()
    {
        // 1. Confirm the SDK version is readable (proves G HUB is connected)
        int major = 0, minor = 0, build = 0;
        bool verOk = LogiLedGetSdkVersion(&major, &minor, &build);
        DBG_LOG("  LogiLedGetSdkVersion: %s  (%d.%d.%d)",
                verOk ? "OK" : "FAILED", major, minor, build);

        // 2. Set target to all devices, then probe with a black SetLighting call
        LogiLedSetTargetDevice(LOGI_DEVICETYPE_ALL);
        bool deviceOk = LogiLedSetLighting(0, 0, 0);
        DBG_LOG("  Probe SetLighting(0,0,0): %s", deviceOk ? "OK (device present)" : "FAILED (no device)");

        if (!deviceOk)
            return InitStatus::NoDevice;

        return InitStatus::OK;
    }

    // ── Public API ────────────────────────────────────────────────────────

    InitStatus Init()
    {
        s_ready = false;

        DBG_LOG("LogiLedWrapper::Init() -- calling LogiLedInitWithName...");
        bool ok = LogiLedInitWithName("GLanglight");
        DBG_LOG("  LogiLedInitWithName returned: %s", ok ? "TRUE" : "FALSE");

        if (!ok)
            return InitStatus::NoGHub;

        // Wait for SDK to fully connect to G HUB
        DBG_LOG("  Sleeping 500 ms for SDK readiness...");
        Sleep(500);

        InitStatus status = ProbeDevice();
        if (status == InitStatus::OK)
            s_ready = true;

        DBG_LOG("  Init status: %s",
                status == InitStatus::OK      ? "OK" :
                status == InitStatus::NoGHub  ? "NoGHub" : "NoDevice");
        return status;
    }

    InitStatus Reinit()
    {
        DBG_LOG("LogiLedWrapper::Reinit()");
        // Shutdown first to release existing handle
        LogiLedShutdown();
        s_ready = false;

        bool ok = LogiLedInitWithName("GLanglight");
        if (!ok)
            return InitStatus::NoGHub;

        Sleep(1000); // G HUB needs time to fully initialise after connect
        InitStatus status = ProbeDevice();
        if (status == InitStatus::OK)
            s_ready = true;
        return status;
    }

    bool IsReady()
    {
        return s_ready;
    }

    void ApplyColor(COLORREF color)
    {
        if (!s_ready)
        {
            DBG_LOG("ApplyColor: SDK not ready, skipping");
            return;
        }

        // Convert COLORREF (0x00BBGGRR) channels to 0-100 percentage
        int r = (GetRValue(color) * 100) / 255;
        int g = (GetGValue(color) * 100) / 255;
        int b = (GetBValue(color) * 100) / 255;

        DBG_LOG("ApplyColor(COLORREF=0x%06X -> r=%d g=%d b=%d)",
                (unsigned)color, r, g, b);

        // Re-assert target device — G HUB may reset it after profile events
        bool tgt = LogiLedSetTargetDevice(LOGI_DEVICETYPE_ALL);
        DBG_LOG("  SetTargetDevice returned: %s", tgt ? "TRUE" : "FALSE");

        bool lit = LogiLedSetLighting(r, g, b);
        DBG_LOG("  LogiLedSetLighting returned: %s", lit ? "TRUE" : "FALSE");

        // If the device stopped responding, mark SDK as not ready
        if (!lit)
        {
            DBG_LOG("  Device lost — marking SDK as not ready");
            s_ready = false;
        }
    }

    void Shutdown()
    {
        DBG_LOG("LogiLedWrapper::Shutdown()");
        s_ready = false;
        LogiLedShutdown();
    }
}
