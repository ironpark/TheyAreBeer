using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Text;
using DXVision;
using DXVision.GUI;
using HarmonyLib;
using ZX;
using ZX.GUI;

namespace Billions
{
    // The "Optimization" page of the game's options window (after Controls): the engine.json settings (physics engine,
    // renderer, DXVK frame cap, fast timing, logic catch-up) and the light health bars (options.json). Values are saved
    // when the window's Apply button is pressed; a changed setting that needs a restart sets the game's NeedReboot, so
    // Apply shows the game's own restart prompt.
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
                    LightLifeBars = MiniJson.Bool(m, "lightLifeBars");
                }
            }
            catch (Exception e) { Log.Error("options load", e); }
            LifeMarkers.Mode = LightLifeBars ? 2 : 0;
            Log.Info("options: 좀비 체력바 가볍게 = " + LightLifeBars);
        }

        static void WriteFile(string path, string text)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string tmp = path + ".tmp";
            File.WriteAllText(tmp, text);
            if (File.Exists(path)) File.Replace(tmp, path, null);
            else File.Move(tmp, path);
        }

        public static void Install(Harmony h)
        {
            try
            {
                Load();
                var m = AccessTools.Method(typeof(ZXGame), "ShowOptionsWindow");
                if (m == null) { Log.Info("HOOK MISSING (optional): ZXGame.ShowOptionsWindow — 최적화 탭 없음(설정은 engine.json/options.json 대로)"); return; }
                h.Patch(m, null, new HarmonyMethod(typeof(GameOptions), nameof(AfterShow)));
                Installed = 1; Log.Info("hook: ZXGame.ShowOptionsWindow (postfix, 「최적화」 탭)");
            }
            catch (Exception e) { Installed = 0; Log.Error("options install", e); }
        }

        // one engine.json setting on the page: a combo box (Values) or a check box (true/false); OnApply runs when Apply
        // saves a changed value (settings the mod can change while the game runs)
        sealed class Choice
        {
            public string Key; public bool Reboot; public object[] Values; public object Initial; public ZXComboBox Box; public ZXCheckBox Check;
            public Action<object> OnApply;
            public object Selected
            {
                get
                {
                    if (Check != null) return Check.Checked;
                    int i = Box.SelectedIndex; return i >= 0 && i < Values.Length ? Values[i] : Initial;
                }
            }
        }

        static void AfterShow()
        {
            try
            {
                var root = DXScene.Current != null ? DXScene.Current.RootObject : null; if (root == null) return;
                ZXWindowTab win = null; var rc = _children(root);
                if (rc != null) for (int i = rc.Count - 1; i >= 0 && win == null; i--) win = rc[i] as ZXWindowTab;
                if (win == null || win._OButtonRight == null) return;
                BuildPage(win);
            }
            catch (Exception e) { Log.Error("options page", e); }
        }

        static void BuildPage(ZXWindowTab win)
        {
            var cfgGame = (ZXGameConfiguration)DXGame.Current.Configuration;
            var cfg = Settings.Config();
            float titleW = win.PageSize.Width * 0.4f, labelW = win.PageSize.Width * 0.3f, rowH = DXScene.ScreenVirtualUnit * 20f;
            var size = new SizeF(win.PageSize.Width * 0.3f, rowH);
            var comboSize = new SizeF(size.Width + DXScene.Unit * 80f, rowH);

            var page = win.AddPage(Lang.T("최적화", "Optimization"));
            var grid = DXGridLayout.NewFormLayout(page.Panel.ContentWidth, 0, fitToContent: true);
            page.Panel.Content.AddObject(grid);
            grid.DefaultRowHeight = rowH;
            grid.Border = null;
            grid.DefaultCellSpacing = new SizeF(DXScene.ScreenVirtualUnit * 5f, DXScene.ScreenVirtualUnit * 5f);

            var choices = new List<Choice>();
            Action<string> title = t => grid.AddRow(TextTemplates.Interface.get_TitleSmall().CreateObject(t, new ContentAlignment?(ContentAlignment.MiddleRight), new float?(titleW)));
            Action<string, string, DXObject> row = (label, help, control) =>
            {
                grid.AddRow(new DXObject[] { TextTemplates.Interface.get_NormalBig().CreateObject(label, new ContentAlignment?(ContentAlignment.TopRight), new float?(labelW)), control });
                grid[0, grid.NRows - 1].Object.Tag = new ZXHelp(ZXAlignment.BottomCenter, label, help, null, null, null);
            };
            Func<string, bool, object, object[], string[], Choice> combo = (key, reboot, current, values, names) =>
            {
                var c = new Choice { Key = key, Reboot = reboot, Initial = current, Values = values, Box = new ZXComboBox() };
                int sel = Array.IndexOf(values, current);
                if (sel < 0)   // a value set by hand in engine.json that is not in the list: keep it as its own entry
                {
                    var v = new List<object>(values) { current }; var n = new List<string>(names) { Convert.ToString(current) };
                    c.Values = v.ToArray(); names = n.ToArray(); sel = c.Values.Length - 1;
                }
                c.Box.Items = new List<string>(names);
                c.Box.SelectedIndex = sel;
                c.Box.Size = comboSize;
                if (reboot) c.Box.SelectedIndexChanged += delegate { if (!Equals(c.Selected, c.Initial)) cfgGame.NeedReboot = true; };
                choices.Add(c);
                return c;
            };
            Func<string, bool, Action<object>, Choice> check = (key, current, onApply) =>
            {
                var c = new Choice { Key = key, Initial = current, Check = new ZXCheckBox(), OnApply = onApply };
                c.Check.Size = size; c.Check.Checked = current;
                choices.Add(c);
                return c;
            };
            string restart = Lang.T(" 게임을 다시 시작하면 적용됩니다.", " Applies after a restart.");
            string nextLevel = Lang.T(" 다음에 게임(맵)을 불러올 때 적용됩니다.", " Applies the next time a game (map) is loaded.");

            title(Lang.T("물리", "Physics"));
            var engine = combo("physicsEngine", true, MiniJson.Str(cfg, "physicsEngine", "chipmunk").Trim().ToLowerInvariant(),
                new object[] { "chipmunk", "rapier", "chipmunk-vulkan", "original" },
                new[] { Lang.T("Chipmunk2D 다중 코어(기본)", "Chipmunk2D multi-core (default)"), "Rapier 2D", "Chipmunk2D Vulkan (GPU)", Lang.T("원작 엔진", "Original engine") });
            row(Lang.T("물리 엔진", "Physics engine"), Lang.T(
                "좀비와 건물의 충돌을 계산하는 엔진입니다. Chipmunk2D 다중 코어가 기본이고, Rapier 2D는 좀비가 아주 많을 때 더 빠릅니다. Chipmunk2D Vulkan은 물리 단계를 그래픽 카드(Vulkan)에서 계산합니다(쓸 수 있는 Vulkan 장치가 없으면 Chipmunk2D 다중 코어). 원작 엔진은 이 모드의 물리 최적화를 끕니다.",
                "The engine that computes collisions. Chipmunk2D multi-core is the default; Rapier 2D is faster with very large hordes. Chipmunk2D Vulkan runs the physics step on the graphics card (Vulkan; without a usable Vulkan device, Chipmunk2D multi-core is used). Original turns this mod's physics changes off.") + restart, engine.Box);
            var threads = combo("physicsThreads", false, (object)Features.PhysicsThreads,
                new object[] { 0, 1, 2, 4, 6, 8, 12, 16 }, new[] { Lang.T("자동(기본)", "Auto (default)"), Lang.T("1 (다중 코어 끔)", "1 (no multi-core)"), "2", "4", "6", "8", "12", "16" });
            threads.OnApply = v => Features.SetPhysicsThreads((int)v);
            row(Lang.T("최대 물리 스레드", "Max physics threads"), Lang.T(
                "물리 계산에 쓰는 스레드 수(계산을 이끄는 스레드 포함)입니다. 자동은 물리 코어 수만큼(최대 17)입니다. 다른 프로그램과 CPU를 나눠 쓰거나 발열을 줄이려면 낮춥니다. Chipmunk2D 다중 코어와 Rapier 2D에 적용되고, 원작 엔진은 늘 1개입니다.",
                "Threads the physics uses, the stepping thread included. Auto = one per physical core (at most 17). Lower it to leave CPU for other programs or to run cooler. Applies to Chipmunk2D multi-core and Rapier 2D; the original engine always uses one.") + nextLevel, threads.Box);

            title(Lang.T("그래픽", "Graphics"));
            var renderer = combo("renderer", true, MiniJson.Str(cfg, "renderer", "dxvk").Trim().ToLowerInvariant(),
                new object[] { "dxvk", "d3d9" }, new[] { Lang.T("DXVK (Vulkan, 기본)", "DXVK (Vulkan, default)"), Lang.T("Direct3D 9 (원작)", "Direct3D 9 (original)") });
            row(Lang.T("렌더러", "Renderer"), Lang.T(
                "DXVK는 게임의 Direct3D 9 그리기를 Vulkan으로 바꿔 좀비가 많을 때 프레임이 더 잘 나옵니다. Vulkan을 지원하는 그래픽 드라이버가 필요합니다.",
                "DXVK runs the game's Direct3D 9 drawing on Vulkan, which keeps the frame rate up with large hordes. Needs a Vulkan graphics driver.") + restart, renderer.Box);
            var fps = combo("dxvkMaxFps", true, (object)MiniJson.Int(cfg, "dxvkMaxFps", 0),
                new object[] { 0, 30, 60, 120, 144 }, new[] { Lang.T("제한 없음", "No limit"), "30", "60", "120", "144" });
            row(Lang.T("DXVK 최대 FPS", "DXVK frame cap"), Lang.T("DXVK로 그릴 때 초당 프레임 수의 상한입니다.", "The frame rate limit when drawing with DXVK.") + restart, fps.Box);

            var light = new ZXCheckBox(); light.Size = size; light.Checked = LightLifeBars;
            row(Lang.T("좀비 체력바 가볍게", "Light zombie health bars"), Lang.T(
                "좀비 체력바를 테두리 없는 얇은 막대 하나로 그려 좀비가 많을 때 화면이 덜 느려집니다. 끄면 원작 체력바 그대로입니다. 적용을 누르면 바로 바뀝니다.",
                "Draws zombie health bars as a single thin bar so large hordes render faster. Off = original health bars. Applies right away."), light);

            title(Lang.T("개별 최적화", "Individual optimizations"));
            string rightAway = Lang.T(" 적용을 누르면 바로 바뀝니다.", " Applies right away.");
            var zsort = check("drawOrderSort", Features.Switch("drawOrderSort"), v => Features.SetSwitch("drawOrderSort", (bool)v));
            row(Lang.T("그리기 순서 최적화", "Draw order optimization"), Lang.T(
                "좀비가 많아 물체의 앞뒤 순서가 한 프레임에 아주 많이 바뀌면, 바뀔 때마다 하나씩 다시 끼워 넣는 대신 한꺼번에 정렬합니다(정렬이 오래 걸리면 몇 프레임에 한 번). 끄면 원작처럼 바뀔 때마다 바로 순서를 맞춥니다.",
                "When very many objects change their front-to-back order in one frame (large hordes), sorts them all at once instead of re-inserting each change (every few frames when the sort is slow). Off = keep the order on every change like the original.") + rightAway, zsort.Check);
            var energy = check("energyFix", Features.Switch("energyFix"), v => Features.SetSwitch("energyFix", (bool)v));
            row(Lang.T("전력망 최적화", "Power grid optimization"), Lang.T(
                "건물이 전력망에 이어졌는지를 길 찾기 대신 이어진 구역 번호로 바로 답합니다. 가끔 원작 길 찾기와 맞춰 보고, 다르면 스스로 꺼집니다. 끄면 원작 길 찾기 그대로입니다.",
                "Answers whether a building is connected to the power grid from connected-area labels instead of path finding. It checks itself against the original now and then and turns itself off on a mismatch. Off = the original path finding.") + rightAway
                + (EnergyFix.Installed == 1 ? "" : Lang.T(" (이 게임/모드로더에서는 설치되지 않아 효과가 없습니다.)", " (Not installed with this game / mod loader, so it has no effect.)")), energy.Check);
            var search = check("zombieSearchSkip", Features.Switch("zombieSearchSkip"), v => Features.SetSwitch("zombieSearchSkip", (bool)v));
            row(Lang.T("좀비 탐색 최적화", "Zombie search optimization"), Lang.T(
                "공격할 대상(건물·유닛)이 탐색 거리보다 넉넉히 먼 곳의 좀비는 적 찾기를 건너뜁니다. 대상까지의 거리 지도로 판단하고 여유를 두어, 찾았어도 아무것도 없었을 경우만 건너뜁니다. 끄면 모든 좀비가 원작처럼 매번 적을 찾습니다.",
                "Zombies well beyond search range of any target (buildings, units) skip their enemy search. A distance map to the targets, with a margin, decides, so only searches that would have found nothing are skipped. Off = every zombie searches every time like the original.") + rightAway, search.Check);

            title(Lang.T("게임 시간", "Game time"));
            var fast = check("fastTiming", MiniJson.Bool(cfg, "fastTiming", true), null);
            row(Lang.T("빠른 타이머 루프", "Fast timing loop"), Lang.T(
                "그래픽 탭의 FastTimming이 꺼져 있어도 게임 시간을 실제 경과 시간으로 진행해, 물리 계산이 길어질 때 게임이 느려지지 않게 합니다. 끄면 게임 옵션을 그대로 따릅니다.",
                "Advances game time by the real elapsed time even with the Graphics tab's FastTimming off, so long physics updates don't slow the game down. Off = follow the game option.") + nextLevel, fast.Check);
            var lag = combo("logicMaxLag", false, (object)MiniJson.Int(cfg, "logicMaxLag", 3),
                new object[] { 1, 2, 3, 4, 5, 6 }, new[] { Lang.T("1 (원작)", "1 (original)"), "2", Lang.T("3 (기본)", "3 (default)"), "4", "5", "6" });
            row(Lang.T("로직 따라잡기", "Logic catch-up"), Lang.T(
                "게임 로직이 밀렸을 때 한 프레임에 더 돌릴 수 있는 틱 수입니다. 크면 좀비가 많을 때 게임 속도가 덜 떨어지지만 프레임이 길어질 수 있습니다.",
                "How many extra logic ticks a frame may run when the game logic falls behind. Higher keeps game speed up with large hordes at the cost of longer frames.") + nextLevel, lag.Box);

            title(Lang.T("벤치마크", "Benchmark"));
            bool canBench = InGameBench.CanStart;
            var bench = BenchOverlay.MakeButton(Lang.T("벤치마크 시작", "Start benchmark"), new SizeF(size.Width, rowH * 1.3f));
            bench.Enabled = canBench;
            bench.Activated += delegate
            {
                if (!InGameBench.CanStart) return;
                try { win.Hide(null); } catch { }
                InGameBench.Start();
            };
            row(Lang.T("성능 측정", "Performance test"), Lang.T(
                "고정된 생존 맵(감염체 최다, 80일, 같은 시드)에서 피해를 끄고 마지막 웨이브를 네 번 불러, 좀비 " + InGameBench.MaxZombies.ToString("N0") + "마리 또는 " + InGameBench.MaxSeconds / 60 + "분까지 돌립니다. 화면 오른쪽 위에 FPS·로직 틱·게임 속도·좀비 수와 그래프를 보여 주고, 끝나면 결과를 보여 줍니다. ESC로 언제든 메인 메뉴로 돌아갑니다. 지금 설정(물리 엔진, 렌더러 등) 그대로 잽니다. 결과는 %APPDATA%\\TABPerf\\benchmark-*.csv에 남습니다.",
                "On a fixed survival map (most infected, 80 days, same seed), turns damage off and calls the final swarm four times, then runs until " + InGameBench.MaxZombies.ToString("N0") + " zombies or " + InGameBench.MaxSeconds / 60 + " minutes. Shows FPS, logic ticks, game speed, zombies and graphs at the top right, and the result at the end. ESC returns to the main menu at any time. Measures with the current settings (physics engine, renderer, ...). Results go to %APPDATA%\\TABPerf\\benchmark-*.csv.")
                + (canBench ? "" : Lang.T(" 메인 메뉴에서만 시작할 수 있습니다.", " Can be started from the main menu only.")), bench);

            win._OButtonRight.Activated += delegate
            {
                try
                {
                    if (light.Checked != LightLifeBars)
                    {
                        LightLifeBars = light.Checked;
                        WriteFile(FilePath, "{\"lightLifeBars\":" + (LightLifeBars ? "true" : "false") + "}");
                        try { LifeMarkers.SetMode(LightLifeBars ? 2 : 0); } catch (Exception e) { Log.Error("options life mode", e); }
                        Log.Info("options: 좀비 체력바 가볍게 = " + LightLifeBars);
                    }
                    var changed = new Dictionary<string, object>();
                    foreach (var c in choices)
                    {
                        if (Equals(c.Selected, c.Initial)) continue;
                        changed[c.Key] = c.Selected;
                        if (c.OnApply != null) try { c.OnApply(c.Selected); } catch (Exception e) { Log.Error("options " + c.Key, e); }
                    }
                    if (changed.Count == 0) return;
                    var now = Settings.Config() ?? new Dictionary<string, object>();   // keep the other keys as they are in the file
                    foreach (var kv in changed) now[kv.Key] = kv.Value;
                    WriteFile(Settings.ConfigPath, Pretty(now));
                    var sb = new StringBuilder();
                    foreach (var kv in changed) sb.Append(sb.Length > 0 ? ", " : "").Append(kv.Key).Append(" = ").Append(MiniJson.Serialize(kv.Value));
                    Log.Info("options: engine.json 저장 — " + sb);
                    foreach (var env in new[] { "TABPERF_ENGINE", "TABPERF_RENDERER", "TABPERF_DXVK_FPS", "TABPERF_FASTTIMING", "TABPERF_LOGIC_LAG", "TABPERF_PHYS_THREADS", "TABPERF_ZSORT", "TABPERF_ENERGYFIX", "TABPERF_ZOMBIESEARCH" })
                        if (Settings.Env(env) != null) Log.Info("options: 환경 변수 " + env + " 가 engine.json 보다 앞선다");
                }
                catch (Exception e) { Log.Error("options save", e); }
            };
        }

        // engine.json one key per line, so it stays easy to edit by hand
        static string Pretty(Dictionary<string, object> m)
        {
            var sb = new StringBuilder("{");
            bool first = true;
            foreach (var kv in m)
            {
                sb.Append(first ? "\r\n  " : ",\r\n  ").Append(MiniJson.Serialize(kv.Key)).Append(": ").Append(MiniJson.Serialize(kv.Value));
                first = false;
            }
            return sb.Append(first ? "}" : "\r\n}\r\n").ToString();
        }
    }
}
