using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using DXVision;
using HarmonyLib;
using ZX;
using ZX.Components;
using ZX.Entities;
using ZX.GameSystems;
using ZX.GUI;
using DXVision.GUI;

namespace Billions
{
    // The benchmark of the Optimization tab: from the main menu, the fixed survival game of the README benchmark (most
    // infected, 80 days, the theme with the highest score factor, seed 20261104), damage off and the final swarm four times
    // 5 s in, the camera at the widest zoom; until 100,000 zombies or 5 minutes, or ESC (the game menu key) back to the
    // main menu. An overlay (BenchOverlay) shows the numbers and graphs each second; the result is shown on the main menu,
    // logged and written to %APPDATA%\TABPerf\benchmark-<time>.csv.
    // Nothing of it runs in normal play: its hooks are patched when a benchmark first starts, and the per-frame call
    // (PerfHooks.OnStartFrame) returns at once unless one runs.
    public static class InGameBench
    {
        public const int MaxZombies = 100000, MaxSeconds = 300, SwarmDelay = 5, SwarmFactor = 4, Seed = 20261104;
        const string SaveName = "TABPerf Benchmark";

        public static volatile bool Active;
        static readonly bool Prof = Settings.Switch("TABPERF_BENCHPROF", false);
        static Harmony _h;
        static bool _hooked, _starting;

        public static void Init(Harmony h)
        {
            _h = h;
            // TABPERF_AUTOBENCH=1 (testing): start the benchmark once the main menu has settled, as the button would
            if (Settings.Switch("TABPERF_AUTOBENCH", false))
                OnMenu(() => { Log.Info("벤치마크: TABPERF_AUTOBENCH — 자동 시작"); Start(); }, 2000);
        }

        public static bool CanStart { get { return !Active && !_starting && OnStartScreen; } }

        static bool OnStartScreen
        {
            get
            {
                try { var g = ZXGame.get_Current(); return g != null && g.CurrentGameSystem is ZXSystem_StartScreen && !g.ChangingScene && !g.IsLoading; }
                catch { return false; }
            }
        }

        // Work for the main menu once it has been up, without a scene change or loading, for `calmMs` (the result window,
        // the automatic start): checked by a DXGame.StartFrame postfix on the render thread, patched the first time.
        static Action _onMenu; static int _calmMs; static long _calmSince;
        static bool _menuHooked;
        static void OnMenu(Action a, int calmMs)
        {
            if (!_menuHooked)
            {
                var sf = AccessTools.Method(typeof(DXGame), "StartFrame", Type.EmptyTypes);
                if (sf == null) { Log.Info("HOOK MISSING: DXGame.StartFrame — 벤치마크 메뉴 작업 못 함"); return; }
                _h.Patch(sf, null, new HarmonyMethod(typeof(InGameBench), nameof(MenuFrame)));
                _menuHooked = true;
            }
            _calmMs = calmMs; _calmSince = 0; _onMenu = a;
        }

        static void MenuFrame()
        {
            var a = _onMenu; if (a == null) return;
            if (!OnStartScreen) { _calmSince = 0; return; }
            long now = Stopwatch.GetTimestamp();
            if (_calmSince == 0) { _calmSince = now; return; }
            if ((now - _calmSince) * 1000 < _calmMs * Stopwatch.Frequency) return;
            _onMenu = null;
            try { a(); } catch (Exception e) { Log.Error("benchmark menu", e); }
        }

        // from the Optimization tab's button (main menu)
        public static void Start()
        {
            if (!CanStart || _h == null) return;
            try
            {
                if (!_hooked) Hook();
                _starting = true;
                DXGame.Current.InvokeOnStartFrame(StartSurvival);
            }
            catch (Exception e) { _starting = false; Log.Error("benchmark start", e); }
        }

