// Settings file (JSON) and date/time helpers.
#include "awake.h"

#include <shlobj.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

std::wstring SettingsDirectory()
{
    wchar_t buffer[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, _countof(buffer));
    std::wstring base;
    if (n > 0 && n < _countof(buffer))
    {
        base = buffer;
    }
    else
    {
        PWSTR known = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known)))
        {
            base = known;
        }

        CoTaskMemFree(known);
    }

    return base + L"\\AwakeStandalone";
}

std::wstring SettingsFilePath()
{
    return SettingsDirectory() + L"\\settings.json";
}

bool ReadSettingsFile(std::string& raw)
{
    // The file may be in the middle of being written by an editor; retry briefly.
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        HANDLE f = CreateFileW(SettingsFilePath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            {
                return false;
            }

            Sleep(50);
            continue;
        }

        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(f, &size) && size.QuadPart < 4 * 1024 * 1024;
        if (ok)
        {
            raw.assign(static_cast<size_t>(size.QuadPart), '\0');
            DWORD read = 0;
            ok = size.QuadPart == 0 || (ReadFile(f, raw.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) && read == size.QuadPart);
        }

        CloseHandle(f);
        if (ok)
        {
            return true;
        }

        Sleep(50);
    }

    return false;
}

bool WriteSettingsFile(const std::string& json)
{
    CreateDirectoryW(SettingsDirectory().c_str(), nullptr);

    // Write to a temp file and swap it in, so readers never see a half-written file.
    std::wstring path = SettingsFilePath();
    std::wstring temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR(L"Could not create %ls (error %lu).", temp.c_str(), GetLastError());
        return false;
    }

    DWORD written = 0;
    bool ok = WriteFile(f, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) && written == json.size();
    CloseHandle(f);

    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        LOG_ERROR(L"Could not save settings to %ls (error %lu).", path.c_str(), GetLastError());
        DeleteFileW(temp.c_str());
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Minimal JSON reader
// ---------------------------------------------------------------------------

namespace
{
    struct JsonValue
    {
        enum class Type
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object,
        };

        Type type = Type::Null;
        bool boolean = false;
        double number = 0;
        std::string string;
        std::vector<JsonValue> items;  // Array elements or object values.
        std::vector<std::string> keys; // Object keys (parallel to items).

