using System;
using System.Collections.Generic;
using System.IO;
using AX.ModLoader;
using DXVision;
using HarmonyLib;
using ZX;

namespace Billions
{

    public class PerfMod : IModEntry
    {
        public const string Version = "1.0.0";
        const string LoadedKey = "tabperf.optimization.loaded";
        static bool _installed;

        public override void OnLoad(Mod mod)
        {
            try { Log.RotateIfLarge(); } catch { }
            try
            {
                Log.Info("==== TAB 최적화 패치 " + Version + " OnLoad ====");
                Log.Info("cpu: 논리 프로세서 " + Environment.ProcessorCount + " · 64bit=" + Environment.Is64BitProcess + " · clr=" + Environment.Version + " · os=" + Environment.OSVersion);
                string mods = mod != null && mod.ModPath != null ? Path.GetDirectoryName(mod.ModPath.TrimEnd('\\', '/')) : null;
                if (mods != null && File.Exists(Path.Combine(Path.Combine(mods, "Billions"), "Billions.dll")))
                {
                    Log.Info("같은 최적화를 이미 하는 다른 모드(Mods\\Billions)가 있다 — 이 패치는 쉰다");
                    return;
                }

                var dom = AppDomain.CurrentDomain; object first = dom.GetData(LoadedKey);
                if (first != null) { Log.Info("이 패치가 이미 다른 폴더에서 실렸다(" + first + ") — 여기(" + (mod != null ? mod.ModPath : "?") + ")는 쉰다"); return; }
                dom.SetData(LoadedKey, mod != null && mod.ModPath != null ? mod.ModPath : "?");
                Dxvk.Load(mod != null ? mod.ModPath : null);   // first: before the game's renderer loads d3d9.dll
                Install(mod != null ? mod.ModPath : null);
            }
            catch (Exception e) { Log.Error("perf OnLoad", e); }
        }

        static readonly string[] Engines = { "chipmunk", "rapier" };

        // engine.json {"physicsEngine": "chipmunk" | "rapier" | "original"}, or TABPERF_ENGINE; default chipmunk (the multi-core
        // Chipmunk replacement).
        static string PhysicsEngine() { return Settings.Str(Settings.Config(), "physicsEngine", "TABPERF_ENGINE", "chipmunk").Trim().ToLowerInvariant(); }

        // Sends the game's physics calls to an engine in the mod folder (<mod>\chipmunk\ or <mod>\rapier\, each in its own
        // folder so the loader does not try it as a .NET assembly and Rapier finds its rapier2d_ffi.dll next to it):
        // the chosen one, else Chipmunk. Neither usable: the game folder's DLL.
        static void Redirect(Harmony h, string modPath)
        {
            if (modPath == null) return;
            string engine = PhysicsEngine();
            if (engine == "original") { Log.Info("물리 엔진 original — 게임 폴더의 물리 DLL 을 쓴다"); return; }
            if (Array.IndexOf(Engines, engine) < 0) { Log.Info("물리 엔진 \"" + engine + "\" 은 모름 — chipmunk 를 쓴다(chipmunk · rapier · original)"); engine = "chipmunk"; }
            var order = new List<string> { engine }; if (engine != "chipmunk") order.Add("chipmunk");
            foreach (var e in order)
            {
                string dir = Path.Combine(modPath, e), dll = Path.Combine(dir, "DXPhysicsNative.dll");
                if (!File.Exists(dll)) { Log.Info("모드 폴더에 " + e + "\\DXPhysicsNative.dll 이 없다"); continue; }
                if (e == "rapier" && !File.Exists(Path.Combine(dir, "rapier2d_ffi.dll"))) { Log.Info("모드 폴더에 rapier\\rapier2d_ffi.dll 이 없다"); continue; }
                if (PhysicsRedirect.Install(h, dll)) return;
            }
            Log.Info("쓸 수 있는 물리 엔진이 모드 폴더에 없다 — 게임 폴더의 물리 DLL 을 쓴다");
        }

