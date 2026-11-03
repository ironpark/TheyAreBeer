using System;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using DXVision;
using HarmonyLib;

namespace Billions
{

    public static class SmoothTime
    {
        public static bool Accurate = true;
        public static int Installed;
        static AccessTools.FieldRef<DXGame, Task> _task;
        static AccessTools.FieldRef<DXGame, Thread> _thread;
        static AccessTools.FieldRef<DXGame, bool> _cancel;
        static AccessTools.FieldRef<DXGame, double> _lastPhysUpd, _totalPhys;
        static MethodInfo _finishPhysics, _setIdThread;
        [DllImport("winmm.dll")] static extern uint timeBeginPeriod(uint p);

        public static void Install(Harmony h)
        {
            try
            {
                _task = AccessTools.FieldRefAccess<DXGame, Task>("_TaskGameTime");
                _thread = AccessTools.FieldRefAccess<DXGame, Thread>("_ThreadPhysic");
                _cancel = AccessTools.FieldRefAccess<DXGame, bool>("_CancelPhysics");
                _lastPhysUpd = AccessTools.FieldRefAccess<DXGame, double>("_LastPhysicsUpdatedTime");
                _totalPhys = AccessTools.FieldRefAccess<DXGame, double>("_TotalTimeInPhysics");
                _finishPhysics = AccessTools.Method(typeof(DXPhysicWorld), "FinishPhysics");
                _setIdThread = AccessTools.PropertySetter(typeof(DXGame), "IDThreadPhysics");
                var lp = AccessTools.Method(typeof(DXGame), "LoadPhysics");
                if (lp == null || _finishPhysics == null || _setIdThread == null) { Log.Info("HOOK MISSING (optional): 매끈한 게임 시간 — 원작 타이머 그대로"); return; }
                h.Patch(lp, new HarmonyMethod(typeof(SmoothTime), nameof(LoadPhysicsPre)));
                Installed = 1;
                Log.Info("hook: DXGame.LoadPhysics (prefix, 매끈한 게임 시간 — 빠른 타이머일 때만)");
            }
            catch (Exception e) { Installed = 0; Log.Error("smooth time install", e); }
        }

        static bool LoadPhysicsPre(DXGame __instance)
        {
            var g = __instance;
            if (Installed != 1 || g == null || g.Configuration == null || !g.Configuration.HighPerformanceTiming) return true;
            try
            {
                if (_task(g) != null || _thread(g) != null) g.UnloadPhysics();
                _cancel(g) = false; _lastPhysUpd(g) = 0.0;
                _task(g) = Task.Factory.StartNew(() => TimeLoop(g), TaskCreationOptions.LongRunning);
                var t = new Thread(() => PhysicsLoop(g));
                _thread(g) = t; t.Start();
                Log.Info("smooth time: 게임 시간 작업 시작(" + (Accurate ? "실제 경과 시간" : "원작 4 ms") + ")");
                return false;
            }
            catch (Exception e) { Log.Error("smooth time load", e); return true; }
        }

        static void TimeLoop(DXGame g)
        {
            try { Thread.CurrentThread.Priority = ThreadPriority.AboveNormal; } catch { }
            try { timeBeginPeriod(1); } catch { }
            var sw = Stopwatch.StartNew();
            double last = sw.Elapsed.TotalMilliseconds, nextTick = DXHelper_Environment.Ticks + 4.0;
            float num3 = 1f;
            while (!_cancel(g) && !g.Disposing)
            {
                double dt;
                bool acc = Accurate;
                if (acc)
                {
                    Thread.Sleep(1);
                    double now = sw.Elapsed.TotalMilliseconds; dt = Math.Min(50.0, now - last); last = now;
                    nextTick = DXHelper_Environment.Ticks + 4.0;
                }
                else
                {
                    DXTimer.Wait(nextTick, 4.0, 1U); nextTick += 4.0; dt = 4.0; last = sw.Elapsed.TotalMilliseconds;
                }
                if (dt <= 0) continue;
                try
                {
                    if (g.Visible && DXScene.Current != null && DXScene.Current.RootObject is DXIsometricGridObject)
                    {
                        g.GameSpeed = DXMath.Limit(0.0, g.GameSpeed, 20.0);
                        float num4 = 1f;
                        if (g.PhysicsUsageFactor > 1f) num4 = 1f / Math.Min(10f, g.PhysicsUsageFactor);
                        if (g.IALogicUsageFactor > 1f) num4 = Math.Min(num4, 1f / Math.Min(10f, g.IALogicUsageFactor));
                        float f = acc ? (float)Math.Pow(1.02, dt / 4.0) : 1.02f;
                        if (num4 > num3) num3 = Math.Min(num4, num3 * f); else if (num4 < num3) num3 = Math.Max(num4, num3 / f);
                        double num5 = g.GameSpeed * num3;
                        if (Math.Max((int)((g.GameTime - DXPhysicWorld.LastGameTimeUpdatePhysics) / 83.333333333333329), (int)((g.GameTime - DXGame.LastGameTimeIAUpdate) / 33.333332061767578)) <= 1)
                            g.GameTime += num5 * dt;
                    }
                }
                catch (Exception e) { Log.Error("smooth time loop", e); Thread.Sleep(50); }
            }
        }

        static void PhysicsLoop(DXGame g)
        {
            try { _setIdThread.Invoke(null, new object[] { Thread.CurrentThread.ManagedThreadId }); } catch (Exception e) { Log.Error("smooth time phys id", e); }
            double nextTick = DXHelper_Environment.Ticks;
            while (!_cancel(g))
            {
                if (g.Disposing) return;
                double ticks2 = DXHelper_Environment.Ticks;
                nextTick = DXTimer.Wait(nextTick, 4.0, 1U);
                if (g.Visible && DXScene.Current != null && DXScene.Current.RootObject is DXIsometricGridObject)
                {
                    while (g.GameTime - DXPhysicWorld.LastGameTimeUpdatePhysics >= 83.333333333333329)
                    {
                        double ticks3 = DXHelper_Environment.Ticks;
                        DXPhysicWorld.LastGameTimeUpdatePhysics += 83.333333333333329;
                        _lastPhysUpd(g) = ticks3;
                        DXPhysicWorld.UpdateDefaultTime();
                        double ticks4 = DXHelper_Environment.Ticks;
                        g.PhysicsUsageFactor = (float)(ticks4 - ticks3) * 12f * ((g.GameSpeed >= 1.0) ? ((float)g.GameSpeed) : 1f) / 1000f;
                    }
                    if (DXScene.FrameRateLabelType != DXScene.TFrameRateLabel.None)
                        lock (DXGame.SyncUsage) { _totalPhys(g) += DXHelper_Environment.Ticks - ticks2; }
                }
            }
            try { _finishPhysics.Invoke(null, null); } catch (Exception e) { Log.Error("smooth time finish physics", e); }
        }
    }
}
