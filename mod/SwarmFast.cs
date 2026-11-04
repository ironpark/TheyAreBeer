using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using DXVision;
using HarmonyLib;
using ZX;
using ZX.Entities;

namespace Billions
{
    // The swarm minimap indicator (ZXMiniMapIndicatorInfectedSwarn) places its icon at the average cell of its wave's
    // living units. Its Update runs on the render thread once per logic tick (ZXLevelState.OnUpdate queues it) and walks
    // the whole unit list twice - drop the dead (CLife.IsAlive), then sum Unit.Cell - which with a wave of 100,000 is
    // about 10 ms per frame of pointer chasing through managed objects.
    //
    // With the Rapier replacement, the wave's physics bodies go into a native body group (DX_GroupNew / Add / Cells): the
    // DLL keeps the records, takes disposed bodies out by itself, and sums the cells of the members whose category is
    // not 0 (CLife clears a unit's body category the moment it dies) in one pass over native memory. Each Update then
    // sets the icon from that sum, every call as before. The game's own Update still runs:
    //   - once a second, to drop the dead from the unit list (what IsFinished and saved games see),
    //   - whenever the group counts no living unit (so the end of the wave is detected the same way and at once),
    //   - for anything this does not cover (no group functions, errors).
    // Units created before their body exists wait in a small managed list (counted from their CLife and Cell) until it
    // does. After each full Update the living count of the group is checked against the unit list; a mismatch rebuilds
    // the group from the list. TABPERF_SWARMFAST=0 turns this off.
    public static class SwarmFast
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NewFn();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void FreeFn(int id);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int AddFn(int id, IntPtr[] bodies, int n);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int CellsFn(int id, int[] out3);

        static NewFn _new; static FreeFn _free; static AddFn _add; static CellsFn _cells;
        public static bool On { get; private set; }
        static long Fast, Full, Rebuilds;
        const long PruneMs = 1000;

        sealed class St
        {
            public int Id = -1;
            public readonly List<Unit> Pending = new List<Unit>();   // no body yet
            public long PruneAt;
            ~St() { int id = Id; if (id >= 0 && On) try { _free(id); } catch { } }
        }
        static readonly ConditionalWeakTable<ZXMiniMapIndicatorInfectedSwarn, St> _st = new ConditionalWeakTable<ZXMiniMapIndicatorInfectedSwarn, St>();
        static readonly int[] _out = new int[3];
        static IntPtr[] _buf = new IntPtr[4096];
        static bool _errLogged, _mismatchLogged;

        public static void Install(Harmony h)
        {
            if (Environment.GetEnvironmentVariable("TABPERF_SWARMFAST") == "0") { Log.Info("swarm: TABPERF_SWARMFAST=0 — 원작 그대로"); return; }
            if (PhysicsRedirect.Module == IntPtr.Zero || PerfMod.PhysOpt(200, 0) != 2) { Log.Info("swarm: Rapier 대체본이 아님 — 원작 그대로"); return; }
            _new = PhysicsRedirect.Proc<NewFn>("DX_GroupNew"); _free = PhysicsRedirect.Proc<FreeFn>("DX_GroupFree");
            _add = PhysicsRedirect.Proc<AddFn>("DX_GroupAdd"); _cells = PhysicsRedirect.Proc<CellsFn>("DX_GroupCells");
            if (_new == null || _free == null || _add == null || _cells == null) { Log.Info("swarm: 물리 DLL 에 몸체 묶음 함수가 없다 — 원작 그대로"); return; }
            var t = typeof(ZXMiniMapIndicatorInfectedSwarn);
            var upd = AccessTools.Method(t, "Update"); var addu = AccessTools.Method(t, "AddUnits");
            if (upd == null || addu == null) { Log.Info("HOOK MISSING (optional): 웨이브 미니맵 표시 — 원작 그대로"); return; }
            h.Patch(upd, new HarmonyMethod(typeof(SwarmFast), nameof(UpdatePre)), new HarmonyMethod(typeof(SwarmFast), nameof(UpdatePost)));
            h.Patch(addu, null, new HarmonyMethod(typeof(SwarmFast), nameof(AddPost)));
            On = true;
            Log.Info("hook: ZXMiniMapIndicatorInfectedSwarn.Update (prefix/postfix, 웨이브 위치를 물리 DLL 몸체 묶음에서 · 죽은 유닛 정리는 1초마다) · AddUnits (postfix)");
        }

        static bool Alive(Unit u)
        {
            if (u == null) return false;
            var l = u.get_CLife();
            return l != null && l.IsAlive;
        }

        // (Re)builds the group from the unit list: units with a body go native, the rest wait in Pending.
        static void Seed(St st, List<Unit> units)
        {
            if (st.Id >= 0) _free(st.Id);
            st.Id = _new(); st.Pending.Clear();
            if (st.Id < 0) return;
            Rebuilds++;
            int n = 0;
            lock (units)
                foreach (var u in units)
                {
                    if (u == null) continue;
                    IntPtr hd = Handle(u);
                    if (hd == IntPtr.Zero) st.Pending.Add(u); else Push(st, hd, ref n);
                }
            if (n > 0) _add(st.Id, _buf, n);
        }

        static IntPtr Handle(Unit u) { var b = u.Body; return b != null ? b.Handle : IntPtr.Zero; }

        // Buffers a body handle for the group, sending a full buffer.
        static void Push(St st, IntPtr hd, ref int n)
        {
            if (n == _buf.Length) { _add(st.Id, _buf, n); n = 0; }
            _buf[n++] = hd;
        }

