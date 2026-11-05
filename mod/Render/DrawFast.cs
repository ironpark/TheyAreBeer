using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq.Expressions;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using DXVision;
using HarmonyLib;

namespace Billions
{
    // Two cuts in the game's scene drawing (DXVision.DXScene) and its Direct3D 9 renderer (DXRender_SlimDX).
    //
    // Root list removals (TABPERF_FASTREMOVE=0 turns off). DrawScene's finally removes the lighting object and the post-
    // processing frame from the root's visible-children list (List.Remove: a front-to-back scan with Equals calls over
    // every visible object, ~66,000 with 100,000 zombies on screen) every frame, the lighting object even when it was
    // never added (no global light). Both only get into that list through AddObjectToRootAndUpdateVisibleChildren, which
    // places them by Z near the end. The removals are replaced with: not added since the last removal -> not there
    // (false, like the scan); added -> search from the end, RemoveAt (the same reference, so the same list).
    //
    // Plain sprites (TABPERF_FASTDRAW=0 turns off). The renderer's DrawObject (IL 9 KB, cannot be patched: its exception
    // blocks break Harmony's method rebuild) runs once per sprite. DXScene.DrawObject's calls to it go through Draw
    // below, which handles the steady-state case itself - same batch (no flush), same scissor, a texture sampler that
    // already holds the texture, no mask, soft border, shader effect or blur, stretched image, and no Direct3D state to
    // change (pixel shader, sampler addressing, alpha/gamma constants, blend mode) - by doing exactly what DrawObject does
    // there: refresh the sampler's time, append the six vertices to the batch, and update the batch state. Everything
    // else, and anything unexpected, goes to the original. TABPERF_FASTDRAW_CHECK=1 computes the fast result, lets the
    // original draw, and compares the vertex bytes and the batch state (log).
    //
    // With SceneDraw (the draw pass replaced) neither DrawScene nor DXScene.DrawObject runs: SceneDraw removes its own
    // objects and calls Draw for every sprite, so only the fast path is set up here.
    public static unsafe class DrawFast
    {
        public static bool On { get; private set; }
        static long SlowSprites, Checked, Mismatch;   // check mode

        public static void Install(Harmony h)
        {
            if (!SceneDraw.Installed) InstallRemove(h);
            InstallSprites(h);
        }

        // ---------------- root list removals ----------------
        static readonly HashSet<DXObject> _added = new HashSet<DXObject>();

        static void InstallRemove(Harmony h)
        {
            if (!Settings.Switch("TABPERF_FASTREMOVE", true)) return;
            try
            {
                var ds = AccessTools.Method(typeof(DXScene), "DrawScene");
                var add = AccessTools.Method(typeof(DXScene), "AddObjectToRootAndUpdateVisibleChildren");
                if (ds == null || add == null) { Log.Info("HOOK MISSING (optional): 루트 목록 지우기 — 원작 그대로"); return; }
                h.Patch(add, new HarmonyMethod(typeof(DrawFast), nameof(AddPre)));
                h.Patch(ds, null, null, new HarmonyMethod(typeof(DrawFast), nameof(RemoveTranspiler)));
                Log.Info("hook: DXScene.DrawScene (transpiler, 빛·후처리 물체를 루트 목록에서 끝부터 찾아 지움 " + _removeSites + "곳) · AddObjectToRootAndUpdateVisibleChildren (prefix)");
            }
            catch (Exception e) { Log.Error("fast remove install", e); }
        }

        static int _removeSites;
        static IEnumerable<CodeInstruction> RemoveTranspiler(IEnumerable<CodeInstruction> code)
        {
            return Patching.Redirect(code, typeof(List<DXObject>).GetMethod("Remove", new[] { typeof(DXObject) }),
                typeof(DrawFast).GetMethod(nameof(RemoveTracked), BindingFlags.Static | BindingFlags.NonPublic), out _removeSites);
        }

        static void AddPre(DXObject o) { if (o != null) _added.Add(o); }

