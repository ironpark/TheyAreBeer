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
            if (Installed != 1 || g == null || g.Configuration == null) return true;
            // With the game option "FastTimming" (HighPerformanceTiming) off, the game runs LoadPhysicsOld: the physics thread
            // itself adds 2 ms of game time per wait, and DXTimer.Wait drops the time a late wait missed, so the game clock
            // stands still for the whole physics update. The game then runs at 83.3 / (83.3 + update ms) of its speed: 0.5x
            // at 70,000 zombies (75 ms updates), 0.76x already at 20,000 (20 ms). This loop (the game's own fast-timing
            // loop, with real elapsed time) is used either way; engine.json "fastTiming": false or TABPERF_FASTTIMING=0
            // keeps the game's choice. The option itself is not changed.
            bool hpt = g.Configuration.HighPerformanceTiming;
            if (!hpt && !Settings.Bool(PhysicsTime.Config(), "fastTiming", "TABPERF_FASTTIMING", true)) return true;
            if (!hpt) Log.Info("smooth time: 게임 옵션 FastTimming 이 꺼져 있지만 빠른 타이머 루프를 쓴다(engine.json fastTiming)");
            Task started = null; bool threadStarted = false;
            try
            {
                if (_task(g) != null || _thread(g) != null) g.UnloadPhysics();
                _cancel(g) = false; _lastPhysUpd(g) = 0.0;
                started = Task.Factory.StartNew(() => TimeLoop(g), TaskCreationOptions.LongRunning);
                _task(g) = started;
                var t = new Thread(() => PhysicsLoop(g));
                _thread(g) = t; t.Start(); threadStarted = true;
                Log.Info("smooth time: 게임 시간 작업 시작(" + (Accurate ? "실제 경과 시간" : "원작 4 ms") + ")");
                return false;
            }
            catch (Exception e)
            {
                Log.Error("smooth time load", e);
                if (threadStarted) return false;
                if (started != null)
                {
                    try
                    {
                        _cancel(g) = true;
                        try { started.Wait(1000); } catch { }
                        _task(g) = null; _thread(g) = null; _cancel(g) = false;
                    }
                    catch (Exception e2) { Log.Error("smooth time load rollback", e2); }
                }
                return true;
            }
        }

        static int _periodSet;
        static void BeginTimerPeriodOnce()
        {
            if (Interlocked.Exchange(ref _periodSet, 1) != 0) return;
            try { timeBeginPeriod(1); } catch { }
        }

        static void TimeLoop(DXGame g)
        {
            try { Thread.CurrentThread.Priority = ThreadPriority.AboveNormal; } catch { }
            BeginTimerPeriodOnce();
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
                        int physLag = (int)((g.GameTime - DXPhysicWorld.LastGameTimeUpdatePhysics) / 83.333333333333329), iaLag = (int)((g.GameTime - DXGame.LastGameTimeIAUpdate) / 33.333332061767578);
                        LogicProf.Gate(dt, iaLag > 1, physLag > 1);
                        if (Math.Max(physLag, iaLag) <= 1)
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
