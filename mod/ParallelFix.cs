using System;
using System.Threading;
using System.Threading.Tasks;
using DXVision;
using HarmonyLib;

namespace Billions
{

    public static class ParallelFix
    {
        public static bool Enabled = true;
        public static int ChunksPerWorker = 8, MinChunk = 16;
        public static int Installed;

        public static void Install(Harmony h)
        {
            try
            {
                int n = 0;
                var t = typeof(DXHelper_Task);
                var m1 = AccessTools.Method(t, "For", new[] { typeof(int), typeof(int), typeof(Action<int>), typeof(int), typeof(int) });
                var m2 = AccessTools.Method(t, "For", new[] { typeof(int), typeof(int), typeof(Action<int, int>), typeof(int), typeof(int) });
                var m3 = AccessTools.Method(t, "For", new[] { typeof(int), typeof(int), typeof(Action<int, int, int>), typeof(int), typeof(int) });
                if (m2 != null) { h.Patch(m2, new HarmonyMethod(typeof(ParallelFix), nameof(For2))); n++; }

                int w, io; ThreadPool.GetMinThreads(out w, out io);
                int want = Math.Max(w, DXHelper_Task.NCores * 3);
                ThreadPool.SetMinThreads(want, io);
                Installed = n == 1 ? 1 : 0;
                Log.Info("hook: DXHelper_Task.For ×" + n + " (prefix, 동적 분할) · NCores " + DXHelper_Task.NCores + " · 스레드풀 최소 " + w + " → " + want);
            }
            catch (Exception e) { Installed = 0; Log.Error("parallel fix install", e); }
        }

        static int Workers(int maxCores) { int c = DXHelper_Task.NCores; return maxCores > 0 ? Math.Min(c, maxCores) : c; }

        static void Plan(int n, int workers, out int chunk, out int chunks, out int used)
        {
            chunk = Math.Max(MinChunk, n / Math.Max(1, workers * ChunksPerWorker));
            chunks = (n + chunk - 1) / chunk;
            used = Math.Max(1, Math.Min(workers, chunks));
        }

        static bool For1(int startInclusive, int endExlusive, Action<int> action, int minIterationsForConcurrency, int maxCores)
        {
            if (!Enabled) return true;
            int n = endExlusive - startInclusive;
            if (n < minIterationsForConcurrency || n <= 0) { for (int i = startInclusive; i < endExlusive; i++) action(i); return false; }
            int chunk, chunks, used; Plan(n, Workers(maxCores), out chunk, out chunks, out used);
            int next = -1;
            Parallel.For(0, used, new ParallelOptions { MaxDegreeOfParallelism = used }, w =>
            {
                int c;
                while ((c = Interlocked.Increment(ref next)) < chunks)
                {
                    int a = startInclusive + c * chunk, b = Math.Min(endExlusive, a + chunk);
                    for (int i = a; i < b; i++) action(i);
                }
            });
            return false;
        }

        static bool Mine(Action<int, int> action)
        {
            var m = action != null ? action.Method : null; if (m == null) return false;
            string n = m.Name;
            return n.IndexOf("UpdateGameLogic", StringComparison.Ordinal) >= 0 || n.IndexOf("UpdateDefaultTime", StringComparison.Ordinal) >= 0 || (m.DeclaringType != null && m.DeclaringType.FullName.StartsWith("Billions.PhysicsTime", StringComparison.Ordinal));
        }
        static bool For2(int startInclusive, int endExlusive, Action<int, int> action, int minIterationsForConcurrency, int maxCores)
        {
            if (!Enabled || !Mine(action)) return true;
            if (action.Method.Name.IndexOf("UpdateGameLogic", StringComparison.Ordinal) >= 0) Watchdog.Logic();
            int n = endExlusive - startInclusive;
            if (n < minIterationsForConcurrency || n <= 0) { action(startInclusive, endExlusive); return false; }
            int chunk, chunks, used; Plan(n, Workers(maxCores), out chunk, out chunks, out used);
            int next = -1;
            Parallel.For(0, used, new ParallelOptions { MaxDegreeOfParallelism = used }, w =>
            {
                int c;
                while ((c = Interlocked.Increment(ref next)) < chunks)
                {
                    int a = startInclusive + c * chunk;
                    action(a, Math.Min(endExlusive, a + chunk));
                }
            });
            return false;
        }

        static bool For3(int startInclusive, int endExlusive, Action<int, int, int> action, int minIterationsForConcurrency, int maxCores)
        {
            if (!Enabled) return true;
            int n = endExlusive - startInclusive;
            if (n < minIterationsForConcurrency || n <= 0) { action(startInclusive, endExlusive, 0); return false; }
            int chunk, chunks, used; Plan(n, Workers(maxCores), out chunk, out chunks, out used);
            int next = -1;
            Parallel.For(0, used, new ParallelOptions { MaxDegreeOfParallelism = used }, w =>
            {
                int c;
                while ((c = Interlocked.Increment(ref next)) < chunks)
                {
                    int a = startInclusive + c * chunk;
                    action(a, Math.Min(endExlusive, a + chunk), w);
                }
            });
            return false;
        }
    }
}