        static bool RemoveTracked(List<DXObject> list, DXObject item)
        {
            if (item == null || !_added.Remove(item)) return false;
            for (int i = list.Count - 1; i >= 0; i--) if (ReferenceEquals(list[i], item)) { list.RemoveAt(i); return true; }
            return false;
        }

        // ---------------- plain sprites ----------------
        // The fast path is one method compiled from an expression tree (anonymously hosted, so it reads and writes the
        // renderer's private fields directly, as DrawObject does). fn(r, o, null): draws into the batch and updates its
        // state; fn(r, o, dest): writes only the six vertices into dest (check mode). False (nothing written to the batch
        // or its state): not this case.
        static Type _rt;
        static Func<object, DXObject, Array, bool> _fn;
        static Func<object, int> rPending; static Func<object, object> rLast, rVerts;
        static int _stride, _oTex1;
        static Array _scratch;
        static readonly bool Check = Settings.Switch("TABPERF_FASTDRAW_CHECK", false);
        static long _checkLogAt; static bool _errLogged;

        static void InstallSprites(Harmony h)
        {
            if (!Settings.Switch("TABPERF_FASTDRAW", true)) return;
            try
            {
                if (!DeviceCheck.On) { Log.Info("fast draw: D3D 장치 확인 캐시가 꺼져 있다 — 원작 그대로"); return; }
                _rt = Renderer.Type;
                var dev = Renderer.DeviceType; var tsType = Renderer.Nested("TextureSampler"); var vt = Renderer.VertexType;
                if (dev == null || tsType == null || vt == null) { Log.Info("HOOK MISSING (optional): 스프라이트 빠른 그리기 — 원작 그대로"); return; }
                var texType = dev.Assembly.GetType("SlimDX.Direct3D9.Texture");
                _stride = Marshal.SizeOf(vt);
                if (_stride != Renderer.VertexSize || texType == null) { Log.Info("fast draw: 정점 배치가 예상과 다름(" + _stride + " B) — 원작 그대로"); return; }
                _oTex1 = Marshal.OffsetOf(vt, "TextureCoordinate1").ToInt32();
                _fn = Build(_rt, vt, tsType, texType);
                if (Check)
                {
                    rPending = Get<int>(_rt, Renderer.Pending); rLast = Get<object>(_rt, Renderer.LastDrawn); rVerts = Get<object>(_rt, Renderer.Vertices);
                    _scratch = Array.CreateInstance(vt, 6);
                }
                if (SceneDraw.Installed) { On = true; Log.Info("fast draw: 그리기 패스 교체(SceneDraw)가 스프라이트마다 부름" + (Check ? " — 검사 모드" : "")); return; }
                var dobj = AccessTools.Method(typeof(DXScene), "DrawObject", new[] { typeof(DXObject) });
                h.Patch(dobj, null, null, new HarmonyMethod(typeof(DrawFast), nameof(DrawTranspiler)));
                On = _drawSites > 0;
                Log.Info("hook: DXScene.DrawObject (transpiler, 렌더러 DrawObject 호출 " + _drawSites + "곳 → 단순 스프라이트는 직접 정점 추가" + (Check ? ", 검사 모드" : "") + ")");
            }
            catch (Exception e) { Log.Error("fast draw install", e); }
        }

        public static bool ScissorSame(Rectangle? sc, RectangleF? last)
        {
            return sc.HasValue == last.HasValue && (!sc.HasValue || (RectangleF)sc.Value == last.Value);
        }

