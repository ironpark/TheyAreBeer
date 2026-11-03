using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Threading;
using Algorithms;
using DXVision;
using HarmonyLib;
using ZX.Components;
using ZX.Entities;
using ZX.Systems;

namespace Billions
{

    public static class EnergyFix
    {
        public static bool Enabled = true;
        public static int SpotEvery = 4000;
        public static int SlowMs = 50;

        static readonly object Gate = new object();
        static readonly EnergyLinks Links = new EnergyLinks();
        static readonly List<PathFinderNode> Found = new List<PathFinderNode>();
        static AccessTools.FieldRef<PathFinderFast, uint[,]> _grid;
        static uint[,] _builtGrid;
        static int _ver = 1, _builtVer, _bypass;
        [ThreadStatic] static long _t0;

        sealed class Acc { public long Q, No, Unknown, Rebuilds, RebuildTicks, Orig, OrigFail, OrigTicks, Spots, Mismatch, Adds, Removes, Unit, UnitFail, UnitTicks, World, WorldTicks; }
        public static bool MeasureUnits = true;
        static Acc _sec = new Acc(), _min = new Acc();
        static long _minAt, _slowLogAt;
        public static int Installed;

        public static void Install(Harmony h)
        {
            try
            {
                _grid = AccessTools.FieldRefAccess<PathFinderFast, uint[,]>("mGrid");
                var fp = AccessTools.Method(typeof(PathFinderFast), "FindPath", new[] { typeof(Point), typeof(Point), typeof(uint) });
                var ue = AccessTools.Method(typeof(SysMiniMapUpdater), "UpdateEntityEnergy");
                var ua = AccessTools.Method(typeof(SysMiniMapUpdater), "UpdateEntitiesEnergy");
                var uf = AccessTools.Method(typeof(SysMiniMapUpdater), "UpdateLayerEnergyFast");
                if (fp == null || ue == null || ua == null) { Log.Info("HOOK MISSING (optional): 전력 연결 번호표 — PathFinderFast.FindPath " + (fp != null) + " · UpdateEntityEnergy " + (ue != null) + " · UpdateEntitiesEnergy " + (ua != null) + " (원작 길 찾기 그대로)"); return; }
                h.Patch(ue, new HarmonyMethod(typeof(EnergyFix), nameof(BeforeUpdate)), new HarmonyMethod(typeof(EnergyFix), nameof(Dirty)));
                h.Patch(ua, new HarmonyMethod(typeof(EnergyFix), nameof(BypassOn)), null, null, new HarmonyMethod(typeof(EnergyFix), nameof(BypassOff)));
                if (uf != null) h.Patch(uf, new HarmonyMethod(typeof(EnergyFix), nameof(BypassOn)), null, null, new HarmonyMethod(typeof(EnergyFix), nameof(BypassOff)));
                h.Patch(fp, new HarmonyMethod(typeof(EnergyFix), nameof(FindPathPre)), new HarmonyMethod(typeof(EnergyFix), nameof(FindPathPost)));
                var cw = AccessTools.Method(typeof(CEnergyTransferer), "System_CheckWorldEnergy");
                if (cw != null) h.Patch(cw, new HarmonyMethod(typeof(EnergyFix), nameof(WorldPre)), new HarmonyMethod(typeof(EnergyFix), nameof(WorldPost)));
                Installed = 1;
                Log.Info("hook: 전력 지도 PathFinderFast.FindPath (prefix, 이어진 덩어리 번호표) · SysMiniMapUpdater.UpdateEntityEnergy (무효) · UpdateEntitiesEnergy (원작 그대로)");
            }
            catch (Exception e) { Installed = 0; Log.Error("energy fix install", e); }
        }

        static void BeforeUpdate(int __2, bool __4)
        {
            Interlocked.Increment(ref _ver);
            if (__4) return;
            if (__2 > 0) Interlocked.Increment(ref _sec.Adds); else Interlocked.Increment(ref _sec.Removes);
        }
        static void Dirty() { Interlocked.Increment(ref _ver); }
        static void BypassOn() { Interlocked.Increment(ref _bypass); Interlocked.Increment(ref _ver); }
        static Exception BypassOff(Exception __exception) { Interlocked.Decrement(ref _bypass); Interlocked.Increment(ref _ver); return __exception; }

