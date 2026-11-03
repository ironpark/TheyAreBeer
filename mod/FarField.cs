using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using DXVision;
using HarmonyLib;
using ZX;
using ZX.Components;
using ZX.Entities;

namespace Billions
{

    public static class FarField
    {
        public static bool Enabled;
        public static int FarRadius = 8, NearRadius = 3, MaxSwitchPerStep = 200, CreatePerStep = 600;

        public static float SepFactor = 1.0f, MaxPush = 0.4f; public static int SepPasses = 2;
        public static int Installed;


        sealed class Far { public ZXEntity E; public CMovable M; public PointF Prev, Next, Pending, Vel; public bool Moving; public float Angle; public bool HasAngle; public int Idx = -1; }
        static Zombie[] _zs = new Zombie[0];
        static PointF[] _pts = new PointF[0]; static int[] _cellOf = new int[0], _start = new int[0], _sorted = new int[0], _fill = new int[0];
        static readonly List<ZXEntity> _born = new List<ZXEntity>();
        static int _removeBudget = 20, _createBudget = 200; public static double PostMsLast;

        static bool Outside(float x, float y) { if (_n == 0) return true; int cx = (int)x, cy = (int)y; if (cx < 0 || cy < 0 || cx >= _n || cy >= _n) return false; return _dist[cx * _n + cy] > NearRadius; }
        static readonly Dictionary<DXEntity, Far> _far = new Dictionary<DXEntity, Far>();
        static Far[] _snap = new Far[0];

        static readonly System.Collections.Concurrent.ConcurrentQueue<ZXEntity> _toFar = new System.Collections.Concurrent.ConcurrentQueue<ZXEntity>(), _toNear = new System.Collections.Concurrent.ConcurrentQueue<ZXEntity>();
        static long _classifyAt;
        static byte[] _dist = new byte[0]; static int _n;

        static byte[] _hot; static int _hotN; static long _hotAt;
        static Task _classTask; static AccessTools.FieldRef<DXGame, List<DXEntity>> _entities;
        static int _step;
        [ThreadStatic] static Far _cur;
        static Task _moveTask;

        public static int FarCount, LastToFar, LastToNear; public static double MoveMs, FrameMs, ClassifyMs, GridMs, P1Ms, SepMs;

        public static void Install(Harmony h)
        {
            try
            {
                var mt = AccessTools.Method(typeof(CMovable), "MoveTo", new[] { typeof(PointF) });
                var cb = AccessTools.Method(typeof(DXEntity), "CreateBody");
                if (mt == null || cb == null) { Log.Info("HOOK MISSING (optional): 먼 좀비 이동 — 원작 물리 그대로"); return; }
                h.Patch(mt, new HarmonyMethod(typeof(FarField), nameof(MoveToPre)));
                h.Patch(cb, new HarmonyMethod(typeof(FarField), nameof(CreateBodyPre)));
                Installed = 1;
                Log.Info("hook: CMovable.MoveTo (prefix, 몸체 없는 먼 좀비는 모드가 움직임 — 기본 꺼짐)");
            }
            catch (Exception e) { Installed = 0; Log.Error("far field install", e); }
            try
            {
                _entities = AccessTools.FieldRefAccess<DXGame, List<DXEntity>>("_Entities");
                var gn = AccessTools.Method(typeof(DXWorldGrid), "GetNearestVisibleEntityAtPoint", new[] { typeof(PointF), typeof(float), typeof(uint), typeof(uint), typeof(float) });
                var wr = AccessTools.Method(typeof(CEntityWatcher), "GetWallInRoute", new[] { typeof(PointF), typeof(int) });
                if (gn == null || wr == null) { Log.Info("HOOK MISSING (optional): 먼 좀비 적 찾기 건너뛰기 — 원작 그대로"); return; }
                h.Patch(gn, new HarmonyMethod(typeof(FarField), nameof(NearestPre)), new HarmonyMethod(typeof(FarField), nameof(NearestPost)));
                h.Patch(wr, new HarmonyMethod(typeof(FarField), nameof(WallPre)), new HarmonyMethod(typeof(FarField), nameof(WallPost)));
                AiInstalled = 1;
                Log.Info("hook: DXWorldGrid.GetNearestVisibleEntityAtPoint · CEntityWatcher.GetWallInRoute (prefix, 주변에 사람·성벽이 없으면 원작과 같은 「없음」을 바로)");
            }
            catch (Exception e) { AiInstalled = 0; Log.Error("far ai install", e); }
        }