        const JsonValue* Get(const char* key) const
        {
            if (type != Type::Object)
            {
                return nullptr;
            }

            for (size_t i = 0; i < keys.size(); ++i)
            {
                if (keys[i] == key)
                {
                    return &items[i];
                }
            }

            return nullptr;
        }
    };

    class JsonParser
    {
    public:
        explicit JsonParser(const std::string& text) :
            s_(text) {}

        bool Parse(JsonValue& out)
        {
            // Skip UTF-8 BOM.
            if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF && static_cast<unsigned char>(s_[1]) == 0xBB && static_cast<unsigned char>(s_[2]) == 0xBF)
            {
                p_ = 3;
            }

            if (!Value(out, 0))
            {
                return false;
            }

            SkipWs();

            // Tolerate trailing NULs (seen with NTFS corruption, see PowerToys #6413).
            while (p_ < s_.size() && s_[p_] == '\0')
            {
                ++p_;
            }

            SkipWs();
            return p_ == s_.size();
        }

        size_t Position() const { return p_; }

    private:
        const std::string& s_;
        size_t p_ = 0;

        void SkipWs()
        {
            while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\r' || s_[p_] == '\n'))
            {
                ++p_;
            }
        }

        bool Literal(const char* word)
        {
            size_t n = strlen(word);
            if (s_.compare(p_, n, word) == 0)
            {
                p_ += n;
                return true;
            }

            return false;
        }

        static void AppendUtf8(std::string& out, uint32_t cp)
        {
            if (cp < 0x80)
            {
                out += static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        bool Hex4(uint32_t& out)
        {
            if (p_ + 4 > s_.size())
            {
                return false;
            }

            out = 0;
            for (int i = 0; i < 4; ++i)
            {
                char c = s_[p_++];
                out <<= 4;
                if (c >= '0' && c <= '9')
                    out |= static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f')
                    out |= static_cast<uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F')
                    out |= static_cast<uint32_t>(c - 'A' + 10);
                else
                    return false;
            }

            return true;
        }

        bool String(std::string& out)
        {
            if (p_ >= s_.size() || s_[p_] != '"')
            {
                return false;
            }

            ++p_;
            while (p_ < s_.size())
            {
                char c = s_[p_++];
                if (c == '"')
                {
                    return true;
                }

                if (c != '\\')
                {
                    out += c;
                    continue;
                }

                if (p_ >= s_.size())
                {
                    return false;
                }

                char e = s_[p_++];
                switch (e)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    uint32_t cp = 0;
                    if (!Hex4(cp))
                    {
                        return false;
                    }

                    if (cp >= 0xD800 && cp <= 0xDBFF && p_ + 1 < s_.size() && s_[p_] == '\\' && s_[p_ + 1] == 'u')
                    {
                        p_ += 2;
                        uint32_t low = 0;
                        if (!Hex4(low))
                        {
                            return false;
                        }

                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }

                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    return false;
                }
            }

            return false;
        }

        bool Value(JsonValue& v, int depth)
        {
            if (depth > 32)
            {
                return false;
            }

            SkipWs();
            if (p_ >= s_.size())
            {
                return false;
            }

            char c = s_[p_];
            if (c == '{')
            {
                ++p_;
                v.type = JsonValue::Type::Object;
                SkipWs();
                if (p_ < s_.size() && s_[p_] == '}')
                {
                    ++p_;
                    return true;
                }

                for (;;)
                {
                    SkipWs();
                    std::string key;
                    if (!String(key))
                    {
                        return false;
                    }

                    SkipWs();
                    if (p_ >= s_.size() || s_[p_] != ':')
                    {
                        return false;
                    }

                    ++p_;
                    JsonValue child;
                    if (!Value(child, depth + 1))
                    {
                        return false;
                    }

                    v.keys.push_back(std::move(key));
                    v.items.push_back(std::move(child));
                    SkipWs();
                    if (p_ < s_.size() && s_[p_] == ',')
                    {
                        ++p_;
                        continue;
                    }

                    if (p_ < s_.size() && s_[p_] == '}')
                    {
                        ++p_;
                        return true;
                    }

                    return false;
                }
            }

            if (c == '[')
            {
                ++p_;
                v.type = JsonValue::Type::Array;
                SkipWs();
                if (p_ < s_.size() && s_[p_] == ']')
                {
                    ++p_;
                    return true;
                }

                for (;;)
                {
                    JsonValue child;
                    if (!Value(child, depth + 1))
                    {
                        return false;
                    }

                    v.items.push_back(std::move(child));
                    SkipWs();
                    if (p_ < s_.size() && s_[p_] == ',')
                    {
                        ++p_;
                        continue;
                    }

                    if (p_ < s_.size() && s_[p_] == ']')
                    {
                        ++p_;
                        return true;
                    }

                    return false;
                }
            }

            if (c == '"')
            {
                v.type = JsonValue::Type::String;
                return String(v.string);
            }

            if (Literal("true"))
            {
                v.type = JsonValue::Type::Bool;
                v.boolean = true;
                return true;
            }

            if (Literal("false"))
            {
                v.type = JsonValue::Type::Bool;
                v.boolean = false;
                return true;
            }

            if (Literal("null"))
            {
                v.type = JsonValue::Type::Null;
                return true;
            }

            // Number.
            size_t start = p_;
            if (p_ < s_.size() && (s_[p_] == '-' || s_[p_] == '+'))
            {
                ++p_;
            }

            while (p_ < s_.size() && (isdigit(static_cast<unsigned char>(s_[p_])) || s_[p_] == '.' || s_[p_] == 'e' || s_[p_] == 'E' || s_[p_] == '-' || s_[p_] == '+'))
            {
                ++p_;
            }

            if (p_ == start)
            {
                return false;
            }

            std::string num = s_.substr(start, p_ - start);
            char* end = nullptr;
            v.type = JsonValue::Type::Number;
            v.number = strtod(num.c_str(), &end);
            return end && *end == '\0';
        }
    };

    bool AsBool(const JsonValue* v, bool& out)
    {
        if (v && v->type == JsonValue::Type::Bool)
        {
            out = v->boolean;
            return true;
        }

        return false;
    }

    bool AsUInt(const JsonValue* v, uint32_t& out)
    {
        if (v && v->type == JsonValue::Type::Number && v->number >= 0 && v->number <= 4294967295.0)
        {
            out = static_cast<uint32_t>(v->number);
            return true;
        }

        return false;
    }

    void JsonEscape(std::string& out, const std::string& s)
    {
        out += '"';
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c);
                }
            }
        }

        out += '"';
    }
}