        static void Install(string modPath)
        {
            if (_installed) return; _installed = true;
            var h = new Harmony("tabperf.optimization");
            // first: no physics call may run on the game's DLL once the replacement is in use
            Step("물리 DLL 연결", () => Redirect(h, modPath));
            Step("전력 연결 번호표", () => EnergyFix.Install(h));
            Step("매끈한 게임 시간", () => SmoothTime.Install(h));
            Step("논리/물리 병렬 동적 분할", () => ParallelFix.Install(h));
            Step("물리 보간 기준", () => PhysicsTime.Install(h));
            Step("좀비 적 찾기 건너뛰기", () => FarField.Install(h));
            Step("그리기 순서 상황형 정렬", () => ZSort.Install(h));
            Step("소리 장치 다시 켜기 줄이기", () => SoundIdle.Install(h));
            Step("좀비 체력바 옵션", () => { LifeMarkers.Install(h); GameOptions.Install(h); });
            Step("웨이브 미니맵 표시", () => SwarmFast.Install(h));
            Step("D3D 장치 확인", () => DeviceCheck.Install(h));
            Step("그리기 빠른 길", () => DrawFast.Install(h));
            Step("프레임 훅", () => PerfHooks.Install(h));
            Step("성능 시험 모드", () => Bench.Install(h));
            Step("로직 갱신 계측", () => LogicProf.Install(h));
            Step("로직 틱 내부 계측", () => TickProf.Install(h));
            Log.Info("설치 끝 — 물리 DLL: " + PhysicsDll());
        }

        static void Step(string name, Action a)
        {
            try { a(); } catch (Exception e) { Log.Error("perf install " + name, e); }
        }

        // Label of the physics DLL the game calls (DX_SetOption: 200 = backend (2 = Rapier), 201 = Rapier build (1 AVX2, 2 AVX-512),
        // 202 = release build).
        internal static string PhysicsDll()
        {
            try
            {
                if (PhysicsRedirect.SetOption == null) return "원작(대체본 아님 — 모드 폴더에 chipmunk\\ 또는 rapier\\ 엔진을 넣으면 대량 좀비 물리가 빨라진다)";
                if (PhysOpt(200, 0) != 2) return "다중 코어 대체본";
                string s = "Rapier 대체본";
                int simd = PhysOpt(201, 0);
                if (simd == 1) s += " (AVX2)"; else if (simd == 2) s += " (AVX-512)";
                if (PhysOpt(203, 0) != 1) s += " — 몸체 값을 하나씩 넘김(일괄 쓰기 함수 없음)";
                if (PhysOpt(202, 0) != 1) s += " — 경고: 디버그 빌드 Rapier 라이브러리라 매우 느리다(릴리스로 다시 빌드)";
                return s;
            }
            catch (Exception e) { return "확인 못 함: " + e.Message; }
        }
        // DX_SetOption of the replacement in use; -1 (unknown option) with the game's own DLL, which has none
        internal static int PhysOpt(int name, int value) { var f = PhysicsRedirect.SetOption; return f != null ? f(name, value) : -1; }
    }

    public static class PerfHooks
    {
        public static void Install(Harmony h)
        {
            var sf = AccessTools.Method(typeof(ZXSystem_GameLevel), "OnStartFrame");
            if (sf != null) h.Patch(sf, null, new HarmonyMethod(typeof(PerfHooks), nameof(OnStartFrame)));
            var ul = AccessTools.Method(typeof(ZXSystem_GameLevel), "UnLoadLevel");
            if (ul != null) h.Patch(ul, null, new HarmonyMethod(typeof(PerfHooks), nameof(OnLevelUnloaded)));
            Log.Info("hook: ZXSystem_GameLevel.OnStartFrame · UnLoadLevel (postfix)");
        }
        static void OnStartFrame()
        {
            SmoothTime.Frame();
            try { ZSort.Frame(); } catch (Exception e) { Log.Error("zsort frame", e); }
            try { if (ZXLevelState.get_Current() != null) FarField.Frame(); } catch (Exception e) { Log.Error("far frame", e); }
            if (Bench.Enabled) try { Bench.Frame(); } catch (Exception e) { Log.Error("bench frame", e); }
        }
        static void OnLevelUnloaded()
        {
            SmoothTime.OnLevelUnloaded();
            try { EnergyFix.OnLevelUnloaded(); } catch (Exception e) { Log.Error("energy unload", e); }
            try { FarField.OnLevelUnloaded(); } catch (Exception e) { Log.Error("far unload", e); }
            try { ZSort.OnLevelUnloaded(); } catch (Exception e) { Log.Error("zsort unload", e); }
        }
    }

    public static class PerfProbe
    {
        public static object Last;
        public static string Text(object last) { return ""; }
    }
}
