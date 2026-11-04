using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Billions
{
    // Optional DXVK (Direct3D 9 on Vulkan) from the mod folder: engine.json "dxvk": true (or TABPERF_DXVK=1) loads
    // <mod>\dxvk\d3d9.dll by its full path when the mod starts. The game's renderer (SlimDX) imports d3d9.dll by name;
    // Windows gives a by-name load the module of that name that is already in the process, so the game's device is then
    // created by DXVK instead of the system's d3d9.dll. That only works before SlimDX is loaded: when d3d9.dll is already
    // in the process, nothing changes (logged). scripts\install.ps1 -Dxvk on puts the DLL there.
    //
    // In the bench (Rapier, 60,000+ zombies) the frame was 8-13% shorter and the process CPU 0.2-0.7 cores lower. With few
    // zombies the game runs above its usual ~32 FPS and uses more CPU; "dxvkMaxFps" (TABPERF_DXVK_FPS) caps the frame rate
    // with DXVK's own limiter (DXVK_FRAME_RATE). DXVK writes its log to %APPDATA%\TABPerf (DXVK_LOG_PATH) unless set.
    internal static class Dxvk
    {
        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern uint GetModuleFileNameW(IntPtr module, StringBuilder name, uint size);
        const uint LOAD_WITH_ALTERED_SEARCH_PATH = 8;

        internal static bool Loaded { get; private set; }

        internal static void Load(string modPath)
        {
            var cfg = Settings.Config();
            if (!Settings.Bool(cfg, "dxvk", "TABPERF_DXVK", false)) return;
            try
            {
                string dll = modPath != null ? Path.Combine(Path.Combine(modPath, "dxvk"), "d3d9.dll") : null;
                if (dll == null || !File.Exists(dll)) { Log.Info("dxvk: 모드 폴더에 dxvk\\d3d9.dll 이 없다 — 시스템 Direct3D 9 를 쓴다 (scripts\\install.ps1 -Dxvk on)"); return; }
                IntPtr have = GetModuleHandleW("d3d9.dll");
                if (have != IntPtr.Zero) { Log.Info("dxvk: d3d9.dll 이 이미 올라와 있다(" + FileName(have) + ") — 이번 실행은 그대로"); return; }
                if (Settings.Env("DXVK_LOG_PATH") == null) Environment.SetEnvironmentVariable("DXVK_LOG_PATH", Log.Dir);
                int fps = Settings.Int(cfg, "dxvkMaxFps", "TABPERF_DXVK_FPS", 0);
                if (fps > 0 && Settings.Env("DXVK_FRAME_RATE") == null) Environment.SetEnvironmentVariable("DXVK_FRAME_RATE", fps.ToString());
                IntPtr m = LoadLibraryExW(dll, IntPtr.Zero, LOAD_WITH_ALTERED_SEARCH_PATH);
                if (m == IntPtr.Zero) { Log.Info("dxvk: " + dll + " 을 불러오지 못했다(오류 " + Marshal.GetLastWin32Error() + ") — 시스템 Direct3D 9 를 쓴다"); return; }
                Loaded = true;
                Log.Info("dxvk: " + FileName(m) + " — Direct3D 9 를 Vulkan 으로" + (fps > 0 ? " · 최대 " + fps + " FPS" : "") + " (로그 " + (Settings.Env("DXVK_LOG_PATH") ?? "") + ")");
            }
            catch (Exception e) { Log.Error("dxvk", e); }
        }

        static string FileName(IntPtr m)
        {
            var sb = new StringBuilder(520);
            return GetModuleFileNameW(m, sb, (uint)sb.Capacity) > 0 ? sb.ToString() : "?";
        }
    }
}
