// Localized UI strings. Original English text from PowerToys Awake (MIT).
#include "awake.h"

#include <cstdarg>
#include <cwchar>

namespace
{
    // Order must match enum class Str.
    const wchar_t* const kEnglish[] = {
        L"Exit",
        L"%u minutes",
        L"%u hour",
        L"%u hours",
        L"Keep awake indefinitely",
        L"Keep awake on interval",
        L"Keep screen on",
        L"Allow screen to turn off while locked",
        L"Keep awake until expiration date and time",
        L"Off (keep using the selected power plan)",
        L"Indefinite",
        L"Timed",
        L"Off",
        L"On",
        L"Off",
        L"Screen: %ls",
        L"Until %ls",
        L"%ls remaining",
        L"Awake - keep your PC awake.\n"
        L"\n"
        L"Usage: Awake.exe [options]\n"
        L"\n"
        L"Options:\n"
        L"  -c, --use-pt-config      Tray mode: manage the state from the tray menu and\n"
        L"                           settings.json (other options are ignored).\n"
        L"  -d, --display-on         Keep the display on as well.\n"
        L"  -t, --time-limit <sec>   Keep awake for this many seconds, then exit.\n"
        L"  -e, --expire-at <time>   Keep awake until this date/time, then exit.\n"
        L"                           e.g. \"2026-12-31 18:00\" or \"18:00\".\n"
        L"  -p, --pid <pid>          Keep awake until the given process exits.\n"
        L"  -u, --use-parent-pid     Keep awake until the parent process exits.\n"
        L"  -?, -h, --help           Show this help.\n"
        L"  --version                Show the version.\n"
        L"\n"
        L"With no options, keeps the PC awake until the console window is closed.\n",
        L"Unrecognized command or argument '%ls'.",
        L"Required argument missing for option: '%ls'.",
        L"Interval in --time-limit could not be parsed correctly. Check that the value is valid and doesn't exceed 4,294,967,295. Value used: %ls.",
        L"PID value in --pid could not be parsed correctly. Check that the value is valid and falls within the boundaries of Windows PID process limits. Value used: %ls.",
        L"Date and time value in --expire-at could not be parsed correctly. Check that the value is a valid date and time. Value used: %ls.",
        L"Cannot parse argument '%ls' as true/false.",
        L"Run Awake.exe --help for usage.",
    };

    const wchar_t* const kTraditionalChinese[] = {
        L"結束",
        L"%u 分鐘",
        L"%u 小時",
        L"%u 小時",
        L"無限期保持喚醒",
        L"保持喚醒一段時間",
        L"保持螢幕開啟",
        L"鎖定時允許關閉螢幕",
        L"保持喚醒到指定日期時間",
        L"關閉（使用目前的電源計劃）",
        L"無限期",
        L"計時",
        L"關閉",
        L"開啟",
        L"關閉",
        L"螢幕：%ls",
        L"直到 %ls",
        L"剩餘 %ls",
        L"Awake - 讓電腦保持喚醒。\n"
        L"\n"
        L"用法：Awake.exe [選項]\n"
        L"\n"
        L"選項：\n"
        L"  -c, --use-pt-config      系統匣模式：用系統匣選單與 settings.json 管理狀態\n"
        L"                           （會忽略其他選項）。\n"
        L"  -d, --display-on         同時保持螢幕開啟。\n"
        L"  -t, --time-limit <秒>    保持喚醒指定秒數後結束。\n"
        L"  -e, --expire-at <時間>   保持喚醒到指定日期時間後結束。\n"
        L"                           例如 \"2026-12-31 18:00\" 或 \"18:00\"。\n"
        L"  -p, --pid <PID>          保持喚醒直到指定行程結束。\n"
        L"  -u, --use-parent-pid     保持喚醒直到父行程結束。\n"
        L"  -?, -h, --help           顯示此說明。\n"
        L"  --version                顯示版本。\n"
        L"\n"
        L"不加任何選項時，會保持喚醒直到關閉主控台視窗。\n",
        L"無法辨識的命令或引數 '%ls'。",
        L"選項 '%ls' 缺少必要的值。",
        L"無法解析 --time-limit 的秒數，請確認數值正確且不超過 4,294,967,295。輸入值：%ls。",
        L"無法解析 --pid 的行程 ID。輸入值：%ls。",
        L"無法解析 --expire-at 的日期時間。輸入值：%ls。",
        L"無法將引數 '%ls' 解析為 true/false。",
        L"執行 Awake.exe --help 查看用法。",
    };