        static Func<object, DXObject, Array, bool> Build(Type rt, Type vt, Type tsType, Type texType)
        {
            var pr = Expression.Parameter(typeof(object), "r"); var po = Expression.Parameter(typeof(DXObject), "o"); var pd = Expression.Parameter(typeof(Array), "dest");
            var vars = new List<ParameterExpression>(); var body = new List<Expression>();
            var ret = Expression.Label(typeof(bool), "ret");
            Func<Type, string, ParameterExpression> V = (t, nm) => { var v = Expression.Variable(t, nm); vars.Add(v); return v; };
            Func<Expression, string, Expression> F = (e, nm) => { var f = AccessTools.Field(e.Type, nm); if (f == null) throw new MissingFieldException(e.Type.Name, nm); return Expression.Field(e, f); };
            Func<Expression, Expression> I = e => Expression.Convert(e, typeof(int));
            Action<Expression> Fail = c => body.Add(Expression.IfThen(c, Expression.Return(ret, Expression.Constant(false))));
            Action<Expression, Expression> Set = (x, y) => body.Add(Expression.Assign(x, y));
            var nul = Expression.Constant(null);

            var R = V(rt, "R"); Set(R, Expression.Convert(pr, rt));
            var commit = V(typeof(bool), "commit"); Set(commit, Expression.ReferenceEqual(pd, nul));
            var scene = V(typeof(DXScene), "scene"); Set(scene, F(R, "_Scene")); Fail(Expression.ReferenceEqual(scene, nul));
            var last = V(typeof(DXObject), "last"); Set(last, F(R, Renderer.LastDrawn));
            var pending = V(typeof(int), "pending"); Set(pending, F(R, Renderer.Pending));
            // no flush: same material, room in the batch, not the lighting object, no shader effect
            Fail(Expression.AndAlso(Expression.ReferenceNotEqual(last, nul), Expression.NotEqual(F(po, "_IDMaterial"), F(last, "_IDMaterial"))));
            Fail(Expression.GreaterThanOrEqual(pending, Expression.Constant(10000)));
            Fail(Expression.ReferenceEqual(po, F(scene, "_OIlumination")));
            Fail(Expression.ReferenceNotEqual(F(po, "_ShaderEffect"), nul));
            // ApplyScissorRect would do nothing
            Fail(Expression.Not(Expression.Call(typeof(DrawFast).GetMethod(nameof(ScissorSame)), F(R, "_ScissorRect"), F(R, "_LastScissorRect"))));
            var st = V(typeof(DXObjectState), "st"); Set(st, F(po, "_State"));
            var img = V(typeof(DXImage), "img"); Set(img, F(st, "_Image")); Fail(Expression.ReferenceEqual(img, nul));
            var tex = V(typeof(object), "tex"); Set(tex, Expression.Property(img, "GPUTexture")); Fail(Expression.Not(Expression.TypeEqual(tex, texType)));
            // no mask, soft border or blur; stretched; no pixel shader, sampler addressing or blend state to change
            Fail(Expression.ReferenceNotEqual(F(po, "_MaskImage"), nul));
            Fail(Expression.NotEqual(I(F(po, "_SoftBorderFilter")), Expression.Constant(0)));
            Fail(Expression.NotEqual(F(po, "_BlurComputedWithParent"), Expression.Constant(0f)));
            var style = V(typeof(int), "style"); Set(style, I(F(st, "_ImageStyle"))); Fail(Expression.NotEqual(style, Expression.Constant(0)));
            Fail(Expression.NotEqual(I(F(R, "_LastEffect")), Expression.Constant((int)TEffect.None)));
            Fail(Expression.LessThan(F(R, "_NLastTextureSampler"), Expression.Constant(0)));
            Fail(Expression.NotEqual(I(F(R, "_LastBlendMode")), I(F(po, "_BlendMode"))));
            var ag = V(typeof(float[]), "ag"); Set(ag, F(R, "_ObjectAlfaGammaArray")); var lag = V(typeof(float[]), "lag"); Set(lag, F(R, "_LastObjectAlfaGammaArray"));
            for (int k = 0; k < 4; k++) Fail(Expression.NotEqual(Expression.ArrayIndex(ag, Expression.Constant(k)), Expression.ArrayIndex(lag, Expression.Constant(k))));
            var verts = V(vt.MakeArrayType(), "verts"); Set(verts, F(R, Renderer.Vertices));
            Fail(Expression.AndAlso(commit, Expression.LessThan(Expression.ArrayLength(verts), Expression.Multiply(Expression.Add(pending, Expression.Constant(1)), Expression.Constant(6)))));
            // GetTextureSampler: the sampler already holding this texture, filter and style
            var smp = V(tsType.MakeArrayType(), "smp"); Set(smp, F(R, "_TextureSamplers"));
            var filter = V(typeof(int), "filter"); Set(filter, I(F(po, "_ImageFilter")));
            var n = V(typeof(int), "n"); Set(n, Expression.Constant(-1));
            var i = V(typeof(int), "i"); Set(i, Expression.Constant(0));
            var s = V(tsType, "s"); var brk = Expression.Label("brk");
            var max = Expression.Field(null, AccessTools.Field(rt, "MaxTextureSamplers"));
            body.Add(Expression.Loop(Expression.IfThenElse(
                Expression.OrElse(Expression.GreaterThanOrEqual(i, max), Expression.GreaterThanOrEqual(i, Expression.ArrayLength(smp))),
                Expression.Break(brk),
                Expression.Block(
                    Expression.Assign(s, Expression.ArrayIndex(smp, i)),
                    Expression.IfThen(
                        Expression.AndAlso(Expression.ReferenceNotEqual(s, nul), Expression.AndAlso(Expression.ReferenceEqual(F(s, "Texture"), tex),
                            Expression.AndAlso(Expression.Equal(I(F(s, "ImageFilter")), filter), Expression.Equal(I(F(s, "ImageStyle")), style)))),
                        Expression.Block(Expression.Assign(n, i), Expression.Break(brk))),
                    Expression.PostIncrementAssign(i))), brk));
            Fail(Expression.LessThan(n, Expression.Constant(0)));
            var cm = V(typeof(float[][]), "cm"); Set(cm, F(po, "_ColorMatrix"));
            var pts = V(typeof(PointF[]), "pts"); Set(pts, F(po, "_InternalPointsObjectAreaToPixelArea4"));
            Fail(Expression.OrElse(Expression.ReferenceEqual(cm, nul), Expression.ReferenceEqual(pts, nul)));
            // from here on DrawObject's own steps
            body.Add(Expression.IfThen(commit, Expression.Assign(F(Expression.ArrayIndex(smp, n), "Time"), F(Expression.Property(null, typeof(DXScene), "Current"), "_VirtualTimeInt"))));
            var sz = V(typeof(Size), "sz"); Set(sz, Expression.Property(img, "Size"));
            var ul = V(typeof(float), "ul"); var ur = V(typeof(float), "ur"); var vtp = V(typeof(float), "vtp"); var vb = V(typeof(float), "vb");
            Set(ul, Expression.Constant(0f)); Set(ur, Expression.Constant(1f)); Set(vtp, Expression.Constant(0f)); Set(vb, Expression.Constant(1f));
            var area = V(typeof(RectangleF?), "area"); Set(area, F(st, "_ImageArea"));
            var ar = V(typeof(RectangleF), "ar");
            var w = Expression.Convert(Expression.Property(sz, "Width"), typeof(float)); var hgt = Expression.Convert(Expression.Property(sz, "Height"), typeof(float));
            body.Add(Expression.IfThen(Expression.Property(area, "HasValue"), Expression.Block(
                Expression.Assign(ar, Expression.Property(area, "Value")),
                Expression.Assign(ul, Expression.Divide(Expression.Property(ar, "Left"), w)), Expression.Assign(ur, Expression.Divide(Expression.Property(ar, "Right"), w)),
                Expression.Assign(vtp, Expression.Divide(Expression.Property(ar, "Top"), hgt)), Expression.Assign(vb, Expression.Divide(Expression.Property(ar, "Bottom"), hgt)))));
            var D = V(vt.MakeArrayType(), "D"); var b0 = V(typeof(int), "b0");
            body.Add(Expression.IfThenElse(commit,
                Expression.Block(Expression.Assign(D, verts), Expression.Assign(b0, Expression.Multiply(pending, Expression.Constant(6)))),
                Expression.Block(Expression.Assign(D, Expression.Convert(pd, vt.MakeArrayType())), Expression.Assign(b0, Expression.Constant(0)))));
            var z = V(typeof(float), "z"); Set(z, Expression.Convert(n, typeof(float)));
            Func<int, Expression> El = k => Expression.ArrayAccess(D, Expression.Add(b0, Expression.Constant(k)));
            var v4 = vt.GetField("Position").FieldType;
            var v4ctor = v4.GetConstructor(new[] { typeof(float), typeof(float), typeof(float), typeof(float) });
            var row = V(v4, "row");
            for (int r = 0; r < 4; r++)
            {
                var cr = Expression.ArrayIndex(cm, Expression.Constant(r));
                Set(row, Expression.New(v4ctor, Expression.ArrayIndex(cr, Expression.Constant(0)), Expression.ArrayIndex(cr, Expression.Constant(1)), Expression.ArrayIndex(cr, Expression.Constant(2)), Expression.ArrayIndex(cr, Expression.Constant(3))));
                foreach (int k in new[] { 0, 2, 1, 4 }) Set(F(El(k), "ColorMatrix" + (r + 1)), row);
            }
            // vertices 0, 1, 2, 4 take corners 2, 0, 3, 1 of the pixel quad (+ the zero offset of the stretched style)
            var corners = new[] { new { k = 0, c = 2, u = ul, v = vb }, new { k = 1, c = 0, u = ul, v = vtp }, new { k = 2, c = 3, u = ur, v = vb }, new { k = 4, c = 1, u = ur, v = vtp } };
            foreach (var c in corners)
            {
                var pt = Expression.ArrayIndex(pts, Expression.Constant(c.c));
                Set(F(F(El(c.k), "Position"), "X"), Expression.Add(Expression.Property(pt, "X"), Expression.Constant(0f)));
                Set(F(F(El(c.k), "Position"), "Y"), Expression.Add(Expression.Property(pt, "Y"), Expression.Constant(0f)));
                Set(F(F(El(c.k), "Position"), "W"), Expression.Constant(1f));
                Set(F(F(El(c.k), "TextureCoordinate"), "X"), c.u);
                Set(F(F(El(c.k), "TextureCoordinate"), "Y"), c.v);
                Set(F(F(El(c.k), "Position"), "Z"), z);
            }
            Set(El(3), El(2)); Set(El(5), El(1));
            body.Add(Expression.IfThen(commit, Expression.Block(
                Expression.Assign(F(R, "_NCurrentTextureSampler"), n),
                Expression.Assign(F(R, "_ForceFirstTextureSampler"), Expression.Constant(false)),
                Expression.Assign(F(R, "_NLastTextureSampler"), n),
                Expression.Assign(F(R, Renderer.Pending), Expression.Add(pending, Expression.Constant(1))),
                Expression.Assign(F(R, Renderer.LastDrawn), po))));
            body.Add(Expression.Label(ret, Expression.Constant(true)));
            var lambda = Expression.Lambda<Func<object, DXObject, Array, bool>>(Expression.Block(vars, body), pr, po, pd);
            return lambda.Compile();
        }

