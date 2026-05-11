#include "lang_detector.h"
#include "dbglog.h"

namespace LangDetector
{
    // Cached language ID from the previous poll
    static LANGID s_lastLangID = 0xFFFF;

    LANGID GetCurrentPrimaryLangID()
    {
        HWND fgWnd = GetForegroundWindow();
        DWORD threadId = 0;
        if (fgWnd)
            threadId = GetWindowThreadProcessId(fgWnd, nullptr);

        HKL hkl = GetKeyboardLayout(threadId);
        // Low-word of HKL is the LANGID
        LANGID langID = LOWORD(HandleToUlong(hkl));
        return PRIMARYLANGID(langID);
    }

    bool Poll(LANGID& outLangID)
    {
        LANGID current = GetCurrentPrimaryLangID();
        if (current != s_lastLangID)
        {
            DBG_LOG("LangDetector::Poll() -- language changed: 0x%04X -> 0x%04X",
                    (unsigned)s_lastLangID, (unsigned)current);
            s_lastLangID = current;
            outLangID = current;
            return true;
        }
        return false;
    }
}
