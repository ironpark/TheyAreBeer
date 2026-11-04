using System;
using System.Drawing;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using DXVision;
using DXVision.GUI;
using HarmonyLib;
using ZX;
using ZX.Components;
using ZX.Entities;
using ZX.GUI;
using ZX.Systems;

namespace Billions
{

    public static class LifeMarkers
    {
        public static int Mode = 0;
        public static float DotWidthFactor = 0.45f;

        public static float DotHeight = 0f, BigHeight = 0f, HeightRatio = 2f / 3f;
        public static float BigWidthFactor = 1.3f;
        static float Dot(int styleH) { return DotHeight > 0 ? DotHeight : Math.Max(2f, (float)Math.Round(styleH * HeightRatio)); }
        static float Big(int styleH) { return BigHeight > 0 ? BigHeight : Math.Max(Dot(styleH) + 1f, styleH); }
        static int StyleH() { try { var st = ZXPalette.MeterStyle_Life_Enemy; return st != null ? st.Height : 7; } catch { return 7; } }
        public static int Installed;

        public static void Install(Harmony h)
        {
            try
            {
                var up = AccessTools.Method(typeof(OMeter), "Update");
                var life = AccessTools.Method(typeof(CLife), "OnUpdateSceneObjectParallel");
                if (up == null || life == null) { Log.Info("HOOK MISSING (optional): 가벼운 좀비 표시 — 원작 체력바 그대로"); return; }
                h.Patch(up, null, new HarmonyMethod(typeof(LifeMarkers), nameof(UpdatePost)));
                h.Patch(life, new HarmonyMethod(typeof(LifeMarkers), nameof(LifePre)));
                Installed = 1;
#if !PERFMOD
                InstallBatch(h);
#endif
                Log.Info("hook: OMeter.Update (postfix, 좀비 체력바 = 작은 막대 하나) · CLife.OnUpdateSceneObjectParallel (prefix, Mode 1)");
            }
            catch (Exception e) { Installed = 0; Log.Error("life markers install", e); }
        }

        static bool IsZombieMeter(OMeter m) { var em = m as OEntityMeter; return em != null && em._Entity is Zombie; }

        static void UpdatePost(OMeter __instance)
        {
            if ((Mode != 2 && Mode != 3) || __instance == null || !IsZombieMeter(__instance)) return;
            try
            {
                __instance.ShaderEffect = null;
                if (__instance._Style != null && __instance._Style.FrontImage != null) __instance.Image = __instance._Style.FrontImage;

                var em = (OEntityMeter)__instance;
                float zf = em._LastCameraWidth > 0 ? (float)DXMath.Limit(0.2, 30.0 / em._LastCameraWidth, 1.5) : 1f;
                bool big = em._Entity is ZombieGiant || em._Entity is ZombieMutant;
                __instance.Width = Math.Max(2f, em._DefaultWidth * zf * (big ? BigWidthFactor : DotWidthFactor));
                int sh = __instance._Style != null ? __instance._Style.Height : StyleH();
                __instance.Height = big ? Big(sh) : Dot(sh);
                if (__instance._OFront != null) __instance._OFront.Visible = false;
                if (__instance._OEnd != null) __instance._OEnd.Visible = false;
            }
            catch { }
        }

        static bool LifePre(CLife __instance)
        {
            if (Mode == 3 && _batchOk) return BatchPre(__instance);
            if (Mode != 1 || !CLife.get_DisplayAllLifeMeters() || __instance == null) return true;
            var e = __instance.Entity; if (!(e is Zombie)) return true;
            var gl = ZXSystem_GameLevel.get_Current(); if (gl != null && gl.MouseEntity == e) return true;
            var m = __instance._OLifeMeter;
            if (m != null && m.Scene != null) DXGame.Current.InvokeOnFinishFrame(delegate { var mm = __instance._OLifeMeter; if (mm != null) mm.RemoveFromScene(); });
            return false;
        }

        static bool _batchOk; static OEntityMeter _anchor; static DXEntity[] _marks = new DXEntity[4096]; static float[] _mx = new float[4096], _my = new float[4096], _mw = new float[4096], _mh = new float[4096]; static int _markN; static volatile bool _overflow;
        static FieldInfo _fVerts, _fPending, _fLast; static MethodInfo _mFlush; static bool _batchErrLogged, _inFlush; static int _frame, _appendedFrame = -1;
        public static int LastMarks, LastBatched; static float _dotH = 5f, _bigH = 7f;
        public static bool BatchActive { get { return Mode == 3 && _batchOk; } }
        const int Stride = Renderer.VertexSize / 4;   // floats per vertex