        // Field getter on an object of the declaring type (check mode only).
        static Func<object, T> Get<T>(Type decl, string name)
        {
            var f = AccessTools.Field(decl, name); if (f == null) throw new MissingFieldException(decl.Name, name);
            var dm = new DynamicMethod("get_" + name, typeof(T), new[] { typeof(object) }, decl, true);
            var il = dm.GetILGenerator();
            il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Castclass, decl); il.Emit(OpCodes.Ldfld, f);
            if (typeof(T) == typeof(object) && f.FieldType.IsValueType) il.Emit(OpCodes.Box, f.FieldType);
            il.Emit(OpCodes.Ret);
            return (Func<object, T>)dm.CreateDelegate(typeof(Func<object, T>));
        }

        static int _drawSites;
        static IEnumerable<CodeInstruction> DrawTranspiler(IEnumerable<CodeInstruction> code)
        {
            return Patching.Redirect(code, typeof(IDXRenderer).GetMethod("DrawObject", new[] { typeof(DXObject) }),
                typeof(DrawFast).GetMethod(nameof(Draw), BindingFlags.Static | BindingFlags.Public), out _drawSites);
        }

        // measuring (the benchmark turns it on): sprites by path, the time of the renderer's own DrawObject, and a sample of
        // what the sprites that miss the fast path are
        public static volatile bool Measure;
        public static long NFast, NSlow, SlowTicks;
        public static readonly Dictionary<string, long> SlowKinds = new Dictionary<string, long>();

