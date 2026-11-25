// Minimal file logger: %LOCALAPPDATA%\AwakeStandalone\Logs\Awake_YYYY-MM-DD.log (last 7 kept).
#include "awake.h"

#include <algorithm>
#include <cstdarg>
#include <cwchar>

namespace
{
    std::wstring g_logFile;
    HANDLE g_console = nullptr;
    CRITICAL_SECTION g_lock;
    bool g_lockInit = false;

    void PruneOldLogs(const std::wstring& dir)
    {
        std::vector<std::wstring> files;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\Awake_*.log").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
        {
            return;
        }

        do
        {
            files.emplace_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);

        // Names sort chronologically (Awake_YYYY-MM-DD.log).
        std::sort(files.begin(), files.end());
        while (files.size() > 7)
        {
            DeleteFileW((dir + L"\\" + files.front()).c_str());
            files.erase(files.begin());
        }
    }
}

void LogInit()
{
    if (!g_lockInit)
    {
        InitializeCriticalSection(&g_lock);
        g_lockInit = true;
    }

    std::wstring dir = SettingsDirectory();
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\Logs";
    CreateDirectoryW(dir.c_str(), nullptr);

    SYSTEMTIME st;
    GetLocalTime(&st);
    g_logFile = Format(L"%ls\\Awake_%04u-%02u-%02u.log", dir.c_str(), st.wYear, st.wMonth, st.wDay);
    PruneOldLogs(dir);
}

void LogSetConsole(HANDLE console)
{
    g_console = console;
}

void LogWrite(const wchar_t* level, const wchar_t* format, ...)
{
    wchar_t message[2048];
    va_list args;
    va_start(args, format);
    int n = _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);
    if (n < 0)
    {
        message[_countof(message) - 1] = L'\0';
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring line = Format(L"[%02u:%02u:%02u.%03u] [%ls] %ls\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level, message);

    if (g_lockInit)
    {
        EnterCriticalSection(&g_lock);
    }

    OutputDebugStringW(line.c_str());

    if (g_console)
    {
        DWORD written = 0;
        WriteConsoleW(g_console, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
    }

    if (!g_logFile.empty())
    {
        HANDLE f = CreateFileW(g_logFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE)
        {
            std::string utf8 = WideToUtf8(line);
            DWORD written = 0;
            WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            CloseHandle(f);
        }
    }

    if (g_lockInit)
    {
        LeaveCriticalSection(&g_lock);
    }
}