        static void Hook()
        {
            var events = AccessTools.Method(typeof(ZXSystem_GameLevel), "ProcessLevelEvents");
            var cam = AccessTools.Method(typeof(ZXSystem_GameLevel), "OnSetCamera");
            var menu = AccessTools.Method(typeof(ZXSystem_GameLevel), "ShowGameMenu");
            if (events == null || menu == null) throw new MissingMethodException("ZXSystem_GameLevel.ProcessLevelEvents / ShowGameMenu");
            _h.Patch(events, null, new HarmonyMethod(typeof(InGameBench), nameof(AfterEvents)));
            if (cam != null) _h.Patch(cam, null, new HarmonyMethod(typeof(InGameBench), nameof(AfterSetCamera)));
            _h.Patch(menu, new HarmonyMethod(typeof(InGameBench), nameof(MenuPre)));
            Log.Info("hook: 벤치마크 — ZXSystem_GameLevel.ProcessLevelEvents (postfix, 마지막 웨이브) · OnSetCamera (postfix, 최대 줌) · ShowGameMenu (prefix, ESC 로 끝내기)");
            _hooked = true;
            if (!Prof) return;
            // TABPERF_BENCHPROF=1: per-frame segments of the render thread (columns after draw_ms in the CSV). Some of
            // these run per object or per entity and slow the frame down themselves, so a plain benchmark goes without.
            Seg(typeof(DXGame), "StartFrame", nameof(StartPost));
            Seg(typeof(DXScene), "RenderScene", nameof(ScenePost));
            Seg(typeof(DXGame), "FinishFrame", nameof(FinishPost));
            // inside the update pass: the root's RenderObject (wall), and the parallel chunks over the root's children
            // (their total time across threads, and first start to last end)
            var ro = AccessTools.Method(typeof(DXScene), "RenderObject", new[] { typeof(DXObject) });
            if (ro != null) _h.Patch(ro, new HarmonyMethod(typeof(InGameBench), nameof(RootPre)), new HarmonyMethod(typeof(InGameBench), nameof(RootPost)));
            Seg(typeof(DXGame), "SetCamera", nameof(CamPost));
            Seg(typeof(ZXSystem_GameLevel), "OnStartFrame", nameof(SysPost));
            Seg(typeof(DXEntity), "UpdateSceneObject", nameof(UsoPost));
            var rc = AccessTools.Method(typeof(DXScene), "RenderRootChildren");
            if (rc != null) _h.Patch(rc, new HarmonyMethod(typeof(InGameBench), nameof(ChunkPre)), new HarmonyMethod(typeof(InGameBench), nameof(ChunkPost)));
            Log.Info("벤치마크: 구간 계측 켬(TABPERF_BENCHPROF) — 계측 자체가 프레임을 늘림");
        }

        // one segment: SegPre starts the clock (while a benchmark runs), the named postfix adds the time to its total
        static void Seg(Type t, string name, string post)
        {
            var m = AccessTools.Method(t, name, Type.EmptyTypes);
            if (m != null) _h.Patch(m, new HarmonyMethod(typeof(InGameBench), nameof(SegPre)), new HarmonyMethod(typeof(InGameBench), post));
        }
        static void SegPre(out long __state) { __state = Active ? Stopwatch.GetTimestamp() : 0; }
        static long Since(long start) { return start != 0 ? Stopwatch.GetTimestamp() - start : 0; }
        static long _segStart, _segScene, _segFinish, _segCam, _segSys, _segUso;
        static void StartPost(long __state) { _segStart += Since(__state); }
        static void ScenePost(long __state) { _segScene += Since(__state); }
        static void FinishPost(long __state) { _segFinish += Since(__state); }
        static void CamPost(long __state) { _segCam += Since(__state); }
        static void SysPost(long __state) { _segSys += Since(__state); }
        static void UsoPost(long __state) { _segUso += Since(__state); }
        static long _segRoot, _segChunks, _segForWall, _nChunks, _forStart, _forEnd;
        static void RootPre(DXObject o, out long __state)
        {
            __state = 0;
            if (!Active || o == null || o._Parent != null) return;
            __state = Stopwatch.GetTimestamp(); _forStart = long.MaxValue; _forEnd = 0;
        }
        static void RootPost(long __state)
        {
            if (__state == 0) return;
            _segRoot += Stopwatch.GetTimestamp() - __state;
            if (_forEnd > 0 && _forStart != long.MaxValue) _segForWall += _forEnd - _forStart;
        }
        static void ChunkPre(out long __state)
        {
            __state = 0; if (!Active) return;
            long t = Stopwatch.GetTimestamp(); __state = t;
            long cur; while (t < (cur = Interlocked.Read(ref _forStart)) && Interlocked.CompareExchange(ref _forStart, t, cur) != cur) { }
        }
        static void ChunkPost(long __state)
        {
            if (__state == 0) return;
            long t = Stopwatch.GetTimestamp();
            Interlocked.Add(ref _segChunks, t - __state); Interlocked.Increment(ref _nChunks);
            long cur; while (t > (cur = Interlocked.Read(ref _forEnd)) && Interlocked.CompareExchange(ref _forEnd, t, cur) != cur) { }
        }

