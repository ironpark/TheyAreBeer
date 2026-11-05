using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Threading;
using DXVision;
using HarmonyLib;

namespace Billions
{
    // Measuring aid (TABPERF_TICKPROF=1): where a logic tick's time goes. A tick (DXGame.UpdateGameLogic's task) runs every
    // enabled system's OnStartUpdate, then every entity's Update in parallel (the entity's OnUpdate, then the handlers on
    // its EventOnUpdate, which is where components do their work), DXGame.EventOnUpdate, and every system's OnFinishUpdate.
    // Times each of them by method: the system and entity overrides are patched at install, the event handlers as they
    // turn up on live entities (scanned every few seconds under the logic lock). Times are inclusive (a method that calls
    // a patched base or handler counts that too) and summed over all threads, so the entity part is CPU time, not wall
    // time. Logged every 20 s with the zombie count, and in total at the end of a bench run.
    public static class TickProf
    {
        static readonly bool Full = string.Equals(Settings.Env("TABPERF_TICKPROF"), "full", StringComparison.OrdinalIgnoreCase);
        public static readonly bool On = Full || Settings.Switch("TABPERF_TICKPROF", false);
        // TABPERF_TICKPROF=full also times the entity OnUpdate overrides, the steps of CMovable.OnUpdate and every leaf of the
        // behaviour trees (the lambdas of ZXBehaviour's named nodes). Without it the entities' own part is the parallel
        // chunk (the game's 100 entities per DXHelper_Task.For call) minus the handlers.
        static readonly string[] MovableSteps = { "GetCurrentSpeed", "UpdateAngle", "UpdateTargetAngle", "CheckJumping", "FindPath", "StopMovement", "MoveToDestinyWithoutPhysics", "NextCell", "IsCellAvailable" };
        static readonly Dictionary<MethodBase, string> _labels = new Dictionary<MethodBase, string>();
        const int Max = 4096;
        static Harmony _h;
        static ModuleBuilder _probes;
        static readonly List<MethodBase> _methods = new List<MethodBase>();
        static readonly HashSet<MethodBase> _seen = new HashSet<MethodBase>();
        static readonly List<long[]> _threads = new List<long[]>();
        [ThreadStatic] static long[] _mine;   // per thread: [2 * id] ticks, [2 * id + 1] calls
        static long[] _last = new long[2 * Max], _start = new long[2 * Max];
        static AccessTools.FieldRef<DXEntity, Action> _entEvent;
        static AccessTools.FieldRef<DXGame, List<DXEntity>> _entities;
        static FieldInfo _gameEvent;
        static int _chunk = -1;
        static long _scanAt, _logAt, _iaLast, _iaStart, _taskLast, _taskStart;

        public static void Install(Harmony h)
        {
            if (!On) return;
            _h = h;
            _entEvent = AccessTools.FieldRefAccess<DXEntity, Action>("EventOnUpdate");
            _entities = AccessTools.FieldRefAccess<DXGame, List<DXEntity>>("_Entities");
            _gameEvent = AccessTools.Field(typeof(DXGame), "EventOnUpdate");
            int n = 0;
            foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type[] types;
                try { types = asm.GetTypes(); } catch (ReflectionTypeLoadException e) { types = e.Types.Where(t => t != null).ToArray(); } catch { continue; }
                foreach (var t in types)
                {
                    if (t.IsSubclassOf(typeof(DXSystem)))
                        foreach (var name in new[] { "OnStartUpdate", "OnFinishUpdate" }) if (Patch(Declared(t, name))) n++;
                    if (Full && t.IsSubclassOf(typeof(DXEntity)) && Patch(Declared(t, "OnUpdate"))) n++;
                }
            }
            // the entity chunk: the parallel body of UpdateGameLogic's task, (int from, int to)
            foreach (var t in new[] { typeof(DXGame) }.Concat(typeof(DXGame).GetNestedTypes(BindingFlags.NonPublic)))
                foreach (var m in t.GetMethods(BindingFlags.Instance | BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.DeclaredOnly))
                {
                    var ps = m.GetParameters();
                    if (m.Name.Contains("<UpdateGameLogic>") && ps.Length == 2 && ps[0].ParameterType == typeof(int) && ps[1].ParameterType == typeof(int) && Patch(m)) { _chunk = _methods.Count - 1; n++; }
                }
            if (_chunk < 0) Log.Info("tickprof: 엔티티 묶음(UpdateGameLogic 병렬 본문) 못 찾음");
            if (Full) n += PatchDetail();
            AppDomain.CurrentDomain.FirstChanceException += (s, e) => OnException(e.Exception);
            Log.Info("tickprof: 시스템·엔티티 갱신 메서드 " + n + "개 계측 (이벤트 처리기는 레벨에서 찾는다)");
        }

