using DXVision;
using ZX;

namespace Billions
{
    // The mod's own on-screen text in the game's language: Korean when the game is set to Korean, else English.
    internal static class Lang
    {
        public static string T(string ko, string en)
        {
            try { var c = DXGame.Current.Configuration as ZXGameConfiguration; if (c != null && c.Language == DXLanguageType.KO) return ko; } catch { }
            return en;
        }
    }
}