        static void StartSurvival()
        {
            try
            {
                var game = ZXGame.get_Current();
                game.DeleteSaveGames(SaveName);
                var p = new ZXRandomLevelParams();
                ZXMapTheme best = null;
                foreach (var t in ZXMapTheme.get_AllThemes().Values) if (best == null || t.ScoreFactor > best.ScoreFactor) best = t;
                p.ThemeType = best.MapThemeType; p.FactorZombiePopulation = 1.5f; p.FactorGameDuration = 0.8f;
                p.Seed = Seed; p.Name = SaveName; p.NCells = 256; p.DifficultyType = (ZXSurvivalDifficultyType)3;
                Reset();
                Active = true;
                DrawFast.Measure = Prof; DrawFast.NFast = DrawFast.NSlow = DrawFast.SlowTicks = 0; DrawFast.SlowKinds.Clear();
                Log.Info("벤치마크: 시작 — 생존 게임 " + best.Name + " · 시드 " + Seed + " · 좀비 " + MaxZombies + " 마리 또는 " + MaxSeconds + " 초까지 · 물리 DLL " + PerfMod.PhysicsDll() + (D3d9.Label.Length > 0 ? " + " + D3d9.Label : ""));
                game.ChangeScene(20, delegate
                {
                    ZXGameState.set_Current(new ZXGameState(SaveName));
                    ZXGameState.get_Current().GameMode = ZXGameModeType.Survival;
                    ZXGameState.get_Current().SurvivalModeParams = p;
                    ZXLevelState.set_Current(new ZXLevelState());
                    ZXLevelState.get_Current().WriteCurrentGameState();
                    game.CurrentGameSystem = DXSystem.Load<ZXSystem_GameLevel>(false);
                    (game.CurrentGameSystem as ZXSystem_GameLevel).LoadLevel(ZXLevelGenerator.Create(p));
                }, delegate { game.CurrentGameSystem.ShowScene(); }, new List<string> { SaveName }, true);
            }
            catch (Exception e) { Active = false; Log.Error("benchmark survival", e); }
            finally { _starting = false; }
        }

        // ---------- state ----------
        static long _levelAt, _secAt, _frameAt, _frames, _frameTicks; static double _drawMs;
        static double _gtAt, _cpuAt; static long _iaAt;
        static bool _swarmed; static volatile int _pending;
        static int _zombiesMax;
        static readonly List<double[]> _rows = new List<double[]>();   // sec, zombies, fps, frame ms, ticks/s, speed, cpu cores
        static string _note = "";

        static void Reset()
        {
            _levelAt = _secAt = _frameAt = _frames = _frameTicks = 0; _swarmed = false; _pending = 0; _zombiesMax = 0;
            _rows.Clear(); _note = "";
        }

        // render thread, every frame of a level while a benchmark runs (PerfHooks.OnStartFrame checks Active)
        public static void Frame()
        {
            long now = Stopwatch.GetTimestamp();
            try
            {
                var g = DXGame.Current;
                if (ZXLevelState.get_Current() == null || !(ZXGame.get_Current().CurrentGameSystem is ZXSystem_GameLevel)) return;
                if (_levelAt == 0)
                {
                    _levelAt = _secAt = now; _gtAt = g.GameTime; _cpuAt = Cpu(); _iaAt = g.IAIteration;
                    BenchOverlay.Show();
                    return;
                }
                if (_frameAt != 0) { long d = now - _frameAt; _frames++; _frameTicks += d; _drawMs += SceneDraw.LastMs; }
                _frameAt = now;
                double sec = (now - _levelAt) / (double)Stopwatch.Frequency;
                if (g.Paused && sec >= 2 && !DXModalScreen.IsInModal) g.Paused = false;   // through the start messages
                if (!_swarmed && sec >= SwarmDelay) { _swarmed = true; CLife.DamageAvailable = false; _pending = 1; }
                if (now - _secAt < Stopwatch.Frequency) return;
                Second(g, now, sec);
            }
            catch (Exception e) { Log.Error("benchmark frame", e); Stop("error"); }
        }

