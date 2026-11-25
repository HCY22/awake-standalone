// Awake (standalone) - a small native port of Microsoft PowerToys Awake.
// Original PowerToys Awake: Copyright (c) Microsoft Corporation, MIT license.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "version.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Settings (same JSON shape as PowerToys' Awake settings.json)
// ---------------------------------------------------------------------------

enum class AwakeMode : int
{
    Passive = 0,
    Indefinite = 1,
    Timed = 2,
    Expirable = 3,
};

struct AwakeSettings
{
    bool keepDisplayOn = false;
    bool allowDisplayOffWhileLocked = true;
    AwakeMode mode = AwakeMode::Passive;
    uint32_t intervalHours = 0;
    uint32_t intervalMinutes = 1;
    int64_t expiration = 0; // Unix seconds (UTC).
    std::vector<std::pair<std::wstring, uint32_t>> customTrayTimes; // Order preserved.
};

std::wstring SettingsDirectory();
std::wstring SettingsFilePath();

// Reads the raw file contents (UTF-8). Returns false if the file can't be read.
bool ReadSettingsFile(std::string& raw);

// Parses settings JSON. Unknown keys are ignored; missing keys keep defaults.
bool ParseSettings(const std::string& raw, AwakeSettings& out, std::wstring& error);

std::string SerializeSettings(const AwakeSettings& settings);

bool WriteSettingsFile(const std::string& json);

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------

int64_t NowUnix();
int64_t NowUnixMs();

// Parses a date/time. Accepts ISO 8601 (with or without offset / 'Z') and common
// forms such as "2026-12-31 18:00", "2026/12/31 6:00 PM", "18:00" (today) or
// "2026-12-31" (midnight). Values without an offset are local time.
bool ParseDateTime(const std::wstring& text, int64_t& outUtc);

// "2026-12-31T18:00:00+08:00" in local time.
std::wstring FormatIso8601Local(int64_t utc);

// Short, user-locale formatted local date/time for the tray tooltip.
std::wstring FormatShortLocal(int64_t utc);

// "H:MM:SS" or "M:SS".
std::wstring FormatDuration(int64_t seconds);

// ---------------------------------------------------------------------------
// Logging (%LOCALAPPDATA%\AwakeStandalone\Logs)
// ---------------------------------------------------------------------------

void LogInit();
void LogSetConsole(HANDLE console);
void LogWrite(const wchar_t* level, const wchar_t* format, ...);

#define LOG_INFO(...) LogWrite(L"INFO", __VA_ARGS__)
#define LOG_ERROR(...) LogWrite(L"ERROR", __VA_ARGS__)

// ---------------------------------------------------------------------------
// Localized strings (English, Traditional Chinese, Simplified Chinese)
// ---------------------------------------------------------------------------

enum class Str : int
{
    Exit,
    MinutesFmt,
    HourFmt,
    HoursFmt,
    KeepIndefinitely,
    KeepOnInterval,
    KeepScreenOn,
    AllowDisplayOffLocked,
    KeepUntilExpiration,
    Off,
    TrayIndefinite,
    TrayTimed,
    TrayOff,
    ScreenOn,
    ScreenOff,
    TrayScreenFmt,
    TrayUntilFmt,
    TrayRemainingFmt,
    Help,
    ErrUnknownArg,
    ErrMissingValue,
    ErrTime,
    ErrPid,
    ErrDate,
    ErrBool,
    SeeHelp,
    Count,
};

void StringsInit();
const wchar_t* S(Str id);
std::wstring Format(const wchar_t* format, ...);
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);
