using System;
using DXVision;
using HarmonyLib;

namespace Billions
{

    public static class SoundIdle
    {
        public static bool Enabled = true;
        public static int Installed, Skipped;
        static int _last = -1; static long _at;

        public static void Install(Harmony h)
        {
            try
            {
                var m = AccessTools.Method(typeof(DXSoundSystem), "PauseAll", new[] { typeof(bool) });
                if (m == null) { Log.Info("HOOK MISSING (optional): DXSoundSystem.PauseAll — 원작 그대로"); return; }
                h.Patch(m, new HarmonyMethod(typeof(SoundIdle), nameof(Pre)));
                Installed = 1; Log.Info("hook: DXSoundSystem.PauseAll (prefix, 같은 값은 0.5초에 한 번)");
            }
            catch (Exception e) { Installed = 0; Log.Error("sound idle install", e); }
        }

        static bool Pre(bool pause)
        {
            if (!Enabled) { _last = -1; return true; }
            int v = pause ? 1 : 0; long now = Clock.Ms;
            if (v == _last && now - _at < 500) { Skipped++; return false; }
            _last = v; _at = now; return true;
        }
    }
}
