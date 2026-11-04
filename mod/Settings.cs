using System;
using System.Collections.Generic;
using System.Globalization;

namespace Billions
{
    // A setting from an environment variable (TABPERF_...) when set, else from engine.json, else the default.
    // Switches take 1/0, on/off, true/false, yes/no.
    internal static class Settings
    {
        internal static string Env(string name)
        {
            string v = Environment.GetEnvironmentVariable(name);
            return string.IsNullOrEmpty(v) || v.Trim().Length == 0 ? null : v.Trim();
        }

        internal static string Str(Dictionary<string, object> cfg, string key, string env, string def)
        {
            return Env(env) ?? MiniJson.Str(cfg, key, def);
        }

        internal static int Int(Dictionary<string, object> cfg, string key, string env, int def)
        {
            string v = Env(env); int n;
            if (v != null)
            {
                if (int.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out n)) return n;
                Log.Info("환경 변수 " + env + "=" + v + " 은 정수가 아니다 — 무시");
            }
            return MiniJson.Int(cfg, key, def);
        }

        internal static bool Bool(Dictionary<string, object> cfg, string key, string env, bool def)
        {
            string v = Env(env);
            if (v != null)
            {
                switch (v.ToLowerInvariant())
                {
                    case "1": case "on": case "true": case "yes": return true;
                    case "0": case "off": case "false": case "no": return false;
                }
                Log.Info("환경 변수 " + env + "=" + v + " 은 켜기/끄기 값이 아니다(1/0, on/off, true/false, yes/no) — 무시");
            }
            return MiniJson.Bool(cfg, key, def);
        }
    }
}
