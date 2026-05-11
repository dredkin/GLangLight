#include "tray.h"
#include <shellapi.h>
#include <cmath>    // sqrt (for luminance)

// Tray icon UID
static const UINT TRAY_UID   = 1;
static const int  ICON_SIZE  = 32;   // 32×32 px icon

// Cached icon handle — destroyed before creating a new one
static HICON s_hCurrentIcon = nullptr;

// ── Luminance helper ──────────────────────────────────────────────────────
// Returns perceived luminance in [0, 1] using the sRGB formula.
// Values > 0.5 are considered "light", < 0.5 are "dark".
static float Luminance(COLORREF c)
{
    float r = GetRValue(c) / 255.0f;
    float g = GetGValue(c) / 255.0f;
    float b = GetBValue(c) / 255.0f;
    // ITU-R BT.709 coefficients
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ── Build a 32×32 HICON with coloured background and a "G" letter ─────────
static HICON CreateColorIcon(COLORREF bgColor)
{
    const int sz = ICON_SIZE;

    // Create 32-bit DIB section (BGRA)
    BITMAPV5HEADER bmi = {};
    bmi.bV5Size        = sizeof(bmi);
    bmi.bV5Width       = sz;
    bmi.bV5Height      = -sz;   // top-down
    bmi.bV5Planes      = 1;
    bmi.bV5BitCount    = 32;
    bmi.bV5Compression = BI_BITFIELDS;
    bmi.bV5RedMask     = 0x00FF0000;
    bmi.bV5GreenMask   = 0x0000FF00;
    bmi.bV5BlueMask    = 0x000000FF;
    bmi.bV5AlphaMask   = 0xFF000000;

    HDC hScreen = GetDC(nullptr);
    void* pBits = nullptr;
    HBITMAP hColor = CreateDIBSection(hScreen, reinterpret_cast<BITMAPINFO*>(&bmi),
                                      DIB_RGB_COLORS, &pBits, nullptr, 0);
    HDC hDC = CreateCompatibleDC(hScreen);
    HGDIOBJ hOld = SelectObject(hDC, hColor);

    // ── 1. Draw rounded-rect background ───────────────────────────────
    HBRUSH hBg = CreateSolidBrush(bgColor);
    RECT rc    = { 0, 0, sz, sz };

    // Fill with transparent first
    {
        // Use AlphaBlend / manual alpha — simplest: just fill all pixels, set alpha=255 later
        FillRect(hDC, &rc, hBg);
    }
    DeleteObject(hBg);

    // ── 2. Choose letter colour by luminance ──────────────────────────
    COLORREF letterColor = (Luminance(bgColor) > 0.40f)
                           ? RGB(0, 0, 0)       // dark letter on light bg
                           : RGB(255, 255, 255); // white letter on dark bg

    // ── 3. Draw bold "G" ──────────────────────────────────────────────
    SetBkMode(hDC, TRANSPARENT);
    SetTextColor(hDC, letterColor);

    // Choose font size to fill roughly 80% of the icon
    int fontH = (sz * 80) / 100;
    HFONT hFont = CreateFontW(
        fontH, 0, 0, 0,
        FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI");

    if (!hFont)
        hFont = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));

    HGDIOBJ hOldFont = SelectObject(hDC, hFont);

    // Centre the letter
    SIZE textSize = {};
    GetTextExtentPoint32W(hDC, L"G", 1, &textSize);
    int x = (sz - textSize.cx) / 2;
    int y = (sz - textSize.cy) / 2;
    TextOutW(hDC, x, y, L"G", 1);

    SelectObject(hDC, hOldFont);
    DeleteObject(hFont);

    // ── 4. Set alpha = 255 for all pixels ─────────────────────────────
    // (DIB section starts with alpha=0; Windows tray needs alpha != 0 for colour icons)
    if (pBits)
    {
        DWORD* px = static_cast<DWORD*>(pBits);
        for (int i = 0; i < sz * sz; ++i)
            px[i] |= 0xFF000000;
    }

    SelectObject(hDC, hOld);
    DeleteDC(hDC);
    ReleaseDC(nullptr, hScreen);

    // ── 5. Build a monochrome mask (all black = fully colour icon) ────
    HBITMAP hMask = CreateBitmap(sz, sz, 1, 1, nullptr);
    {
        HDC hM = CreateCompatibleDC(nullptr);
        SelectObject(hM, hMask);
        RECT mr = { 0, 0, sz, sz };
        FillRect(hM, &mr, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        DeleteDC(hM);
    }

    // ── 6. Create HICON ───────────────────────────────────────────────
    ICONINFO ii   = {};
    ii.fIcon      = TRUE;
    ii.hbmMask    = hMask;
    ii.hbmColor   = hColor;
    HICON hIcon   = CreateIconIndirect(&ii);

    DeleteObject(hColor);
    DeleteObject(hMask);

    return hIcon;
}

// ── Public API ────────────────────────────────────────────────────────────

namespace Tray
{
    bool Create(HWND hwnd, HINSTANCE hInst)
    {
        // Build an initial icon (dark background as fallback)
        s_hCurrentIcon = CreateColorIcon(RGB(30, 30, 30));
        if (!s_hCurrentIcon)
        {
            s_hCurrentIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
            if (!s_hCurrentIcon)
                s_hCurrentIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }

        NOTIFYICONDATAW nid = {};
        nid.cbSize           = sizeof(nid);
        nid.hWnd             = hwnd;
        nid.uID              = TRAY_UID;
        nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon            = s_hCurrentIcon;
        wcscpy_s(nid.szTip, L"GLanglight");

        return Shell_NotifyIconW(NIM_ADD, &nid) == TRUE;
    }

    void UpdateIcon(HWND hwnd, COLORREF bgColor)
    {
        HICON hNew = CreateColorIcon(bgColor);
        if (!hNew) return;

        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = hwnd;
        nid.uID    = TRAY_UID;
        nid.uFlags = NIF_ICON;
        nid.hIcon  = hNew;
        Shell_NotifyIconW(NIM_MODIFY, &nid);

        // Destroy old icon after handing the new one to the tray
        if (s_hCurrentIcon)
            DestroyIcon(s_hCurrentIcon);
        s_hCurrentIcon = hNew;
    }

    void SetTooltip(HWND hwnd, const wchar_t* text)
    {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = hwnd;
        nid.uID    = TRAY_UID;
        nid.uFlags = NIF_TIP;
        wcscpy_s(nid.szTip, text);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void Destroy(HWND hwnd)
    {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = hwnd;
        nid.uID    = TRAY_UID;
        Shell_NotifyIconW(NIM_DELETE, &nid);

        if (s_hCurrentIcon)
        {
            DestroyIcon(s_hCurrentIcon);
            s_hCurrentIcon = nullptr;
        }
    }

    void ShowContextMenu(HWND hwnd)
    {
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Settings...");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

        SetForegroundWindow(hwnd);

        POINT pt;
        GetCursorPos(&pt);
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);

        DestroyMenu(hMenu);
    }
}