        // Pending units whose body now exists go native; dead ones without a body are dropped.
        static void Flush(St st)
        {
            if (st.Pending.Count == 0) return;
            int n = 0, w = 0;
            for (int i = 0; i < st.Pending.Count; i++)
            {
                var u = st.Pending[i];
                IntPtr hd = Handle(u);
                if (hd != IntPtr.Zero) Push(st, hd, ref n);
                else if (Alive(u)) st.Pending[w++] = u;
            }
            st.Pending.RemoveRange(w, st.Pending.Count - w);
            if (n > 0) _add(st.Id, _buf, n);
        }

        // Living units and the sums of their cells: the group's plus the pending ones'. False: stale group.
        static bool Count(St st, out int alive, out int sx, out int sy)
        {
            alive = sx = sy = 0;
            if (st.Id < 0 || _cells(st.Id, _out) < 0) return false;
            alive = _out[0]; sx = _out[1]; sy = _out[2];
            foreach (var u in st.Pending)
                if (Alive(u)) { Point c = u.Cell; alive++; sx = unchecked(sx + c.X); sy = unchecked(sy + c.Y); }
            return true;
        }

        static bool UpdatePre(ZXMiniMapIndicatorInfectedSwarn __instance, ref St __state)
        {
            __state = null;
            if (!On) return true;
            try
            {
                var units = __instance.get_Units(); if (units == null) return true;
                var st = _st.GetValue(__instance, k => new St());
                int alive, sx, sy;
                lock (st)
                {
                    if (st.Id < 0) Seed(st, units); else Flush(st);
                    if (!Count(st, out alive, out sx, out sy)) { Seed(st, units); if (!Count(st, out alive, out sx, out sy)) return true; }
                }
                long now = Clock.Ms;
                if (alive == 0 || now - st.PruneAt >= PruneMs) { st.PruneAt = now; __state = st; Full++; return true; }
                if (Check) Compare(units, alive, sx, sy);
                // what the game's Update does while units remain: started, icon at the average cell (Point.Divide: int /)
                __instance._Started = true;
                __instance.Cell = new Point(sx / alive, sy / alive);
                Fast++;
                return false;
            }
            catch (Exception e)
            {
                if (!_errLogged) { _errLogged = true; Log.Error("swarm update", e); }
                return true;
            }
        }

        // After the game's own Update (dead dropped from the list): the group must count the same living units.
        static void UpdatePost(ZXMiniMapIndicatorInfectedSwarn __instance, St __state)
        {
            var st = __state; if (st == null) return;
            try
            {
                var units = __instance.get_Units(); if (units == null) return;
                int alive, sx, sy, listed;
                lock (units) listed = units.Count;
                lock (st)
                {
                    if (__instance.IsFinished) { if (st.Id >= 0) { _free(st.Id); st.Id = -1; } st.Pending.Clear(); return; }
                    if (Count(st, out alive, out sx, out sy) && alive == listed) return;
                    if (!_mismatchLogged) { _mismatchLogged = true; Log.Info("swarm: 몸체 묶음 " + alive + " ≠ 유닛 목록 " + listed + " — 목록에서 다시 만든다(이후로는 기록 안 함)"); }
                    Seed(st, units);
                }
            }
            catch (Exception e) { if (!_errLogged) { _errLogged = true; Log.Error("swarm check", e); } }
        }

        // TABPERF_SWARMCHECK=1: every fast call also walks the unit list the game's way and counts disagreements (log).
        static readonly bool Check = Environment.GetEnvironmentVariable("TABPERF_SWARMCHECK") == "1";
        static long Checked, CountDiff, CellDiff; static long _checkLogAt;
        static void Compare(List<Unit> units, int alive, int sx, int sy)
        {
            int a = 0, x = 0, y = 0;
            lock (units) foreach (var u in units) if (Alive(u)) { Point c = u.Cell; a++; x = unchecked(x + c.X); y = unchecked(y + c.Y); }
            Checked++;
            bool cd = a != alive, pd = a > 0 && alive > 0 && (x / a != sx / alive || y / a != sy / alive);
            if (cd) CountDiff++; if (pd) CellDiff++;
            long now = Clock.Ms;
            if ((cd || pd) && now - _checkLogAt > 5000) { _checkLogAt = now; Log.Info("swarm check: 원작 " + a + "마리 (" + (a > 0 ? x / a + "," + y / a : "-") + ") · 묶음 " + alive + "마리 (" + (alive > 0 ? sx / alive + "," + sy / alive : "-") + ") — 누적 " + Checked + "회 중 수 다름 " + CountDiff + " · 위치 다름 " + CellDiff); }
            if (Checked % 500 == 0) Log.Info("swarm check: " + Checked + "회 · 수 다름 " + CountDiff + " · 위치 다름 " + CellDiff + " · 빠른 " + Fast + " · 원작 " + Full + " · 다시 만듦 " + Rebuilds);
        }

        static void AddPost(ZXMiniMapIndicatorInfectedSwarn __instance, List<Unit> unitsToGenerate)
        {
            if (!On || unitsToGenerate == null) return;
            St st;
            if (!_st.TryGetValue(__instance, out st)) return;   // not seeded yet: the first Update takes the whole list
            lock (st) foreach (var u in unitsToGenerate) if (u != null) st.Pending.Add(u);
        }
    }
}
