#include "settings.h"
#include "dbglog.h"
#include <cstdlib>   // rand, srand
#include <ctime>     // time
#include <strsafe.h> // StringCchPrintfW

// Registry key under HKCU
static const wchar_t* REG_KEY = L"Software\\GLanglight\\LangColors";

// Default colours for the first 3 languages
static const COLORREF DEFAULT_COLORS[] = {
    RGB(255, 255, 255),   // 1st  White
    RGB(255,   0,   0),   // 2nd  Red
    RGB(  0,   0, 255),   // 3rd  Blue
};

// Returns a random bright colour for any language beyond index 2
static COLORREF RandomColor()
{
    // Ensure at least one channel is bright (>= 192)
    int r = rand() % 256;
    int g = rand() % 256;
    int b = rand() % 256;
    // Clamp max channel to 255
    if (r < 128 && g < 128 && b < 128) r = 200;
    return RGB(r, g, b);
}

namespace Settings
{
    void Load(std::vector<LangEntry>& entries)
    {
        entries.clear();

        // ── 1. Enumerate installed keyboard layouts ──────────────────
        int n = GetKeyboardLayoutList(0, nullptr);
        if (n <= 0) n = 1;

        std::vector<HKL> hkls(n);
        n = GetKeyboardLayoutList(n, hkls.data());

        DBG_LOG("Settings::Load() -- %d HKL(s) found", n);

        srand(static_cast<unsigned>(time(nullptr)));

        // Open (or create) registry key
        HKEY hKey = nullptr;
        RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
                        nullptr, &hKey, nullptr);

        for (int i = 0; i < n; ++i)
        {
            LANGID langID = PRIMARYLANGID(LOWORD(HandleToUlong(hkls[i])));

            // Skip duplicates (multiple sub-languages sharing same primary)
            bool dup = false;
            for (auto& e : entries)
                if (e.langID == langID) { dup = true; break; }
            if (dup) continue;

            // Get language name
            wchar_t buf[256] = {};
            LCID lcid = MAKELCID(MAKELANGID(langID, SUBLANG_DEFAULT), SORT_DEFAULT);
            GetLocaleInfoW(lcid, LOCALE_SLANGUAGE, buf, 256);
            if (buf[0] == L'\0')
                swprintf_s(buf, L"Language 0x%04X", (unsigned)langID);

            // Determine default colour
            COLORREF defColor;
            int idx = static_cast<int>(entries.size());
            if (idx < 3)
                defColor = DEFAULT_COLORS[idx];
            else
                defColor = RandomColor();

            // Try to load saved colour from registry
            COLORREF savedColor = defColor;
            if (hKey)
            {
                wchar_t valName[16];
                swprintf_s(valName, L"%04X", (unsigned)langID);
                DWORD data = 0, dataSize = sizeof(data);
                if (RegGetValueW(hKey, nullptr, valName,
                                 RRF_RT_REG_DWORD, nullptr,
                                 &data, &dataSize) == ERROR_SUCCESS)
                {
                    savedColor = static_cast<COLORREF>(data);
                    DBG_LOG("  Loaded color for 0x%04X from registry: 0x%06X",
                            (unsigned)langID, (unsigned)savedColor);
                }
                else
                {
                    DBG_LOG("  No registry entry for 0x%04X, using default 0x%06X",
                            (unsigned)langID, (unsigned)defColor);
                }
            }

            LangEntry e;
            e.langID = langID;
            e.name   = buf;
            e.color  = savedColor;
            entries.push_back(e);
        }

        if (hKey) RegCloseKey(hKey);

        // ── 2. First-run: save defaults if nothing was in the registry ─
        Save(entries);

        DBG_LOG("Settings::Load() done, %zu entries", entries.size());
    }

    void Save(const std::vector<LangEntry>& entries)
    {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE,
                            nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        {
            DBG_LOG("Settings::Save() -- RegCreateKeyEx FAILED");
            return;
        }

        for (const auto& e : entries)
        {
            wchar_t valName[16];
            swprintf_s(valName, L"%04X", (unsigned)e.langID);
            DWORD data = static_cast<DWORD>(e.color);
            RegSetValueExW(hKey, valName, 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&data), sizeof(data));
            DBG_LOG("  Saved 0x%04X -> 0x%06X", (unsigned)e.langID, (unsigned)e.color);
        }

        RegCloseKey(hKey);
        DBG_LOG("Settings::Save() done");
    }

    COLORREF ColorForLang(const std::vector<LangEntry>& entries, LANGID primaryLangID)
    {
        for (const auto& e : entries)
            if (e.langID == primaryLangID)
                return e.color;
        return RGB(255, 255, 255); // fallback White
    }

    // ── Autostart ─────────────────────────────────────────────────────

    static const wchar_t* RUN_KEY  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static const wchar_t* APP_NAME = L"GLanglight";

    bool IsAutoStartEnabled()
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
            return false;

        DWORD type = 0, size = 0;
        LSTATUS st = RegQueryValueExW(hKey, APP_NAME, nullptr, &type, nullptr, &size);
        RegCloseKey(hKey);
        return (st == ERROR_SUCCESS);
    }

    void SetAutoStart(bool enable)
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
        {
            DBG_LOG("SetAutoStart: failed to open Run key");
            return;
        }

        if (enable)
        {
            // Get full path of our own EXE
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);

            // Wrap in quotes in case the path contains spaces
            wchar_t quoted[MAX_PATH + 4] = {};
            swprintf_s(quoted, L"\"%s\"", exePath);

            DWORD size = static_cast<DWORD>((wcslen(quoted) + 1) * sizeof(wchar_t));
            LSTATUS st = RegSetValueExW(hKey, APP_NAME, 0, REG_SZ,
                                        reinterpret_cast<const BYTE*>(quoted), size);
            DBG_LOG("SetAutoStart(true): %s  path=%ls",
                    st == ERROR_SUCCESS ? "OK" : "FAILED", quoted);
        }
        else
        {
            LSTATUS st = RegDeleteValueW(hKey, APP_NAME);
            DBG_LOG("SetAutoStart(false): %s",
                    (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) ? "OK" : "FAILED");
        }

        RegCloseKey(hKey);
    }
}
