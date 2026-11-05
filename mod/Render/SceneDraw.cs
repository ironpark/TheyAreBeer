using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Runtime.CompilerServices;
using DXVision;
using HarmonyLib;

namespace Billions
{
    // DXScene.DrawScene, the scene's draw pass (render thread, once per frame, after RenderObject's update pass), replaced
    // as a whole. It is the game's code (TABModLoader v1.1.7 DXVision, DXScene.DrawScene / DrawObject / DrawEndObjects)
    // rewritten with the same steps and order:
    //   global light   (GlobalLightEnable) the tree into the light texture, light objects only; the result object is put
    //                  into the root's visible list for the main pass
    //   post-processing (OnPostProcessing) the tree up to the post-processing frame into its texture, bloom's blur
    //                  passes, then the rest of the tree from the frame on
    //   main           the tree to the screen (or the render-to-image texture)
    // and removes the light / post-processing objects from the root's visible list at the end. Changes:
    //   - every sprite goes through DrawFast.Draw (plain sprites appended to the batch directly, the rest to the renderer)
    //     instead of DrawFast patching the original's calls (DrawFast leaves DrawScene / DrawObject alone then);
    //   - the end-of-frame removals happen only when this frame added the object, searching the list from its end
    //     (the original scans the whole list from the front every frame, ~66,000 objects at 100,000 zombies);
    //   - one Pass helper for the start scene / tree / end objects / finish scene sequence the original repeats;
    //   - the time of the whole draw pass for the benchmark (LastMs).
    // TABPERF_SCENEDRAW=0 keeps the game's own. Installed only when its methods compile against the running DXVision.
    public static class SceneDraw
    {
        public static bool Installed { get; private set; }
        public static double LastMs;

        public static void Install(Harmony h)
        {
            if (!Settings.Switch("TABPERF_SCENEDRAW", true)) return;
            try
            {
                var ds = AccessTools.Method(typeof(DXScene), "DrawScene");
                if (ds == null || ds.ReturnType != typeof(bool)) { Log.Info("HOOK MISSING (optional): DXScene.DrawScene — 원작 그리기 그대로"); return; }
                // JIT now: a member missing or changed in this DXVision fails here instead of in the middle of a frame
                foreach (var m in new[] { nameof(Draw), nameof(Pass), nameof(DrawObject), nameof(DrawEndObjects), nameof(PostProcessingPass), nameof(Detach) })
                    RuntimeHelpers.PrepareMethod(AccessTools.Method(typeof(SceneDraw), m).MethodHandle);
                h.Patch(ds, new HarmonyMethod(typeof(SceneDraw), nameof(Pre)));
                Installed = true;
                Log.Info("hook: DXScene.DrawScene (prefix, 모드의 그리기 패스로 통째로 바꿈 — 빠른 스프라이트 · 끝부터 지우기 포함)");
            }
            catch (Exception e) { Log.Error("scene draw install (원작 그리기 그대로)", e); }
        }

        static bool Pre(DXScene __instance, ref bool __result)
        {
            __result = Draw(__instance);
            return false;
        }

