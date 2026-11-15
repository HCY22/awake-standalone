// Standalone replacement for PowerToys ManagedCommon.Logger.
// Writes a simple daily log file to %LOCALAPPDATA%\AwakeStandalone\Logs.
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;

namespace ManagedCommon
{
    public static class Logger
    {
        private static readonly object _lock = new();
        private static string? _logFile;

        public static void InitializeLogger(string applicationLogPath, bool isLocalLow = false)
        {
            try
            {
                Directory.CreateDirectory(AwakeStandalone.Paths.Logs);
                _logFile = Path.Combine(AwakeStandalone.Paths.Logs, $"Awake_{DateTime.Now:yyyy-MM-dd}.log");

                // Keep only the last 7 log files.
                var old = new DirectoryInfo(AwakeStandalone.Paths.Logs).GetFiles("Awake_*.log");
                Array.Sort(old, (a, b) => b.LastWriteTimeUtc.CompareTo(a.LastWriteTimeUtc));
                for (int i = 7; i < old.Length; i++)
                {
                    try { old[i].Delete(); } catch { }
                }
            }
            catch
            {
                _logFile = null;
            }
        }

        public static void LogError(string? message, [CallerMemberName] string memberName = "", [CallerFilePath] string sourceFilePath = "", [CallerLineNumber] int sourceLineNumber = 0)
            => Write("ERROR", message, memberName);

        public static void LogError(string? message, Exception ex, [CallerMemberName] string memberName = "", [CallerFilePath] string sourceFilePath = "", [CallerLineNumber] int sourceLineNumber = 0)
            => Write("ERROR", $"{message}{Environment.NewLine}{ex}", memberName);

        public static void LogWarning(string? message, [CallerMemberName] string memberName = "", [CallerFilePath] string sourceFilePath = "", [CallerLineNumber] int sourceLineNumber = 0)
            => Write("WARN", message, memberName);

        public static void LogInfo(string? message, [CallerMemberName] string memberName = "", [CallerFilePath] string sourceFilePath = "", [CallerLineNumber] int sourceLineNumber = 0)
            => Write("INFO", message, memberName);

        public static void LogDebug(string? message, [CallerMemberName] string memberName = "", [CallerFilePath] string sourceFilePath = "", [CallerLineNumber] int sourceLineNumber = 0)
        {
#if DEBUG
            Write("DEBUG", message, memberName);
#endif
        }

        private static void Write(string level, string? message, string memberName)
        {
            string line = $"[{DateTime.Now:HH:mm:ss.fff}] [{level}] [{memberName}] {message}";
            Trace.WriteLine(line);

            if (_logFile is null)
            {
                return;
            }

            lock (_lock)
            {
                try
                {
                    File.AppendAllText(_logFile, line + Environment.NewLine);
                }
                catch
                {
                    // Logging must never crash the app.
                }
            }
        }
    }
}
