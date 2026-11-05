using System;
using System.Diagnostics;
using System.Reflection;
using System.Threading;
using DXVision;
using HarmonyLib;

namespace Billions
{
    // Measuring aid for the game logic rate. The render loop calls DXGame.UpdateGameLogic once per frame: it waits for the
    // previous logic task (WaitForGameLogic), then starts a task that runs every 33.3 ms logic tick that is due. The game
    // clock stops while logic (or physics) is two ticks behind, so the logic rate sets the game speed. Counted per
    // second (Bench reads and resets them): UpdateGameLogic calls and the time they waited for the previous task, logic
    // tasks with their start delay, run time and ticks run, and the time the game clock loop (SmoothTime) was held back by
    // logic or by physics.
    public static class LogicProf
    {
        public static long Calls, CallTicks, Tasks, TaskTicks, StartDelayTicks, TicksRun, IdleTasks, BlockIaUs, BlockPhysUs, RunUs;
        public static long TaskTicksAll;   // never reset (TickProf)
        static long _postAt; static long _iaAtStart;
        [ThreadStatic] static long _t0;

        public static void Install(Harmony h)
        {
            var ugl = AccessTools.Method(typeof(DXGame), "UpdateGameLogic");
            if (ugl == null) { Log.Info("HOOK MISSING (optional): 로직 갱신 계측"); return; }
            h.Patch(ugl, new HarmonyMethod(typeof(LogicProf), nameof(CallPre)), new HarmonyMethod(typeof(LogicProf), nameof(CallPost)));
            // the task body: the compiler-generated instance lambda of UpdateGameLogic without parameters
            MethodInfo body = null;
            foreach (var m in typeof(DXGame).GetMethods(BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.DeclaredOnly))
                if (m.Name.StartsWith("<UpdateGameLogic>b__", StringComparison.Ordinal) && m.GetParameters().Length == 0 && m.ReturnType == typeof(void)) { body = m; break; }
            if (body != null) h.Patch(body, new HarmonyMethod(typeof(LogicProf), nameof(TaskPre)), new HarmonyMethod(typeof(LogicProf), nameof(TaskPost)));
            Log.Info("hook: DXGame.UpdateGameLogic (로직 갱신 계측" + (body != null ? ", 작업 " + body.Name : ", 작업 본문 못 찾음") + ")");
        }

        static void CallPre() { _t0 = Stopwatch.GetTimestamp(); }
        static void CallPost()
        {
            long now = Stopwatch.GetTimestamp();
            Interlocked.Increment(ref Calls); Interlocked.Add(ref CallTicks, now - _t0);
            Volatile.Write(ref _postAt, now);
        }
        static void TaskPre(DXGame __instance)
        {
            long now = Stopwatch.GetTimestamp(); _t0 = now;
            long post = Volatile.Read(ref _postAt);
            if (post > 0 && now > post) Interlocked.Add(ref StartDelayTicks, now - post);
            _iaAtStart = __instance.IAIteration;
        }
        static void TaskPost(DXGame __instance)
        {
            long d = Stopwatch.GetTimestamp() - _t0, n = __instance.IAIteration - _iaAtStart;
            Interlocked.Increment(ref Tasks); Interlocked.Add(ref TaskTicks, d); if (TickProf.On) Interlocked.Add(ref TaskTicksAll, d); Interlocked.Add(ref TicksRun, n);
            if (n == 0) Interlocked.Increment(ref IdleTasks);
        }

        // SmoothTime's clock loop: dt (ms) it could not add because logic or physics was two ticks behind, or did add
        public static void Gate(double dt, bool iaBehind, bool physBehind)
        {
            long us = (long)(dt * 1000.0);
            if (iaBehind) BlockIaUs += us; else if (physBehind) BlockPhysUs += us; else RunUs += us;
        }
    }
}
