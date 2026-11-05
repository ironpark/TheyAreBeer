using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Billions
{
    // Which Direct3D 9 the game's renderer (SlimDX, DXPlatform_Desktop.DXRender_SlimDX) runs on: engine.json "renderer"
    // (TABPERF_RENDERER), read when the mod starts.
    //   "dxvk" (default)  DXVK, Direct3D 9 on Vulkan, from <mod>\dxvk\d3d9.dll (scripts\install.ps1 puts it there). The game
    //                     imports d3d9.dll by name and Windows hands a by-name load the module of that name already in the
    //                     process, so loading DXVK by its full path before SlimDX loads makes the game's device a DXVK one.
    //                     "dxvkMaxFps" (TABPERF_DXVK_FPS) caps the frame rate with DXVK's limiter (DXVK_FRAME_RATE); DXVK
    //                     logs to %APPDATA%\TABPerf (DXVK_LOG_PATH) unless set.
    //   "d3d9"            the system Direct3D 9, as the game does.
    // A choice that cannot be used (file missing, d3d9.dll already loaded) falls back to the system Direct3D 9 and is logged. (The former "dxvk": true / false key is no longer read; install.ps1 removes it.)
    internal static class D3d9
    {
        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern uint GetModuleFileNameW(IntPtr module, StringBuilder name, uint size);
        const uint LOAD_WITH_ALTERED_SEARCH_PATH = 8;

        // what the renderer runs on, for the log and the bench: "DXVK" or "" (system Direct3D 9)
        internal static string Label { get; private set; } = "";

        internal static void Load(string modPath)
        {
            string mode;
            try
            {
                var cfg = Settings.Config();
                mode = Settings.Str(cfg, "renderer", "TABPERF_RENDERER", "dxvk").Trim().ToLowerInvariant();
                if (mode == "dxvk") LoadDxvk(cfg, modPath);
                else if (mode != "d3d9") Log.Info("renderer \"" + mode + "\" 은 모름 — 시스템 Direct3D 9 를 쓴다(dxvk · d3d9)");
            }
            catch (Exception e) { Log.Error("renderer", e); }
        }

        static void LoadDxvk(Dictionary<string, object> cfg, string modPath)
        {
            string dll = modPath != null ? Path.Combine(Path.Combine(modPath, "dxvk"), "d3d9.dll") : null;
            if (dll == null || !File.Exists(dll)) { Log.Info("dxvk: 모드 폴더에 dxvk\\d3d9.dll 이 없다 — 시스템 Direct3D 9 를 쓴다 (scripts\\install.ps1)"); return; }
            IntPtr have = GetModuleHandleW("d3d9.dll");
            if (have != IntPtr.Zero) { Log.Info("dxvk: d3d9.dll 이 이미 올라와 있다(" + FileName(have) + ") — 이번 실행은 그대로"); return; }
            if (Settings.Env("DXVK_LOG_PATH") == null) Environment.SetEnvironmentVariable("DXVK_LOG_PATH", Log.Dir);
            int fps = Settings.Int(cfg, "dxvkMaxFps", "TABPERF_DXVK_FPS", 0);
            if (fps > 0 && Settings.Env("DXVK_FRAME_RATE") == null) Environment.SetEnvironmentVariable("DXVK_FRAME_RATE", fps.ToString());
            IntPtr m = LoadLibraryExW(dll, IntPtr.Zero, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (m == IntPtr.Zero) { Log.Info("dxvk: " + dll + " 을 불러오지 못했다(오류 " + Marshal.GetLastWin32Error() + ") — 시스템 Direct3D 9 를 쓴다"); return; }
            Label = "DXVK";
            Log.Info("dxvk: " + FileName(m) + " — Direct3D 9 를 Vulkan 으로" + (fps > 0 ? " · 최대 " + fps + " FPS" : "") + " (로그 " + (Settings.Env("DXVK_LOG_PATH") ?? "") + ")");
        }

        static string FileName(IntPtr m)
        {
            var sb = new StringBuilder(520);
            return GetModuleFileNameW(m, sb, (uint)sb.Capacity) > 0 ? sb.ToString() : "?";
        }
    }
}