        static void Second(DXGame g, long now, double sec)
        {
            double span = (now - _secAt) / (double)Stopwatch.Frequency;
            double gt = g.GameTime, cpu = Cpu(); long ia = g.IAIteration;
            int zombies = g.CountEntitiesOfType<Zombie>();
            double perFrame = _frames > 0 ? 1.0 / _frames : 0, tk = 1000.0 / Stopwatch.Frequency * perFrame;   // totals -> per frame (ms)
            double fps = _frames / span, frameMs = _frameTicks * tk, drawMs = _drawMs * perFrame;
            double startMs = _segStart * tk, sceneMs = _segScene * tk, finishMs = _segFinish * tk;
            double rootMs = _segRoot * tk, chunkMs = Interlocked.Read(ref _segChunks) * tk, forMs = _segForWall * tk, chunks = Interlocked.Read(ref _nChunks) * perFrame;
            double camMs = _segCam * tk, sysMs = _segSys * tk, usoMs = _segUso * tk;
            double nFast = DrawFast.NFast * perFrame, nSlow = DrawFast.NSlow * perFrame, slowMs = DrawFast.SlowTicks * tk;
            double speed = (gt - _gtAt) / 1000.0 / span, ticks = (ia - _iaAt) / span, cores = (cpu - _cpuAt) / 1000.0 / span;
            // the next second starts from zero
            _secAt = now; _gtAt = gt; _cpuAt = cpu; _iaAt = ia; _frames = 0; _frameTicks = 0; _drawMs = 0;
            _segStart = _segScene = _segFinish = _segCam = _segSys = _segUso = _segRoot = _segForWall = 0;
            Interlocked.Exchange(ref _segChunks, 0); Interlocked.Exchange(ref _nChunks, 0);
            DrawFast.NFast = DrawFast.NSlow = DrawFast.SlowTicks = 0;
            if (zombies > _zombiesMax) _zombiesMax = zombies;
            _rows.Add(new[] { sec, zombies, fps, frameMs, ticks, speed, cores, drawMs, startMs, sceneMs, finishMs, rootMs, forMs, chunkMs, chunks, camMs, sysMs, usoMs, nFast, nSlow, slowMs });
            BenchOverlay.Update(sec, zombies, fps, frameMs, SceneDraw.Installed ? drawMs : -1, ticks, speed, cores, _note);
            if (zombies >= MaxZombies) Stop("end");
            else if (sec >= MaxSeconds) Stop("timeout");
        }

        // ---------- final swarm, on the game's event thread ----------
        static void AfterEvents()
        {
            if (!Active || _pending == 0) return;
            _pending = 0;
            try
            {
                var ls = ZXLevelState.get_Current(); if (ls == null || ls.LevelEvents == null) return;
                ZXLevelEvent fin = null;
                foreach (var e in ls.LevelEvents) if (e != null && e.FinalSwarm) { fin = e; break; }
                if (fin == null) { Log.Info("벤치마크: 이 맵에 마지막 웨이브가 없다"); return; }
                if (!fin.Created) fin.Create();
                int before = fin.NTimesTriggered;
                for (int i = 0; i < SwarmFactor; i++) { fin.NTimesTriggered = before; fin.Trigger(); }
                fin.NTimesTriggered = before;
                _note = Lang.T("마지막 웨이브 ×", "final swarm x") + SwarmFactor;
                Log.Info("벤치마크: 피해 끔, 마지막 웨이브 ×" + SwarmFactor);
            }
            catch (Exception e) { Log.Error("benchmark swarm", e); }
        }

        // ---------- the widest camera the game allows ----------
        static void AfterSetCamera(ZXSystem_GameLevel __instance)
        {
            if (!Active) return;
            try
            {
                if (!DXGame.Current.InputEnabled || DXLevel.Current == null) return;
                var pa = DXLevel.Current.PlayableArea; var screen = DXScene.ScreenArea;
                if (screen.Height <= 0 || pa.Width <= 0) return;
                float aspect = (float)screen.Width / screen.Height;
                float max = Math.Min(Math.Min(45f * aspect, pa.Width), pa.Width * DXScene.ScreenVirtualRatio * 0.5625f * 0.5625f * aspect);
                var cur = __instance.Scene.CameraArea;
                if (cur.Width <= 0 || cur.Width >= max - 0.01f) return;
                var view = DXScreenParameters.Current.ViewArea;
                float h = view.Width > 0 ? max * view.Height / view.Width : cur.Height * max / cur.Width;
                float cx = cur.X + cur.Width / 2, cy = cur.Y + cur.Height / 2;
                __instance.SetCameraArea(new System.Drawing.RectangleF(cx - max / 2, cy - h / 2, max, h));
            }
            catch { }
        }

        // ---------- ESC (the game menu) ends the benchmark ----------
        static bool MenuPre()
        {
            if (!Active) return true;
            Stop("esc");
            return false;
        }

        // the level went away some other way
        public static void OnLevelUnloaded() { End(); }

        // the benchmark's own state back to normal play; false: none was running
        static bool End()
        {
            if (!Active) return false;
            Active = false;
            DrawFast.Measure = false;
            try { CLife.DamageAvailable = true; } catch { }
            BenchOverlay.Hide();
            return true;
        }

