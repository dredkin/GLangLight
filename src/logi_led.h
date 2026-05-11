#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // COLORREF

// Logitech LED wrapper — sets full-keyboard colour from a COLORREF value.

namespace LogiLedWrapper
{
    enum class InitStatus
    {
        OK,                // SDK initialised and at least one device responded
        NoGHub,            // LogiLedInitWithName returned false (G HUB / LGS not running or SDK DLL missing)
        NoDevice,          // SDK connected to G HUB but no compatible RGB device is responding
    };

    // Initialise the Logitech LED SDK.
    // Returns InitStatus::OK on full success.
    // On NoGHub or NoDevice the app can still run (device may connect later),
    // but lighting calls will silently do nothing until Reinit() succeeds.
    InitStatus Init();

    // Try to reconnect (call periodically if Init returned NoDevice).
    InitStatus Reinit();

    // Returns true if Init() previously returned OK and the SDK is still live.
    bool IsReady();

    // Apply full-keyboard colour from a Win32 COLORREF (0x00BBGGRR).
    // No-ops silently if the SDK is not ready.
    void ApplyColor(COLORREF color);

    // Shutdown the Logitech LED SDK.
    void Shutdown();
}