bool ParseSettings(const std::string& raw, AwakeSettings& out, std::wstring& error)
{
    JsonValue root;
    JsonParser parser(raw);
    if (!parser.Parse(root) || root.type != JsonValue::Type::Object)
    {
        error = Format(L"Invalid JSON near byte %u.", static_cast<unsigned>(parser.Position()));
        return false;
    }

    AwakeSettings s;
    const JsonValue* props = root.Get("properties");
    if (props && props->type == JsonValue::Type::Object)
    {
        AsBool(props->Get("keepDisplayOn"), s.keepDisplayOn);
        AsBool(props->Get("allowDisplayOffWhileLocked"), s.allowDisplayOffWhileLocked);
        AsUInt(props->Get("intervalHours"), s.intervalHours);
        AsUInt(props->Get("intervalMinutes"), s.intervalMinutes);

        const JsonValue* mode = props->Get("mode");
        uint32_t modeNumber = 0;
        if (AsUInt(mode, modeNumber) && modeNumber <= 3)
        {
            s.mode = static_cast<AwakeMode>(modeNumber);
        }
        else if (mode && mode->type == JsonValue::Type::String)
        {
            std::string m = mode->string;
            for (char& ch : m)
                ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
            if (m == "PASSIVE")
                s.mode = AwakeMode::Passive;
            else if (m == "INDEFINITE")
                s.mode = AwakeMode::Indefinite;
            else if (m == "TIMED")
                s.mode = AwakeMode::Timed;
            else if (m == "EXPIRABLE")
                s.mode = AwakeMode::Expirable;
        }

        const JsonValue* exp = props->Get("expirationDateTime");
        int64_t when = 0;
        if (exp && exp->type == JsonValue::Type::String && ParseDateTime(Utf8ToWide(exp->string), when))
        {
            s.expiration = when;
        }
        else
        {
            s.expiration = NowUnix();
        }

        const JsonValue* times = props->Get("customTrayTimes");
        if (times && times->type == JsonValue::Type::Object)
        {
            for (size_t i = 0; i < times->keys.size(); ++i)
            {
                uint32_t seconds = 0;
                if (AsUInt(&times->items[i], seconds))
                {
                    s.customTrayTimes.emplace_back(Utf8ToWide(times->keys[i]), seconds);
                }
            }
        }
    }

    out = std::move(s);
    return true;
}

