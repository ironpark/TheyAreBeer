using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Globalization;
using DXVision;
using DXVision.GUI;

namespace Billions
{
    // The benchmark's on-screen widget (InGameBench), top right: the numbers of the last second and two graphs of the last
    // 60 seconds (FPS, game speed). The whole widget is drawn with GDI+ into one bitmap once per second and shown as one
    // scene object in screen pixels (the game's text objects do not place in screen pixels); the previous texture is
    // freed each time. Exists only while a benchmark runs.
    internal static class BenchOverlay
    {
        const int Bars = 60;
        static DXObject _o;
        static DXImage _img;
        static readonly double[] _fps = new double[Bars], _speed = new double[Bars];
        static int _n;
        static float _s;
        static int _w, _h;
        static Font _fTitle, _fLabel, _fValue, _fSmall;

        static readonly Color Panel = Color.FromArgb(215, 12, 16, 20), Title = Color.FromArgb(255, 200, 230, 140),
            Label = Color.FromArgb(255, 150, 165, 150), Value = Color.FromArgb(255, 245, 245, 235), Grid = Color.FromArgb(90, 255, 255, 255),
            FpsC = Color.FromArgb(255, 70, 160, 255), FpsLow = Color.FromArgb(255, 255, 105, 80), SpeedC = Color.FromArgb(255, 95, 205, 120), SpeedLow = Color.FromArgb(255, 255, 190, 60);

        static Font MakeFont(float px, FontStyle st)
        {
            try { return new Font("Malgun Gothic", px, st, GraphicsUnit.Pixel); } catch { return new Font(FontFamily.GenericSansSerif, px, st, GraphicsUnit.Pixel); }
        }

        public static void Show()
        {
            try
            {
                Hide();
                var scr = DXScene.ScreenArea;
                _s = Math.Max(0.7f, scr.Height / 1080f);
                _w = (int)(560 * _s); _h = (int)(304 * _s);
                _fTitle = MakeFont(19 * _s, FontStyle.Bold); _fLabel = MakeFont(15 * _s, FontStyle.Regular);
                _fValue = MakeFont(27 * _s, FontStyle.Bold); _fSmall = MakeFont(13 * _s, FontStyle.Regular);
                _n = 0; Array.Clear(_fps, 0, Bars); Array.Clear(_speed, 0, Bars);
                _o = new DXObject("TABPerfBench", DXImage.FromColor(Color.Transparent));
                _o.State.AreaStyle = DXObjectState.TAreaStyle.AreaInPixels;
                float margin = 16 * _s;
                _o.State.Area = new RectangleF(scr.Width - _w - margin, margin, _w, _h);   // same margin from the top and the right
                _o.Optimization = DXObject.TOptimization.NoRenderNorUpdateSceneObject;
                _o.Z = 2147483600;
                DXScene.Current.AddObject(_o);
                Draw(0, 0, 0, 0, -1, 0, 0, 0, "");
            }
            catch (Exception e) { Log.Error("benchmark overlay", e); Hide(); }
        }

        public static void Update(double sec, int zombies, double fps, double frameMs, double drawMs, double ticks, double speed, double cores, string note)
        {
            if (_o == null) return;
            if (_n < Bars) _n++;
            Array.Copy(_fps, 1, _fps, 0, Bars - 1); _fps[Bars - 1] = fps;
            Array.Copy(_speed, 1, _speed, 0, Bars - 1); _speed[Bars - 1] = speed;
            try { Draw(sec, zombies, fps, frameMs, drawMs, ticks, speed, cores, note); } catch (Exception e) { Log.Error("benchmark overlay update", e); }
        }

        static void Draw(double sec, int zombies, double fps, double frameMs, double drawMs, double ticks, double speed, double cores, string note)
        {
            var inv = CultureInfo.InvariantCulture;
            var bmp = new Bitmap(_w, _h, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                float pad = 16 * _s;
                using (var b = new SolidBrush(Panel)) g.FillRectangle(b, 0, 0, _w, _h);   // a plain rectangle, no border
                using (var bt = new SolidBrush(Title)) g.DrawString(Lang.T("TABPerf 벤치마크", "TABPerf benchmark"), _fTitle, bt, pad, pad * 0.7f);
                string esc = Lang.T("ESC: 끝내기", "ESC: stop");
                using (var bl = new SolidBrush(Label)) { var sz = g.MeasureString(esc, _fLabel); g.DrawString(esc, _fLabel, bl, _w - pad - sz.Width, pad * 0.8f); }

                // numbers: 3 columns x 2 rows
                float top = pad * 0.7f + 34 * _s, colW = (_w - 2 * pad) / 3f, rowH = 56 * _s;
                Cell(g, pad, top, Lang.T("좀비", "Zombies"), zombies.ToString("N0"), "/ " + InGameBench.MaxZombies.ToString("N0"));
                Cell(g, pad + colW, top, "FPS", fps.ToString("0.0", inv), frameMs.ToString("0", inv) + " ms" + (drawMs >= 0 ? Lang.T(" · 그리기 ", " · draw ") + drawMs.ToString("0", inv) : ""));
                Cell(g, pad + 2 * colW, top, Lang.T("게임 속도", "Game speed"), speed.ToString("0.00", inv) + "x", "");
                Cell(g, pad, top + rowH, Lang.T("로직 틱", "Logic ticks"), ticks.ToString("0.0", inv), "/s");
                Cell(g, pad + colW, top + rowH, "CPU", cores.ToString("0.0", inv), Lang.T("코어", "cores"));
                Cell(g, pad + 2 * colW, top + rowH, Lang.T("시간", "Time"), sec.ToString("0", inv), "/ " + InGameBench.MaxSeconds + " s");

                // graphs
                float gTop = top + 2 * rowH + 6 * _s, gH = 50 * _s, gW = _w - 2 * pad;
                double fpsMax = 30; for (int i = 0; i < Bars; i++) if (_fps[i] > fpsMax) fpsMax = _fps[i];
                fpsMax = Math.Ceiling(fpsMax / 10) * 10;
                Graph(g, pad, gTop, gW, gH, Lang.T("FPS · 최근 60초", "FPS · last 60 s") + "  (" + Lang.T("최대 ", "max ") + fpsMax.ToString("0", inv) + ")", _fps, fpsMax, 20, FpsC, FpsLow, double.NaN);
                Graph(g, pad, gTop + gH + 24 * _s, gW, gH, Lang.T("게임 속도 · 1.0 = 제 속도", "Game speed · 1.0 = full"), _speed, 1.2, 0.9, SpeedC, SpeedLow, 1.0);
                if (!string.IsNullOrEmpty(note)) using (var bs = new SolidBrush(Label)) { var sz = g.MeasureString(note, _fSmall); g.DrawString(note, _fSmall, bs, _w - pad - sz.Width, gTop - 2 * _s); }
            }
            var img = new DXImageBitmap(bmp);
            var old = _img;
            _img = img;
            _o.Image = img;
            if (old != null) try { old.Dispose(); } catch { }
        }

