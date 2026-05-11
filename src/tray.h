#pragma once
#include <windows.h>
#include "../resources/resource_ids.h"

// Custom window message sent by the tray icon
#define WM_TRAYICON  (WM_USER + 1)

// Context menu command IDs
#define IDM_EXIT     1001
#define IDM_SETTINGS 1002

namespace Tray
{
    // Create and register the tray icon on the given window.
    // The window will receive WM_TRAYICON messages.
    bool Create(HWND hwnd, HINSTANCE hInst);

    // Regenerate the tray icon using bgColor as background.
    // Draws a bold "G" in black (light background) or white (dark background).
    // Replaces the current tray icon in-place.
    void UpdateIcon(HWND hwnd, COLORREF bgColor);

    // Update the tooltip text shown on hover.
    void SetTooltip(HWND hwnd, const wchar_t* text);

    // Remove the tray icon (call before exit).
    void Destroy(HWND hwnd);

    // Show right-click context menu at current cursor position.
    void ShowContextMenu(HWND hwnd);
}
