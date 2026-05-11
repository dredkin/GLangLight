#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <vector>
#include "tray.h"
#include "lang_detector.h"
#include "logi_led.h"
#include "settings.h"
#include "settings_dlg.h"
#include "dbglog.h"

// ── Constants ────────────────────────────────────────────────────────────
static const UINT POLL_TIMER_ID      = 1;
static const UINT REINIT_TIMER_ID    = 2;
static const UINT POLL_INTERVAL_MS   = 200;
static const UINT REINIT_INTERVAL_MS = 5000;

// Posted from the reinit worker thread to the message window
// wParam = (WPARAM)LogiLedWrapper::InitStatus
static const UINT WM_REINIT_DONE = WM_APP + 1;

static const wchar_t* WND_CLASS_NAME = L"GLanglightMsgWnd";

// ── Global state ─────────────────────────────────────────────────────────
static std::vector<LangEntry> g_entries;
static HINSTANCE               g_hInst     = nullptr;
static bool                    g_reiniting = false; // worker thread in flight

// Registered by Windows — broadcast when Explorer (re)creates the taskbar
static UINT g_taskbarCreatedMsg = 0;

// ── Helpers ──────────────────────────────────────────────────────────────
static void UpdateTooltip(HWND hwnd, LANGID primaryLangID)
{
    const wchar_t* name = L"?";
    for (const auto& e : g_entries)
        if (e.langID == primaryLangID) { name = e.name.c_str(); break; }

    wchar_t tip[128];
    swprintf_s(tip, L"GLanglight - %.60s", name);
    Tray::SetTooltip(hwnd, tip);
}

static void ApplyCurrentLang(HWND hwnd)
{
    LANGID langID = LangDetector::GetCurrentPrimaryLangID();
    COLORREF color = Settings::ColorForLang(g_entries, langID);
    DBG_LOG("ApplyCurrentLang: langID=0x%04X color=0x%06X",
            (unsigned)langID, (unsigned)color);
    LogiLedWrapper::ApplyColor(color);
    Tray::UpdateIcon(hwnd, color);
    UpdateTooltip(hwnd, langID);
}

