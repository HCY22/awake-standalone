# Awake（獨立版，C++）

[Microsoft PowerToys](https://github.com/microsoft/PowerToys) **Awake** 的原生 Win32 C++ 移植版：讓電腦保持喚醒（可選擇是否保持螢幕開啟），不需安裝 PowerToys，也不需要 .NET。

- 單一 `Awake.exe`，約 300 KB，只依賴 Windows 內建 DLL
- 功能與命令列參數與 PowerToys 版相同
- 介面支援英文、繁體中文、簡體中文（依系統語言自動切換）
- 無遙測

> 舊的 C# 版本（直接沿用 PowerToys 原始碼，約 4–80 MB）保留在 [`csharp`](https://github.com/HCY22/awake-standalone/tree/csharp) 分支。

行為依據 PowerToys `src/modules/awake/Awake`（commit `e0c1a847e4363daf9c4fa08adec3762d79290c53`，MIT 授權）重新以 C++ 實作。

## 安裝

到 [Actions](https://github.com/HCY22/awake-standalone/actions) 或 [Releases](https://github.com/HCY22/awake-standalone/releases) 下載 `Awake-win-x64.zip`（ARM 電腦用 `Awake-win-arm64.zip`），解壓後執行：

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Startup
```

- 安裝到 `%LOCALAPPDATA%\Programs\Awake`，建立開始功能表捷徑；`-Startup` 會在登入時自動啟動
- 不需要系統管理員權限
- 移除：`powershell -ExecutionPolicy Bypass -File .\uninstall.ps1`（加 `-KeepSettings` 保留設定）

也可以不安裝，直接執行 `Awake.exe -c`。

## 使用

捷徑以 `Awake.exe -c`（系統匣模式）啟動。點系統匣圖示開啟選單：

- 保持喚醒一段時間 ▸ 30 分鐘 / 1 小時 / 2 小時
- 保持喚醒到指定日期時間（只能用 `settings.json` 或 `-e` 設定）
- 無限期保持喚醒
- 關閉（使用目前的電源計劃）
- 保持螢幕開啟 / 鎖定時允許關閉螢幕
- 結束

選擇會存到 `%LOCALAPPDATA%\AwakeStandalone\settings.json`，下次啟動沿用。直接編輯這個檔案也會立即生效。例如自訂計時選項（值為秒）：

```json
"customTrayTimes": { "45 分鐘": 2700, "3 小時": 10800 }
```

記錄檔在 `%LOCALAPPDATA%\AwakeStandalone\Logs`（保留 7 天）。

## 命令列

```
Awake.exe [options]
  -c, --use-pt-config      系統匣模式，使用 settings.json（忽略其他選項）
  -d, --display-on         同時保持螢幕開啟
  -t, --time-limit <秒>    保持喚醒指定秒數後結束
  -e, --expire-at <時間>   保持喚醒到指定日期時間後結束
  -p, --pid <PID>          保持喚醒直到指定行程結束
  -u, --use-parent-pid     保持喚醒直到父行程結束
  -?, -h, --help           說明
  --version                版本
```

```powershell
Awake.exe                         # 無限期（開一個主控台視窗，關閉即結束）
Awake.exe -d -t 3600              # 保持喚醒與螢幕 1 小時
Awake.exe -e "2026-12-31 18:00"   # 到指定時間；也接受 "18:00"、"2026/12/31 6:00 PM"、ISO 8601
Awake.exe -p 1234                 # 直到 PID 1234 結束
```

## 建置

需要 CMake 3.20+ 與 Visual Studio 2022（含「使用 C++ 的桌面開發」）：

```powershell
cmake -S . -B build -A x64          # ARM64 改成 -A ARM64
cmake --build build --config MinSizeRel
```

輸出：`build\MinSizeRel\Awake.exe`。CRT 靜態連結，不需安裝 VC++ 可轉散發套件。

從 Linux 交叉編譯（MinGW-w64）：

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake
cmake --build build
```

測試：加 `-DAWAKE_BUILD_TESTS=ON` 會建置 `awake_tests.exe`（解析與序列化的單元測試）；`tests/smoke.ps1` 在 Windows 上實際執行 Awake.exe 做端對端檢查（需系統管理員權限以使用 `powercfg /requests`）。CI 每次推送都會在 Windows 上跑這兩項，並產生 x64 / ARM64 的 zip；推送 `v*` 標籤時附加到 Release。

## 與 PowerToys 版的差異

- 設定與記錄位於 `%LOCALAPPDATA%\AwakeStandalone`，設定檔格式相同
- `-c` 模式不開主控台視窗；命令列模式的系統匣選單提供「結束」
- 沒有遙測、沒有群組原則（GPO）支援
- 與 PowerToys 版使用同一個單一執行個體名稱，**兩者不能同時執行**；若已安裝 PowerToys，請先在 PowerToys 中停用 Awake

## 程式結構

| 檔案 | 內容 |
|---|---|
| `src/main.cpp` | 系統匣、選單、模式切換、計時、命令列、行程綁定 |
| `src/settings.cpp` | settings.json 讀寫（內建小型 JSON 解析器）、日期時間解析 |
| `src/strings.cpp` | 介面文字（en / zh-Hant / zh-Hans） |
| `src/log.cpp` | 記錄檔 |
| `assets/` | 圖示（取自 PowerToys，已移除未使用的尺寸並以 PNG 壓縮） |
