// settings_dlg.cpp
// Non-modal Settings window in Windows 11 Settings-app style.
// All child controls are drawn as pure GDI (no child HWNDs for buttons/toggle);
// clicks are detected via hit-testing in WM_LBUTTONDOWN/UP.
// The language list is an OwnerDraw listbox (LBS_OWNERDRAWFIXED).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <objbase.h>
#include <gdiplus.h>
#include "settings_dlg.h"
#include "settings.h"
#include "dbglog.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

// ── DWMWA_WINDOW_CORNER_PREFERENCE (Win11 SDK already has this) ───────────
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

// ── Palette ───────────────────────────────────────────────────────────────
static const COLORREF CLR_BG       = RGB(249,249,249);
static const COLORREF CLR_CARD     = RGB(255,255,255);
static const COLORREF CLR_CARD_BDR = RGB(229,229,229);
static const COLORREF CLR_BODY_TXT = RGB( 26, 26, 26);
static const COLORREF CLR_SEC_TXT  = RGB( 96, 96, 96);
static const COLORREF CLR_DIV      = RGB(229,229,229);
static const COLORREF CLR_ACCENT   = RGB(  0,120,212);
static const COLORREF CLR_A_HOV    = RGB( 16,110,190);
static const COLORREF CLR_A_PRE    = RGB(  0, 95,164);
static const COLORREF CLR_TOG_OFF  = RGB(138,138,138);
static const COLORREF CLR_THUMB    = RGB(255,255,255);
static const COLORREF CLR_BTN_BG   = RGB(255,255,255);
static const COLORREF CLR_BTN_HOV  = RGB(246,246,246);
static const COLORREF CLR_BTN_PRE  = RGB(237,237,237);
static const COLORREF CLR_BTN_BDR  = RGB(173,173,173);
static const COLORREF CLR_TXT_INV  = RGB(255,255,255);
static const COLORREF CLR_SWATCH_BDR = RGB(180,180,180);

// ── Window metrics ────────────────────────────────────────────────────────
static const int WND_W = 520;
static const int WND_H = 390;

// Card rectangles (client pixels)
static const RECT RC_CARD_L = { 16,  56, 504, 288 };  // lighting card
static const RECT RC_CARD_G = { 16, 316, 504, 372 };  // general card – 56px tall, one row

// Hit rectangles for interactive elements (within client coords)
static RECT s_rcColorBtn   = {  28, 252,  158, 280 };  // "Change colour…"
static RECT s_rcToggle     = { 448, 318,  492, 342 };  // toggle pill

// ── Control IDs ──────────────────────────────────────────────────────────
#define IDC_LB  2001

// ── OwnerDraw item metrics ──────────────────────────────────────────────
static const int LB_ITEM_H = 28;          // height of each listbox item
static const int LB_SWATCH = 16;          // swatch size (square)
static const int LB_SWATCH_X = 10;        // swatch left margin
static const int LB_TEXT_X   = 38;        // text left offset
static const int LB_TEXT_Y   = 4;         // text top offset within item

// ── Singleton ────────────────────────────────────────────────────────────
static HWND s_hwnd = nullptr;

// ── Per-window state ──────────────────────────────────────────────────────
struct WndState
{
    std::vector<LangEntry>*    pEntries  = nullptr;
    SettingsWnd::ApplyCallback onApply;
    HINSTANCE                  hInst     = nullptr;
    ULONG_PTR                  gdipToken = 0;
    HFONT                      hFontN    = nullptr;
    HFONT                      hFontS    = nullptr;   // semibold
    HBRUSH                     hBrBg     = nullptr;
    HBRUSH                     hBrCard   = nullptr;

    // Hover/press state for GDI buttons
    bool colHov = false, colPre = false;
    bool togHov = false, togPre = false;

    COLORREF custColors[16] = {
        RGB(255,255,255), RGB(255,0,0),   RGB(0,0,255),   RGB(0,255,0),
        RGB(255,255,0),   RGB(0,255,255), RGB(255,0,255), RGB(128,0,0),
        RGB(0,128,0),     RGB(0,0,128),   RGB(128,128,0), RGB(0,128,128),
        RGB(128,0,128),   RGB(192,192,192),RGB(128,128,128),RGB(0,0,0)
    };
};

