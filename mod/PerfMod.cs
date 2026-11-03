using System;
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
                Preload(mod);
                Install();
            }
            catch (Exception e) { Log.Error("perf OnLoad", e); }
        }

        [System.Runtime.InteropServices.DllImport("kernel32", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)] static extern IntPtr LoadLibraryW(string path);
        [System.Runtime.InteropServices.DllImport("kernel32", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
        static void Preload(Mod mod)
        {
            try
            {
                if (mod == null || mod.ModPath == null) return;
                string p = Path.Combine(mod.ModPath, "DXPhysicsNative.dll");
                if (!File.Exists(p)) return;
                if (GetModuleHandleW("DXPhysicsNative.dll") != IntPtr.Zero) { Log.Info("물리 DLL 이 이미 올라와 있다 — 모드 폴더의 대체본은 쓰지 않음(게임 폴더 쪽이 쓰인다)"); return; }
                var h = LoadLibraryW(p);
                Log.Info(h != IntPtr.Zero ? "물리 DLL 대체본을 모드 폴더에서 먼저 올림: " + p : "물리 DLL 대체본 올리기 실패(오류 " + System.Runtime.InteropServices.Marshal.GetLastWin32Error() + ") — 원작 물리 그대로");
            }
            catch (Exception e) { Log.Error("physics preload", e); }
        }

        static void Install()
        {
            if (_installed) return; _installed = true;
            var h = new Harmony("tabperf.optimization");
            Step("전력 연결 번호표", () => EnergyFix.Install(h));
            Step("매끈한 게임 시간", () => SmoothTime.Install(h));
            Step("논리/물리 병렬 동적 분할", () => ParallelFix.Install(h));
            Step("물리 보간 기준", () => PhysicsTime.Install(h));
            Step("좀비 적 찾기 건너뛰기", () => FarField.Install(h));
            Step("그리기 순서 상황형 정렬", () => ZSort.Install(h));
            Step("소리 장치 다시 켜기 줄이기", () => SoundIdle.Install(h));
            Step("좀비 체력바 옵션", () => { LifeMarkers.Install(h); GameOptions.Install(h); });
            Step("프레임 훅", () => PerfHooks.Install(h));
            Log.Info("설치 끝 — 물리 DLL: " + PhysicsDll());
        }

        static void Step(string name, Action a)
        {
            try { a(); } catch (Exception e) { Log.Error("perf install " + name, e); }
        }

        static string PhysicsDll()
        {
            try { return PhysOpt(100, 0) >= 0 ? "다중 코어 대체본" : "원작"; }
            catch (EntryPointNotFoundException) { return "원작(대체본 아님 — 패키지의 설치.cmd 로 넣으면 대량 좀비 물리가 빨라진다)"; }
            catch (Exception e) { return "확인 못 함: " + e.Message; }
        }
        [System.Runtime.InteropServices.DllImport("DXPhysicsNative.dll", EntryPoint = "DX_SetOption")]
        static extern int PhysOpt(int name, int value);
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
            try { ZSort.Frame(); } catch (Exception e) { Log.Error("zsort frame", e); }
            try { if (ZXLevelState.get_Current() != null) FarField.Frame(); } catch (Exception e) { Log.Error("far frame", e); }
        }
        static void OnLevelUnloaded()
        {
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
