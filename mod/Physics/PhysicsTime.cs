using System;
using System.IO;
using System.Reflection;
using DXVision;
using HarmonyLib;

namespace Billions
{

    public static class PhysicsTime
    {
        public static bool Nominal = true;
        public static int Installed, Iterations = 10;
        static Action<DXEntity> _startUpdatePhysics;
        static FieldInfo _pmField; static Type _execType; static MethodInfo _execAll;

        static object PostMethods(out MethodInfo exec)
        {
            var o = _pmField != null ? _pmField.GetValue(null) : null;
            if (o == null) { exec = null; return null; }
            var t = o.GetType();
            if (t != _execType) { _execAll = AccessTools.Method(t, "ExecuteAll"); _execType = t; }
            exec = _execAll; return o;
        }

        [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern IntPtr GetCurrentThread();
        [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern bool GetThreadTimes(IntPtr h, out long c, out long e, out long k, out long u);
        public static long StepWallTicks, StepCpu100ns, Steps, SepCalls, PreTicks, PostTicks, PostLoopTicks, PostMethodTicks, SyncWaitTicks, UpdateTicks; public static double PostMs;   // PostTicks: wait for SyncLogic, SyncWaitTicks: wait for DXPhysicWorld.Sync
        static long Cpu() { long c, e, k, u; return GetThreadTimes(GetCurrentThread(), out c, out e, out k, out u) ? k + u : 0; }
        static void SepPre() { System.Threading.Interlocked.Increment(ref SepCalls); }

        public static void Install(Harmony h)
        {
            try
            {
                var init = AccessTools.Method(typeof(DXPhysicWorld), "Init", new[] { typeof(int) });
                if (init != null) h.Patch(init, new HarmonyMethod(typeof(PhysicsTime), nameof(InitPre)));
            }
            catch (Exception e) { Log.Error("physics init hook", e); }
            try
            {
                var sup = AccessTools.Method(typeof(DXEntity), "StartUpdatePhysics");
                var pm = AccessTools.Field(typeof(DXPhysicWorld), "PostMethods");
                var ud = AccessTools.Method(typeof(DXPhysicWorld), "UpdateDefaultTime");
                if (sup == null || pm == null || ud == null) { Log.Info("HOOK MISSING (optional): 물리 보간 기준 — 원작 그대로"); return; }
                _startUpdatePhysics = AccessTools.MethodDelegate<Action<DXEntity>>(sup);
                _pmField = pm;
                MethodInfo exec; var pmo = PostMethods(out exec);
                if (pmo != null && exec == null) { Log.Info("HOOK MISSING (optional): 물리 보간 기준 — PostMethods.ExecuteAll 없음, 원작 그대로"); return; }
                h.Patch(ud, new HarmonyMethod(typeof(PhysicsTime), nameof(UpdatePre)));
                var sep = AccessTools.Method(typeof(DXPhysicWorld), "OnBodySeparation"); if (sep != null) h.Patch(sep, new HarmonyMethod(typeof(PhysicsTime), nameof(SepPre)));
                Installed = 1;
                Log.Info("hook: DXPhysicWorld.UpdateDefaultTime (prefix, 보간 기준 = 단계 시각)");
            }
            catch (Exception e) { Installed = 0; Log.Error("physics time install", e); }
        }

        static void InitPre(ref int __0)
        {
            try
            {
                var m = Settings.Config();
                int it = MiniJson.Int(m, "physicsIterations", 0);
                if (it >= 1 && it <= 30) __0 = it;
                Iterations = __0;
                Log.Info("physics: Init iterations " + __0 + (m != null ? " (engine.json)" : " (원작)"));
            }
            catch (Exception e) { Log.Error("physics init", e); }
        }

        static bool UpdatePre()
        {
            if (Installed != 1 || !Nominal) return true;
            try
            {
                if (!DXPhysicWorld.Initialized || !DXPhysicWorld.Enabled) return false;
                long u0 = System.Diagnostics.Stopwatch.GetTimestamp();
                MethodInfo exec; var postMethods = PostMethods(out exec);
                if (postMethods == null || exec == null) return true;
                var bodies = DXPhysicWorld.Bodies;
                long s0 = System.Diagnostics.Stopwatch.GetTimestamp();
                lock (DXPhysicWorld.Sync)
                {
                    var game = DXGame.Current; SyncWaitTicks += System.Diagnostics.Stopwatch.GetTimestamp() - s0;
                    game.OnStartPhysics();
                    try
                    {
                        long pm0 = System.Diagnostics.Stopwatch.GetTimestamp();
                        if (!PostProf.On || !PostProf.Run(postMethods)) exec.Invoke(postMethods, null);
                        long pm1 = System.Diagnostics.Stopwatch.GetTimestamp(); PostMethodTicks += pm1 - pm0;
                        PostMs = (pm1 - pm0) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
                        long t0 = System.Diagnostics.Stopwatch.GetTimestamp();
                        DXHelper_Task.For(0, bodies.Count, delegate (int a, int b)
                        {
                            for (int i = a; i < b; i++) { var e = bodies[i].GameEntity; if (e != null) _startUpdatePhysics(e); }
                        }, 200, 0);
                        FarField.BeforeStep();
                        long t1 = System.Diagnostics.Stopwatch.GetTimestamp(), c0 = Cpu();
                        DXPhysicWorld.Step(0.083333333333333329);
                        long t2 = System.Diagnostics.Stopwatch.GetTimestamp(), c1 = Cpu();
                        PreTicks += t1 - t0; StepWallTicks += t2 - t1; StepCpu100ns += c1 - c0; Steps++;
                    }
                    finally { game.OnFinishPhysics(); }
                }
                FarField.WaitMove();
                long w0 = System.Diagnostics.Stopwatch.GetTimestamp();
                lock (DXGame.SyncLogic)
                {
                    PostTicks += System.Diagnostics.Stopwatch.GetTimestamp() - w0;
                    double t = DXPhysicWorld.LastGameTimeUpdatePhysics;
                    DXPhysicWorld.TimeLastBodyPosition = DXPhysicWorld.TimeBodyPosition;
                    DXPhysicWorld.TimeBodyPosition = t;
                    double span = t - DXPhysicWorld.TimeLastBodyPosition;
                    if (span <= 0 || span > 1000) span = 83.333333333333329;
                    DXPhysicWorld.FactorTimeForInterpolation = 1.0 / span;
                    long p0 = System.Diagnostics.Stopwatch.GetTimestamp();
                    DXHelper_Task.For(0, bodies.Count, delegate (int a, int b)
                    {
                        for (int j = a; j < b; j++)
                        {
                            var bd = bodies[j];
                            if (bd.Handle != IntPtr.Zero && bd.GameEntity != null && bd.GameEntity.PhysicalType != DXBodyType.Static) bd.OnNewPositionComputed();
                        }
                    }, 200, 0);
                    PostLoopTicks += System.Diagnostics.Stopwatch.GetTimestamp() - p0;
                    FarField.AfterStep();
                }
                DXPhysicWorld.NUpdates += 1U;
                DXPhysicWorld.Iteration += 1U;
                UpdateTicks += System.Diagnostics.Stopwatch.GetTimestamp() - u0;
            }
            catch (Exception e) { Log.Error("physics step", e); }
            return false;
        }
    }
}