        public static bool AiSkip = true; public static int AiMargin = 6, AiInstalled;
        public static long AiTotal, AiSkipped, WallTotal, WallSkipped;

        public static bool AiVerify; public static long AiWrong, WallWrong, AiChecked;
        [ThreadStatic] static bool _expectNull, _expectNullWall;
        static void NearestPost(DXEntity __result) { if (!_expectNull) return; _expectNull = false; Interlocked.Increment(ref AiChecked); if (__result != null) { Interlocked.Increment(ref AiWrong); } }
        static void WallPost(DeffensiveStructure __result) { if (!_expectNullWall) return; _expectNullWall = false; if (__result != null) Interlocked.Increment(ref WallWrong); }
        static bool HotFar(int cx, int cy, int need)
        {
            var hot = _hot; int n = _hotN;
            if (hot == null || n <= 0 || n != DXWorldGrid.NCells || Clock.Ms - _hotAt > 2500) return false;
            if (cx < 0 || cy < 0 || cx >= n || cy >= n) return false;
            return hot[cx * n + cy] > need;
        }
        static bool NearestPre(PointF p, float radius, uint mask, ref DXEntity __result)
        {
            if (!AiSkip || mask != 32768U) return true;
            Interlocked.Increment(ref AiTotal);
            if (!HotFar((int)p.X, (int)p.Y, (int)Math.Ceiling(radius) + 1 + AiMargin)) return true;
            Interlocked.Increment(ref AiSkipped);
            if (AiVerify) { _expectNull = true; return true; }
            __result = null; return false;
        }
        static bool WallPre(CEntityWatcher __instance, int radius, ref DeffensiveStructure __result)
        {
            if (!AiSkip) return true;
            Interlocked.Increment(ref WallTotal);
            var e = __instance.Entity; if (e == null) return true;
            var c = e.Cell;
            if (!HotFar(c.X, c.Y, Math.Max(1, radius) + (int)Math.Ceiling(e.Width) + 2 + 2)) return true;
            Interlocked.Increment(ref WallSkipped);
            if (AiVerify) { _expectNullWall = true; return true; }
            __result = null; return false;
        }
        public static void TakeAiStats(out long total, out long skipped, out long wtotal, out long wskipped)
        { total = Interlocked.Exchange(ref AiTotal, 0); skipped = Interlocked.Exchange(ref AiSkipped, 0); wtotal = Interlocked.Exchange(ref WallTotal, 0); wskipped = Interlocked.Exchange(ref WallSkipped, 0); }


        static bool CreateBodyPre(DXEntity __instance)
        {
            if (!Enabled || _n == 0) return true;
            var z = __instance as Zombie; if (z == null || z.Disposed) return true;
            var c = z.Cell; if (c.X < 0 || c.Y < 0 || c.X >= _n || c.Y >= _n || _dist[c.X * _n + c.Y] < FarRadius) return true;
            try { var life = z.get_CLife(); if (life != null && !life.IsAlive) return true; } catch { }
            z.PhysicsDisabled = true;
            lock (_born) _born.Add(z);
            return false;
        }


        static bool MoveToPre(CMovable __instance, PointF destiny, ref bool __result)
        {
            var f = _cur; if (f == null || !ReferenceEquals(f.M, __instance)) return true;
            var e = f.E; var pos = f.Next;
            if (destiny == pos) { __result = true; return false; }
            float speed = __instance._CurrentSpeed, num;
            if (__instance._BellMovingFactor > 0f)
            {
                var sp = e.get_CSprite(); num = sp != null ? sp._CurrentFactorFrame : 0.5f; float bell = __instance._BellMovingFactor;
                num = num > bell ? (1f - num) / (1f - bell) : num / bell;
                num *= num * 2.5f * speed; num = Math.Max(speed / 2f, num);
            }
            else num = speed;
            num *= 1.3f;
            float dx = destiny.X - pos.X, dy = destiny.Y - pos.Y, l2 = dx * dx + dy * dy;
            if (l2 <= num * num / 144f) { __instance._RemoveFirstPathCell = true; f.Vel = new PointF(dx, dy); }
            else { float l = (float)Math.Sqrt(l2); f.Vel = new PointF(dx / l * num, dy / l * num); }
            f.Moving = true;
            try { f.Angle = e.AngleToLookAt(destiny); f.HasAngle = true; } catch { }
            __result = true; return false;
        }