        // ---------------- DrawScene ----------------
        static bool Draw(DXScene s)
        {
            long t0 = Stopwatch.GetTimestamp();
            bool lightAdded = false, postAdded = false;   // in the root's visible list this frame
            try
            {
                var r = s._Renderer;
                if (!r.Enabled)
                {
                    r.InitRenderer(null);
                    if (!r.Enabled) return false;
                }
                if (DXScene.ScreenArea.IsEmpty) return true;
                lock (DXGame.SyncDrawing)
                {
                    s._ScissorRectangles.Clear();
                    // the scene drawn into an image (screenshots, previews)
                    if (!s.RenderToTexture && s.DrawingToImage)
                    {
                        if (s._FrameImageRenderToImage == null) s._FrameImageRenderToImage = new DXImageNativeTexture("_FrameImageRenderToImage");
                        s._FrameImageRenderToImage.Size = s.DrawToBitmapSize;
                        s._NObjectsDrawn = 0;
                        return Pass(s, r, s.BackgroundColor, s._FrameImageRenderToImage);
                    }
                    if (s.GlobalLightEnable)
                    {
                        s.DrawingIlumination = true;
                        s._OIlumination.Area = DXScene.WindowArea;
                        if (s._FrameImageIlumination == null) { s._FrameImageIlumination = new DXImageNativeTexture("_FrameImageIlumination"); s._FrameImageIlumination.EnableMipmaps = false; }
                        s._FrameImageIlumination.Size = s.GlobalLightSize;
                        if (!Pass(s, r, s.GlobalLightColor, s._FrameImageIlumination)) return false;
                        s._OIlumination.Z = s.GlobalLightZ;
                        s.IsRendering = false;
                        s.AddObjectToRootAndUpdateVisibleChildren(s._OIlumination);
                        lightAdded = true;
                        s.IsRendering = true;
                        s._OIlumination.Image = s._FrameImageIlumination;
                    }
                    s.DrawingIlumination = false;
                    s._NObjectsDrawn = 0;
                    if (s.OnPostProcessing != null && !PostProcessingPass(s, r, ref postAdded)) return false;
                    if (!r.Enabled) return false;
                    if (s.RenderToTexture)
                    {
                        if (s._FrameImageRenderToImage == null) s._FrameImageRenderToImage = new DXImageNativeTexture("_FrameImageRenderToImage");
                        s._FrameImageRenderToImage.Size = s.DrawToBitmapSize;
                    }
                    bool ok = Pass(s, r, s.BackgroundColor, s.RenderToTexture ? s._FrameImageRenderToImage : null);
                    if (ok) s.RenderMode = DXRenderMode.Normal;
                    return ok;
                }
            }
            catch (Exception ex)
            {
                DXLog.Write("DXScene:DrawScene: Draw Scene Exception: " + ex.Message + "\n" + ex.StackTrace);
                return false;
            }
            finally
            {
                Detach(s, s._OIlumination, lightAdded);
                Detach(s, s._OPostProcessing, postAdded);
                LastMs = (Stopwatch.GetTimestamp() - t0) * 1000.0 / Stopwatch.Frequency;
            }
        }

        // the tree (from the root) into `target` (null: the screen), then the objects drawn last
        static bool Pass(DXScene s, IDXRenderer r, Color background, DXImageNativeTexture target)
        {
            if (!r.Enabled) return false;
            r.StartScene(s, background, target);
            s._DrawnEndObjects = false;
            DrawObject(s, r, s.RootObject);
            if (!s._DrawnEndObjects) DrawEndObjects(s, r);
            if (!r.Enabled) return false;
            r.FinishScene();
            return true;
        }

        // The tree up to the post-processing frame into its texture (first pass), bloom's two blur passes, and the
        // frame set up so that the main pass draws the frame and the rest of the tree (second pass).
        static bool PostProcessingPass(DXScene s, IDXRenderer r, ref bool added)
        {
            s.RenderMode = DXRenderMode.PostProcessingFirstPass;
            if (s._FrameImagePostprocessing == null) s._FrameImagePostprocessing = new DXImageNativeTexture("_FrameImagePostprocessing");
            s._FrameImagePostprocessing.Size = DXScene.ScreenArea.Size.Scale(1f);
            if (s._OPostProcessing == null)
            {
                s._OPostProcessing = new DXObject("PostProcessingFrame");
                s._OPostProcessing.Image = s._FrameImagePostprocessing;
                s._OPostProcessing.State.AreaStyle = DXObjectState.TAreaStyle.AreaInPixels;
                s._OPostProcessing.PixelVisibilityMode = DXObject.TPixelVisibilityMode.Invisible;
            }
            var frame = s._OPostProcessing;
            frame.Size = DXScene.ScreenArea.Size;
            frame.Z = s.PostProcessingMaxZ;
            s.OnPostProcessing(frame);
            s.IsRendering = false;
            frame.RemoveFromParent();
            s.AddObjectToRootAndUpdateVisibleChildren(frame);
            added = true;
            s.IsRendering = true;
            // the frame's place in the root's visible list (binary search by Z, then the exact object nearby)
            var visible = s._RootObject._VisibleChildren;
            int at = visible.BinarySearch(frame), count = visible.Count;
            if (at < 0 || at >= count || visible[at] != frame)
            {
                // the game's own search: it steps 2 at a time (its loop variable is also incremented in the body)
                for (int d = 0; d < count; d++)
                {
                    d++;
                    if (at + d < count && at + d >= 0 && visible[at + d] == frame) { at += d; break; }
                    if (at - d >= 0 && at - d < count && visible[at - d] == frame) { at -= d; break; }
                }
            }
            s._OPostProcessingIndexInVisibleChildren = at;
            if (!DrawInto(s, r, s.RootObject, s._FrameImagePostprocessing)) return false;   // first pass

            var bloom = frame._ShaderEffect as DXShaderEffect_Bloom;
            if (bloom != null)
            {
                if (s._OPostProcessingAux == null)
                {
                    s._OPostProcessingAux = new DXObject("PostProcessingFrameAux");
                    s._OPostProcessingAux.State.AreaStyle = DXObjectState.TAreaStyle.AreaInPixels;
                    s._OPostProcessingAux.PixelVisibilityMode = DXObject.TPixelVisibilityMode.Invisible;
                    s._OPostProcessingAux._IDMaterial = -1;
                    s._OPostProcessingAux._ImageFilter = DXObject.TImageFilter.Linear;
                }
                var aux = s._OPostProcessingAux;
                aux.Size = DXScene.ScreenArea.Size;
                aux.Z = s.PostProcessingMaxZ;
                float scale = DXScene.ShaderQuality == DXQuality4Type.Medium ? 0.6f : DXScene.ShaderQuality == DXQuality4Type.Low ? 0.3f : DXScene.ShaderQuality == DXQuality4Type.VeryLow ? 0.2f : 0.8f;
                if (s._FrameImagePostprocessingAux == null) s._FrameImagePostprocessingAux = new DXImageNativeTexture("_FrameImagePostprocessingAux");
                s._FrameImagePostprocessingAux.Size = DXScene.ScreenArea.Size.Scale(scale);
                if (s._FrameImagePostprocessingAux2 == null) s._FrameImagePostprocessingAux2 = new DXImageNativeTexture("_FrameImagePostprocessingAux2");
                s._FrameImagePostprocessingAux2.Size = DXScene.ScreenArea.Size.Scale(scale);
                aux.ShaderEffect = bloom;
                aux.PreComputeMatrixes();
                // horizontal blur: the frame's texture -> aux; vertical blur: aux -> aux2
                bloom.Stage = DXShaderEffect_Bloom.TBloomStage.BlurHorizontal;
                aux.Image = s._FrameImagePostprocessing;
                if (!DrawInto(s, r, aux, s._FrameImagePostprocessingAux)) return false;
                bloom.Stage = DXShaderEffect_Bloom.TBloomStage.BlurVertical;
                aux.Image = s._FrameImagePostprocessingAux;
                if (!DrawInto(s, r, aux, s._FrameImagePostprocessingAux2)) return false;
                bloom.Stage = DXShaderEffect_Bloom.TBloomStage.Bloom;
                frame.Image = s._FrameImagePostprocessing;
                frame._MaskImage = s._FrameImagePostprocessingAux2;
            }
            s.RenderMode = DXRenderMode.PostProcessingSecondPass;
            return true;
        }