        static void Cell(Graphics g, float x, float y, string label, string value, string unit)
        {
            using (var bl = new SolidBrush(Label)) using (var bv = new SolidBrush(Value))
            {
                g.DrawString(label, _fLabel, bl, x, y);
                g.DrawString(value, _fValue, bv, x - 2 * _s, y + 17 * _s);
                if (unit.Length > 0) { var sz = g.MeasureString(value, _fValue); g.DrawString(unit, _fSmall, bl, x - 2 * _s + sz.Width - 4 * _s, y + 30 * _s); }
            }
        }

        static void Graph(Graphics g, float x, float y, float w, float h, string label, double[] v, double max, double low, Color c, Color cLow, double refLine)
        {
            using (var bl = new SolidBrush(Label)) g.DrawString(label, _fSmall, bl, x, y);
            float gy = y + 20 * _s, gh = h - 20 * _s;
            using (var bg = new SolidBrush(Color.FromArgb(60, 0, 0, 0))) g.FillRectangle(bg, x, gy, w, gh);
            using (var pg = new Pen(Grid, 1)) { g.DrawLine(pg, x, gy + gh, x + w, gy + gh); if (!double.IsNaN(refLine)) { float ry = gy + gh - (float)(refLine / max) * gh; g.DrawLine(pg, x, ry, x + w, ry); } }
            float bw = w / Bars;
            var old = g.SmoothingMode; g.SmoothingMode = SmoothingMode.None;
            using (var b = new SolidBrush(c)) using (var bLow = new SolidBrush(cLow))
                for (int i = Bars - _n; i < Bars; i++)
                {
                    float bh = (float)(Math.Max(0, Math.Min(max, v[i])) / max) * gh;
                    if (bh < 1) continue;
                    g.FillRectangle(v[i] < low ? bLow : b, x + i * bw + 0.5f, gy + gh - bh, Math.Max(1, bw - 1.5f), bh);
                }
            g.SmoothingMode = old;
        }

        static GraphicsPath Rounded(RectangleF r, float rad)
        {
            var p = new GraphicsPath(); float d = rad * 2;
            p.AddArc(r.X, r.Y, d, d, 180, 90); p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90); p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            p.CloseFigure(); return p;
        }

        public static void Hide()
        {
            try { if (_o != null) _o.RemoveFromParent(); } catch { }
            _o = null;
            if (_img != null) try { _img.Dispose(); } catch { }
            _img = null;
            foreach (var f in new[] { _fTitle, _fLabel, _fValue, _fSmall }) if (f != null) f.Dispose();
            _fTitle = _fLabel = _fValue = _fSmall = null;
        }

        // the Optimization tab's start button: a plain button with an image drawn here (DXButton greys it out when disabled
        // and brightens it under the mouse)
        public static DXButton MakeButton(string text, SizeF size)
        {
            float k = 2f;   // draw at twice the GUI size, the image is scaled down to the button
            int w = Math.Max(40, (int)(size.Width * k)), h = Math.Max(16, (int)(size.Height * k));
            var bmp = new Bitmap(w, h, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                var rect = new RectangleF(1, 1, w - 2, h - 2);
                using (var path = Rounded(rect, h * 0.22f))
                {
                    using (var lg = new LinearGradientBrush(rect, Color.FromArgb(255, 58, 92, 52), Color.FromArgb(255, 28, 48, 26), LinearGradientMode.Vertical)) g.FillPath(lg, path);
                    using (var p = new Pen(Color.FromArgb(255, 176, 150, 90), Math.Max(2, h * 0.06f))) g.DrawPath(p, path);
                }
                using (var f = MakeFont(h * 0.42f, FontStyle.Bold))
                using (var b = new SolidBrush(Color.FromArgb(255, 230, 240, 200)))
                using (var sf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                    g.DrawString(text, f, b, rect, sf);
            }
            var btn = new DXButton();
            btn.Image = new DXImageBitmap(bmp);
            btn.Border = null;
            btn.Size = size;
            return btn;
        }
    }
}
