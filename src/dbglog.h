#pragma once
// Debug-only console logging.
// In Release builds every DBG_LOG call compiles to nothing.

#ifdef _DEBUG

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <ctime>

namespace DbgLog
{
    // Call once from WinMain in DEBUG builds to open a console window
    // and redirect stdout/stderr to it.
    inline void InitConsole()
    {
        AllocConsole();
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
        SetConsoleTitleW(L"GLanglight DEBUG");
    }

    // Write a timestamped line to stdout AND to the VS Output window.
    inline void Log(const char* fmt, ...)
    {
        // timestamp
        SYSTEMTIME st;
        GetLocalTime(&st);
        char prefix[32];
        _snprintf_s(prefix, sizeof(prefix), "[%02d:%02d:%02d.%03d] ",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        char body[512];
        va_list args;
        va_start(args, fmt);
        _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, args);
        va_end(args);

        // stdout (our AllocConsole window)
        printf("%s%s\n", prefix, body);
        fflush(stdout);

        // Visual Studio Output window
        char full[576];
        _snprintf_s(full, sizeof(full), "%s%s\n", prefix, body);
        OutputDebugStringA(full);
    }
}

#define DBG_INIT_CONSOLE()  DbgLog::InitConsole()
#define DBG_LOG(fmt, ...)   DbgLog::Log(fmt, ##__VA_ARGS__)

#else  // Release — compile out completely

#define DBG_INIT_CONSOLE()  ((void)0)
#define DBG_LOG(fmt, ...)   ((void)0)

#endif  // _DEBUG