        // one object (and its tree) into `target`, without the end objects
        static bool DrawInto(DXScene s, IDXRenderer r, DXObject o, DXImageNativeTexture target)
        {
            if (!r.Enabled) return false;
            r.StartScene(s, s.BackgroundColor, target);
            DrawObject(s, r, o);
            if (!r.Enabled) return false;
            r.FinishScene();
            return true;
        }

        // the light / post-processing object out of the scene, and out of the root's visible list when this frame put it
        // there (it sits near the end: placed by Z)
        static void Detach(DXScene s, DXObject o, bool added)
        {
            if (o == null) return;
            s.IsRendering = false;
            o.RemoveFromScene();
            s.IsRendering = true;
            if (!added) return;
            var list = s.RootObject._VisibleChildren;
            if (list != null) for (int i = list.Count - 1; i >= 0; i--) if (ReferenceEquals(list[i], o)) { list.RemoveAt(i); break; }
        }

        static void DrawEndObjects(DXScene s, IDXRenderer r)
        {
            s._DrawingEndObjects = true;
            try
            {
                var list = s._DrawEndObjectsList;
                for (int i = 0; i < list.Count; i++) DrawObject(s, r, list[i]);
            }
            finally { s._DrawingEndObjects = false; s._DrawnEndObjects = true; }
        }

