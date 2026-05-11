#pragma once
#include <windows.h>

namespace LangDetector
{
    // Returns the PRIMARYLANGID of the active keyboard layout
    // for the foreground window's thread.
    LANGID GetCurrentPrimaryLangID();

    // Polls the current language; returns true if it changed since last call.
    // Writes the new PRIMARYLANGID into *outLangID when returning true.
    bool Poll(LANGID& outLangID);
}
