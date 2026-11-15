// Standalone Awake - shared paths.
using System;
using System.IO;

namespace AwakeStandalone
{
    internal static class Paths
    {
        /// <summary>%LOCALAPPDATA%\AwakeStandalone</summary>
        public static string Root { get; } = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "AwakeStandalone");

        public static string Logs => Path.Combine(Root, "Logs");
    }
}