        static void InstallBatch(Harmony h)
        {
            try
            {
                _fVerts = Renderer.VerticesField; _fPending = Renderer.Field(Renderer.Pending); _fLast = Renderer.Field(Renderer.LastDrawn);
                _mFlush = Renderer.Method("DrawPendingObjects");
                var onr = AccessTools.Method(typeof(OEntityMeter), "OnRender");
                if (_fVerts == null || _fPending == null || _fLast == null || _mFlush == null || onr == null) { Log.Info("life markers: 렌더러 내부 없음 — Mode 2"); return; }
                int sz = Marshal.SizeOf(Renderer.VertexType);
                if (sz != Renderer.VertexSize) { Log.Info("life markers: 정점 크기 " + sz + " ≠ " + Renderer.VertexSize + " — Mode 2"); return; }

                h.Patch(_mFlush, new HarmonyMethod(typeof(LifeMarkers), nameof(FlushPre)));
                h.Patch(onr, new HarmonyMethod(typeof(LifeMarkers), nameof(AnchorRenderPre)));
                _batchOk = true; Log.Info("hook: 좀비 위치 표시 묶어 그리기(Mode 3) — DrawPendingObjects prefix · OEntityMeter.OnRender prefix");
            }
            catch (Exception e) { _batchOk = false; Log.Error("life markers batch install", e); }
        }

        static bool BatchPre(CLife life)
        {
            if (life == null) return true;
            var e = life.Entity; if (!(e is Zombie)) return true;
            try
            {
                bool flag = false; var so = e.SceneObject;
                if (life.IsAlive && so != null && so.Opacity != 0f && !ZXGame.CinematicMode && DXGame.Current.InputEnabled)
                {
                    var gl = ZXSystem_GameLevel.get_Current();
                    if ((gl != null && gl.MouseEntity == e) || CLife.get_DisplayAllLifeMeters())
                    {
                        var cs = (e as ZXEntity).get_CSelectable();
                        if (cs != null && cs.Enabled && SysMiniMapUpdater.Current.IsCellWatched(e.Cell) && SysMiniMapUpdater.Current.IsCellExplored(e.Cell) && !DXModalScreen.IsInModal && !ZXSystem_GameLevel.DrawingMiniMap)
                            flag = so.IsPixelAreaVisible && so.Opacity > 0.2f;
                    }
                }
                var m = life._OLifeMeter;
                if (flag)
                {

                    int i = Interlocked.Increment(ref _markN) - 1; var arr = _marks;
                    if (i < arr.Length && i < _mx.Length)
                    {
                        PointF sp = DXWorldGrid.ScenePointFromWorldPoint(e.Position); sp.Y -= (e as ZXEntity).get_Params().Height;
                        bool big = e is ZombieGiant || e is ZombieMutant; int def = m != null ? m._DefaultWidth : 10;
                        _mx[i] = sp.X; _my[i] = sp.Y; _mw[i] = def * (big ? BigWidthFactor : DotWidthFactor); _mh[i] = big ? _bigH : _dotH; arr[i] = e;
                    }
                    else _overflow = true;
                }
                if (m != null && m != _anchor && m.Scene != null) DXGame.Current.InvokeOnFinishFrame(delegate { var mm = life._OLifeMeter; if (mm != null && mm != _anchor) mm.RemoveFromScene(); });
            }
            catch { }
            return false;
        }

        public static void FrameStart()
        {
            if (Installed != 1) return;
            if (Mode != 3 || !_batchOk) { if (_anchor != null) RemoveAnchor(); return; }
            LastMarks = Math.Min(_markN, _marks.Length); _frame++;
            { int sh = StyleH(); _dotH = Dot(sh); _bigH = Big(sh); }
            if (_overflow) { _overflow = false; int nn = _marks.Length * 2; _marks = new DXEntity[nn]; _mx = new float[nn]; _my = new float[nn]; _mw = new float[nn]; _mh = new float[nn]; }
            _markN = 0;
            try
            {
                var grid = DXIsometricGridObject.Current; if (grid == null) return;
                if (_anchor == null)
                {
                    Zombie z0 = null; foreach (var z in DXGame.Current.EntitiesOfType<Zombie>()) { z0 = z; break; }
                    if (z0 == null) return;
                    _anchor = new OEntityMeter(z0, ZXPalette.MeterStyle_Life_Enemy, 0);
                }
                if (_anchor.Parent != grid)
                {
                    var a = _anchor;
                    DXGame.Current.InvokeOnFinishFrame(delegate
                    {
                        var g = DXIsometricGridObject.Current;
                        if (a.Parent != null && a.Parent != g) a.RemoveFromParent();
                        if (a.Parent == null && g != null) g.AddObject(a);
                    });
                }
                _anchor.Visible = true;
            }
            catch (Exception e) { BatchFail("frame", e); }
        }
        static void RemoveAnchor()
        {
            var a = _anchor; _anchor = null; if (a == null) return;
            try { DXGame.Current.InvokeOnFinishFrame(delegate { a.RemoveFromScene(); }); } catch { }
        }
        static void BatchFail(string where, Exception e)
        {
            _batchOk = false; RemoveAnchor();
            if (!_batchErrLogged) { _batchErrLogged = true; Log.Error("life markers batch " + where + " — Mode 2 로", e); }
        }

