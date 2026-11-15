// Standalone replacement for PowerToys ManagedCommon.LanguageHelper.
// Without PowerToys there is no language.json; use the OS UI language.
namespace ManagedCommon
{
    public static class LanguageHelper
    {
        public static string LoadLanguage() => string.Empty;
    }
}