std::string SerializeSettings(const AwakeSettings& s)
{
    std::string j;
    j += "{\n  \"properties\": {\n";
    j += "    \"keepDisplayOn\": ";
    j += s.keepDisplayOn ? "true" : "false";
    j += ",\n    \"allowDisplayOffWhileLocked\": ";
    j += s.allowDisplayOffWhileLocked ? "true" : "false";
    j += ",\n    \"mode\": " + std::to_string(static_cast<int>(s.mode));
    j += ",\n    \"intervalHours\": " + std::to_string(s.intervalHours);
    j += ",\n    \"intervalMinutes\": " + std::to_string(s.intervalMinutes);
    j += ",\n    \"expirationDateTime\": ";
    JsonEscape(j, WideToUtf8(FormatIso8601Local(s.expiration)));
    j += ",\n    \"customTrayTimes\": {";
    for (size_t i = 0; i < s.customTrayTimes.size(); ++i)
    {
        j += i == 0 ? "\n      " : ",\n      ";
        JsonEscape(j, WideToUtf8(s.customTrayTimes[i].first));
        j += ": " + std::to_string(s.customTrayTimes[i].second);
    }

    j += s.customTrayTimes.empty() ? "}" : "\n    }";
    j += "\n  },\n  \"name\": \"Awake\",\n  \"version\": \"" AWAKE_VERSION_STRING "\"\n}\n";
    return j;
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

namespace
{
    constexpr int64_t kUnixEpochIn100ns = 116444736000000000LL;

    int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
    {
        y -= m <= 2;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int64_t>(doe) - 719468;
    }

    int64_t FieldsToSeconds(const SYSTEMTIME& st)
    {
        return DaysFromCivil(st.wYear, st.wMonth, st.wDay) * 86400 + st.wHour * 3600 + st.wMinute * 60 + st.wSecond;
    }

    bool UnixToSystemTime(int64_t unix, SYSTEMTIME& st)
    {
        ULARGE_INTEGER u;
        u.QuadPart = static_cast<ULONGLONG>(unix * 10000000LL + kUnixEpochIn100ns);
        FILETIME ft{ u.LowPart, u.HighPart };
        return FileTimeToSystemTime(&ft, &st) != FALSE;
    }

    bool UtcToLocal(int64_t utc, SYSTEMTIME& local)
    {
        SYSTEMTIME st;
        return UnixToSystemTime(utc, st) && SystemTimeToTzSpecificLocalTime(nullptr, &st, &local);
    }

    bool LocalToUtc(const SYSTEMTIME& local, int64_t& utc)
    {
        SYSTEMTIME st;
        if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &st))
        {
            return false;
        }

        utc = FieldsToSeconds(st);
        return true;
    }

    bool IsLeap(unsigned y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

    unsigned DaysInMonth(unsigned y, unsigned m)
    {
        static const unsigned days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        return m == 2 && IsLeap(y) ? 29 : days[m - 1];
    }

    class Scanner
    {
    public:
        explicit Scanner(const std::wstring& s) :
            s_(s) {}

        void SkipSpaces()
        {
            while (p_ < s_.size() && iswspace(s_[p_]))
                ++p_;
        }

        bool AtEnd() const { return p_ >= s_.size(); }
        wchar_t Peek(size_t ahead = 0) const { return p_ + ahead < s_.size() ? s_[p_ + ahead] : L'\0'; }
        void Advance(size_t n = 1) { p_ += n; }
        size_t Pos() const { return p_; }
        void Reset(size_t pos) { p_ = pos; }

        // Reads 1..maxDigits digits.
        bool Number(unsigned& out, size_t maxDigits, size_t* count = nullptr)
        {
            size_t n = 0;
            out = 0;
            while (n < maxDigits && iswdigit(Peek()))
            {
                out = out * 10 + static_cast<unsigned>(Peek() - L'0');
                ++p_;
                ++n;
            }

            if (count)
                *count = n;
            return n > 0;
        }

        bool MatchI(const wchar_t* word)
        {
            size_t n = wcslen(word);
            if (p_ + n > s_.size() || _wcsnicmp(s_.c_str() + p_, word, n) != 0)
            {
                return false;
            }

            p_ += n;
            return true;
        }

    private:
        const std::wstring& s_;
        size_t p_ = 0;
    };

    bool ParseTime(Scanner& sc, unsigned& h, unsigned& m, unsigned& s)
    {
        s = 0;
        if (!sc.Number(h, 2) || sc.Peek() != L':')
        {
            return false;
        }

        sc.Advance();
        if (!sc.Number(m, 2))
        {
            return false;
        }

        if (sc.Peek() == L':')
        {
            sc.Advance();
            if (!sc.Number(s, 2))
            {
                return false;
            }

            // Fractional seconds (ignored).
            if (sc.Peek() == L'.' || sc.Peek() == L',')
            {
                sc.Advance();
                unsigned frac = 0;
                size_t n = 0;
                while (iswdigit(sc.Peek()))
                {
                    sc.Number(frac, 1, &n);
                }
            }
        }

        // Optional AM/PM.
        size_t save = sc.Pos();
        sc.SkipSpaces();
        if (sc.MatchI(L"AM") || sc.MatchI(L"A.M.") || sc.MatchI(L"上午"))
        {
            if (h < 1 || h > 12)
                return false;
            if (h == 12)
                h = 0;
        }
        else if (sc.MatchI(L"PM") || sc.MatchI(L"P.M.") || sc.MatchI(L"下午"))
        {
            if (h < 1 || h > 12)
                return false;
            if (h != 12)
                h += 12;
        }
        else
        {
            sc.Reset(save);
        }

        return h < 24 && m < 60 && s < 60;
    }
}

int64_t NowUnixMs()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (static_cast<int64_t>(u.QuadPart) - kUnixEpochIn100ns) / 10000;
}

int64_t NowUnix()
{
    return NowUnixMs() / 1000;
}

