using System;
using System.Collections.Generic;

namespace Billions
{
    // engine.json settings the Optimization tab changes while the game runs (GameOptions), applied at start from the file:
    //   "physicsThreads": 0 (auto) | N   physics threads in all (the stepping thread + N-1 workers); the physics DLL reads
    //                                    DX_PHYS_THREADS when it creates a world, so it applies from the next map load.
    //                                    Auto = physical cores - 1 workers, at most 16 (native/dxphysics/dxpool.c).
    //   "drawOrderSort": true            draw order sorting (ZSort)
    //   "energyFix": true                power grid connection labels (EnergyFix)
    //   "zombieSearchSkip": true         zombies far from any target skip their enemy search (FarField.AiSkip)
    // Environment variables (TABPERF_PHYS_THREADS, TABPERF_ZSORT, TABPERF_ENERGYFIX, TABPERF_ZOMBIESEARCH) take precedence.
    internal static class Features
    {
        public static readonly string[] SwitchKeys = { "drawOrderSort", "energyFix", "zombieSearchSkip" };
        static readonly string[] SwitchEnv = { "TABPERF_ZSORT", "TABPERF_ENERGYFIX", "TABPERF_ZOMBIESEARCH" };
        static string _envThreads;   // DX_PHYS_THREADS as the process started with it (restored for "auto")

        public static void ApplyAtStart()
        {
            _envThreads = Environment.GetEnvironmentVariable("DX_PHYS_THREADS");
            var cfg = Settings.Config();
            SetPhysicsThreads(Settings.Int(cfg, "physicsThreads", "TABPERF_PHYS_THREADS", 0));
            for (int i = 0; i < SwitchKeys.Length; i++) SetSwitch(SwitchKeys[i], Settings.Bool(cfg, SwitchKeys[i], SwitchEnv[i], true));
            Log.Info("설정: 물리 스레드 " + ThreadsText(PhysicsThreads) + " · 그리기 순서 정렬 " + ZSort.Enabled + " · 전력망 번호표 " + EnergyFix.Enabled + " · 좀비 적 찾기 건너뛰기 " + FarField.AiSkip);
        }

        public static int PhysicsThreads { get; private set; }
        public static string ThreadsText(int n) { return n > 0 ? n.ToString() : "자동"; }

        // n = physics threads in all (0 = auto); the next world the physics DLL creates uses it
        public static void SetPhysicsThreads(int n)
        {
            PhysicsThreads = Math.Max(0, Math.Min(64, n));
            Environment.SetEnvironmentVariable("DX_PHYS_THREADS", PhysicsThreads > 0 ? (PhysicsThreads - 1).ToString() : _envThreads);
        }

        public static bool Switch(string key)
        {
            switch (key)
            {
                case "drawOrderSort": return ZSort.Enabled;
                case "energyFix": return EnergyFix.Enabled;
                case "zombieSearchSkip": return FarField.AiSkip;
            }
            return true;
        }

        public static void SetSwitch(string key, bool on)
        {
            switch (key)
            {
                case "drawOrderSort": ZSort.Set(on); break;
                case "energyFix": EnergyFix.SetEnabled(on); break;
                case "zombieSearchSkip": FarField.AiSkip = on; break;
            }
        }
    }
}