// ── GDI helpers ───────────────────────────────────────────────────────────
static void FillRR(HDC hdc, RECT r, int rx, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    HRGN   rg = CreateRoundRectRgn(r.left, r.top, r.right, r.bottom, rx*2, rx*2);
    FillRgn(hdc, rg, br);
    DeleteObject(rg);
    DeleteObject(br);
}
static void FrameRR(HDC hdc, RECT r, int rx, COLORREF c)
{
    HPEN   p  = CreatePen(PS_SOLID, 1, c);
    HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    HGDIOBJ op= SelectObject(hdc, p);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, rx*2, rx*2);
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(p);
}
static bool PtInR(const RECT& r, int x, int y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

static HFONT MakeFont(int pt, bool semi)
{
    const wchar_t* faces[] = { L"Segoe UI Variable Text", L"Segoe UI" };
    HDC hdc = GetDC(nullptr);
    int h   = -MulDiv(pt, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);
    for (auto* f : faces)
    {
        HFONT hf = CreateFontW(h,0,0,0, semi ? FW_SEMIBOLD : FW_NORMAL,
                                0,0,0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH|FF_SWISS, f);
        if (hf) return hf;
    }
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

// ── Draw helpers ─────────────────────────────────────────────────────────
static void DrawBtn(HDC hdc, RECT r, const wchar_t* txt,
                    bool accent, bool hov, bool pre,
                    HFONT hf, HBRUSH bgBr)
{
    COLORREF fill = accent
        ? (pre ? CLR_A_PRE : hov ? CLR_A_HOV : CLR_ACCENT)
        : (pre ? CLR_BTN_PRE : hov ? CLR_BTN_HOV : CLR_BTN_BG);
    COLORREF bdr  = accent ? fill : CLR_BTN_BDR;
    COLORREF tc   = accent ? CLR_TXT_INV : CLR_BODY_TXT;

    FillRect(hdc, &r, bgBr);
    FillRR(hdc, r, 5, fill);
    FrameRR(hdc, r, 5, bdr);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, tc);
    SelectObject(hdc, hf);
    DrawTextW(hdc, txt, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

// Helper: build a rounded-rectangle GraphicsPath with equal corner radius
static void AddRoundRect(Gdiplus::GraphicsPath& path,
                         Gdiplus::REAL x, Gdiplus::REAL y,
                         Gdiplus::REAL w, Gdiplus::REAL h,
                         Gdiplus::REAL r)
{
    path.AddArc(x,         y,         r*2, r*2,  180,  90);
    path.AddArc(x+w-r*2,   y,         r*2, r*2,  270,  90);
    path.AddArc(x+w-r*2,   y+h-r*2,   r*2, r*2,    0,  90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2,   90,  90);
    path.CloseFigure();
}

static Gdiplus::Color ToGdip(COLORREF c)
{
    return Gdiplus::Color(GetRValue(c), GetGValue(c), GetBValue(c));
}

static void DrawToggle(HDC hdc, RECT r, bool on, bool hov, bool pre,
                       HBRUSH bgBr)
{
    using namespace Gdiplus;

    COLORREF pillC = on
        ? (pre ? CLR_A_PRE : hov ? CLR_A_HOV : CLR_ACCENT)
        : (pre ? RGB(100,100,100) : hov ? RGB(120,120,120) : CLR_TOG_OFF);

    // Erase background with parent brush
    FillRect(hdc, &r, bgBr);

    // Pill geometry: 44 × 24, fully pill-shaped (radius = 12)
    const REAL PW = 44, PH = 24, PR = PH / 2.0f;
    REAL px = (REAL)r.left, py = (REAL)r.top;

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    // Draw pill
    GraphicsPath pillPath;
    AddRoundRect(pillPath, px, py, PW, PH, PR);
    SolidBrush pillBrush(ToGdip(pillC));
    g.FillPath(&pillBrush, &pillPath);

    // Draw thumb (circle, 16×16, 4px margin from edge)
    const REAL TS = 16, TM = 4;
    REAL tx = on ? px + PW - TM - TS : px + TM;
    REAL ty = py + TM;
    SolidBrush thumbBrush(ToGdip(CLR_THUMB));
    g.FillEllipse(&thumbBrush, tx, ty, TS, TS);
}

// Draws a single owner-drawn listbox row
static void DrawSwatch(HDC hdc, const RECT& r, const LangEntry* e, bool sel)
{
    COLORREF c = e ? e->color : RGB(200,200,200);

    // Fill background
    HBRUSH bgBr = CreateSolidBrush(sel ? CLR_ACCENT : CLR_CARD);
    FillRect(hdc, &r, bgBr);
    DeleteObject(bgBr);

    // Colour swatch square (vertically centred)
    int cy = (r.top + r.bottom) / 2;
    RECT sq = { r.left + LB_SWATCH_X,      cy - LB_SWATCH/2,
                r.left + LB_SWATCH_X + LB_SWATCH, cy + LB_SWATCH/2 };

    HBRUSH brSwatch = CreateSolidBrush(c);
    FillRect(hdc, &sq, brSwatch);
    DeleteObject(brSwatch);

    HPEN pen = CreatePen(PS_SOLID, 1, CLR_SWATCH_BDR);
    HGDIOBJ oldPen   = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, sq.left, sq.top, sq.right, sq.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, sel ? CLR_TXT_INV : CLR_BODY_TXT);
    SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));

    // Hex code – right-aligned, fixed width column
    static const int HEX_W = 64;  // px reserved for "#RRGGBB"
    wchar_t hex[16];
    swprintf_s(hex, L"#%02X%02X%02X",
               GetRValue(c), GetGValue(c), GetBValue(c));
    RECT hexRect = { r.right - HEX_W - 8, r.top, r.right - 8, r.bottom };
    DrawTextW(hdc, hex, -1, &hexRect,
              DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    // Language name – left-aligned, clipped before hex column
    if (e)
    {
        RECT nameRect = { r.left + LB_TEXT_X, r.top,
                          r.right - HEX_W - 12, r.bottom };
        DrawTextW(hdc, e->name.c_str(), -1, &nameRect,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
}

// ── Listbox subclass – suppress system NC border ─────────────────────────
static LRESULT CALLBACK LB_SubclassProc(HWND hwnd, UINT msg, WPARAM wP, LPARAM lP,
                                         UINT_PTR /*uid*/, DWORD_PTR /*ref*/)
{
    if (msg == WM_NCPAINT) return 0;   // don't draw any non-client border
    return DefSubclassProc(hwnd, msg, wP, lP);
}

// ── OwnerDraw helpers ─────────────────────────────────────────────────────
static void LB_FillItems(HWND hLB, const std::vector<LangEntry>& entries)
{
    // Store each entry's full display string as item data.
    // We store a pointer to the entry's name+hex colour in the item data
    // and build the display text on-the-fly in WM_DRAWITEM.
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        wchar_t hex[16];
        swprintf_s(hex, L"#%02X%02X%02X",
                   GetRValue(e.color), GetGValue(e.color), GetBValue(e.color));

        // Combine name + hex for display (stored as item data via WM_SETITEMDATA)
        std::wstring display = e.name + L"   " + hex;

        int idx = (int)SendMessageW(hLB, LB_ADDSTRING, 0,
                                    reinterpret_cast<LPARAM>(display.c_str()));
        // Remember the pointer to the string allocated from the vector entry
        // We'll use item data to store a pointer back to the LangEntry so
        // WM_DRAWITEM can access the colour.  Because the vector is stable,
        // we store a pointer to the entry itself.
        SendMessageW(hLB, LB_SETITEMDATA, idx,
                     reinterpret_cast<LPARAM>(&entries[i]));
    }
}

static LangEntry* LB_GetEntry(HWND hLB, int idx)
{
    if (idx < 0) return nullptr;
    LRESULT data = SendMessageW(hLB, LB_GETITEMDATA, idx, 0);
    return (data == LB_ERR) ? nullptr : reinterpret_cast<LangEntry*>(data);
}

// ── Trigger colour picker ─────────────────────────────────────────────────
static void OpenColorPicker(HWND hwnd, WndState* s)
{
    HWND hLB = GetDlgItem(hwnd, IDC_LB);
    int sel = (int)SendMessageW(hLB, LB_GETCURSEL, 0, 0);
    if (sel < 0)
    {
        MessageBoxW(hwnd, L"Please select a language row first.",
                    L"GLanglight", MB_ICONINFORMATION|MB_OK);
        return;
    }
    LangEntry* e = LB_GetEntry(hLB, sel);
    if (!e) return;

    CHOOSECOLORW cc = {};
    cc.lStructSize  = sizeof(cc);
    cc.hwndOwner    = hwnd;
    cc.lpCustColors = s->custColors;
    cc.rgbResult    = e->color;
    cc.Flags        = CC_FULLOPEN|CC_RGBINIT;
    if (ChooseColorW(&cc))
    {
        e->color = cc.rgbResult;
        Settings::Save(*s->pEntries);

        // Repopulate listbox to reflect new hex colour in item text
        SendMessageW(hLB, LB_RESETCONTENT, 0, 0);
        LB_FillItems(hLB, *s->pEntries);
        SendMessageW(hLB, LB_SETCURSEL, sel, 0);

        InvalidateRect(hwnd, nullptr, FALSE);
        if (s->onApply) s->onApply(*s->pEntries);
    }
}

// ── Window procedure ──────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wP, LPARAM lP)
{
    WndState* s = reinterpret_cast<WndState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    // ── Create ────────────────────────────────────────────────────────
    case WM_CREATE:
    {
        s = reinterpret_cast<WndState*>(
            reinterpret_cast<CREATESTRUCTW*>(lP)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));

        // Win11 rounded corners
        DWORD pref = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &pref, sizeof(pref));

        Gdiplus::GdiplusStartupInput gsi;
        Gdiplus::GdiplusStartup(&s->gdipToken, &gsi, nullptr);

        s->hBrBg   = CreateSolidBrush(CLR_BG);
        s->hBrCard = CreateSolidBrush(CLR_CARD);
        s->hFontN  = MakeFont(9, false);
        s->hFontS  = MakeFont(9, true);

        // Set window icon to match the application tray icon
        HICON hIconBig   = LoadIconW(s->hInst, MAKEINTRESOURCEW(IDI_APPICON));
        HICON hIconSmall = static_cast<HICON>(
            LoadImageW(s->hInst, MAKEINTRESOURCEW(IDI_APPICON),
                       IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        if (hIconBig)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                                     reinterpret_cast<LPARAM>(hIconBig));
        if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                                     reinterpret_cast<LPARAM>(hIconSmall));

        INITCOMMONCONTROLSEX icx = { sizeof(icx), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icx);

        // OwnerDraw listbox (replaces the old SysListView32)
        int lbX = RC_CARD_L.left + 12;
        int lbY = RC_CARD_L.top  + 12;
        int lbW = RC_CARD_L.right - RC_CARD_L.left - 24;
        int lbH = RC_CARD_L.bottom - RC_CARD_L.top - 52;

        HWND hLB = CreateWindowExW(
            0,
            WC_LISTBOXW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY |
            WS_VSCROLL,
            lbX, lbY, lbW, lbH,
            hwnd, reinterpret_cast<HMENU>(IDC_LB), s->hInst, nullptr);

        SendMessageW(hLB, WM_SETFONT,
                     reinterpret_cast<WPARAM>(s->hFontN), TRUE);

        // Suppress the system-drawn NC border
        SetWindowSubclass(hLB, LB_SubclassProc, 1, 0);

        // Populate the listbox
        LB_FillItems(hLB, *s->pEntries);

        // Pre-select the first item
        if (s->pEntries->size() > 0)
            SendMessageW(hLB, LB_SETCURSEL, 0, 0);

        // Compute button hit-rects from the card geometry
        s_rcColorBtn = { RC_CARD_L.left+12,
                         RC_CARD_L.bottom-42,
                         RC_CARD_L.left+152,
                         RC_CARD_L.bottom-14 };
        // Centre the toggle pill vertically inside the General card
        {
            int cy = (RC_CARD_G.top + RC_CARD_G.bottom) / 2;
            s_rcToggle = { RC_CARD_G.right-58, cy-12,
                           RC_CARD_G.right-14, cy+12 };
        }

        return 0;
    }

    // ── WM_MEASUREITEM – set item height for owner-draw listbox ────────
    case WM_MEASUREITEM:
    {
        if (wP == IDC_LB)
        {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lP);
            mis->itemHeight = LB_ITEM_H;
            return TRUE;
        }
        break;
    }

    // ── WM_DRAWITEM – draw each language row ─────────────────────────
    case WM_DRAWITEM:
    {
        if (wP == IDC_LB)
        {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lP);
            if (dis->itemID == (UINT)-1) break;

            HWND hLB = dis->hwndItem;
            bool sel = (dis->itemState & ODS_SELECTED) != 0;

            LangEntry* e = LB_GetEntry(hLB, (int)dis->itemID);

            DrawSwatch(dis->hDC, dis->rcItem, e, sel);

            if (dis->itemState & ODS_FOCUS)
                DrawFocusRect(dis->hDC, &dis->rcItem);

            return TRUE;
        }
        break;
    }

    // ── WM_CTLCOLOR – paint listbox background ────────────────────────
    case WM_CTLCOLORLISTBOX:
        return s ? reinterpret_cast<LRESULT>(s->hBrCard)
                 : DefWindowProcW(hwnd, msg, wP, lP);

    // ── Paint ─────────────────────────────────────────────────────────
    case WM_PAINT:
    {
        if (!s) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Window background
        RECT cli; GetClientRect(hwnd, &cli);
        FillRect(hdc, &cli, s->hBrBg);

        SetBkMode(hdc, TRANSPARENT);

        // ── Lighting card ──────────────────────────────────────────
        SelectObject(hdc, s->hFontS);
        SetTextColor(hdc, CLR_BODY_TXT);
        RECT rH1 = { RC_CARD_L.left, RC_CARD_L.top-22,
                     RC_CARD_L.right, RC_CARD_L.top-4 };
        DrawTextW(hdc, L"Keyboard lighting", -1, &rH1,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        FillRR(hdc, RC_CARD_L, 8, CLR_CARD);
        FrameRR(hdc, RC_CARD_L, 8, CLR_CARD_BDR);

        // Divider above colour button
        RECT div = { RC_CARD_L.left+12, s_rcColorBtn.top-8,
                     RC_CARD_L.right-12, s_rcColorBtn.top-7 };
        FillRect(hdc, &div, CreateSolidBrush(CLR_DIV));

        // "Change colour" button (GDI, no HWND)
        DrawBtn(hdc, s_rcColorBtn, L"Change colour...",
                false, s->colHov, s->colPre, s->hFontN, s->hBrBg);

        // ── General card ──────────────────────────────────────────
        SelectObject(hdc, s->hFontS);          // same semibold as "Keyboard lighting"
        SetTextColor(hdc, CLR_BODY_TXT);
        RECT rH2 = { RC_CARD_G.left, RC_CARD_G.top-22,
                     RC_CARD_G.right, RC_CARD_G.top-4 };
        DrawTextW(hdc, L"General", -1, &rH2,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        FillRR(hdc, RC_CARD_G, 8, CLR_CARD);
        FrameRR(hdc, RC_CARD_G, 8, CLR_CARD_BDR);

        // Autostart label — vertically centred inside the card
        SelectObject(hdc, s->hFontN);
        SetTextColor(hdc, CLR_BODY_TXT);
        RECT rTL = { RC_CARD_G.left+16, RC_CARD_G.top,
                     s_rcToggle.left-8, RC_CARD_G.bottom };
        DrawTextW(hdc, L"Run at Windows startup", -1, &rTL,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        // On/Off label — same rect as the toggle pill so they share baseline
        bool on = Settings::IsAutoStartEnabled();
        SetTextColor(hdc, CLR_SEC_TXT);
        RECT rTS = { s_rcToggle.left-44, s_rcToggle.top,
                     s_rcToggle.left-4,  s_rcToggle.bottom };
        DrawTextW(hdc, on ? L"On" : L"Off", -1, &rTS,
                  DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

        // Toggle (GDI, no HWND)
        DrawToggle(hdc, s_rcToggle, on, s->togHov, s->togPre, s->hBrBg);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    // ── Mouse ─────────────────────────────────────────────────────────
    case WM_MOUSEMOVE:
    {
        if (!s) break;
        int x = LOWORD(lP), y = HIWORD(lP);
        bool ch = false;
        auto upd = [&](RECT& r, bool& h) {
            bool n = PtInR(r,x,y);
            if (n!=h) { h=n; ch=true; }
        };
        upd(s_rcColorBtn, s->colHov);
        upd(s_rcToggle,   s->togHov);
        if (ch) InvalidateRect(hwnd, nullptr, FALSE);

        TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hwnd,0};
        TrackMouseEvent(&tme);
        break;
    }

    case WM_MOUSELEAVE:
        if (s)
        {
            bool ch = s->colHov || s->togHov;
            s->colHov = s->togHov = false;
            if (ch) InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case WM_LBUTTONDOWN:
    {
        if (!s) break;
        int x = LOWORD(lP), y = HIWORD(lP);
        if (PtInR(s_rcColorBtn,x,y)) { s->colPre=true; InvalidateRect(hwnd,nullptr,FALSE); }
        if (PtInR(s_rcToggle,  x,y)) { s->togPre=true; InvalidateRect(hwnd,nullptr,FALSE); }
        SetCapture(hwnd);
        break;
    }

    case WM_LBUTTONUP:
    {
        if (!s) break;
        ReleaseCapture();
        int x = LOWORD(lP), y = HIWORD(lP);
        bool wpCol = s->colPre, wpTog = s->togPre;
        s->colPre = s->togPre = false;
        InvalidateRect(hwnd, nullptr, FALSE);

        if (wpCol && PtInR(s_rcColorBtn,x,y))
            OpenColorPicker(hwnd, s);
        else if (wpTog && PtInR(s_rcToggle,x,y))
        {
            bool cur = Settings::IsAutoStartEnabled();
            Settings::SetAutoStart(!cur);
            DBG_LOG("SettingsWnd: autostart -> %s", (!cur)?"ON":"OFF");
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }

    // ── Keyboard ──────────────────────────────────────────────────────
    case WM_KEYDOWN:
        if (wP == VK_ESCAPE) DestroyWindow(hwnd);
        break;

    // ── Listbox double-click ─────────────────────────────────────────
    case WM_COMMAND:
    {
        if (HIWORD(wP) == LBN_DBLCLK && LOWORD(wP) == IDC_LB)
            OpenColorPicker(hwnd, s);
        break;
    }

    // ── Cleanup ───────────────────────────────────────────────────────
    case WM_DESTROY:
        if (s)
        {
            if (s->hFontN)   { DeleteObject(s->hFontN); }
            if (s->hFontS)   { DeleteObject(s->hFontS); }
            if (s->hBrBg)    { DeleteObject(s->hBrBg);  }
            if (s->hBrCard)  { DeleteObject(s->hBrCard);}
            if (s->gdipToken){ Gdiplus::GdiplusShutdown(s->gdipToken); }
            delete s;
        }
        s_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wP, lP);
}

// ── Public API ────────────────────────────────────────────────────────────
namespace SettingsWnd
{
    static const wchar_t* CLASS_NAME = L"GLanglightSettingsWnd";

    void Open(HWND /*hParent*/, HINSTANCE hInst,
              std::vector<LangEntry>& entries,
              ApplyCallback onApply)
    {
        if (s_hwnd && IsWindow(s_hwnd))
        {
            SetForegroundWindow(s_hwnd);
            return;
        }

        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        wc.lpszClassName = CLASS_NAME;
        RegisterClassExW(&wc);

        WndState* st = new WndState();
        st->pEntries  = &entries;
        st->onApply   = onApply;
        st->hInst     = hInst;

        int sx = GetSystemMetrics(SM_CXSCREEN);
        int sy = GetSystemMetrics(SM_CYSCREEN);

        // Size window to accommodate content
        RECT rw = { 0, 0, WND_W, WND_H };
        AdjustWindowRectEx(&rw, WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, FALSE, WS_EX_APPWINDOW);
        int ww = rw.right - rw.left;
        int wh = rw.bottom - rw.top;

        s_hwnd = CreateWindowExW(
            WS_EX_APPWINDOW,
            CLASS_NAME,
            L"GLanglight Settings",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,
            (sx-ww)/2, (sy-wh)/2, ww, wh,
            nullptr, nullptr, hInst, st);

        if (s_hwnd)
            ShowWindow(s_hwnd, SW_SHOW);
    }

    void Close()
    {
        if (s_hwnd && IsWindow(s_hwnd))
            DestroyWindow(s_hwnd);
    }

    bool IsOpen()
    {
        return s_hwnd && IsWindow(s_hwnd);
    }
}