        static void Stop(string reason)
        {
            if (!End()) return;
            try { var kinds = new List<KeyValuePair<string, long>>(DrawFast.SlowKinds); kinds.Sort((a, b) => b.Value.CompareTo(a.Value)); for (int i = 0; i < Math.Min(12, kinds.Count); i++) Log.Info("벤치마크: 빠른 길 밖 스프라이트(표본) " + kinds[i].Value + " · " + kinds[i].Key); } catch { }
            string summary = Summary(reason), csv = WriteCsv();
            Log.Info("벤치마크: 끝(" + reason + ") — " + summary.Replace("\n", " · ") + (csv != null ? " · " + csv : ""));
            try
            {
                var game = ZXGame.get_Current();
                game.ShowStartScreen(delegate { try { game.DeleteSaveGames(SaveName); } catch { } }, true);
                // the result window on the main menu, the next frame after it has settled
                OnMenu(() => DXGame.Current.InvokeOnStartFrame(() => ZXMessageBox.ShowOK(Lang.T("벤치마크 결과", "Benchmark result"), summary + (csv != null ? "\n\n" + csv : ""), null)), 1000);
            }
            catch (Exception e) { Log.Error("benchmark stop", e); }
        }

        static string Summary(string reason)
        {
            var inv = CultureInfo.InvariantCulture;
            int n = _rows.Count;
            double last = n > 0 ? _rows[n - 1][0] : 0, fps = 0, speed = 0, frame = 0;
            int m = 0;
            foreach (var r in _rows) if (r[0] >= SwarmDelay) { fps += r[2]; speed += r[5]; frame += r[3]; m++; }
            if (m > 0) { fps /= m; speed /= m; frame /= m; }
            // the last 10 s: the heaviest part
            double fps10 = 0, speed10 = 0; int k = 0;
            for (int i = Math.Max(0, n - 10); i < n; i++) { fps10 += _rows[i][2]; speed10 += _rows[i][5]; k++; }
            if (k > 0) { fps10 /= k; speed10 /= k; }
            string why = reason == "end" ? Lang.T("좀비 " + MaxZombies.ToString("N0") + " 마리 도달", "reached " + MaxZombies.ToString("N0") + " zombies")
                : reason == "timeout" ? Lang.T(MaxSeconds / 60 + "분 지남", MaxSeconds / 60 + " minutes passed")
                : reason == "esc" ? Lang.T("ESC 로 끝냄", "ended with ESC") : Lang.T("오류로 끝냄", "ended by an error");
            return why + "\n"
                + Lang.T("물리: ", "Physics: ") + PerfMod.PhysicsDll() + (D3d9.Label.Length > 0 ? " · " + D3d9.Label : "") + "\n"
                + Lang.T("시간 ", "Time ") + last.ToString("0", inv) + Lang.T(" 초 · 최대 좀비 ", " s · max zombies ") + _zombiesMax.ToString("N0") + "\n"
                + Lang.T("평균 FPS ", "Average FPS ") + fps.ToString("0.0", inv) + " (" + frame.ToString("0", inv) + " ms) · " + Lang.T("게임 속도 ", "game speed ") + speed.ToString("0.00", inv) + "\n"
                + Lang.T("마지막 10초: FPS ", "Last 10 s: FPS ") + fps10.ToString("0.0", inv) + " · " + Lang.T("게임 속도 ", "game speed ") + speed10.ToString("0.00", inv);
        }

        static string WriteCsv()
        {
            try
            {
                if (_rows.Count == 0) return null;
                Directory.CreateDirectory(Log.Dir);
                string path = Path.Combine(Log.Dir, "benchmark-" + DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture) + ".csv");
                var sb = new StringBuilder("sec,zombies,fps,frame_ms,logic_ticks,game_speed,cpu_cores,draw_ms,start_ms,scene_ms,finish_ms,root_ms,for_wall_ms,chunks_ms,chunks,setcamera_ms,systems_startframe_ms,update_scene_object_ms,fast_sprites,slow_sprites,slow_ms\r\n");
                foreach (var r in _rows) sb.Append(string.Join(",", Array.ConvertAll(r, v => v.ToString("0.###", CultureInfo.InvariantCulture)))).Append("\r\n");
                File.WriteAllText(path, sb.ToString());
                return path;
            }
            catch (Exception e) { Log.Error("benchmark csv", e); return null; }
        }

        static double Cpu() { using (var p = Process.GetCurrentProcess()) return p.TotalProcessorTime.TotalMilliseconds; }
    }
}
