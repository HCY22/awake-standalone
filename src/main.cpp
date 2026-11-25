// Awake (standalone) - native Win32 port of Microsoft PowerToys Awake.
// Behaviour follows PowerToys' src/modules/awake/Awake (MIT, Copyright (c) Microsoft Corporation).
//
// Everything runs on one UI thread: SetThreadExecutionState is per-thread, so the
// thread that owns the tray window also owns the keep-awake request. Background
// notifications (settings file changes, bound process exit, exit event, console
// close) are marshalled to that thread with PostMessage.
#include "awake.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wtsapi32.h>

#include <cmath>
#include <cwchar>
#include <cwctype>

namespace
{
    // Window messages.
    constexpr UINT WM_TRAY = WM_APP + 1;
    constexpr UINT WM_APP_SETTINGS_CHANGED = WM_APP + 2;
    constexpr UINT WM_APP_QUIT = WM_APP + 3; // wParam: exit code, lParam: QuitReason

    // Timers.
    constexpr UINT_PTR TIMER_TICK = 1;
    constexpr UINT_PTR TIMER_SETTINGS_DEBOUNCE = 2;
    constexpr UINT_PTR TIMER_ICON_RETRY = 3;

    // Menu command IDs (same values as PowerToys' TrayCommands).
    constexpr UINT TC_DISPLAY_SETTING = WM_USER + 0x2;
    constexpr UINT TC_MODE_PASSIVE = WM_USER + 0x3;
    constexpr UINT TC_MODE_INDEFINITE = WM_USER + 0x4;
    constexpr UINT TC_MODE_EXPIRABLE = WM_USER + 0x5;
    constexpr UINT TC_ALLOW_DISPLAY_OFF_LOCKED = WM_USER + 0x6;
    constexpr UINT TC_EXIT = WM_USER + 0x64;
    constexpr UINT TC_TIME = WM_USER + 0x65;

    constexpr UINT kTrayIconId = 1000;
    constexpr wchar_t kWindowClass[] = L"Awake.MessageWindow";
    constexpr wchar_t kAppName[] = L"Awake";
    constexpr wchar_t kMutexName[] = L"Awake"; // Same as PowerToys Awake: only one of them runs at a time.
    constexpr wchar_t kExitEventName[] = L"Local\\AwakeStandaloneExitEvent-5b1f3c2e-8d0a-4f5e-9b7a-1c2d3e4f5a6b";

    enum QuitReason : LPARAM
    {
        QuitTrayExit,
        QuitConsole,
        QuitExitEvent,
        QuitBoundProcess,
        QuitTimerDone,
    };

    struct App
    {
        HINSTANCE instance = nullptr;
        HWND hwnd = nullptr;
        HMENU menu = nullptr;
        UINT taskbarCreated = 0;
        HANDLE mutex = nullptr;
        HANDLE console = nullptr;

        // How we were started.
        bool configMode = false;    // -c: settings.json + tray menu
        bool processBound = false;  // -p / -u: hides "Exit" like PowerToys does

        // Current state.
        AwakeMode mode = AwakeMode::Passive;
        bool displayOn = false;
        bool screenLocked = false;
        bool allowDisplayOffWhileLocked = true;
        int shownPid = 0;          // PID shown in the tooltip for process-scoped keep-awake
        int64_t timedEndMs = 0;    // TIMED: wall-clock end (Unix ms)
        int64_t expireAt = 0;      // EXPIRABLE: Unix seconds

        // Tray icon.
        HICON icons[5] = {};
        bool iconAdded = false;
        int iconRetries = 0;
        std::wstring tip;
        HICON currentIcon = nullptr;

        // Settings (config mode).
        AwakeSettings settings;
        std::string lastSettingsJson; // What we last read or wrote; used to ignore our own writes.

        // Wait handles.
        HANDLE exitEvent = nullptr;
        HANDLE exitEventWait = nullptr;
        HANDLE boundProcess = nullptr;
        HANDLE boundProcessWait = nullptr;
    };

    App g;

    enum IconIndex
    {
        IconDefault,
        IconTimed,
        IconExpirable,
        IconIndefinite,
        IconDisabled,
    };

    // -----------------------------------------------------------------------
    // Console helpers
    // -----------------------------------------------------------------------

    HANDLE OpenConsoleOutput()
    {
        HANDLE h = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        return h == INVALID_HANDLE_VALUE ? nullptr : h;
    }

    void ConsoleWrite(HANDLE h, const std::wstring& text)
    {
        if (!h)
        {
            return;
        }

        // Normalize to CRLF.
        std::wstring out;
        out.reserve(text.size() + 16);
        for (wchar_t c : text)
        {
            if (c == L'\n')
                out += L'\r';
            out += c;
        }

        DWORD written = 0;
        WriteConsoleW(h, out.c_str(), static_cast<DWORD>(out.size()), &written, nullptr);
    }