        public static void BeforeStep()
        {
            if (Installed != 1) return;
            if (!Enabled && _far.Count == 0) return;
            if (_moveTask != null) return;
            try
            {
                Watchdog.Phase = "switch"; ApplySwitches();
                var snap = _far.Values.ToArray(); _snap = snap; FarCount = snap.Length;

                if (snap.Length > 0) { _moveSnap = snap; _moveTask = Task.Factory.StartNew(() => { try { Move(snap); } catch (Exception e) { Log.Error("far move", e); } }); }
            }
            catch (Exception e) { Log.Error("far field before", e); }
            Watchdog.Phase = "step";
        }

        static Far[] _moveSnap, _ready;
        public static void WaitMove()
        {
            var t = _moveTask; if (t == null) return;
            Watchdog.Phase = "wait";
            bool done; try { done = t.Wait(250); } catch { done = true; }
            if (!done) return;
            _ready = _moveSnap; _moveSnap = null; _moveTask = null;
        }

        public static void AfterStep()
        {
            var ready = _ready; if (ready == null) return; _ready = null;
            Watchdog.Phase = "after";
            foreach (var f in ready)
            {
                var e = f.E; if (e.Disposed || e.Body != null) continue;
                f.Prev = f.Next; f.Next = f.Pending; e.LastPosition = e.Position; e.Position = f.Next;
                if (f.HasAngle) { try { f.M.TargetAngle = f.Angle; } catch { } }
            }
        }

        static void Move(Far[] snap)
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            const float dt = 0.0833333358f;

            var zl = _zs; int n = DXWorldGrid.NCells, cnt = zl.Length;
            if (_pts.Length < cnt) { _pts = new PointF[cnt * 2]; _cellOf = new int[cnt * 2]; _sorted = new int[cnt * 2]; }
            if (_start.Length != n * n + 1) { _start = new int[n * n + 1]; _fill = new int[n * n]; }
            Chunks(snap.Length, k => snap[k].Idx = -1);
            Chunks(cnt, i =>
            {
                var z = zl[i]; PointF p;
                try { if (z == null || z.Disposed) p = new PointF(-1, -1); else { Far ff; if (_far.TryGetValue(z, out ff)) ff.Idx = i; p = z.Position; } } catch { p = new PointF(-1, -1); }
                _pts[i] = p;
            });
            GridMs = sw.Elapsed.TotalMilliseconds;

            Chunks(snap.Length, i =>
            {
                var f = snap[i]; var e = f.E;
                try
                {
                    if (e.Disposed || e.Body != null) { f.Pending = f.Next; return; }
                    var pos = f.Next; f.Pending = pos; f.Moving = false; f.HasAngle = false; f.Vel = PointF.Empty;
                    var m = f.M;
                    if (m != null && m.Enable && m.TargetPosition != null) { _cur = f; try { m.MoveToDestiny(); } finally { _cur = null; } }
                    float nx = pos.X + f.Vel.X * dt, ny = pos.Y + f.Vel.Y * dt;
                    if (Free(nx, ny) && Outside(nx, ny)) f.Pending = new PointF(nx, ny);
                }
                catch { }
            });
            P1Ms = sw.Elapsed.TotalMilliseconds - GridMs;

            for (int pass = 0; pass < SepPasses; pass++)
            {
                Watchdog.Phase = "sep" + pass;
                Chunks(snap.Length, k => { var f = snap[k]; if (f.Idx >= 0) _pts[f.Idx] = f.Pending; });
                BuildGrid(cnt, n);
                Chunks(snap.Length, k => Separate(snap[k], n));
            }
            MoveMs = sw.Elapsed.TotalMilliseconds; SepMs = MoveMs - GridMs - P1Ms;
        }


        static void BuildGrid(int cnt, int n)
        {
            Chunks(cnt, i => { var q = _pts[i]; int qx = (int)q.X, qy = (int)q.Y; _cellOf[i] = (qx < 0 || qy < 0 || qx >= n || qy >= n) ? -1 : qx * n + qy; });
            Array.Clear(_start, 0, _start.Length);
            for (int i = 0; i < cnt; i++) { int c = _cellOf[i]; if (c >= 0) _start[c + 1]++; }
            for (int c = 0; c < n * n; c++) _start[c + 1] += _start[c];
            Array.Copy(_start, _fill, n * n);
            for (int i = 0; i < cnt; i++) { int c = _cellOf[i]; if (c >= 0) _sorted[_fill[c]++] = i; }
        }

