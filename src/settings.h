#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <string>

// One entry per keyboard layout installed in the system
struct LangEntry
{
    LANGID      langID;     // PRIMARYLANGID of the HKL
    std::wstring name;      // Human-readable name, e.g. "English (United States)"
    COLORREF    color;      // LED colour for this language (COLORREF = 0x00BBGGRR)
};

namespace Settings
{
    // Enumerate all HKLs installed for the current user and populate
    // entries with default colours (1st=White, 2nd=Red, 3rd=Blue, rest random).
    // Existing registry values override the defaults.
    void Load(std::vector<LangEntry>& entries);

    // Persist all entries to HKCU\Software\GLanglight\LangColors
    void Save(const std::vector<LangEntry>& entries);

    // Look up the colour for a given PRIMARYLANGID.
    // Returns White if not found.
    COLORREF ColorForLang(const std::vector<LangEntry>& entries, LANGID primaryLangID);

    // ── Autostart helpers ─────────────────────────────────────────────
    // Returns true if GLanglight is registered in HKCU Run key.
    bool IsAutoStartEnabled();

    // Add / remove the Run key entry for the current EXE path.
    // enable=true  → adds   HKCU\...\Run\GLanglight = "<exe path>"
    // enable=false → removes the value (if present)
    void SetAutoStart(bool enable);
}