        // ---------------- DrawObject: one object and its visible children ----------------
        static void DrawObject(DXScene s, IDXRenderer r, DXObject o)
        {
            if (!s._DrawingEndObjects && o.ForceRenderToEnd) return;   // drawn by DrawEndObjects
            var root = s._RootObject;
            if (!s._DrawnEndObjects && o._Parent == root && o._Z >= DXScene.ZForDrawToEndObjects && !s._DrawingEndObjects) DrawEndObjects(s, r);
            if (!o.PreComputedVisible) return;
            bool light = s.DrawingIlumination, isLight = light && IsLight(o), clip = o.Clip;
            var visible = o._VisibleChildren;
            // light pass: an object that is no light and has nothing inside only takes its draw position
            if (light && o != root && !isLight && !clip && (visible == null || visible.Count == 0))
            {
                o._DrawPosition = s._NObjectsDrawn++;
                return;
            }
            int from = 0, to = visible != null ? visible.Count : 0;
            bool self = true;   // draw the object itself (and its children that come before it)
            if (o == root && visible != null)
            {
                if (s.RenderMode == DXRenderMode.PostProcessingFirstPass) to = s._OPostProcessingIndexInVisibleChildren;
                else if (s.RenderMode == DXRenderMode.PostProcessingSecondPass) { from = s._OPostProcessingIndexInVisibleChildren; self = false; }
            }
            if (self)
            {
                if (to > 0)
                {
                    if (o.DrawChildrenFirst) for (int i = from; i < to; i++) DrawObject(s, r, visible[i]);
                    else if (o.HasAnyChildToDrawBeforeParent) for (int i = from; i < to; i++) { var c = visible[i]; if (c.DrawBeforeParent) DrawObject(s, r, c); }
                }
                o._DrawPosition = s._NObjectsDrawn++;
                bool ilu = o == s._OIlumination;
                if ((o._State._Image != null || ilu) && (ilu || o.IsPixelAreaVisible) && (light ? isLight : o._BlendMode != DXObject.TBlendMode.Light))
                    DrawSprite(r, o);
            }
            if (clip && !PushScissor(s, r, o)) return;
            if (to > 0 && !o.DrawChildrenFirst)
            {
                if (o.HasAnyChildToDrawBeforeParent) for (int i = from; i < to; i++) { var c = visible[i]; if (!c.DrawBeforeParent) DrawObject(s, r, c); }
                else for (int i = from; i < to; i++) DrawObject(s, r, visible[i]);
            }
            if (clip) PopScissor(s, r);
        }

        static bool IsLight(DXObject o) { return o._BlendMode == DXObject.TBlendMode.Light || o._BlendMode == DXObject.TBlendMode.LightAndBright; }

        // the object's image (and mask) on the GPU, then the sprite; blend modes from 10 up are an Add and a Normal sprite
        // mixed by the mode's value (percent)
        static void DrawSprite(IDXRenderer r, DXObject o)
        {
            var st = o._State;
            if (st._Image != null) st._Image.LoadInGPU();
            if (o._MaskImage != null) o._MaskImage.LoadInGPU();
            try
            {
                if (st._Opacity <= 0f) return;
                int mode = (int)o._BlendMode;
                if (mode < 10) { DrawFast.Draw(r, o); return; }
                float opacity = st._Opacity;
                o._BlendMode = DXObject.TBlendMode.Add;
                st.Opacity = opacity * (1f - mode / 100f);
                o._ColorMatrix = o.ColorMatrix;
                DrawFast.Draw(r, o);
                o._BlendMode = DXObject.TBlendMode.Normal;
                st.Opacity = opacity * (mode / 100f);
                o._ColorMatrix = o.ColorMatrix;
                DrawFast.Draw(r, o);
                o._BlendMode = (DXObject.TBlendMode)mode;
                st._Opacity = opacity;
            }
            catch (Exception ex)
            {
                DXHelper_Console.Debug_WriteLine("DXScene:DrawObject: [" + o.ID + "] Exception:" + ex.Message);
                r.DrawObject(o);
            }
        }

        // A clipping object: its pixel area (on the render area) intersected with the open scissor rectangles. An empty
        // area restores the enclosing scissor and skips the children (false), as the game does.
        static bool PushScissor(DXScene s, IDXRenderer r, DXObject o)
        {
            var list = s._ScissorRectangles;
            Rectangle a = o._PixelArea;
            a.X += DXScreenParameters.Current.RenderArea.X;
            a.Y += DXScreenParameters.Current.RenderArea.Y;
            if (a.X < 0) { a.Width += a.X; a.X = 0; }
            if (a.Y < 0) { a.Height += a.Y; a.Y = 0; }
            if (a.Width <= 0 || a.Height <= 0) { RestoreScissor(list, r); return false; }
            Rectangle rect = DXHelper_Rectangle.Round(a);
            foreach (var x in list) rect.Intersect(x);
            r.SetScissorRect(rect);
            list.Add(rect);
            return true;
        }

        static void PopScissor(DXScene s, IDXRenderer r)
        {
            var list = s._ScissorRectangles;
            if (list.Count == 0) return;
            list.RemoveAt(list.Count - 1);
            RestoreScissor(list, r);
        }

        // the scissor of the open clipping objects (none: no scissor)
        static void RestoreScissor(List<Rectangle> list, IDXRenderer r)
        {
            if (list.Count == 0) { r.SetScissorRect(null); return; }
            Rectangle v = list[list.Count - 1];
            foreach (var x in list) v.Intersect(x);
            r.SetScissorRect(v);
        }
    }
}