        public static void Draw(IDXRenderer r, DXObject o)
        {
            if (!On || r == null || o == null || r.GetType() != _rt || !DeviceCheck.SceneOk) { r.DrawObject(o); return; }
            if (Measure)
            {
                if (TryFast(r, o, null)) { NFast++; return; }
                long t = System.Diagnostics.Stopwatch.GetTimestamp();
                r.DrawObject(o);
                SlowTicks += System.Diagnostics.Stopwatch.GetTimestamp() - t;
                if (++NSlow % 97 == 0)
                {
                    var img = o._State._Image; object tex = img != null ? img.GPUTexture : null;
                    string texName = tex != null ? tex.GetType().Name : "null";
                    string k = o.GetType().Name + (o._ShaderEffect != null ? " fx:" + o._ShaderEffect.GetType().Name : "") + (o._MaskImage != null ? " mask" : "")
                        + (o._State._ImageStyle != 0 ? " style:" + o._State._ImageStyle : "") + (o._BlendMode != DXObject.TBlendMode.Normal ? " blend:" + o._BlendMode : "")
                        + (img != null && texName != "Texture" ? " tex:" + texName : "");
                    long c; SlowKinds.TryGetValue(k, out c); SlowKinds[k] = c + 1;
                }
                return;
            }
            if (!Check) { if (!TryFast(r, o, null)) r.DrawObject(o); return; }
            int before = rPending(r);
            bool ok = TryFast(r, o, _scratch);
            r.DrawObject(o);
            if (!ok) { SlowSprites++; return; }
            Compare(r, o, before);
        }

