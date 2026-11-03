using System;
using System.Collections.Generic;
using DXVision;
using HarmonyLib;

namespace Billions
{

    public static class ZSort
    {
        public static bool Enabled = true;
        public static int MinChildren = 3000, HighWater = 1500, LowWater = 400; public static double BudgetMs = 1.0;
        public static int Installed;
        public static double LastMs; public static int LastCount;
        static volatile bool _deferred; static bool _finalSort; static int _changes, _highFrames, _lowFrames, _since; public static int Period = 1, Switches;
        public static bool Deferred { get { return _deferred; } }
        static readonly HashSet<DXObject> _dirty = new HashSet<DXObject>();
        static readonly object _lock = new object();
        static DXObject[] _items = new DXObject[0];

        static readonly AccessTools.FieldRef<DXObject, DXObject> _parent = AccessTools.FieldRefAccess<DXObject, DXObject>("_Parent");
        static readonly AccessTools.FieldRef<DXObject, List<DXObject>> _children = AccessTools.FieldRefAccess<DXObject, List<DXObject>>("_Children");
        static readonly AccessTools.FieldRef<DXObject, int> _z = AccessTools.FieldRefAccess<DXObject, int>("_Z");

        public static void Install(Harmony h)
        {
            try
            {
                var setz = AccessTools.PropertySetter(typeof(DXObject), "Z");
                if (setz == null) { Log.Info("HOOK MISSING (optional): 그리기 순서 몰아서 정렬 — 원작 그대로"); return; }
                h.Patch(setz, new HarmonyMethod(typeof(ZSort), nameof(SetZPre)));
                Installed = 1;
                Log.Info("hook: DXObject.Z setter (prefix, 자식 " + MinChildren + "개 넘는 부모의 Z 변경이 프레임당 " + HighWater + "번 넘으면 몰아서 정렬)");
            }
            catch (Exception e) { Installed = 0; Log.Error("zsort install", e); }
        }

        static bool SetZPre(DXObject __instance, int value)
        {
            if (!Enabled && !_deferred) return true;
            var p = _parent(__instance); if (p == null) return true;
            var ch = _children(p); if (ch == null || ch.Count < MinChildren) return true;
            if (_z(__instance) == value) return !_deferred;
            System.Threading.Interlocked.Increment(ref _changes);
            if (!_deferred) return true;
            _z(__instance) = Math.Min(value, 2147483637);
            lock (_lock) _dirty.Add(p);
            return false;
        }

        public static void Frame()
        {
            if (Installed != 1) return;
            int ch = System.Threading.Interlocked.Exchange(ref _changes, 0);
            if (!_deferred)
            {
                if (_finalSort) { _finalSort = false; SortDirty(); }
                if (Enabled && ch >= HighWater) { if (++_highFrames >= 2) { _deferred = true; _since = 0; _lowFrames = 0; Period = 1; Switches++; Log.Info("zsort: 몰아서 정렬로(Z 변경 " + ch + "/프레임)"); } }
                else _highFrames = 0;
                return;
            }
            _since++;
            if (ch < LowWater) _lowFrames++; else _lowFrames = 0;
            bool leave = !Enabled || _lowFrames >= 30;
            if (leave || _since >= Period)
            {
                SortDirty(); _since = 0;
                Period = Math.Max(1, Math.Min(4, (int)Math.Ceiling(LastMs / BudgetMs)));
            }
            if (leave) { _deferred = false; _finalSort = true; _highFrames = 0; Switches++; Log.Info("zsort: 원작 방식으로(Z 변경 " + ch + "/프레임, 마지막 정렬 " + LastMs.ToString("0.0") + " ms)"); }
        }

        static int[] _zs = new int[0];
        public static int CountingSorts, CompareSorts;
        static void SortDirty()
        {
            DXObject[] ps;
            lock (_lock) { if (_dirty.Count == 0) return; ps = new DXObject[_dirty.Count]; _dirty.CopyTo(ps); _dirty.Clear(); }
            var sw = System.Diagnostics.Stopwatch.StartNew(); int total = 0;
            foreach (var p in ps)
            {
                try
                {
                    var ch = _children(p); if (ch == null) continue;
                    int n = ch.Count; if (n < 2) continue;
                    if (_zs.Length < n) { _zs = new int[n * 2]; _items = new DXObject[n * 2]; }
                    bool sorted = true; int prev = int.MinValue;
                    for (int i = 0; i < n; i++)
                    {
                        var o = ch[i]; _items[i] = o;
                        int z = o != null ? _z(o) : int.MinValue;
                        _zs[i] = z; if (z < prev) sorted = false; prev = z;
                    }
                    total += n;
                    if (sorted) { Array.Clear(_items, 0, n); continue; }
                    if (ZOrder.Sort(_zs, _items, n, ch)) CountingSorts++; else CompareSorts++;
                    Array.Clear(_items, 0, n);
                }
                catch (Exception e) { Log.Error("zsort", e); }
            }
            LastMs = sw.Elapsed.TotalMilliseconds; LastCount = total;
        }

        public static string Set(bool on) { Enabled = on; return "zsort " + on; }

        public static void OnLevelUnloaded()
        {
            _deferred = false; _finalSort = false; _highFrames = 0; _lowFrames = 0; _since = 0; Period = 1;
            System.Threading.Interlocked.Exchange(ref _changes, 0);
            lock (_lock) _dirty.Clear();
            Array.Clear(_items, 0, _items.Length);
        }
    }
}
