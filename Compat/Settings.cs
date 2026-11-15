// Standalone replacements for the parts of PowerToys Settings.UI.Library that Awake uses.
// Settings are stored in %LOCALAPPDATA%\AwakeStandalone\settings.json using the same
// JSON shape as PowerToys, so the tray menu and the config file stay in sync.
using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using ManagedCommon;

namespace Microsoft.PowerToys.Settings.UI.Library
{
    public enum AwakeMode
    {
        PASSIVE = 0,
        INDEFINITE = 1,
        TIMED = 2,
        EXPIRABLE = 3,
    }

    public class AwakeProperties
    {
        private readonly Dictionary<string, uint> _customTrayTimes = [];

        public AwakeProperties()
        {
            KeepDisplayOn = false;
            AllowDisplayOffWhileLocked = true;
            Mode = AwakeMode.PASSIVE;
            IntervalHours = 0;
            IntervalMinutes = 1;
            ExpirationDateTime = DateTimeOffset.Now;
        }

        [JsonPropertyName("keepDisplayOn")]
        public bool KeepDisplayOn { get; set; }

        [JsonPropertyName("allowDisplayOffWhileLocked")]
        public bool AllowDisplayOffWhileLocked { get; set; }

        [JsonPropertyName("mode")]
        public AwakeMode Mode { get; set; }

        [JsonPropertyName("intervalHours")]
        public uint IntervalHours { get; set; }

        [JsonPropertyName("intervalMinutes")]
        public uint IntervalMinutes { get; set; }

        [JsonPropertyName("expirationDateTime")]
        public DateTimeOffset ExpirationDateTime { get; set; }

        [JsonPropertyName("customTrayTimes")]
        public Dictionary<string, uint> CustomTrayTimes
        {
            get => _customTrayTimes;
            init => _customTrayTimes = value ?? [];
        }
    }

    public class AwakeSettings
    {
        public const string ModuleName = "Awake";

        public AwakeSettings()
        {
            Name = ModuleName;
            Version = typeof(AwakeSettings).Assembly.GetName().Version?.ToString() ?? "1.0.0";
            Properties = new AwakeProperties();
        }

        [JsonPropertyName("name")]
        public string Name { get; set; }

        [JsonPropertyName("version")]
        public string Version { get; set; }

        [JsonPropertyName("properties")]
        public AwakeProperties Properties { get; set; }
    }

    public class SettingsUtils
    {
        public const string DefaultFileName = "settings.json";

        public static SettingsUtils Default { get; } = new();

        public string GetSettingsFilePath(string powertoy = "", string fileName = DefaultFileName)
            => Path.Combine(AwakeStandalone.Paths.Root, fileName);

        public bool SettingsExists(string powertoy = "", string fileName = DefaultFileName)
            => File.Exists(GetSettingsFilePath(powertoy, fileName));

        public T GetSettings<T>(string powertoy = "", string fileName = DefaultFileName)
            where T : new()
        {
            string path = GetSettingsFilePath(powertoy, fileName);
            if (!File.Exists(path))
            {
                throw new FileNotFoundException(path);
            }

            // Trim('\0') mirrors upstream's workaround for NTFS zero-padding corruption.
            string json = ReadShared(path).Trim('\0');
            return JsonSerializer.Deserialize<T>(json) ?? new T();
        }

        public void SaveSettings(string jsonSettings, string powertoy = "", string fileName = DefaultFileName)
        {
            try
            {
                string path = GetSettingsFilePath(powertoy, fileName);
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllText(path, jsonSettings);
            }
            catch (Exception e)
            {
                Logger.LogError($"Exception encountered while saving {powertoy} settings.", e);
            }
        }

        private static string ReadShared(string path)
        {
            // The file watcher can fire while the file is still being written; retry briefly.
            for (int attempt = 0; ; attempt++)
            {
                try
                {
                    using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    using var reader = new StreamReader(fs);
                    return reader.ReadToEnd();
                }
                catch (IOException) when (attempt < 5)
                {
                    System.Threading.Thread.Sleep(50);
                }
            }
        }
    }
}
