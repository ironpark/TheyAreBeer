using System;
using System.Reflection;
using System.Threading;
using HarmonyLib;

namespace Billions
{
    // The game's Direct3D 9 renderer (DXPlatform_Desktop.DXRender_SlimDX) checks the device before every sprite:
    // IDXRenderer.DrawObject starts with D3DCheckDevice, a COM call to Device.TestCooperativeLevel. With 100,000 zombies on
    // screen that is a few hundred thousand calls per frame, about 4 ms. (D3DCheckDevice itself is inlined into DrawObject,
    // and DrawObject cannot be patched, so the hook is on SlimDX's Device.TestCooperativeLevel.)
    //
    // Within one scene (IDXRenderer.StartScene to FinishScene) on the render thread, once a check has succeeded the later
    // ones return that success without the call. StartScene and FinishScene clear it first, so their own checks
    // (D3DCheckAndRestoreDevice, the one before Present) are real, and so is every check on another thread or outside a
    // scene. A device lost in the middle of a scene: D3D 9 ignores the draw calls on a lost device, and FinishScene's real
    // check sees the loss as before. TABPERF_DEVICECHECK=0 turns this off.
    public static class DeviceCheck
    {
        public static bool On { get; private set; }
        static volatile bool _inScene; static int _tid; static object _ok; static PropertyInfo _isSuccess;

        public static void Install(Harmony h)
        {
            if (!Settings.Switch("TABPERF_DEVICECHECK", true)) { Log.Info("device check: TABPERF_DEVICECHECK 끔 — 원작 그대로"); return; }
            try
            {
                var dev = Renderer.DeviceType;
                var tcl = dev != null ? dev.GetMethod("TestCooperativeLevel", Type.EmptyTypes) : null;
                var ss = Renderer.Method("DXVision.IDXRenderer.StartScene");
                var fs = Renderer.Method("DXVision.IDXRenderer.FinishScene");
                _isSuccess = tcl != null ? tcl.ReturnType.GetProperty("IsSuccess") : null;
                if (tcl == null || ss == null || fs == null || _isSuccess == null) { Log.Info("HOOK MISSING (optional): D3D 장치 확인 — 원작 그대로"); return; }
                h.Patch(ss, new HarmonyMethod(typeof(DeviceCheck), nameof(SceneStart)));
                h.Patch(fs, new HarmonyMethod(typeof(DeviceCheck), nameof(SceneEnd)), new HarmonyMethod(typeof(DeviceCheck), nameof(SceneEnd)));
                h.Patch(tcl, new HarmonyMethod(typeof(DeviceCheck), nameof(CheckPre)), new HarmonyMethod(typeof(DeviceCheck), nameof(CheckPost)));
                On = true;
                Log.Info("hook: SlimDX Device.TestCooperativeLevel (prefix/postfix, 장면 안에서는 처음 성공한 확인을 다시 씀) · IDXRenderer.StartScene/FinishScene");
            }
            catch (Exception e) { Log.Error("device check install", e); }
        }

        // In a scene on the render thread with a successful check already made (what the renderer's own check would return).
        internal static bool SceneOk { get { return _inScene && _ok != null && Thread.CurrentThread.ManagedThreadId == _tid; } }
        // SceneOk for a caller known to be on the scene's thread (SceneDraw): no thread id lookup
        internal static bool SceneDeviceOk { get { return _inScene && _ok != null; } }

        static void SceneStart() { _ok = null; _tid = Thread.CurrentThread.ManagedThreadId; _inScene = true; }
        static void SceneEnd() { _inScene = false; _ok = null; }

        static bool CheckPre(ref object __result)
        {
            object ok = _ok;
            if (ok == null || !SceneOk) return true;
            __result = ok;
            return false;
        }

        static void CheckPost(object __result)
        {
            if (!_inScene || _ok != null || Thread.CurrentThread.ManagedThreadId != _tid) return;
            try { if ((bool)_isSuccess.GetValue(__result, null)) _ok = __result; } catch { }
        }
    }
}