        static bool FindPathPre(PathFinderFast __instance, Point __0, Point __1, ref List<PathFinderNode> __result, ref int __state)
        {
            __state = -1;
            var mm = SysMiniMapUpdater.Current;
            if (mm == null || !ReferenceEquals(__instance, mm._PFEnergy)) { if (MeasureUnits) { __state = 30; _t0 = Stopwatch.GetTimestamp(); } return true; }
            Interlocked.Increment(ref _sec.Q);
            if (!Enabled || Volatile.Read(ref _bypass) > 0) { __state = 20; _t0 = Stopwatch.GetTimestamp(); return true; }
            int a;
            try
            {
                var g = _grid(__instance);
                lock (Gate)
                {
                    int ver = Volatile.Read(ref _ver);
                    if (!ReferenceEquals(g, _builtGrid) || ver != _builtVer)
                    {
                        long t = Stopwatch.GetTimestamp();
                        Links.Build(g); _builtGrid = g; _builtVer = ver;
                        _sec.Rebuilds++; _sec.RebuildTicks += Stopwatch.GetTimestamp() - t;
                    }
                    a = Links.Connected(g, __0.X, __0.Y, __1.X, __1.Y);
                }
            }
            catch (Exception e) { Log.Error("energy links", e); Enabled = false; __state = 20; _t0 = Stopwatch.GetTimestamp(); return true; }
            if (a == EnergyLinks.Unknown) { Interlocked.Increment(ref _sec.Unknown); __state = 20; _t0 = Stopwatch.GetTimestamp(); return true; }
            if (SpotEvery > 0 && Interlocked.Read(ref _sec.Q) % SpotEvery == 1) { __state = 10 + a; _t0 = Stopwatch.GetTimestamp(); return true; }
            if (a == EnergyLinks.No) Interlocked.Increment(ref _sec.No);
            __result = a == EnergyLinks.Yes ? Found : null;
            __state = a;
            return false;
        }

        static void FindPathPost(PathFinderFast __instance, List<PathFinderNode> __result, int __state)
        {
            if (__state < 10) return;
            long dt = Stopwatch.GetTimestamp() - _t0;
            if (__state == 30) { var a0 = _sec; Interlocked.Increment(ref a0.Unit); Interlocked.Add(ref a0.UnitTicks, dt); if (__result == null) Interlocked.Increment(ref a0.UnitFail); return; }
            bool orig = __result != null;
            Interlocked.Increment(ref _sec.Orig); Interlocked.Add(ref _sec.OrigTicks, dt); if (!orig) Interlocked.Increment(ref _sec.OrigFail);
            if (__state >= 20) return;
            Interlocked.Increment(ref _sec.Spots);
            bool mine = __state == 11;
            if (orig != mine && !__instance.Stopped)
            {
                long n = Interlocked.Increment(ref _sec.Mismatch);
                if (n <= 3) Log.Info("energy links MISMATCH: 원작 " + orig + " · 번호표 " + mine + " (덩어리 " + Links.Groups + ")");
            }
        }

        [ThreadStatic] static long _w0;
        static void WorldPre() { _w0 = Stopwatch.GetTimestamp(); }
        static void WorldPost() { var a = _sec; Interlocked.Increment(ref a.World); Interlocked.Add(ref a.WorldTicks, Stopwatch.GetTimestamp() - _w0); }

        static double Ms(long ticks) { return ticks * 1000.0 / Stopwatch.Frequency; }
        static void Add(Acc to, Acc s) { to.Q += s.Q; to.No += s.No; to.Unknown += s.Unknown; to.Rebuilds += s.Rebuilds; to.RebuildTicks += s.RebuildTicks; to.Orig += s.Orig; to.OrigFail += s.OrigFail; to.OrigTicks += s.OrigTicks; to.Spots += s.Spots; to.Mismatch += s.Mismatch; to.Adds += s.Adds; to.Removes += s.Removes; to.Unit += s.Unit; to.UnitFail += s.UnitFail; to.UnitTicks += s.UnitTicks; to.World += s.World; to.WorldTicks += s.WorldTicks; }
        static string Text(Acc s)
        {
            string t = "energy " + (Enabled ? "번호표" : "원작") + ": 질문 " + s.Q + (Enabled ? " (안 이어짐 " + s.No + ")" : "") + " · 켜기/끄기 " + s.Adds + "/" + s.Removes;
            if (s.Rebuilds > 0) t += " · 번호표 " + s.Rebuilds + "번 " + Ms(s.RebuildTicks).ToString("0.0") + " ms";
            if (s.Orig > 0) t += " · 원작 길 찾기 " + s.Orig + "번(실패 " + s.OrigFail + ") " + Ms(s.OrigTicks).ToString("0.0") + " ms";
            if (s.Unknown > 0) t += " · 모름 " + s.Unknown;
            if (s.Spots > 0 || s.Mismatch > 0) t += " · 맞춰 보기 " + s.Spots + "(틀림 " + s.Mismatch + ")";
            if (s.World > 0) t += " · 전력망 검사 " + s.World + "번 " + Ms(s.WorldTicks).ToString("0.0") + " ms";
            if (s.Unit > 0) t += " · 유닛 길 찾기 " + s.Unit + "번(실패 " + s.UnitFail + ") " + Ms(s.UnitTicks).ToString("0.0") + " ms";
            return t;
        }