        static int PatchDetail()
        {
            int n = 0;
            var mov = AccessTools.TypeByName("ZX.Components.CMovable");
            if (mov != null)
                foreach (var m in mov.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                    if (Array.IndexOf(MovableSteps, m.Name) >= 0 && Patch(m)) n++;
            // behaviour leaves: static DXNode<BData> fields of ZXBehaviour; a leaf's OnExecute is a lambda of the game's own
            // assembly (the & | ! combinators are DXVision's). Inclusive: a leaf that evaluates other nodes counts them too.
            var bh = AccessTools.TypeByName("ZX.Behaviours.ZXBehaviour");
            if (bh != null)
                foreach (var f in bh.GetFields(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic))
                {
                    try
                    {
                        if (!f.FieldType.Name.StartsWith("DXNode", StringComparison.Ordinal)) continue;
                        var node = f.GetValue(null); if (node == null) continue;
                        var ex = AccessTools.Property(f.FieldType, "OnExecute")?.GetValue(node, null) as Delegate;
                        if (ex == null || ex.Method.DeclaringType == null || ex.Method.DeclaringType.Assembly != bh.Assembly || _labels.ContainsKey(ex.Method)) continue;
                        var name = AccessTools.Property(f.FieldType, "Name")?.GetValue(node, null) as string;
                        _labels[ex.Method] = "행동 " + (string.IsNullOrEmpty(name) ? f.Name : name);
                        if (Patch(ex.Method as MethodInfo)) n++;
                    }
                    catch (Exception e) { Log.Info("tickprof: 행동 노드 " + f.Name + " — " + e.Message); }
                }
            return n;
        }

        // exceptions thrown anywhere, by type and throwing method (a swallowed exception per entity is expensive)
        static readonly Dictionary<string, long> _exc = new Dictionary<string, long>();
        static long _excAll;
        static void OnException(Exception e)
        {
            Interlocked.Increment(ref _excAll);
            string k;
            try { var st = new StackTrace(e, false); var f = st.FrameCount > 0 ? st.GetFrame(0).GetMethod() : null; k = e.GetType().Name + " @ " + (f != null ? Name(f) : "?"); }
            catch { k = e.GetType().Name; }
            lock (_exc) { long n; _exc.TryGetValue(k, out n); _exc[k] = n + 1; }
        }
        static void DumpExceptions(string label, int top)
        {
            List<KeyValuePair<string, long>> rows; lock (_exc) rows = _exc.OrderByDescending(r => r.Value).Take(top).ToList();
            Log.Info("tickprof [" + label + "] 예외 누적 " + Interlocked.Read(ref _excAll) + "개");
            foreach (var r in rows) Log.Info("tickprof   예외 " + r.Value.ToString().PadLeft(9) + "  " + r.Key);
        }

        static MethodInfo Declared(Type t, string name)
        {
            var m = t.GetMethod(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly, null, Type.EmptyTypes, null);
            return m != null && !m.IsAbstract && m.GetMethodBody() != null ? m : null;
        }

        static bool Patch(MethodInfo m)
        {
            if (m == null || !_seen.Add(m) || _methods.Count >= Max || m.DeclaringType == null || m.DeclaringType.Assembly == typeof(TickProf).Assembly) return false;
            if (m.ContainsGenericParameters || m.GetMethodBody() == null) return false;
            try
            {
                var probe = Probe(_methods.Count);
                _methods.Add(m);
                _h.Patch(m, new HarmonyMethod(probe.GetMethod("Prefix")), new HarmonyMethod(probe.GetMethod("Postfix")));
                return true;
            }
            catch (Exception e) { Log.Info("tickprof: " + Name(m) + " 계측 못 함 — " + e.Message); return false; }
        }

        // A prefix/postfix pair per method with its id as a constant. One shared pair needs Harmony's __originalMethod to tell
        // the methods apart, and that lookup made every timed call several microseconds slower (the logic ran 7-9 times
        // slower at 40,000-60,000 zombies).
        static Type Probe(int id)
        {
            if (_probes == null)
                _probes = AppDomain.CurrentDomain.DefineDynamicAssembly(new AssemblyName("TABPerfTickProbes"), AssemblyBuilderAccess.Run).DefineDynamicModule("TABPerfTickProbes");
            var tb = _probes.DefineType("TickProbe" + id, TypeAttributes.Public | TypeAttributes.Abstract | TypeAttributes.Sealed);
            var pre = tb.DefineMethod("Prefix", MethodAttributes.Public | MethodAttributes.Static, typeof(void), new[] { typeof(long).MakeByRefType() });
            pre.DefineParameter(1, ParameterAttributes.Out, "__state");
            var il = pre.GetILGenerator();
            il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Call, typeof(Stopwatch).GetMethod("GetTimestamp")); il.Emit(OpCodes.Stind_I8); il.Emit(OpCodes.Ret);
            var post = tb.DefineMethod("Postfix", MethodAttributes.Public | MethodAttributes.Static, typeof(void), new[] { typeof(long) });
            post.DefineParameter(1, ParameterAttributes.None, "__state");
            il = post.GetILGenerator();
            il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Ldc_I4, id); il.Emit(OpCodes.Call, typeof(TickProf).GetMethod(nameof(Record))); il.Emit(OpCodes.Ret);
            return tb.CreateType();
        }

        public static void Record(long t0, int id)
        {
            long d = Stopwatch.GetTimestamp() - t0;
            var a = _mine;
            if (a == null) { a = _mine = new long[2 * Max]; lock (_threads) _threads.Add(a); }
            a[2 * id] += d; a[2 * id + 1]++;
        }

        // every level frame from Bench (render thread): new event handlers every 5 s, a log line every 20 s
        public static void Frame(int zombies)
        {
            if (!On || _h == null) return;
            var g = DXGame.Current; if (g == null) return;
            long now = Stopwatch.GetTimestamp(), f = Stopwatch.Frequency;
            if (_scanAt == 0 || now - _scanAt > 5 * f) { _scanAt = now; Scan(g); }
            if (_logAt == 0) { _logAt = now; Snapshot(_last); _iaLast = g.IAIteration; _taskLast = LogicProf.TaskTicksAll; Array.Copy(_last, _start, _last.Length); _iaStart = _iaLast; _taskStart = _taskLast; return; }
            if (now - _logAt < 20 * f) return;
            _logAt = now;
            var cur = new long[2 * Max]; Snapshot(cur);
            long ia = g.IAIteration, task = LogicProf.TaskTicksAll;
            Report("좀비 " + zombies, _last, cur, ia - _iaLast, task - _taskLast, 12);
            DumpExceptions("좀비 " + zombies, 5);
            _last = cur; _iaLast = ia; _taskLast = task;
        }

        public static void Dump()
        {
            if (!On || _h == null || _logAt == 0) return;
            var cur = new long[2 * Max]; Snapshot(cur);
            Report("전체", _start, cur, DXGame.Current.IAIteration - _iaStart, LogicProf.TaskTicksAll - _taskStart, Full ? 90 : 40);
            DumpExceptions("전체", 15);
        }

        static void Scan(DXGame g)
        {
            // under the logic lock: no tick runs (or changes the entity list) while methods are patched
            if (!Monitor.TryEnter(DXGame.SyncLogic, 200)) return;
            int before = _methods.Count;
            try
            {
                var list = _entities(g);
                var handlers = new HashSet<MethodInfo>();
                for (int i = 0; i < list.Count; i++)
                {
                    var ev = _entEvent(list[i]);
                    if (ev != null) foreach (var d in ev.GetInvocationList()) handlers.Add(d.Method);
                }
                var gev = _gameEvent != null ? _gameEvent.GetValue(g) as Action : null;
                if (gev != null) foreach (var d in gev.GetInvocationList()) handlers.Add(d.Method);
                foreach (var m in handlers) Patch(m);
            }
            catch (Exception e) { Log.Error("tickprof scan", e); }
            finally { Monitor.Exit(DXGame.SyncLogic); }
            if (_methods.Count > before) Log.Info("tickprof: 이벤트 처리기 " + (_methods.Count - before) + "개 더 계측");
        }

        static void Snapshot(long[] into)
        {
            Array.Clear(into, 0, into.Length);
            lock (_threads) foreach (var a in _threads) for (int i = 0; i < into.Length; i++) into[i] += a[i];
        }

        static void Report(string label, long[] from, long[] to, long ticks, long taskTicks, int top)
        {
            if (ticks <= 0) return;
            double f = 1000.0 / Stopwatch.Frequency / ticks;
            var rows = new List<KeyValuePair<int, long>>();
            long sys = 0, all = 0, chunk = 0;
            for (int id = 0; id < _methods.Count; id++)
            {
                long d = to[2 * id] - from[2 * id]; if (d <= 0) continue;
                rows.Add(new KeyValuePair<int, long>(id, d));
                if (id == _chunk) chunk = d;
                else { all += d; if (_methods[id].DeclaringType.IsSubclassOf(typeof(DXSystem))) sys += d; }
            }
            Log.Info("tickprof [" + label + "] 틱 " + ticks + "회 · 틱당 로직 작업 " + (taskTicks * f).ToString("0.0") + " ms(벽시계) · 시스템 " + (sys * f).ToString("0.0")
                + " ms · 엔티티 묶음 " + (chunk * f).ToString("0.0") + " ms · 그 안의 처리기 등 " + ((all - sys) * f).ToString("0.0") + " ms(모든 스레드 합)");
            foreach (var r in rows.OrderByDescending(r => r.Value).Take(top))
            {
                long calls = to[2 * r.Key + 1] - from[2 * r.Key + 1];
                Log.Info("tickprof   " + (r.Value * f).ToString("0.00").PadLeft(8) + " ms/틱 " + ((double)calls / ticks).ToString("0").PadLeft(7) + "회/틱 "
                    + (r.Value * 1e9 / Stopwatch.Frequency / Math.Max(1, calls)).ToString("0").PadLeft(7) + " ns  " + Name(_methods[r.Key]));
            }
        }

        static string Name(MethodBase m) { string l; if (_labels.TryGetValue(m, out l)) return l; return (m.DeclaringType != null ? m.DeclaringType.FullName : "?") + "." + m.Name; }
    }
}