        static bool TryFast(IDXRenderer r, DXObject o, Array dest)
        {
            try { return _fn(r, o, dest); }
            catch (Exception e) { if (!_errLogged) { _errLogged = true; Log.Error("fast draw", e); } return false; }
        }

        static void Compare(object r, DXObject o, int before)
        {
            Checked++;
            string why = null;
            int pending = rPending(r);
            if (pending != before + 1) why = "묶음 " + before + "→" + pending;
            else if (!ReferenceEquals(rLast(r), o)) why = "마지막 물체";
            else
            {
                var verts = rVerts(r) as Array;
                GCHandle h1 = GCHandle.Alloc(verts, GCHandleType.Pinned), h2 = GCHandle.Alloc(_scratch, GCHandleType.Pinned);
                try
                {
                    byte* b = (byte*)h1.AddrOfPinnedObject() + (long)before * 6 * _stride, t = (byte*)h2.AddrOfPinnedObject();
                    for (int k = 0; k < 6 && why == null; k++)
                        for (int off = 0; off < _stride; off += 4)
                        {
                            if (off >= _oTex1 && off < _oTex1 + 8) continue;   // TextureCoordinate1: untouched without a mask
                            if (*(int*)(b + k * _stride + off) != *(int*)(t + k * _stride + off)) { why = "정점 " + k + " +" + off + " 원작 " + *(float*)(b + k * _stride + off) + " · 빠른 " + *(float*)(t + k * _stride + off); break; }
                        }
                }
                finally { h1.Free(); h2.Free(); }
            }
            if (why != null) Mismatch++;
            long now = Clock.Ms;
            if (why != null && now - _checkLogAt > 2000) { _checkLogAt = now; Log.Info("fast draw check: 다름 — " + why + " (누적 " + Checked + "회 중 " + Mismatch + ")"); }
            if (Checked % 200000 == 0) Log.Info("fast draw check: " + Checked + "회 · 다름 " + Mismatch + " · 해당 없음 " + SlowSprites);
        }
    }
}
