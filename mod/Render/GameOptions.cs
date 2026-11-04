using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using DXVision;
using DXVision.GUI;
using HarmonyLib;
using ZX;
using ZX.GUI;

namespace Billions
{

    public static class GameOptions
    {
        public static bool LightLifeBars;
        public static int Installed;
        static string FilePath { get { return Path.Combine(Log.Dir, "options.json"); } }
        static readonly AccessTools.FieldRef<DXObject, List<DXObject>> _children = AccessTools.FieldRefAccess<DXObject, List<DXObject>>("_Children");

        public static void Load()
        {
            try
            {
                if (File.Exists(FilePath))
                {
                    var m = MiniJson.Parse(File.ReadAllText(FilePath)) as Dictionary<string, object>;
                    object v; LightLifeBars = m != null && m.TryGetValue("lightLifeBars", out v) && v is bool && (bool)v;
                }
            }
            catch (Exception e) { Log.Error("options load", e); }
            LifeMarkers.Mode = LightLifeBars ? 2 : 0;
            Log.Info("options: 좀비 체력바 가볍게 = " + LightLifeBars);
        }

        static void Save()
        {
            try
            {
                Directory.CreateDirectory(Log.Dir);
                string tmp = FilePath + ".tmp";
                File.WriteAllText(tmp, "{\"lightLifeBars\":" + (LightLifeBars ? "true" : "false") + "}");
                if (File.Exists(FilePath)) File.Replace(tmp, FilePath, null);
                else File.Move(tmp, FilePath);
            }
            catch (Exception e) { Log.Error("options save", e); }
        }

        public static void Install(Harmony h)
        {
            try
            {
                Load();
                var m = AccessTools.Method(typeof(ZXGame), "ShowOptionsWindow");
                if (m == null) { Log.Info("HOOK MISSING (optional): ZXGame.ShowOptionsWindow — 옵션 줄 없음(체력바는 저장값대로)"); return; }
                h.Patch(m, null, new HarmonyMethod(typeof(GameOptions), nameof(AfterShow)));
                Installed = 1; Log.Info("hook: ZXGame.ShowOptionsWindow (postfix, 그래픽 탭에 「좀비 체력바 가볍게」)");
            }
            catch (Exception e) { Installed = 0; Log.Error("options install", e); }
        }

        static void AfterShow()
        {
            try
            {
                var root = DXScene.Current != null ? DXScene.Current.RootObject : null; if (root == null) return;

                ZXWindowTab win = null; var rc = _children(root);
                if (rc != null) for (int i = rc.Count - 1; i >= 0 && win == null; i--) win = rc[i] as ZXWindowTab;
                if (win == null) return;
                string fast = ZXGame.W("FastTimming");
                var grid = FindGrid(win, fast); if (grid == null) { Log.Info("options: 그래픽 표를 못 찾음 — 줄을 더하지 않음"); return; }
                float labelW = win.PageSize.Width * 0.3f;
                var size = new SizeF(win.PageSize.Width * 0.3f, DXScene.ScreenVirtualUnit * 20f);
                string title = Text("좀비 체력바 가볍게(최적화)", "Light zombie health bars");
                if (HasRow(grid, title)) return;
                var label = TextTemplates.Interface.get_NormalBig().CreateObject(title, new ContentAlignment?(ContentAlignment.TopRight), new float?(labelW));
                var ch = new ZXCheckBox(); ch.Size = size; ch.Checked = LightLifeBars;
                ch.CheckedChanged += delegate (DXCheckBox c)
                {
                    LightLifeBars = c.Checked; Save();
                    try { LifeMarkers.SetMode(LightLifeBars ? 2 : 0); } catch (Exception e) { Log.Error("options life mode", e); }
                    Log.Info("options: 좀비 체력바 가볍게 = " + LightLifeBars);
                };
                grid.AddRow(new DXObject[] { label, ch });
                grid[0, grid.NRows - 1].Object.Tag = new ZXHelp(ZXAlignment.BottomCenter, title,
                    Text("좀비 체력바를 테두리 없는 얇은 막대 하나로 그려 좀비가 많을 때 화면이 덜 느려집니다. 끄면 원작 체력바 그대로입니다. 바로 적용됩니다.",
                         "Draws zombie health bars as a single thin bar so large hordes render faster. Off = original health bars. Applies immediately."), null, null, null);
            }
            catch (Exception e) { Log.Error("options row", e); }
        }

        static string Text(string ko, string en)
        {
            try { var c = DXGame.Current.Configuration as ZXGameConfiguration; if (c != null && c.Language == DXLanguageType.KO) return ko; } catch { }
            return en;
        }

        static bool HasRow(DXGridLayout g, string label)
        {
            try
            {
                for (int r = 0; r < g.NRows; r++)
                {
                    var t = g[0, r] != null ? g[0, r].Object as DXTextObject : null;
                    if (t != null && t.Text == label) return true;
                }
            }
            catch { }
            return false;
        }

        static DXGridLayout FindGrid(DXObject o, string fastLabel)
        {
            var g = o as DXGridLayout;
            if (g != null && HasRow(g, fastLabel)) return g;
            var ch = _children(o); if (ch == null) return null;
            for (int i = 0; i < ch.Count; i++) { var f = ch[i] != null ? FindGrid(ch[i], fastLabel) : null; if (f != null) return f; }
            return null;
        }
    }
}
