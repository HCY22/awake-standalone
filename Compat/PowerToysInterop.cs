// Standalone replacements for the native PowerToys interop / GPO projections.
namespace PowerToys.Interop
{
    public static class Constants
    {
        // Named event used to signal Awake to exit (same name PowerToys uses,
        // so "only one Awake at a time" semantics are preserved).
        public static string AwakeExitEvent() => "Local\\PowerToysAwakeExitEvent-c0d5e305-35fc-4fb5-83ec-f6070cfaf7fe";
    }
}

namespace PowerToys.GPOWrapper
{
    public enum GpoRuleConfigured
    {
        WrongValue = -3,
        Unavailable = -2,
        NotConfigured = -1,
        Disabled = 0,
        Enabled = 1,
    }

    public static class GPOWrapper
    {
        // No PowerToys group policy applies to the standalone build.
        public static GpoRuleConfigured GetConfiguredAwakeEnabledValue() => GpoRuleConfigured.NotConfigured;
    }
}