        static void Chunks(int count, Action<int> body)
        {
            if (count < 2048) { for (int i = 0; i < count; i++) body(i); return; }
            int chunk = 512, chunks = (count + chunk - 1) / chunk, next = -1;
            int workers = Math.Max(1, Math.Min(chunks, Environment.ProcessorCount - 4));
            Parallel.For(0, workers, new ParallelOptions { MaxDegreeOfParallelism = workers }, w =>
            {
                int c;
                while ((c = System.Threading.Interlocked.Increment(ref next)) < chunks)
                {
                    int a = c * chunk, b = Math.Min(count, a + chunk);
                    for (int i = a; i < b; i++) body(i);
                }
            });
        }

        static void Separate(Far f, int n)
        {
            var e = f.E;
            try
            {
                if (e.Disposed || e.Body != null) return;
                var pos = f.Pending;
                {
                    float nx = pos.X, ny = pos.Y;

                    float r = Math.Max(0.2f, e.Radius) * 2f * SepFactor, px = 0, py = 0; int cx0 = (int)pos.X, cy0 = (int)pos.Y;
                    for (int ax = cx0 - 1; ax <= cx0 + 1; ax++) for (int ay = cy0 - 1; ay <= cy0 + 1; ay++)
                    {
                        if (ax < 0 || ay < 0 || ax >= n || ay >= n) continue; int c = ax * n + ay;
                        for (int k = _start[c]; k < _start[c + 1]; k++)
                        {
                            int qi = _sorted[k]; if (qi == f.Idx) continue;
                            var q = _pts[qi]; float ddx = pos.X - q.X, ddy = pos.Y - q.Y, d2 = ddx * ddx + ddy * ddy;
                            if (d2 <= 1e-8f) { px += (f.GetHashCode() & 1) == 0 ? 0.05f : -0.05f; continue; }
                            if (d2 >= r * r) continue; float d = (float)Math.Sqrt(d2), w = (r - d) / d * 0.5f; px += ddx * w; py += ddy * w;
                        }
                    }
                    float pl = (float)Math.Sqrt(px * px + py * py); if (pl > MaxPush) { px *= MaxPush / pl; py *= MaxPush / pl; }
                    float tx = nx + px, ty = ny + py;
                    if (Free(tx, ty) && Outside(tx, ty)) f.Pending = new PointF(tx, ty);
                    else if (Free(tx, ny) && Outside(tx, ny)) f.Pending = new PointF(tx, ny);
                    else if (Free(nx, ty) && Outside(nx, ty)) f.Pending = new PointF(nx, ty);
                }
            }
            catch { }
        }
        static bool Free(float x, float y) { int cx = (int)x, cy = (int)y; return cx >= 0 && cy >= 0 && cx < DXWorldGrid.NCells && cy < DXWorldGrid.NCells && DXWorldGrid.IsCellAvailable(cx, cy); }