        public static Dictionary<string, object> LastSecond = new Dictionary<string, object>();

        public static void Second(int frameAvg, int frameMax, int zombies, bool paused)
        {
            var s = Interlocked.Exchange(ref _sec, new Acc());
            Add(_min, s);
            LastSecond = new Dictionary<string, object> {
                { "fix", Enabled }, { "installed", Installed == 1 }, { "frameAvg", frameAvg }, { "frameMax", frameMax }, { "paused", paused },
                { "queries", s.Q }, { "no", s.No }, { "adds", s.Adds }, { "removes", s.Removes }, { "rebuilds", s.Rebuilds }, { "rebuildMs", Math.Round(Ms(s.RebuildTicks), 2) },
                { "orig", s.Orig }, { "origFail", s.OrigFail }, { "origMs", Math.Round(Ms(s.OrigTicks), 2) }, { "unknown", s.Unknown }, { "mismatch", s.Mismatch },
                { "unit", s.Unit }, { "unitFail", s.UnitFail }, { "unitMs", Math.Round(Ms(s.UnitTicks), 2) }, { "world", s.World }, { "worldMs", Math.Round(Ms(s.WorldTicks), 2) } };
            long now = Clock.Ms;
            if (frameAvg >= SlowMs && !paused && now - _slowLogAt >= 3000)
            {
                _slowLogAt = now;
                Log.Info("slow second: avg " + frameAvg + " ms max " + frameMax + " ms · zombies " + zombies + " · " + Text(s) + " · " + PerfProbe.Text(PerfProbe.Last));
            }
            if (_minAt == 0) _minAt = now;
            if (now - _minAt >= 60000)
            {
                _minAt = now;
                var m = _min; _min = new Acc();
                if (m.Q > 0 || m.Mismatch > 0 || Ms(m.UnitTicks) > 6000) Log.Info(Text(m));
            }
        }

        public static void OnLevelUnloaded() { Interlocked.Increment(ref _ver); lock (Gate) { _builtGrid = null; } }

        public static Dictionary<string, object> Diag(Dictionary<string, object> req)
        {
            object on;
            if (req != null && req.TryGetValue("on", out on) && on is bool && (bool)on != Enabled) { Enabled = (bool)on; Interlocked.Increment(ref _ver); Log.Info("energy fix: " + (Enabled ? "on" : "off") + " (player)"); }
            int total = 0, enabled = 0, connected = 0, painting = 0;
            var towers = new List<object>();
            try
            {
                foreach (var c in DXGame.Current.ComponentsOfType<CEnergyTransferer>())
                {
                    total++;
                    var e = c.Entity as ZX.Entities.ZXEntity; var b = c.Entity as Building;
                    bool en = e != null && e.Enabled, co = b == null || b.ConnectedToEnergy;
                    if (en) enabled++; if (co) connected++; if (c._EnergyEnabled) painting++;
                    if (towers.Count < 400 && e != null) towers.Add(new Dictionary<string, object> { { "x", e.Cell.X }, { "y", e.Cell.Y }, { "r", e.get_Params() != null ? e.get_Params().EnergyTransferRadius : 0 }, { "on", en }, { "link", co }, { "paint", c._EnergyEnabled }, { "cc", e is CommandCenter } });
                }
            }
            catch (Exception ex) { Log.Error("energy diag", ex); }
            return new Dictionary<string, object> { { "type", "energy" }, { "fix", Enabled }, { "installed", Installed == 1 }, { "transferers", total }, { "enabled", enabled }, { "connected", connected }, { "painting", painting }, { "last", LastSecond }, { "towers", towers } };
        }
    }
}