    // Prints to the parent console if there is one, otherwise shows a message box.
    void ShowOutput(const std::wstring& text, bool error)
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
        {
            HANDLE h = OpenConsoleOutput();
            ConsoleWrite(h, L"\n" + text + L"\n");
            if (h)
                CloseHandle(h);
            FreeConsole();
            return;
        }

        MessageBoxW(nullptr, text.c_str(), kAppName, MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    }

    // -----------------------------------------------------------------------
    // Execution state
    // -----------------------------------------------------------------------

    EXECUTION_STATE ComputeAwakeState()
    {
        if (g.mode == AwakeMode::Passive)
        {
            return ES_CONTINUOUS;
        }

        bool display = g.displayOn && !(g.screenLocked && g.allowDisplayOffWhileLocked);
        return ES_CONTINUOUS | ES_SYSTEM_REQUIRED | (display ? ES_DISPLAY_REQUIRED : 0);
    }

    void UpdateTrayIcon(bool forceAdd = false);

    void ApplyExecutionState()
    {
        EXECUTION_STATE state = ComputeAwakeState();
        if (SetThreadExecutionState(state) == 0)
        {
            LOG_ERROR(L"Failed to set execution state to 0x%08lX. Reverting to passive mode.", state);
            g.mode = AwakeMode::Passive;
            SetThreadExecutionState(ES_CONTINUOUS);
            UpdateTrayIcon();
        }
        else
        {
            LOG_INFO(L"Execution state set to 0x%08lX.", state);
        }
    }

    // -----------------------------------------------------------------------
    // Tray icon and menu
    // -----------------------------------------------------------------------

