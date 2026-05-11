#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>
#include <vector>
#include "settings.h"
#include "../resources/resource_ids.h"

// Non-modal Settings window in Windows 11 Settings-app style.
// Changes are applied immediately; there is no OK/Cancel.

namespace SettingsWnd
{
    // Callback signature: called every time a language colour changes.
    // entries is the current (already-updated) list.
    using ApplyCallback = std::function<void(const std::vector<LangEntry>&)>;

    // Open (or bring-to-front) the Settings window.
    // hParent  — owner window (may be HWND_MESSAGE; nullptr is fine)
    // hInst    — application HINSTANCE
    // entries  — shared settings list (kept by reference; window writes directly to it)
    // onApply  — called whenever a colour changes
    void Open(HWND hParent, HINSTANCE hInst,
              std::vector<LangEntry>& entries,
              ApplyCallback onApply);

    // Close the window if it is open.
    void Close();

    // Returns true if the window currently exists.
    bool IsOpen();
}