bool ParseDateTime(const std::wstring& text, int64_t& outUtc)
{
    Scanner sc(text);
    sc.SkipSpaces();

    SYSTEMTIME local{};
    GetLocalTime(&local);
    local.wHour = local.wMinute = local.wSecond = local.wMilliseconds = 0;

    bool haveDate = false;
    bool haveTime = false;
    unsigned h = 0, mi = 0, se = 0;

    // Date: YYYY-MM-DD, YYYY/MM/DD or YYYY.MM.DD
    size_t start = sc.Pos();
    unsigned y = 0, mo = 0, d = 0;
    size_t digits = 0;
    if (sc.Number(y, 4, &digits) && digits == 4 && (sc.Peek() == L'-' || sc.Peek() == L'/' || sc.Peek() == L'.'))
    {
        wchar_t sep = sc.Peek();
        sc.Advance();
        if (!sc.Number(mo, 2) || sc.Peek() != sep)
        {
            return false;
        }

        sc.Advance();
        if (!sc.Number(d, 2))
        {
            return false;
        }

        if (y < 1601 || mo < 1 || mo > 12 || d < 1 || d > DaysInMonth(y, mo))
        {
            return false;
        }

        local.wYear = static_cast<WORD>(y);
        local.wMonth = static_cast<WORD>(mo);
        local.wDay = static_cast<WORD>(d);
        haveDate = true;

        if (sc.Peek() == L'T' || sc.Peek() == L't')
        {
            sc.Advance();
        }
        else
        {
            sc.SkipSpaces();
        }
    }
    else
    {
        sc.Reset(start);
    }

    if (!sc.AtEnd() && iswdigit(sc.Peek()))
    {
        if (!ParseTime(sc, h, mi, se))
        {
            return false;
        }

        haveTime = true;
    }

    if (!haveDate && !haveTime)
    {
        return false;
    }

    local.wHour = static_cast<WORD>(h);
    local.wMinute = static_cast<WORD>(mi);
    local.wSecond = static_cast<WORD>(se);

    // Optional offset: Z, +HH, +HH:MM, +HHMM
    sc.SkipSpaces();
    if (sc.Peek() == L'Z' || sc.Peek() == L'z')
    {
        sc.Advance();
        sc.SkipSpaces();
        if (!sc.AtEnd())
            return false;
        outUtc = FieldsToSeconds(local);
        return true;
    }

    if (sc.Peek() == L'+' || sc.Peek() == L'-')
    {
        int sign = sc.Peek() == L'-' ? -1 : 1;
        sc.Advance();
        unsigned oh = 0, om = 0;
        size_t n = 0;
        if (!sc.Number(oh, 2, &n) || n != 2)
        {
            return false;
        }

        if (sc.Peek() == L':')
        {
            sc.Advance();
        }

        if (iswdigit(sc.Peek()) && (!sc.Number(om, 2, &n) || n != 2))
        {
            return false;
        }

        sc.SkipSpaces();
        if (!sc.AtEnd() || oh > 14 || om > 59)
        {
            return false;
        }

        outUtc = FieldsToSeconds(local) - sign * static_cast<int64_t>(oh * 3600 + om * 60);
        return true;
    }

    if (!sc.AtEnd())
    {
        return false;
    }

    return LocalToUtc(local, outUtc);
}

std::wstring FormatIso8601Local(int64_t utc)
{
    SYSTEMTIME local;
    if (!UtcToLocal(utc, local))
    {
        return L"1970-01-01T00:00:00+00:00";
    }

    int64_t offset = FieldsToSeconds(local) - utc;
    wchar_t sign = offset < 0 ? L'-' : L'+';
    int64_t abs = offset < 0 ? -offset : offset;
    return Format(L"%04u-%02u-%02uT%02u:%02u:%02u%lc%02d:%02d",
                  local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond,
                  sign, static_cast<int>(abs / 3600), static_cast<int>((abs % 3600) / 60));
}

std::wstring FormatShortLocal(int64_t utc)
{
    SYSTEMTIME local;
    if (!UtcToLocal(utc, local))
    {
        return L"?";
    }

    wchar_t date[64] = L"";
    wchar_t time[64] = L"";
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, _countof(date), nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, _countof(time));
    return std::wstring(date) + L" " + time;
}

std::wstring FormatDuration(int64_t seconds)
{
    if (seconds < 0)
    {
        seconds = 0;
    }

    int64_t h = seconds / 3600;
    int m = static_cast<int>((seconds % 3600) / 60);
    int s = static_cast<int>(seconds % 60);
    if (h >= 1)
    {
        return Format(L"%d:%02d:%02d", static_cast<int>(h), m, s);
    }

    return Format(L"%d:%02d", m, s);
}