        static bool AnchorRenderPre(OEntityMeter __instance)
        {
            if (__instance == null || __instance != _anchor) return true;
            int n = Math.Min(_markN, _marks.Length);
            if (n <= 0 || _marks[0] == null) { __instance.Visible = false; return false; }
            var e0 = _marks[0]; __instance._Entity = e0;
            try { var l0 = e0.GetComponent<CLife>(); if (l0 != null && l0._OLifeMeter != null) __instance._DefaultWidth = l0._OLifeMeter._DefaultWidth; } catch { }
            __instance._LastCameraWidth = 0;
            return true;
        }

        static void FlushPre(object __instance)
        {
            if (_inFlush || !_batchOk) return;
            var a = _anchor; if (a == null || _appendedFrame == _frame) return;
            try { if (!ReferenceEquals(_fLast.GetValue(__instance), a)) return; } catch { return; }
            _appendedFrame = _frame; _inFlush = true;
            try { Append(__instance, a); } finally { _inFlush = false; }
        }
        static unsafe void Append(object __instance, OEntityMeter a)
        {
            int n = Math.Min(_markN, _marks.Length); if (n <= 1) { LastBatched = 0; return; }
            try
            {
                var grid = DXIsometricGridObject.Current; if (grid == null) return;
                int pending = (int)_fPending.GetValue(__instance); if (pending <= 0) return;
                var verts = (Array)_fVerts.GetValue(__instance); int cap = Math.Min(10000, verts.Length / 6);
                float camW = (int)DXScene.Current.CameraArea.Width; float zf = camW > 0 ? (float)DXMath.Limit(0.2, 30.0 / camW, 1.5) : 1f;
                float aw = a.Width, ah = a.Height; if (aw <= 0 || ah <= 0) return;

                PointF o0 = grid.ObjectPointToPixelPoint(new PointF(0f, 0f), true), o1 = grid.ObjectPointToPixelPoint(new PointF(1000f, 0f), true), o2 = grid.ObjectPointToPixelPoint(new PointF(0f, 1000f), true);
                float m00 = (o1.X - o0.X) / 1000f, m10 = (o1.Y - o0.Y) / 1000f, m01 = (o2.X - o0.X) / 1000f, m11 = (o2.Y - o0.Y) / 1000f;
                float p0x = o0.X + m00 * _mx[0] + m01 * _my[0], p0y = o0.Y + m10 * _mx[0] + m11 * _my[0];
                float* tmp = stackalloc float[6 * Stride];
                var hnd = GCHandle.Alloc(verts, GCHandleType.Pinned); int done = 0;
                try
                {
                    float* v = (float*)hnd.AddrOfPinnedObject();
                    float* av = v + (pending - 1) * 6 * Stride;
                    for (int k = 0; k < 6 * Stride; k++) tmp[k] = av[k];
                    float cx = (tmp[0] + tmp[Stride] + tmp[2 * Stride] + tmp[4 * Stride]) * 0.25f, cy = (tmp[1] + tmp[Stride + 1] + tmp[2 * Stride + 1] + tmp[4 * Stride + 1]) * 0.25f;
                    float ox = cx - p0x, oy = cy - p0y;
                    for (int i = 1; i < n; i++)
                    {
                        if (_marks[i] == null) continue;
                        float w = Math.Max(2f, _mw[i] * zf), hgt = _mh[i];
                        float sx = w / aw, sy = hgt / ah;
                        float px = o0.X + m00 * _mx[i] + m01 * _my[i] + ox, py = o0.Y + m10 * _mx[i] + m11 * _my[i] + oy;
                        if (pending >= cap) { _fPending.SetValue(__instance, pending); _mFlush.Invoke(__instance, null); pending = 0; }
                        float* d = v + pending * 6 * Stride;
                        for (int k = 0; k < 6; k++)
                        {
                            float* s0 = tmp + k * Stride; float* d0 = d + k * Stride;
                            for (int f = 2; f < Stride; f++) d0[f] = s0[f];
                            d0[0] = px + (s0[0] - cx) * sx; d0[1] = py + (s0[1] - cy) * sy;
                        }
                        pending++; done++;
                    }
                }
                finally { hnd.Free(); }
                _fPending.SetValue(__instance, pending); _fLast.SetValue(__instance, a);
                LastBatched = done;
            }
            catch (Exception e) { BatchFail("draw", e); }
        }

        public static string SetMode(int mode)
        {
            Mode = Math.Max(0, Math.Min(3, mode)); int n = 0;
            if (Mode != 3) RemoveAnchor();
            try
            {
                foreach (var z in DXGame.Current.EntitiesOfType<Zombie>())
                {
                    var life = z.GetComponent<CLife>(); var m = life != null ? life._OLifeMeter : null; if (m == null) continue;
                    if (m._OFront != null) m._OFront.Visible = true; if (m._OEnd != null) m._OEnd.Visible = true;
                    m.Update(); n++;
                }
            }
            catch (Exception e) { Log.Error("life markers mode", e); }
            Log.Info("life markers: mode " + Mode + " (" + n + " zombie meters)"); return "mode " + Mode;
        }
    }
}
