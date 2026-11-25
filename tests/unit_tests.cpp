// Unit tests for parsing and serialization. Built with -DAWAKE_BUILD_TESTS=ON.
#include "awake.h"
#include <cstring>
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while(0)
int wmain() {
  StringsInit();
  int64_t t = 0;
  CHECK(ParseDateTime(L"2026-12-31T18:00:00Z", t)); CHECK(t == 1798740000);
  CHECK(ParseDateTime(L"2026-12-31T18:00:00+08:00", t)); CHECK(t == 1798740000 - 8*3600);
  CHECK(ParseDateTime(L"2026-12-31T18:00:00.1234567-05:30", t)); CHECK(t == 1798740000 + 5*3600+1800);
  CHECK(ParseDateTime(L"2026/12/31 6:00 PM", t));
  int64_t t2=0; CHECK(ParseDateTime(L"2026-12-31 18:00", t2)); CHECK(t == t2);
  CHECK(ParseDateTime(L"18:00", t)); CHECK(ParseDateTime(L"2026-12-31", t));
  CHECK(!ParseDateTime(L"2026-02-30 10:00", t)); CHECK(!ParseDateTime(L"abc", t)); CHECK(!ParseDateTime(L"25:00", t));
  CHECK(!ParseDateTime(L"", t)); CHECK(!ParseDateTime(L"2026-12-31 18:00 xyz", t));
  // ISO round trip
  int64_t now = NowUnix(); int64_t back = 0;
  std::wstring iso = FormatIso8601Local(now); CHECK(ParseDateTime(iso, back)); CHECK(back == now);
  wprintf(L"iso=%ls short=%ls dur=%ls %ls\n", iso.c_str(), FormatShortLocal(now).c_str(), FormatDuration(3725).c_str(), FormatDuration(65).c_str());
  CHECK(FormatDuration(3725) == L"1:02:05"); CHECK(FormatDuration(65) == L"1:05");
  // JSON
  const char* pt = "\xEF\xBB\xBF{\"properties\":{\"keepDisplayOn\":true,\"allowDisplayOffWhileLocked\":false,\"mode\":2,\"intervalHours\":1,\"intervalMinutes\":30,\"expirationDateTime\":\"2026-12-31T18:00:00.000+08:00\",\"customTrayTimes\":{\"\\u534a\\u5c0f\\u6642\":1800,\"3 h\":10800}},\"name\":\"Awake\",\"version\":\"0.90.0\"}\0\0\0";
  std::string raw(pt, strlen(pt) + 3);
  AwakeSettings s; std::wstring err;
  CHECK(ParseSettings(raw, s, err));
  CHECK(s.keepDisplayOn && !s.allowDisplayOffWhileLocked && s.mode == AwakeMode::Timed && s.intervalHours == 1 && s.intervalMinutes == 30);
  CHECK(s.expiration == 1798740000 - 8*3600);
  CHECK(s.customTrayTimes.size() == 2 && s.customTrayTimes[0].first == L"半小時" && s.customTrayTimes[1].second == 10800);
  std::string out = SerializeSettings(s);
  AwakeSettings s2; CHECK(ParseSettings(out, s2, err));
  CHECK(SerializeSettings(s2) == out);
  CHECK(!ParseSettings("{\"properties\":", s2, err)); wprintf(L"err=%ls\n", err.c_str());
  AwakeSettings def; def.expiration = now; std::string d = SerializeSettings(def); AwakeSettings s3; CHECK(ParseSettings(d, s3, err)); CHECK(s3.mode==AwakeMode::Passive && s3.allowDisplayOffWhileLocked);
  CHECK(ParseSettings("{\"properties\":{\"mode\":\"INDEFINITE\"}}", s3, err)); CHECK(s3.mode==AwakeMode::Indefinite);
  wprintf(L"%ls\n", Format(S(Str::TrayRemainingFmt), L"1:00").c_str());
  printf(fails ? "FAILED %d\n" : "ALL PASSED\n", fails);
  return fails;
}