    void LoadIcons()
    {
        const int ids[] = { IDI_AWAKE, IDI_TIMED, IDI_EXPIRABLE, IDI_INDEFINITE, IDI_DISABLED };
        for (int i = 0; i < 5; ++i)
        {
            if (g.icons[i])
            {
                DestroyIcon(g.icons[i]);
                g.icons[i] = nullptr;
            }

            if (FAILED(LoadIconMetric(g.instance, MAKEINTRESOURCEW(ids[i]), LIM_SMALL, &g.icons[i])))
            {
                g.icons[i] = static_cast<HICON>(LoadImageW(g.instance, MAKEINTRESOURCEW(ids[i]), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
            }
        }
    }

    std::wstring ScreenLine()
    {
        return Format(S(Str::TrayScreenFmt), S(g.displayOn ? Str::ScreenOn : Str::ScreenOff));
    }

    void BuildTip()
    {
        std::wstring name = kAppName;
        switch (g.mode)
        {
        case AwakeMode::Indefinite:
            g.tip = name + L"\n" + S(Str::TrayIndefinite) + L"\n" + ScreenLine();
            if (g.shownPid != 0)
            {
                g.tip += Format(L"\nPID: %d", g.shownPid);
            }

            g.currentIcon = g.icons[IconIndefinite];
            break;
        case AwakeMode::Timed:
        {
            int64_t remaining = (g.timedEndMs - NowUnixMs() + 999) / 1000;
            g.tip = name + L"\n" + Format(S(Str::TrayRemainingFmt), FormatDuration(remaining).c_str()) + L"\n" + ScreenLine();
            g.currentIcon = g.icons[IconTimed];
            break;
        }
        case AwakeMode::Expirable:
            g.tip = name + L"\n" + Format(S(Str::TrayUntilFmt), FormatShortLocal(g.expireAt).c_str()) + L"\n" + ScreenLine();
            g.currentIcon = g.icons[IconExpirable];
            break;
        case AwakeMode::Passive:
        default:
            g.tip = name + L"\n" + S(Str::TrayOff);
            g.currentIcon = g.icons[IconDisabled];
            break;
        }
    }

    bool NotifyIcon(DWORD message)
    {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = g.hwnd;
        nid.uID = kTrayIconId;
        if (message != NIM_DELETE)
        {
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
            nid.uCallbackMessage = WM_TRAY;
            nid.hIcon = g.currentIcon ? g.currentIcon : g.icons[IconDefault];
            wcsncpy_s(nid.szTip, g.tip.c_str(), _TRUNCATE);
        }

        return Shell_NotifyIconW(message, &nid) != FALSE;
    }

    void UpdateTrayIcon(bool forceAdd)
    {
        if (!g.hwnd)
        {
            return;
        }

        BuildTip();

        if (g.iconAdded && !forceAdd && NotifyIcon(NIM_MODIFY))
        {
            return;
        }

        if (NotifyIcon(NIM_ADD))
        {
            g.iconAdded = true;
            g.iconRetries = 0;
            KillTimer(g.hwnd, TIMER_ICON_RETRY);
            return;
        }

        // Explorer may not be ready yet (e.g. right after sign-in); retry for up to a minute.
        g.iconAdded = false;
        if (g.iconRetries++ < 60)
        {
            LOG_INFO(L"Could not add the tray icon (error %lu), retrying.", GetLastError());
            SetTimer(g.hwnd, TIMER_ICON_RETRY, 1000, nullptr);
        }
        else
        {
            LOG_ERROR(L"Giving up adding the tray icon.");
        }
    }

    const std::vector<std::pair<std::wstring, uint32_t>>& TrayTimes()
    {
        static std::vector<std::pair<std::wstring, uint32_t>> defaults;
        if (!g.settings.customTrayTimes.empty())
        {
            return g.settings.customTrayTimes;
        }

        if (defaults.empty())
        {
            defaults.emplace_back(Format(S(Str::MinutesFmt), 30u), 1800);
            defaults.emplace_back(Format(S(Str::HourFmt), 1u), 3600);
            defaults.emplace_back(Format(S(Str::HoursFmt), 2u), 7200);
        }

        return defaults;
    }

    void RebuildMenu()
    {
        if (g.menu)
        {
            DestroyMenu(g.menu);
            g.menu = nullptr;
        }

        g.menu = CreatePopupMenu();
        if (!g.menu)
        {
            return;
        }

        if (!g.configMode)
        {
            // Command-line mode: there's no state to change from the tray, only a way out.
            if (!g.processBound)
            {
                AppendMenuW(g.menu, MF_STRING, TC_EXIT, S(Str::Exit));
            }

            return;
        }

        bool passive = g.mode == AwakeMode::Passive;
        bool keepDisplayOn = g.settings.keepDisplayOn;

        HMENU times = CreatePopupMenu();
        const auto& entries = TrayTimes();
        for (size_t i = 0; i < entries.size() && i < 200; ++i)
        {
            AppendMenuW(times, MF_STRING, TC_TIME + static_cast<UINT>(i), entries[i].first.c_str());
        }

        // Same layout as PowerToys Awake.
        AppendMenuW(g.menu, MF_POPUP, reinterpret_cast<UINT_PTR>(times), S(Str::KeepOnInterval));
        AppendMenuW(g.menu, MF_STRING | MF_GRAYED, TC_MODE_EXPIRABLE, S(Str::KeepUntilExpiration));
        AppendMenuW(g.menu, MF_STRING, TC_MODE_INDEFINITE, S(Str::KeepIndefinitely));
        AppendMenuW(g.menu, MF_STRING, TC_MODE_PASSIVE, S(Str::Off));
        AppendMenuW(g.menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(g.menu, MF_STRING | (keepDisplayOn ? MF_CHECKED : 0) | (passive ? MF_GRAYED : 0), TC_DISPLAY_SETTING, S(Str::KeepScreenOn));
        AppendMenuW(g.menu, MF_STRING | (g.settings.allowDisplayOffWhileLocked ? MF_CHECKED : 0) | ((passive || !keepDisplayOn) ? MF_GRAYED : 0), TC_ALLOW_DISPLAY_OFF_LOCKED, S(Str::AllowDisplayOffLocked));

        if (!g.processBound)
        {
            AppendMenuW(g.menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(g.menu, MF_STRING, TC_EXIT, S(Str::Exit));
        }

        UINT checked = 3;
        switch (g.mode)
        {
        case AwakeMode::Timed: checked = 0; break;
        case AwakeMode::Expirable: checked = 1; break;
        case AwakeMode::Indefinite: checked = 2; break;
        default: checked = 3; break;
        }

        CheckMenuRadioItem(g.menu, 0, 3, checked, MF_BYPOSITION);
    }

    void ShowTrayMenu()
    {
        if (!g.menu)
        {
            return;
        }

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(g.hwnd);
        UINT flags = TPM_RIGHTBUTTON | (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_BOTTOMALIGN;
        TrackPopupMenuEx(g.menu, flags, pt.x, pt.y, g.hwnd, nullptr);
        PostMessageW(g.hwnd, WM_NULL, 0, 0);
    }

    // -----------------------------------------------------------------------
    // Exit
    // -----------------------------------------------------------------------

    [[noreturn]] void ExitApp(int code, const wchar_t* reason)
    {
        LOG_INFO(L"%ls", reason);

        if (g.hwnd)
        {
            KillTimer(g.hwnd, TIMER_TICK);
        }

        SetThreadExecutionState(ES_CONTINUOUS);

        if (g.iconAdded)
        {
            NotifyIcon(NIM_DELETE);
            g.iconAdded = false;
        }

        if (g.mutex)
        {
            ReleaseMutex(g.mutex);
        }

        ExitProcess(static_cast<UINT>(code));
    }

    // -----------------------------------------------------------------------
    // Modes (apply state only; persisting is done by the callers in config mode)
    // -----------------------------------------------------------------------

    void StopTimer()
    {
        KillTimer(g.hwnd, TIMER_TICK);
    }

    void EnterPassive()
    {
        LOG_INFO(L"Passive mode (system power plan).");
        StopTimer();
        g.mode = AwakeMode::Passive;
        g.shownPid = 0;
        ApplyExecutionState();
        UpdateTrayIcon();
    }

    void EnterIndefinite(bool keepDisplayOn, int pid = 0)
    {
        LOG_INFO(L"Indefinite keep-awake. Display on: %d.", keepDisplayOn);
        StopTimer();
        g.mode = AwakeMode::Indefinite;
        g.displayOn = keepDisplayOn;
        g.shownPid = pid;
        ApplyExecutionState();
        UpdateTrayIcon();
    }

    void EnterTimed(uint32_t seconds, bool keepDisplayOn)
    {
        LOG_INFO(L"Timed keep-awake for %u seconds. Display on: %d.", seconds, keepDisplayOn);
        StopTimer();
        g.mode = AwakeMode::Timed;
        g.displayOn = keepDisplayOn;
        g.shownPid = 0;
        g.timedEndMs = NowUnixMs() + static_cast<int64_t>(seconds) * 1000;
        ApplyExecutionState();
        UpdateTrayIcon();
        SetTimer(g.hwnd, TIMER_TICK, 1000, nullptr);
    }

    bool EnterExpirable(int64_t expireAt, bool keepDisplayOn)
    {
        LOG_INFO(L"Expirable keep-awake until %ls. Display on: %d.", FormatIso8601Local(expireAt).c_str(), keepDisplayOn);
        if (expireAt <= NowUnix())
        {
            LOG_ERROR(L"The specified target date and time is not in the future.");
            return false;
        }

        StopTimer();
        g.mode = AwakeMode::Expirable;
        g.displayOn = keepDisplayOn;
        g.shownPid = 0;
        g.expireAt = expireAt;
        ApplyExecutionState();
        UpdateTrayIcon();
        SetTimer(g.hwnd, TIMER_TICK, 1000, nullptr);
        return true;
    }

    // -----------------------------------------------------------------------
    // Config mode: settings.json is the source of truth
    // -----------------------------------------------------------------------

    void SaveSettings()
    {
        std::string json = SerializeSettings(g.settings);
        if (WriteSettingsFile(json))
        {
            g.lastSettingsJson = json;
        }
    }

    void UserPassive()
    {
        g.settings.mode = AwakeMode::Passive;
        SaveSettings();
        EnterPassive();
        RebuildMenu();
    }

    void UserIndefinite()
    {
        g.settings.mode = AwakeMode::Indefinite;
        SaveSettings();
        EnterIndefinite(g.settings.keepDisplayOn);
        RebuildMenu();
    }

    void UserTimed(uint32_t seconds)
    {
        g.settings.mode = AwakeMode::Timed;
        g.settings.intervalHours = seconds / 3600;
        g.settings.intervalMinutes = (seconds % 3600 + 59) / 60;
        SaveSettings();
        EnterTimed(seconds, g.settings.keepDisplayOn);
        RebuildMenu();
    }

    void UserToggleDisplay()
    {
        g.settings.keepDisplayOn = !g.settings.keepDisplayOn;
        SaveSettings();

        // Change the display request in place; a running timer keeps going.
        g.displayOn = g.settings.keepDisplayOn;
        if (g.mode != AwakeMode::Passive)
        {
            ApplyExecutionState();
        }

        UpdateTrayIcon();
        RebuildMenu();
    }

    void UserToggleAllowDisplayOffWhileLocked()
    {
        g.settings.allowDisplayOffWhileLocked = !g.settings.allowDisplayOffWhileLocked;
        SaveSettings();

        g.allowDisplayOffWhileLocked = g.settings.allowDisplayOffWhileLocked;
        if (g.mode != AwakeMode::Passive)
        {
            ApplyExecutionState();
        }

        RebuildMenu();
    }

    // Applies settings read from disk. `initial` forces the mode to be (re)started.
    void ApplySettings(const AwakeSettings& incoming, bool initial)
    {
        AwakeSettings previous = g.settings;
        AwakeMode previousMode = g.mode;
        g.settings = incoming;
        g.allowDisplayOffWhileLocked = incoming.allowDisplayOffWhileLocked;

        bool sameMode = !initial && previousMode == incoming.mode;

        switch (incoming.mode)
        {
        case AwakeMode::Passive:
            if (!sameMode)
            {
                EnterPassive();
            }

            break;

        case AwakeMode::Indefinite:
            if (sameMode)
            {
                g.displayOn = incoming.keepDisplayOn;
                ApplyExecutionState();
                UpdateTrayIcon();
            }
            else
            {
                EnterIndefinite(incoming.keepDisplayOn);
            }

            break;

        case AwakeMode::Timed:
            if (sameMode && previous.intervalHours == incoming.intervalHours && previous.intervalMinutes == incoming.intervalMinutes)
            {
                g.displayOn = incoming.keepDisplayOn;
                ApplyExecutionState();
                UpdateTrayIcon();
            }
            else
            {
                EnterTimed(incoming.intervalHours * 3600 + incoming.intervalMinutes * 60, incoming.keepDisplayOn);
            }

            break;

        case AwakeMode::Expirable:
            // Never start in an expiration that is already in the past (same as PowerToys).
            if (g.settings.expiration <= NowUnix())
            {
                g.settings.expiration = NowUnix() + 5 * 60;
                SaveSettings();
            }

            if (sameMode && previous.expiration == g.settings.expiration)
            {
                g.displayOn = incoming.keepDisplayOn;
                ApplyExecutionState();
                UpdateTrayIcon();
            }
            else if (!EnterExpirable(g.settings.expiration, incoming.keepDisplayOn))
            {
                EnterPassive();
            }

            break;
        }

        RebuildMenu();
    }

    void LoadSettingsFromDisk(bool initial)
    {
        std::string raw;
        if (!ReadSettingsFile(raw))
        {
            if (initial)
            {
                LOG_INFO(L"The settings file does not exist. Scaffolding default configuration...");
                g.settings = AwakeSettings{};
                g.settings.expiration = NowUnix();
                SaveSettings();
                ApplySettings(g.settings, true);
            }

            return;
        }

        if (!initial && raw == g.lastSettingsJson)
        {
            return; // Our own write, or no real change.
        }

        g.lastSettingsJson = raw;

        AwakeSettings parsed;
        std::wstring error;
        if (!ParseSettings(raw, parsed, error))
        {
            LOG_ERROR(L"There was a problem reading the configuration file: %ls", error.c_str());
            if (initial)
            {
                ApplySettings(AwakeSettings{}, true);
            }

            return;
        }

        if (!initial)
        {
            LOG_INFO(L"Detected a settings file change. Updating configuration...");
        }

        ApplySettings(parsed, initial);
    }

    DWORD WINAPI SettingsWatcherThread(LPVOID)
    {
        std::wstring dir = SettingsDirectory();
        HANDLE change = FindFirstChangeNotificationW(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
        if (change == INVALID_HANDLE_VALUE)
        {
            LOG_ERROR(L"Could not watch %ls for changes (error %lu).", dir.c_str(), GetLastError());
            return 1;
        }

        while (WaitForSingleObject(change, INFINITE) == WAIT_OBJECT_0)
        {
            PostMessageW(g.hwnd, WM_APP_SETTINGS_CHANGED, 0, 0);
            if (!FindNextChangeNotification(change))
            {
                break;
            }
        }

        FindCloseChangeNotification(change);
        return 0;
    }

    // -----------------------------------------------------------------------
    // Timer completion
    // -----------------------------------------------------------------------

    void OnTick()
    {
        if (g.mode == AwakeMode::Timed)
        {
            if (NowUnixMs() >= g.timedEndMs)
            {
                LOG_INFO(L"Completed timed keep-awake.");
            }
            else
            {
                UpdateTrayIcon();
                return;
            }
        }
        else if (g.mode == AwakeMode::Expirable)
        {
            if (NowUnix() < g.expireAt)
            {
                return;
            }

            LOG_INFO(L"Completed expirable keep-awake.");
        }
        else
        {
            StopTimer();
            return;
        }

        StopTimer();
        if (g.configMode)
        {
            UserPassive();
        }
        else
        {
            ExitApp(0, L"Exiting after keep-awake completed.");
        }
    }

    // -----------------------------------------------------------------------
    // Session / process helpers
    // -----------------------------------------------------------------------

    bool IsWorkstationLocked()
    {
        // WTSINFOEXW level 1 (declared locally; not all SDK headers have it).
        struct InfoEx
        {
            DWORD Level;
            DWORD Reserved;
            ULONG SessionId;
            int SessionState;
            LONG SessionFlags;
        };

        LPWSTR buffer = nullptr;
        DWORD bytes = 0;
        if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, static_cast<WTS_INFO_CLASS>(25 /* WTSSessionInfoEx */), &buffer, &bytes))
        {
            return false;
        }

        bool locked = false;
        if (bytes >= sizeof(InfoEx))
        {
            const InfoEx* info = reinterpret_cast<const InfoEx*>(buffer);
            locked = info->Level == 1 && info->SessionFlags == 0 /* WTS_SESSIONSTATE_LOCK */;
        }

        WTSFreeMemory(buffer);
        return locked;
    }

    bool ProcessExists(int pid)
    {
        if (pid <= 0)
        {
            return false;
        }

        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (!h)
        {
            return false;
        }

        DWORD code = 0;
        bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        CloseHandle(h);
        return alive;
    }

    int ParentProcessId()
    {
        DWORD self = GetCurrentProcessId();
        int parent = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
        {
            return 0;
        }

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (pe.th32ProcessID == self)
                {
                    parent = static_cast<int>(pe.th32ParentProcessID);
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }

        CloseHandle(snap);
        return ProcessExists(parent) ? parent : 0;
    }

    VOID CALLBACK OnBoundProcessExit(PVOID, BOOLEAN)
    {
        PostMessageW(g.hwnd, WM_APP_QUIT, 0, QuitBoundProcess);
    }

    bool BindToProcess(int pid)
    {
        g.boundProcess = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (!g.boundProcess)
        {
            LOG_ERROR(L"Could not open process %d for an exit notification (error %lu).", pid, GetLastError());
            return false;
        }

        if (!RegisterWaitForSingleObject(&g.boundProcessWait, g.boundProcess, OnBoundProcessExit, nullptr, INFINITE, WT_EXECUTEONLYONCE))
        {
            LOG_ERROR(L"Could not wait for process %d (error %lu).", pid, GetLastError());
            return false;
        }

        LOG_INFO(L"Bound to target process: %d", pid);
        return true;
    }

    VOID CALLBACK OnExitEvent(PVOID, BOOLEAN)
    {
        PostMessageW(g.hwnd, WM_APP_QUIT, 0, QuitExitEvent);
    }

    BOOL WINAPI ConsoleHandler(DWORD)
    {
        // Runs on a separate thread; let the UI thread clean up and exit.
        DWORD_PTR result = 0;
        SendMessageTimeoutW(g.hwnd, WM_APP_QUIT, 0, QuitConsole, SMTO_BLOCK, 3000, &result);
        return FALSE;
    }

    void AllocateLocalConsole()
    {
        FreeConsole();
        if (!AllocConsole())
        {
            return;
        }

        SetConsoleTitleW(kAppName);
        g.console = OpenConsoleOutput();
        LogSetConsole(g.console);
        SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    }

    std::wstring OsBuildString()
    {
        wchar_t product[128] = L"", display[64] = L"", build[64] = L"";
        DWORD size = sizeof(product);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName", RRF_RT_REG_SZ, nullptr, product, &size);
        size = sizeof(display);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion", RRF_RT_REG_SZ, nullptr, display, &size);
        size = sizeof(build);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild", RRF_RT_REG_SZ, nullptr, build, &size);
        return Format(L"%ls %ls (build %ls)", product, display, build);
    }

    // -----------------------------------------------------------------------
    // Window procedure
    // -----------------------------------------------------------------------

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
            return 0;

        case WM_TRAY:
            switch (LOWORD(lParam))
            {
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                ShowTrayMenu();
                break;
            }

            return 0;

        case WM_COMMAND:
        {
            UINT id = LOWORD(wParam);
            if (id == TC_EXIT)
            {
                ExitApp(0, L"Exit selected from the tray menu.");
            }
            else if (!g.configMode)
            {
                break;
            }
            else if (id == TC_DISPLAY_SETTING)
            {
                UserToggleDisplay();
            }
            else if (id == TC_ALLOW_DISPLAY_OFF_LOCKED)
            {
                UserToggleAllowDisplayOffWhileLocked();
            }
            else if (id == TC_MODE_INDEFINITE)
            {
                UserIndefinite();
            }
            else if (id == TC_MODE_PASSIVE)
            {
                UserPassive();
            }
            else if (id >= TC_TIME)
            {
                size_t index = id - TC_TIME;
                const auto& entries = TrayTimes();
                if (index < entries.size())
                {
                    UserTimed(entries[index].second);
                }
                else
                {
                    LOG_ERROR(L"Custom tray time index %u is out of range.", static_cast<unsigned>(index));
                }
            }

            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_TICK)
            {
                OnTick();
            }
            else if (wParam == TIMER_SETTINGS_DEBOUNCE)
            {
                KillTimer(hwnd, TIMER_SETTINGS_DEBOUNCE);
                LoadSettingsFromDisk(false);
            }
            else if (wParam == TIMER_ICON_RETRY)
            {
                KillTimer(hwnd, TIMER_ICON_RETRY);
                UpdateTrayIcon(true);
            }

            return 0;

        case WM_APP_SETTINGS_CHANGED:
            // Coalesce bursts of change notifications.
            SetTimer(hwnd, TIMER_SETTINGS_DEBOUNCE, 100, nullptr);
            return 0;

        case WM_APP_QUIT:
            switch (lParam)
            {
            case QuitConsole: ExitApp(static_cast<int>(wParam), L"Exiting because the console was closed."); break;
            case QuitExitEvent: ExitApp(0, L"Received a signal to end the process."); break;
            case QuitBoundProcess: ExitApp(0, L"Terminating because the bound process exited."); break;
            default: ExitApp(static_cast<int>(wParam), L"Exiting."); break;
            }

            return 0;

        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_UNLOCK)
            {
                g.screenLocked = wParam == WTS_SESSION_LOCK;
                LOG_INFO(L"Session %ls.", g.screenLocked ? L"locked" : L"unlocked");
                if (g.mode != AwakeMode::Passive)
                {
                    ApplyExecutionState();
                }
            }

            return 0;

        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMPOWERSTATUSCHANGE)
            {
                if (g.mode != AwakeMode::Passive)
                {
                    LOG_INFO(L"Power event received. Reapplying awake state.");
                    ApplyExecutionState();
                }
            }

            return TRUE;

        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
            // Reload icons at the new tray icon size.
            LoadIcons();
            UpdateTrayIcon();
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            if (msg == g.taskbarCreated && g.taskbarCreated != 0)
            {
                LOG_INFO(L"Taskbar re-created.");
                LoadIcons();
                g.iconAdded = false;
                UpdateTrayIcon(true);
                return 0;
            }

            break;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // -----------------------------------------------------------------------
    // Command line (same options as PowerToys Awake)
    // -----------------------------------------------------------------------

    struct Options
    {
        bool usePtConfig = false;
        bool displayOn = false;
        uint32_t timeLimit = 0;
        int pid = 0;
        std::wstring expireAtText;
        int64_t expireAt = 0;
        bool useParentPid = false;
        bool help = false;
        bool version = false;
        std::vector<std::wstring> errors;
    };

    bool EqualsI(const std::wstring& a, const wchar_t* b)
    {
        return _wcsicmp(a.c_str(), b) == 0;
    }

    bool ParseUInt(const std::wstring& s, uint32_t& out)
    {
        if (s.empty() || s.size() > 10)
            return false;
        uint64_t v = 0;
        for (wchar_t c : s)
        {
            if (c < L'0' || c > L'9')
                return false;
            v = v * 10 + static_cast<uint64_t>(c - L'0');
        }

        if (v > 0xFFFFFFFFull)
            return false;
        out = static_cast<uint32_t>(v);
        return true;
    }

    bool ParseInt(const std::wstring& s, int& out)
    {
        bool negative = !s.empty() && s[0] == L'-';
        uint32_t v = 0;
        if (!ParseUInt(negative ? s.substr(1) : s, v) || v > 0x7FFFFFFFu)
            return false;
        out = negative ? -static_cast<int>(v) : static_cast<int>(v);
        return true;
    }

    Options ParseCommandLine()
    {
        Options o;
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
        {
            return o;
        }

        std::vector<std::wstring> args(argv + 1, argv + argc);
        LocalFree(argv);

        for (size_t i = 0; i < args.size(); ++i)
        {
            std::wstring name = args[i];
            std::wstring inlineValue;
            bool hasInline = false;

            if (name.size() > 1 && (name[0] == L'-' || name[0] == L'/'))
            {
                size_t sep = name.find_first_of(L"=:");
                if (sep != std::wstring::npos && sep > 1)
                {
                    inlineValue = name.substr(sep + 1);
                    name = name.substr(0, sep);
                    hasInline = true;
                }
            }

            auto is = [&](const wchar_t* shortName, const wchar_t* longName) {
                return EqualsI(name, shortName) || EqualsI(name, longName);
            };

            // Optional true/false value for flags.
            auto flag = [&](bool& target) {
                std::wstring v;
                if (hasInline)
                {
                    v = inlineValue;
                }
                else if (i + 1 < args.size() && (EqualsI(args[i + 1], L"true") || EqualsI(args[i + 1], L"false")))
                {
                    v = args[++i];
                }

                if (v.empty())
                {
                    target = true;
                }
                else if (EqualsI(v, L"true"))
                {
                    target = true;
                }
                else if (EqualsI(v, L"false"))
                {
                    target = false;
                }
                else
                {
                    o.errors.push_back(Format(S(Str::ErrBool), v.c_str()));
                }
            };

            // Value for options that take one.
            auto value = [&](std::wstring& out) -> bool {
                if (hasInline)
                {
                    out = inlineValue;
                    return true;
                }

                if (i + 1 < args.size())
                {
                    out = args[++i];
                    return true;
                }

                o.errors.push_back(Format(S(Str::ErrMissingValue), name.c_str()));
                return false;
            };

            if (is(L"-h", L"--help") || EqualsI(name, L"-?") || EqualsI(name, L"/?") || EqualsI(name, L"/h"))
            {
                o.help = true;
            }
            else if (EqualsI(name, L"--version"))
            {
                o.version = true;
            }
            else if (is(L"-c", L"--use-pt-config"))
            {
                flag(o.usePtConfig);
            }
            else if (is(L"-d", L"--display-on"))
            {
                flag(o.displayOn);
            }
            else if (is(L"-u", L"--use-parent-pid"))
            {
                flag(o.useParentPid);
            }
            else if (is(L"-t", L"--time-limit"))
            {
                std::wstring v;
                if (value(v) && !ParseUInt(v, o.timeLimit))
                {
                    o.errors.push_back(Format(S(Str::ErrTime), v.c_str()));
                }
            }
            else if (is(L"-p", L"--pid"))
            {
                std::wstring v;
                if (value(v) && !ParseInt(v, o.pid))
                {
                    o.errors.push_back(Format(S(Str::ErrPid), v.c_str()));
                }
            }
            else if (is(L"-e", L"--expire-at"))
            {
                std::wstring v;
                if (value(v))
                {
                    o.expireAtText = v;
                    if (!ParseDateTime(v, o.expireAt))
                    {
                        o.errors.push_back(Format(S(Str::ErrDate), v.c_str()));
                    }
                }
            }
            else
            {
                o.errors.push_back(Format(S(Str::ErrUnknownArg), args[i].c_str()));
            }
        }

        return o;
    }

    // -----------------------------------------------------------------------
    // Startup
    // -----------------------------------------------------------------------

    bool CreateTrayWindow()
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = g.instance;
        wc.lpszClassName = kWindowClass;
        wc.hIcon = g.icons[IconDefault];
        if (!RegisterClassExW(&wc))
        {
            return false;
        }

        // A hidden top-level window (not message-only) so it receives TaskbarCreated,
        // power and session broadcasts.
        g.hwnd = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, g.instance, nullptr);
        return g.hwnd != nullptr;
    }

