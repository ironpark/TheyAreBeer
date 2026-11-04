using System;
using HarmonyLib;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;

namespace Billions
{
    // Measuring aid (TABPERF_POSTPROF=1): runs the physics thread's queued post methods (DXPhysicWorld.Invoke) itself,
    // timing each by the method behind the delegate, and logs the totals (Dump, at the end of a bench run).
    public static class PostProf
    {
        public static readonly bool On = Settings.Switch("TABPERF_POSTPROF", false);
        static FieldInfo _queueField; static Type _queueOwner;
        sealed class Stat { public long Count, Ticks; }
        static readonly Dictionary<MethodInfo, Stat> _stats = new Dictionary<MethodInfo, Stat>();
        static long _batches;

        // Runs the queue of a DXPostMethodManager; false when its queue field is not the expected one (caller runs ExecuteAll).
        public static bool Run(object manager)
        {
            var t = manager.GetType();
            if (t != _queueOwner) { _queueField = AccessTools.Field(t, "_PendingMethods"); _queueOwner = t; }
            var q = _queueField != null ? _queueField.GetValue(manager) as ConcurrentQueue<Action> : null;
            if (q == null) return false;
            _batches++;
            Action a;
            while (q.TryDequeue(out a))
            {
                long t0 = Stopwatch.GetTimestamp();
                a();
                long d = Stopwatch.GetTimestamp() - t0;
                Stat s;
                if (!_stats.TryGetValue(a.Method, out s)) _stats[a.Method] = s = new Stat();
                s.Count++; s.Ticks += d;
            }
            return true;
        }

        public static void Dump()
        {
            if (!On) return;
            double f = 1000.0 / Stopwatch.Frequency;
            long all = _stats.Values.Sum(s => s.Ticks);
            Log.Info("postprof: 물리 대기 작업 " + _batches + "회 실행, 합계 " + (all * f).ToString("0.0") + " ms");
            foreach (var kv in _stats.OrderByDescending(kv => kv.Value.Ticks).Take(15))
                Log.Info("postprof: " + (kv.Value.Ticks * f).ToString("0.0").PadLeft(9) + " ms " + kv.Value.Count.ToString().PadLeft(8) + "회  " + kv.Key.DeclaringType.FullName + "." + kv.Key.Name);
        }
    }
}