static void ShowTrayBalloon(HWND hwnd, const wchar_t* title,
                             const wchar_t* text, DWORD infoFlags)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize      = sizeof(nid);
    nid.hWnd        = hwnd;
    nid.uID         = 1;
    nid.uFlags      = NIF_INFO;
    nid.dwInfoFlags = infoFlags;
    wcscpy_s(nid.szInfoTitle, title);
    wcscpy_s(nid.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ── Reinit worker thread ──────────────────────────────────────────────────
// Runs Reinit() (which calls Sleep(500)) off the UI thread,
// then posts WM_REINIT_DONE back so the result is processed on the UI thread.
struct ReinitCtx { HWND hwnd; };

static DWORD WINAPI ReinitThread(LPVOID param)
{
    ReinitCtx* ctx = static_cast<ReinitCtx*>(param);
    HWND hwnd = ctx->hwnd;
    delete ctx;

    auto status = LogiLedWrapper::Reinit();
    PostMessageW(hwnd, WM_REINIT_DONE, static_cast<WPARAM>(status), 0);
    return 0;
}

static void StartReinitThread(HWND hwnd)
{
    if (g_reiniting) return;
    g_reiniting = true;
    ReinitCtx* ctx = new ReinitCtx{ hwnd };
    HANDLE h = CreateThread(nullptr, 0, ReinitThread, ctx, 0, nullptr);
    if (h) CloseHandle(h);
    else { delete ctx; g_reiniting = false; }
}

// ── Window procedure ─────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Explorer restarted — re-add tray icon
    if (msg == g_taskbarCreatedMsg)
    {
        DBG_LOG("TaskbarCreated: re-registering tray icon");
        Tray::Create(hwnd, g_hInst);
        if (LogiLedWrapper::IsReady())
            ApplyCurrentLang(hwnd);
        return 0;
    }

    switch (msg)
    {
    case WM_CREATE:
        SetTimer(hwnd, POLL_TIMER_ID, POLL_INTERVAL_MS, nullptr);
        return 0;

    case WM_TIMER:
        if (wParam == POLL_TIMER_ID)
        {
            LANGID langID = 0;
            if (LangDetector::Poll(langID))
            {
                DBG_LOG("WM_TIMER: lang changed to 0x%04X", (unsigned)langID);
                Tray::UpdateIcon(hwnd, Settings::ColorForLang(g_entries, langID));
                UpdateTooltip(hwnd, langID);
                if (LogiLedWrapper::IsReady())
                    LogiLedWrapper::ApplyColor(
                        Settings::ColorForLang(g_entries, langID));
            }
        }
        else if (wParam == REINIT_TIMER_ID)
        {
            if (!LogiLedWrapper::IsReady())
            {
                DBG_LOG("REINIT_TIMER: spawning reinit thread");
                StartReinitThread(hwnd);
            }
            else
            {
                // Already connected — stop the timer
                KillTimer(hwnd, REINIT_TIMER_ID);
            }
        }
        return 0;

    case WM_REINIT_DONE:
    {
        g_reiniting = false;
        auto status = static_cast<LogiLedWrapper::InitStatus>(wParam);
        if (status == LogiLedWrapper::InitStatus::OK)
        {
            DBG_LOG("WM_REINIT_DONE: SDK connected, applying color");
            ApplyCurrentLang(hwnd);

            if (LogiLedWrapper::IsReady())
            {
                // ApplyColor succeeded — SDK is truly ready
                DBG_LOG("WM_REINIT_DONE: color applied OK, stopping reinit timer");
                KillTimer(hwnd, REINIT_TIMER_ID);
                ShowTrayBalloon(hwnd, L"GLanglight",
                    L"Logitech LED connected.", NIIF_INFO);
            }
            else
            {
                // G HUB accepted Init but rejected SetLighting — keep retrying
                DBG_LOG("WM_REINIT_DONE: ApplyColor failed, will retry");
            }
        }
        else
        {
            DBG_LOG("WM_REINIT_DONE: still not ready (%s)",
                    status == LogiLedWrapper::InitStatus::NoGHub
                    ? "NoGHub" : "NoDevice");
        }
        return 0;
    }

    case WM_TRAYICON:
        switch (LOWORD(lParam))
        {
        case WM_LBUTTONDBLCLK:
            DBG_LOG("WM_TRAYICON: double-click -> open settings");
            SettingsWnd::Open(hwnd, g_hInst, g_entries,
                [hwnd](const std::vector<LangEntry>&) {
                    if (LogiLedWrapper::IsReady())
                        ApplyCurrentLang(hwnd);
                });
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            Tray::ShowContextMenu(hwnd);
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_SETTINGS:
            DBG_LOG("IDM_SETTINGS: opening settings window");
            SettingsWnd::Open(hwnd, g_hInst, g_entries,
                [hwnd](const std::vector<LangEntry>&) {
                    if (LogiLedWrapper::IsReady())
                        ApplyCurrentLang(hwnd);
                });
            break;

        case IDM_EXIT:
            DBG_LOG("IDM_EXIT");
            KillTimer(hwnd, POLL_TIMER_ID);
            KillTimer(hwnd, REINIT_TIMER_ID);
            SettingsWnd::Close();
            Tray::Destroy(hwnd);
            LogiLedWrapper::Shutdown();
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Entry point ──────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    LPWSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
    DBG_INIT_CONSOLE();
    DBG_LOG("=== GLanglight starting ===");

    g_hInst = hInstance;

    // Register for taskbar re-creation (Explorer restart / first shell load)
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Single-instance guard
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GLanglightSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        DBG_LOG("Already running -- exiting.");
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // Load settings
    DBG_LOG("Loading settings...");
    Settings::Load(g_entries);
    DBG_LOG("  %zu language entries loaded", g_entries.size());

    // Initialise Logitech LED SDK
    DBG_LOG("Calling LogiLedWrapper::Init()...");
    auto initStatus = LogiLedWrapper::Init();
    DBG_LOG("Init returned: %s",
            initStatus == LogiLedWrapper::InitStatus::OK      ? "OK" :
            initStatus == LogiLedWrapper::InitStatus::NoGHub  ? "NoGHub" : "NoDevice");

    if (initStatus == LogiLedWrapper::InitStatus::NoGHub)
    {
        int answer = MessageBoxW(nullptr,
            L"Logitech G HUB (or LGS) is not running or the\n"
            L"Logitech LED SDK is not installed.\n\n"
            L"GLanglight will run in the system tray but cannot\n"
            L"control keyboard lighting until G HUB is started.\n\n"
            L"Continue anyway?",
            L"GLanglight - No G HUB", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        if (answer == IDNO)
        {
            CloseHandle(hMutex);
            return 1;
        }
    }

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = WND_CLASS_NAME;
    if (!RegisterClassExW(&wc))
    {
        DBG_LOG("RegisterClassExW FAILED (%lu)", GetLastError());
        LogiLedWrapper::Shutdown();
        CloseHandle(hMutex);
        return 1;
    }

    // Message-only hidden window
    HWND hwnd = CreateWindowExW(
        0, WND_CLASS_NAME, L"GLanglight",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hInstance, nullptr);

    if (!hwnd)
    {
        DBG_LOG("CreateWindowExW FAILED (%lu)", GetLastError());
        LogiLedWrapper::Shutdown();
        CloseHandle(hMutex);
        return 1;
    }
    DBG_LOG("Message-only window created: 0x%p", (void*)hwnd);

    // Tray icon
    bool trayOk = Tray::Create(hwnd, hInstance);
    DBG_LOG("Tray::Create: %s", trayOk ? "OK" : "FAILED (non-fatal)");

    if (initStatus == LogiLedWrapper::InitStatus::OK)
    {
        ApplyCurrentLang(hwnd);
        { LANGID dummy; LangDetector::Poll(dummy); }
    }
    else
    {
        // NoDevice: inform user via balloon (NoGHub already showed a MessageBox)
        if (initStatus == LogiLedWrapper::InitStatus::NoDevice)
        {
            ShowTrayBalloon(hwnd, L"GLanglight",
                L"No Logitech RGB device detected.\n"
                L"Will retry automatically every 5 seconds.",
                NIIF_WARNING);
        }
        DBG_LOG("LED not ready, starting REINIT timer");
        SetTimer(hwnd, REINIT_TIMER_ID, REINIT_INTERVAL_MS, nullptr);
    }

    DBG_LOG("Entering message loop...");
    MSG wmsg;
    while (GetMessageW(&wmsg, nullptr, 0, 0) > 0)
    {
        HWND hSettings = SettingsWnd::IsOpen()
            ? FindWindowW(L"GLanglightSettingsWnd", nullptr) : nullptr;
        if (hSettings && IsDialogMessageW(hSettings, &wmsg))
            continue;

        TranslateMessage(&wmsg);
        DispatchMessageW(&wmsg);
    }

    DBG_LOG("Message loop exited.");
    CloseHandle(hMutex);
    return static_cast<int>(wmsg.wParam);
}
