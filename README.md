# Awake（獨立版）

從 [Microsoft PowerToys](https://github.com/microsoft/PowerToys) 抽出的 **Awake** 工具，不需安裝整套 PowerToys 即可單獨使用。
Awake 透過 Win32 `SetThreadExecutionState()` 讓電腦保持喚醒（可選擇是否保持螢幕開啟）。

原始碼來源：`microsoft/PowerToys` commit `e0c1a847e4363daf9c4fa08adec3762d79290c53`（`src/modules/awake/Awake`），MIT 授權。

## 與 PowerToys 版的差異

| 項目 | PowerToys 版 | 獨立版 |
|---|---|---|
| 相依 | ManagedCommon、Settings.UI.Library、GPOWrapper（C++）、Interop（C++）、遙測 | 只用 NuGet 套件：System.CommandLine、System.Reactive、System.Drawing.Common |
| 設定檔 | `%LOCALAPPDATA%\Microsoft\PowerToys\Awake\settings.json` | `%LOCALAPPDATA%\AwakeStandalone\settings.json`（格式相同） |
| 記錄檔 | PowerToys 記錄目錄 | `%LOCALAPPDATA%\AwakeStandalone\Logs`（保留 7 天） |
| 遙測 | 有 | **已移除**（no-op） |
| 群組原則（GPO） | 支援 | 不適用 |
| `-c` 模式 | 由 PowerToys 啟動 | 系統匣模式，**不跳出主控台視窗** |

`src/` 內是上游原始碼，只改了 `src/Program.cs` 一處（標記為 `[standalone]`：`-c` 模式不開主控台）。
所有 PowerToys 專屬相依都以 `Compat/` 中的小型替代實作取代，命名空間與上游相同，因此上游程式碼無需修改即可編譯。

## 建置

需求：[.NET 10 SDK](https://dotnet.microsoft.com/download/dotnet/10.0)（Windows、macOS、Linux 皆可建置，產物只能在 Windows 10/11 執行）。

```powershell
# 小體積（約 4 MB），執行的電腦需安裝 .NET 10 Runtime
.\scripts\build.ps1

# 自帶 runtime（約 80 MB），不需任何前置安裝
.\scripts\build.ps1 -SelfContained

# ARM64
.\scripts\build.ps1 -Runtime win-arm64 -SelfContained
```

輸出位於 `dist\<rid>\`。也可直接用：

```
dotnet publish -c Release -r win-x64 --self-contained true -o dist/win-x64
```

推上 GitHub 後，`.github/workflows/build.yml` 會自動產生 x64/ARM64 的 zip；推送 `v*` 標籤會附加到 Release。

## 安裝（僅目前使用者，不需系統管理員）

```powershell
.\scripts\install.ps1            # 安裝到 %LOCALAPPDATA%\Programs\Awake，建立開始功能表捷徑
.\scripts\install.ps1 -Startup   # 另外設定登入時自動啟動
.\scripts\uninstall.ps1          # 移除（加 -KeepSettings 保留設定）
```

若 PowerShell 擋下腳本：`powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1`

捷徑會以 `Awake.exe -c` 啟動：在系統匣常駐，右鍵選單可切換「關閉 / 無限期 / 計時 / 到指定時間」、保持螢幕開啟、結束。選擇會存到 `settings.json`，下次啟動沿用。

## 命令列

```
Awake.exe [options]
  -c, --use-pt-config     系統匣模式，使用設定檔（忽略其他參數）
  -d, --display-on        保持螢幕開啟
  -t, --time-limit <秒>   保持喚醒指定秒數後結束
  -e, --expire-at <時間>  保持喚醒到指定日期時間後結束
  -p, --pid <PID>         保持喚醒直到指定行程結束
  -u, --use-parent-pid    保持喚醒直到父行程結束
```

範例：

```powershell
Awake.exe                         # 無限期保持喚醒（會開主控台視窗，關閉即結束）
Awake.exe -d -t 3600              # 保持喚醒與螢幕 1 小時
Awake.exe -e "2026-12-31 18:00"   # 到指定時間
Awake.exe -p 1234                 # 直到 PID 1234 結束
```

自訂系統匣計時選項：編輯 `settings.json` 的 `properties.customTrayTimes`，例如
`{"30 分鐘": 1800, "2 小時": 7200}`（值為秒），存檔後立即生效。

## 注意

- 與 PowerToys 版共用相同的單一執行個體名稱，**兩者不能同時執行**；若已裝 PowerToys，請先在 PowerToys 中停用 Awake。
- 不支援單一檔案（single-file）發佈：上游程式碼使用 `Assembly.Location` 與 `Marshal.GetHINSTANCE`，在單一檔案模式下會失效。

## 同步上游

```bash
git clone --depth 1 --filter=blob:none --sparse https://github.com/microsoft/PowerToys.git pt
git -C pt sparse-checkout set src/modules/awake/Awake
```

將 `pt/src/modules/awake/Awake` 內容覆蓋到 `src/`（刪除其中的 `Awake.csproj`），重新套用 `src/Program.cs` 中的 `[standalone]` 修改，更新 `UPSTREAM_COMMIT`，然後建置。若上游新增了 PowerToys 相依，於 `Compat/` 補上對應替代實作即可。