        static void ClassifyStart()
        {
            if (_classTask != null && !_classTask.IsCompleted) return;
            int n = DXWorldGrid.NCells; if (n <= 0 || _entities == null) return;
            DXEntity[] all;
            try { var g = DXGame.Current; lock (DXGame.SyncLogic) all = _entities(g).ToArray(); } catch { return; }
            _classTask = Task.Factory.StartNew(() =>
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                try { ClassifyBg(all, n); } catch (Exception e) { Log.Error("far classify", e); }
                ClassifyMs = sw.Elapsed.TotalMilliseconds;
            });
        }


        static void ClassifyBg(DXEntity[] all, int n)
        {
            var bas = new byte[n * n]; var hot = new byte[n * n];
            for (int i = 0; i < bas.Length; i++) { bas[i] = 255; hot[i] = 255; }
            const uint HotMask = 32768U | 8192U;
            var zs = new List<Zombie>(all.Length);
            foreach (var e in all)
            {
                if (e == null) continue;
                try
                {
                    if (e.Disposed || !e.InScene) continue;
                    var z = e as Zombie; if (z != null) { zs.Add(z); continue; }
                    bool isHot = (e._BodyCategoryMask & HotMask) != 0;
                    var ze = e as ZXEntity; bool mine = ze != null && ze.Team == ZXTeamType.Player && (e is Structure || e is Unit);
                    if (!isHot && !mine) continue;
                    Rectangle a; if (e.IsOneCellSize) { var c = e.Cell; a = new Rectangle(c.X, c.Y, 1, 1); } else a = e.CellArea;
                    for (int x = Math.Max(0, a.Left); x < Math.Min(n, a.Right); x++) for (int y = Math.Max(0, a.Top); y < Math.Min(n, a.Bottom); y++) { int i = x * n + y; if (mine) bas[i] = 0; if (isHot) hot[i] = 0; }
                }
                catch { }
            }
            Chebyshev(bas, n); Chebyshev(hot, n);
            bool en = Enabled; int nearR = NearRadius, farR = FarRadius;
            var toFar = new List<Zombie>(); var toNear = new List<Zombie>();
            foreach (var z in zs)
            {
                try
                {
                    var life = z.get_CLife(); if (life == null || !life.IsAlive) continue;
                    var c = z.Cell; if (c.X < 0 || c.Y < 0 || c.X >= n || c.Y >= n) continue;
                    int d = bas[c.X * n + c.Y];
                    bool ours = z.Body == null && z.PhysicsDisabled;
                    if (!en) { if (ours) toNear.Add(z); continue; }

                    if (ours && d <= nearR + 2 && Active(z)) toNear.Add(z);
                    else if (!ours && d >= farR && z.Body != null && !z.PhysicsDisabled && z.get_CMovable() != null) toFar.Add(z);
                }
                catch { }
            }

            _zs = zs.ToArray(); _dist = bas; _n = n; _hot = hot; _hotN = n; _hotAt = Clock.Ms;
            ZXEntity dummy; while (_toFar.TryDequeue(out dummy)) { } while (_toNear.TryDequeue(out dummy)) { }
            foreach (var z in toNear) _toNear.Enqueue(z); foreach (var z in toFar) _toFar.Enqueue(z);
        }

        static bool Active(Zombie z)
        {
            try
            {
                if (z.AttackCommandCenter) return true;
                var w = z.get_CEntityWatcher(); if (w != null && w.get_NearestEntityEnemy() != null) return true;
                var b = z.get_CBehaviour(); if (b != null && b.Behaviour != null && b.Behaviour.Data != null && b.Behaviour.Data.CellActivity != null) return true;
            }
            catch { return true; }
            return false;
        }


        static void Chebyshev(byte[] a, int n)
        {
            for (int x = 0; x < n; x++) for (int y = 0; y < n; y++)
            {
                int i = x * n + y, v = a[i];
                if (x > 0) { v = Math.Min(v, a[i - n] + 1); if (y > 0) v = Math.Min(v, a[i - n - 1] + 1); if (y < n - 1) v = Math.Min(v, a[i - n + 1] + 1); }
                if (y > 0) v = Math.Min(v, a[i - 1] + 1);
                a[i] = (byte)Math.Min(255, v);
            }
            for (int x = n - 1; x >= 0; x--) for (int y = n - 1; y >= 0; y--)
            {
                int i = x * n + y, v = a[i];
                if (x < n - 1) { v = Math.Min(v, a[i + n] + 1); if (y > 0) v = Math.Min(v, a[i + n - 1] + 1); if (y < n - 1) v = Math.Min(v, a[i + n + 1] + 1); }
                if (y < n - 1) v = Math.Min(v, a[i + 1] + 1);
                a[i] = (byte)Math.Min(255, v);
            }
        }
        static void ApplySwitches()
        {

            List<DXEntity> gone = null;
            foreach (var kv in _far) { var gk = kv.Key; if (gk.Disposed || !gk.InScene || (kv.Value.E.get_CLife() != null && !kv.Value.E.get_CLife().IsAlive)) (gone ?? (gone = new List<DXEntity>())).Add(gk); }
            if (gone != null) foreach (var gk in gone) _far.Remove(gk);

            lock (_born) { foreach (var bz in _born) if (!bz.Disposed && !_far.ContainsKey(bz)) _far[bz] = new Far { E = bz, M = bz.get_CMovable(), Prev = bz.Position, Next = bz.Position, Pending = bz.Position }; _born.Clear(); }

            PostMsLast = PhysicsTime.PostMs;
            if (PostMsLast > 8) _removeBudget = Math.Max(0, _removeBudget / 2); else if (PostMsLast < 2) _removeBudget = Math.Min(MaxSwitchPerStep, _removeBudget + 10);

            _createBudget = CreatePerStep;
            int k = 0;
            ZXEntity z;
            while (k < _createBudget && _toNear.TryDequeue(out z))
            {
                Far f; if (!_far.TryGetValue(z, out f)) continue;
                _far.Remove(z); k++;
                if (z.Disposed) continue;
                z.PhysicsDisabled = false; z.CreateBody();
            }
            LastToNear = k; k = 0;
            while (k < _removeBudget && _toFar.TryDequeue(out z))
            {
                if (z.Disposed || z.Body == null || _far.ContainsKey(z)) continue;
                var f = new Far { E = z, M = z.get_CMovable(), Prev = z.Position, Next = z.Position, Pending = z.Position };
                z.DeleteBodySafe(); z.PhysicsDisabled = true; _far[z] = f; k++;
            }
            LastToFar = k;
        }


        public static void Frame()
        {
            if ((Installed == 1 || AiInstalled == 1) && (Enabled || _snap.Length > 0 || AiSkip) && Clock.Ms - _classifyAt >= 1000) { _classifyAt = Clock.Ms; try { ClassifyStart(); } catch (Exception e) { Log.Error("far classify", e); } }
            var snap = _snap; if (snap.Length == 0) return;
            double span = DXPhysicWorld.FactorTimeForInterpolation;
            float t = (float)((DXGame.Current.GameTime - 83.333333333333329 - DXPhysicWorld.TimeLastBodyPosition) * span);
            if (t < 0f) t = 0f; if (t > 1f) t = 1f;
            float maxc = DXWorldGrid.NCells - 0.01f;

            var sw = System.Diagnostics.Stopwatch.StartNew();
            lock (DXGame.SyncLogic)
            {
                for (int i = 0; i < snap.Length; i++)
                {
                    var f = snap[i]; var e = f.E; if (e.Disposed || e.Body != null) continue;
                    try { float x = f.Prev.X + (f.Next.X - f.Prev.X) * t, y = f.Prev.Y + (f.Next.Y - f.Prev.Y) * t; e.Position = new PointF(Math.Max(0f, Math.Min(maxc, x)), Math.Max(0f, Math.Min(maxc, y))); } catch { }
                }
            }
            FrameMs = sw.Elapsed.TotalMilliseconds;
        }


        static readonly System.Reflection.FieldInfo _vis = AccessTools.Field(typeof(DXEntity), "_VisibilityCheckMode");
        public static string Diag()
        {
            int always = 0, fast = 0, other = 0, bodyless = 0, outb = 0, n = DXWorldGrid.NCells;

            int[] hb = new int[5], hn = new int[5]; var dist = _dist; int dn = _n;
            try
            {
                foreach (var z in DXGame.Current.EntitiesOfType<Zombie>())
                {
                    string m = _vis != null ? Convert.ToString(_vis.GetValue(z)) : "?"; if (m == "Always") always++; else if (m == "Fast") fast++; else other++;
                    if (z.Body == null) bodyless++; var c = z.Cell; if (c.X < 0 || c.Y < 0 || c.X >= n || c.Y >= n) { outb++; continue; }
                    if (dn == n && dist.Length == n * n) { int d = dist[c.X * n + c.Y], k = d <= 2 ? 0 : d <= 5 ? 1 : d <= 9 ? 2 : d <= 19 ? 3 : 4; if (z.Body != null) hb[k]++; else hn[k]++; }
                }
            }
            catch (Exception e) { return "diag error " + e.Message; }
            return "vis always " + always + " fast " + fast + " other " + other + " · bodyless " + bodyless + " · out " + outb + " · far " + _far.Count
                + " · 거리(0-2/3-5/6-9/10-19/20+) 몸체 " + string.Join("/", hb) + " 없음 " + string.Join("/", hn);
        }

        public static void OnLevelUnloaded() { _hot = null; _hotN = 0; _zs = new Zombie[0]; _far.Clear(); _snap = new Far[0]; ZXEntity d; while (_toFar.TryDequeue(out d)) { } while (_toNear.TryDequeue(out d)) { } _moveTask = null; _moveSnap = null; _ready = null; _n = 0; }
        public static string Set(bool? on, int? farR, int? nearR)
        {
            if (on.HasValue) Enabled = on.Value; if (farR.HasValue) FarRadius = Math.Max(4, farR.Value); if (nearR.HasValue) NearRadius = Math.Max(1, Math.Min(FarRadius - 1, nearR.Value));
            string s = "far=" + Enabled + " farR=" + FarRadius + " nearR=" + NearRadius; Log.Info("far field: " + s); return s;
        }
    }
}