    const wchar_t* const kSimplifiedChinese[] = {
        L"退出",
        L"%u 分钟",
        L"%u 小时",
        L"%u 小时",
        L"无限期保持唤醒",
        L"保持唤醒一段时间",
        L"保持屏幕开启",
        L"锁定时允许关闭屏幕",
        L"保持唤醒到指定日期时间",
        L"关闭（使用当前的电源计划）",
        L"无限期",
        L"计时",
        L"关闭",
        L"开启",
        L"关闭",
        L"屏幕：%ls",
        L"直到 %ls",
        L"剩余 %ls",
        L"Awake - 让电脑保持唤醒。\n"
        L"\n"
        L"用法：Awake.exe [选项]\n"
        L"\n"
        L"选项：\n"
        L"  -c, --use-pt-config      托盘模式：用托盘菜单与 settings.json 管理状态\n"
        L"                           （会忽略其他选项）。\n"
        L"  -d, --display-on         同时保持屏幕开启。\n"
        L"  -t, --time-limit <秒>    保持唤醒指定秒数后退出。\n"
        L"  -e, --expire-at <时间>   保持唤醒到指定日期时间后退出。\n"
        L"                           例如 \"2026-12-31 18:00\" 或 \"18:00\"。\n"
        L"  -p, --pid <PID>          保持唤醒直到指定进程结束。\n"
        L"  -u, --use-parent-pid     保持唤醒直到父进程结束。\n"
        L"  -?, -h, --help           显示此帮助。\n"
        L"  --version                显示版本。\n"
        L"\n"
        L"不加任何选项时，会保持唤醒直到关闭控制台窗口。\n",
        L"无法识别的命令或参数 '%ls'。",
        L"选项 '%ls' 缺少必需的值。",
        L"无法解析 --time-limit 的秒数，请确认数值正确且不超过 4,294,967,295。输入值：%ls。",
        L"无法解析 --pid 的进程 ID。输入值：%ls。",
        L"无法解析 --expire-at 的日期时间。输入值：%ls。",
        L"无法将参数 '%ls' 解析为 true/false。",
        L"运行 Awake.exe --help 查看用法。",
    };

    static_assert(sizeof(kEnglish) / sizeof(kEnglish[0]) == static_cast<size_t>(Str::Count), "English table size");
    static_assert(sizeof(kTraditionalChinese) / sizeof(kTraditionalChinese[0]) == static_cast<size_t>(Str::Count), "zh-Hant table size");
    static_assert(sizeof(kSimplifiedChinese) / sizeof(kSimplifiedChinese[0]) == static_cast<size_t>(Str::Count), "zh-Hans table size");

    const wchar_t* const* g_table = kEnglish;
}

void StringsInit()
{
    LANGID lang = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(lang) == LANG_CHINESE)
    {
        WORD sub = SUBLANGID(lang);
        g_table = (sub == SUBLANG_CHINESE_SIMPLIFIED || sub == SUBLANG_CHINESE_SINGAPORE)
                      ? kSimplifiedChinese
                      : kTraditionalChinese;
    }
    else
    {
        g_table = kEnglish;
    }
}

const wchar_t* S(Str id)
{
    return g_table[static_cast<int>(id)];
}

std::wstring Format(const wchar_t* format, ...)
{
    wchar_t buffer[2048];
    va_list args;
    va_start(args, format);
    int n = _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);
    if (n < 0)
    {
        buffer[_countof(buffer) - 1] = L'\0';
    }

    return buffer;
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
    {
        return {};
    }

    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty())
    {
        return {};
    }

    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