    void HandleArguments(const Options& o)
    {
        if (o.pid == 0 && !o.useParentPid && o.usePtConfig)
        {
            LOG_INFO(L"Tray mode (settings file), no console.");
        }
        else if (o.pid == 0 && !o.useParentPid)
        {
            LOG_INFO(L"No PID specified. Allocating console...");
            AllocateLocalConsole();
        }
        else
        {
            LOG_INFO(L"Starting with PID binding.");
            g.processBound = true;
        }

        LOG_INFO(L"--use-pt-config=%d --display-on=%d --time-limit=%u --pid=%d --expire-at=\"%ls\" --use-parent-pid=%d",
                 o.usePtConfig, o.displayOn, o.timeLimit, o.pid, o.expireAtText.c_str(), o.useParentPid);

        g.exitEvent = CreateEventW(nullptr, TRUE, FALSE, kExitEventName);
        if (g.exitEvent)
        {
            RegisterWaitForSingleObject(&g.exitEventWait, g.exitEvent, OnExitEvent, nullptr, INFINITE, WT_EXECUTEONLYONCE);
        }

        if (o.usePtConfig)
        {
            // Settings file mode: everything else on the command line is ignored (except --pid).
            g.configMode = true;
            CreateDirectoryW(SettingsDirectory().c_str(), nullptr);
            LOG_INFO(L"Reading configuration file: %ls", SettingsFilePath().c_str());
            LoadSettingsFromDisk(true);
            CloseHandle(CreateThread(nullptr, 0, SettingsWatcherThread, nullptr, 0, nullptr));

            if (o.pid != 0)
            {
                if (!ProcessExists(o.pid))
                {
                    ExitApp(1, L"Exiting because the requested process ID could not be found.");
                }

                BindToProcess(o.pid);
            }

            return;
        }

        if (o.pid != 0 || o.useParentPid)
        {
            int target = 0;
            if (o.pid != 0)
            {
                if (o.pid == static_cast<int>(GetCurrentProcessId()))
                {
                    ExitApp(1, L"Exiting because the provided process ID is Awake's own.");
                }

                if (!ProcessExists(o.pid))
                {
                    ExitApp(1, L"Exiting because the requested process ID could not be found.");
                }

                target = o.pid;
            }
            else
            {
                target = ParentProcessId();
                if (target == 0)
                {
                    ExitApp(1, L"Exiting because the parent process ID could not be found.");
                }
            }

            EnterIndefinite(o.displayOn, target);
            RebuildMenu();
            BindToProcess(target);
            return;
        }

        // Date-based expiration takes precedence over a time limit.
        if (!o.expireAtText.empty())
        {
            if (!EnterExpirable(o.expireAt, o.displayOn))
            {
                ExitApp(1, L"The specified expiration date and time is not in the future.");
            }
        }
        else if (o.timeLimit == 0)
        {
            EnterIndefinite(o.displayOn);
        }
        else
        {
            EnterTimed(o.timeLimit, o.displayOn);
        }

        RebuildMenu();
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    g.instance = instance;
    StringsInit();

    Options options = ParseCommandLine();

    if (options.help)
    {
        ShowOutput(S(Str::Help), false);
        return 0;
    }

    if (options.version)
    {
        ShowOutput(L"Awake " AWAKE_VERSION_WSTRING, false);
        return 0;
    }

    if (!options.errors.empty())
    {
        std::wstring text;
        for (const auto& e : options.errors)
        {
            text += e + L"\n";
        }

        text += L"\n";
        text += S(Str::SeeHelp);
        ShowOutput(text, true);
        return 1;
    }

    LogInit();

    g.mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (g.mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        LOG_INFO(L"Awake is already running! Exiting the application.");
        CloseHandle(g.mutex);
        return 1;
    }

    LOG_INFO(L"Launching Awake %ls (standalone).", AWAKE_VERSION_WSTRING);
    LOG_INFO(L"OS: %ls", OsBuildString().c_str());

    LoadIcons();
    if (!CreateTrayWindow())
    {
        LOG_ERROR(L"Failed to create the tray window (error %lu).", GetLastError());
        return 1;
    }

    WTSRegisterSessionNotification(g.hwnd, NOTIFY_FOR_THIS_SESSION);
    g.screenLocked = IsWorkstationLocked();

    g.tip = kAppName;
    g.currentIcon = g.icons[IconDefault];
    UpdateTrayIcon(true);

    HandleArguments(options);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ExitApp(static_cast<int>(msg.wParam), L"Message loop terminated.");
}
