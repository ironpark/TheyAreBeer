using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using AX.ModLoader;
using DXVision;
using HarmonyLib;
using ZX;
using ZX.Components;
using ZX.Entities;
using ZX.GameSystems;

namespace TABBench
{
    // In-game benchmark, separate from TABPerf so that the same measuring code runs on the unmodded game, on any version of
    // TABPerf and on the current one (scripts\bench.ps1 installs it into Mods\TABBench for the runs only). It does nothing
    // unless TABBENCH_CSV names the CSV to write. Settings (environment variables):
    //   TABBENCH_CSV       CSV path (one row per second)
    //   TABBENCH_AUTO      1 (default): start the fixed survival game from the start screen, turn damage off and trigger the
    //                      final swarm TABBENCH_FACTOR times TABBENCH_DELAY seconds into the level, quit once TABBENCH_ZOMBIES
    //                      zombies are alive or TABBENCH_TIMEOUT seconds have passed. 0: only record; Ctrl+Shift+F9 triggers the swarm.
    //   TABBENCH_SEED (20261104) TABBENCH_DELAY (5) TABBENCH_FACTOR (4) TABBENCH_ZOMBIES (100000) TABBENCH_TIMEOUT (3600)
    //   TABBENCH_ZOOM      max (default): the camera at the widest view the game allows, so every run draws the same view; off
    //
    // What is measured, on the render thread (ZXSystem_GameLevel.OnStartFrame runs once per frame, after the frame waited for
    // the previous logic task and before it updates and draws the scene):
    //  - frame time: the time between two frame starts, minus the time this mod spent in its own frame hook (the second's
    //    bookkeeping and CSV row), so the mod's own work is never counted;
    //  - logic wait: the part of the frame spent waiting for the logic task the previous frame started (the logic ticks).
    //    DXGame.SetCamera runs the systems' OnSetCamera, then DXGame.WaitForGameLogic, then the frame-start callbacks; the
    //    wait is how far that task's end (the end of its body, <UpdateGameLogic>b__*) lies after ZXSystem_GameLevel's
    //    OnSetCamera, or 0 when it had already ended. (WaitForGameLogic itself is not hooked: it is a few bytes of IL that
    //    the JIT inlines into its caller, so a patch on it is not reliably called.) The rest of the frame is the render
    //    thread's own work (scene update, drawing, present);
    //  - zombies (the game's per-type entity count), the game clock (DXGame.GameTime) and the process CPU time, once a second.
    public class BenchMod : IModEntry
    {
        static bool _installed;
        public override void OnLoad(Mod mod)
        {
            try
            {
                if (_installed) return; _installed = true;
                Bench.Install(new Harmony("tabbench"));
            }
            catch (Exception e) { Log.Write("OnLoad: " + e); }
        }
    }

    static class Log
    {
        static readonly string Path0 = Path.Combine(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "TABPerf"), "tabbench.log");
        public static void Write(string s)
        {
            try { Directory.CreateDirectory(Path.GetDirectoryName(Path0)); File.AppendAllText(Path0, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff", CultureInfo.InvariantCulture) + " " + s + "\r\n"); } catch { }
        }
    }

    static class Bench
    {
        static string _csv;
        static bool _auto, _zoomMax, _terrain;
        static int _seed, _delay, _factor, _zombies, _timeout;
        static volatile int _pending;

        static string Env(string name, string def) { var v = Environment.GetEnvironmentVariable(name); return string.IsNullOrEmpty(v) ? def : v.Trim(); }
        static int EnvInt(string name, int def, int lo, int hi) { int v; return int.TryParse(Env(name, ""), NumberStyles.Integer, CultureInfo.InvariantCulture, out v) ? Math.Max(lo, Math.Min(hi, v)) : def; }

        public static void Install(Harmony h)
        {
            _csv = Env("TABBENCH_CSV", null);
            if (_csv == null) return;
            _auto = Env("TABBENCH_AUTO", "1") != "0";
            _seed = EnvInt("TABBENCH_SEED", 20261104, int.MinValue, int.MaxValue);
            _delay = EnvInt("TABBENCH_DELAY", 5, 1, 600);
            _factor = EnvInt("TABBENCH_FACTOR", 4, 1, 20);
            _zombies = EnvInt("TABBENCH_ZOMBIES", 100000, 1, 10000000);
            _timeout = EnvInt("TABBENCH_TIMEOUT", 3600, 30, 86400);
            _zoomMax = Env("TABBENCH_ZOOM", "max").ToLowerInvariant() != "off";
            _terrain = Env("TABBENCH_TERRAIN", "0") == "1";
            Patch(h, AccessTools.Method(typeof(ZXSystem_GameLevel), "OnStartFrame"), null, nameof(OnStartFrame));
            Patch(h, LogicTaskBody(), null, nameof(TaskPost));
            Patch(h, AccessTools.Method(typeof(ZXSystem_GameLevel), "ProcessLevelEvents"), null, nameof(AfterEvents));
            if (_auto) Patch(h, AccessTools.Method(typeof(ZXSystem_StartScreen), "ShowScene"), null, nameof(AfterStartScreen));
            Patch(h, AccessTools.Method(typeof(ZXSystem_GameLevel), "OnSetCamera"), null, nameof(AfterSetCamera));
            Log.Write("==== TABBench ==== csv " + _csv + (_auto ? " · auto: seed " + _seed + ", swarm x" + _factor + " after " + _delay + " s, stop at " + _zombies + " zombies or " + _timeout + " s" : " · manual (Ctrl+Shift+F9)")
                + " · zoom " + (_zoomMax ? "max" : "off") + " · cpu " + Environment.ProcessorCount);
        }

        static void Patch(Harmony h, System.Reflection.MethodInfo m, string pre, string post)
        {
            if (m == null) { Log.Write("hook missing — benchmark off"); _csv = null; return; }
            h.Patch(m, pre != null ? new HarmonyMethod(typeof(Bench), pre) : null, post != null ? new HarmonyMethod(typeof(Bench), post) : null);
        }

        // ---------- start screen -> fixed survival game ----------
        static bool _started;
        static void AfterStartScreen()
        {
            if (_csv == null || !_auto || _started) return;
            _started = true;
            // The start screen is shown from inside the game's first scene change; a second ChangeScene before that one ends
            // loses its fade callback. Wait until it is over and the menu settled.
            new Thread(() =>
            {
                try
                {
                    int calm = 0;
                    for (int i = 0; i < 600 && calm < 20; i++) { Thread.Sleep(100); calm = ZXGame.get_Current().ChangingScene || ZXGame.get_Current().IsLoading ? 0 : calm + 1; }
                    DXGame.Current.InvokeOnStartFrame(StartSurvival);
                }
                catch (Exception e) { Log.Write("start wait: " + e); }
            }) { IsBackground = true, Name = "TABBench start" }.Start();
        }
        // The survival window's "Start Game" (ZXSystem_StartScreen.NewSurvivalGame) with fixed settings: most infected (150%),
        // shortest duration (80 days), the map theme with the highest score factor; the fixed seed makes the same map.
        static void StartSurvival()
        {
            try
            {
                const string name = "TABBench";
                ZXGame.get_Current().DeleteSaveGames(name);
                var p = new ZXRandomLevelParams();
                ZXMapTheme best = null;
                foreach (var t in ZXMapTheme.get_AllThemes().Values) if (best == null || t.ScoreFactor > best.ScoreFactor) best = t;
                p.ThemeType = best.MapThemeType; p.FactorZombiePopulation = 1.5f; p.FactorGameDuration = 0.8f;
                p.Seed = _seed; p.Name = name; p.NCells = 256; p.DifficultyType = (ZXSurvivalDifficultyType)3;
                Log.Write("survival game: theme " + best.Name + " · score factor " + Math.Round(p.ScoreFactor * 100) + "% · seed " + p.Seed
                    + " · game options: HighPerformanceTiming " + DXGame.Current.Configuration.HighPerformanceTiming + ", VSync " + DXScene.VSyncEnabled);
                ZXGame.get_Current().ChangeScene(20, delegate
                {
                    ZXGameState.set_Current(new ZXGameState(name));
                    ZXGameState.get_Current().GameMode = ZXGameModeType.Survival;
                    ZXGameState.get_Current().SurvivalModeParams = p;
                    ZXLevelState.set_Current(new ZXLevelState());
                    ZXLevelState.get_Current().WriteCurrentGameState();
                    ZXGame.get_Current().CurrentGameSystem = DXSystem.Load<ZXSystem_GameLevel>(false);
                    (ZXGame.get_Current().CurrentGameSystem as ZXSystem_GameLevel).LoadLevel(ZXLevelGenerator.Create(p));
                }, delegate { ZXGame.get_Current().CurrentGameSystem.ShowScene(); }, new List<string> { "TABBench" }, true);
            }
            catch (Exception e) { Log.Write("start: " + e); }
        }

        // ---------- camera at the widest view the game allows (its own zoom-out limit, see OnSetCamera) ----------
        static bool _zoomLogged;
        static void AfterSetCamera(ZXSystem_GameLevel __instance)
        {
            if (_csv == null) return;
            _cameraAt = Stopwatch.GetTimestamp();   // the logic wait starts right after this (see the class comment)
            try
            {
                if (!_zoomMax || !DXGame.Current.InputEnabled || DXLevel.Current == null) return;
                var pa = DXLevel.Current.PlayableArea;
                var screen = DXScene.ScreenArea;
                if (screen.Height <= 0 || pa.Width <= 0) return;
                float aspect = (float)screen.Width / screen.Height;
                float max = Math.Min(Math.Min(45f * aspect, pa.Width), pa.Width * DXScene.ScreenVirtualRatio * 0.5625f * 0.5625f * aspect);
                var cur = __instance.Scene.CameraArea;
                if (cur.Width <= 0 || cur.Width >= max - 0.01f) return;
                var view = DXScreenParameters.Current.ViewArea;
                float h = view.Width > 0 ? max * view.Height / view.Width : cur.Height * max / cur.Width;
                float cx = cur.X + cur.Width / 2, cy = cur.Y + cur.Height / 2;
                __instance.SetCameraArea(new System.Drawing.RectangleF(cx - max / 2, cy - h / 2, max, h));
                if (!_zoomLogged) { _zoomLogged = true; Log.Write("zoom: camera width " + cur.Width.ToString("0.0", CultureInfo.InvariantCulture) + " -> " + max.ToString("0.0", CultureInfo.InvariantCulture)); }
            }
            catch (Exception e) { Log.Write("zoom: " + e); _zoomMax = false; }
        }

        // ---------- final swarm, on the game's event thread ----------
        static void AfterEvents()
        {
            if (_pending == 0) return;
            _pending = 0;
            try
            {
                var ls = ZXLevelState.get_Current(); if (ls == null || ls.LevelEvents == null) return;
                ZXLevelEvent fin = null;
                foreach (var e in ls.LevelEvents) if (e != null && e.FinalSwarm) { fin = e; break; }
                if (fin == null) { Log.Write("no final swarm event in this level"); return; }
                if (!fin.Created) fin.Create();
                int before = fin.NTimesTriggered;
                for (int i = 0; i < _factor; i++) { fin.NTimesTriggered = before; fin.Trigger(); }   // each one sized like a first trigger
                fin.NTimesTriggered = before;   // the scheduled final swarm still comes at its time
                int per = 0; try { per = fin.GetInfectedCount(); } catch { }
                Log.Write("final swarm x" + _factor + " (about " + per + " each, " + (fin.CurrentUnitGenerators != null ? fin.CurrentUnitGenerators.Count : 0) + " generators)");
                _note = "swarm x" + _factor;
            }
            catch (Exception e) { Log.Write("swarm: " + e); }
        }

        // ---------- logic wait ----------
        static long _cameraAt, _taskEnd, _waitTicks;
        // the logic task's body: the compiler-generated instance lambda of DXGame.UpdateGameLogic without parameters
        static System.Reflection.MethodInfo LogicTaskBody()
        {
            foreach (var m in typeof(DXGame).GetMethods(System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.DeclaredOnly))
                if (m.Name.StartsWith("<UpdateGameLogic>b__", StringComparison.Ordinal) && m.GetParameters().Length == 0 && m.ReturnType == typeof(void)) return m;
            return null;
        }
        static long _tasks;
        static void TaskPost() { Volatile.Write(ref _taskEnd, Stopwatch.GetTimestamp()); Interlocked.Increment(ref _tasks); }

        // ---------- per frame / per second (render thread) ----------
        [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vk);
        static bool _keyDown;
        static long _levelAt, _frameAt, _ownTicks, _secAt, _secOwn;
        static long _frames, _frameTicks, _frameMax, _secWait;
        static double _gtAt, _cpuAt;
        static long _iaAt;
        static bool _swarmed, _done;
        static volatile string _note = "";

        static void OnStartFrame()
        {
            if (_csv == null || _done) return;
            long now = Stopwatch.GetTimestamp();
            try
            {
                long end = Volatile.Read(ref _taskEnd);
                if (_cameraAt != 0 && end > _cameraAt) _waitTicks += Math.Min(end, now) - _cameraAt;
                if (_frameAt != 0)
                {
                    long d = now - _frameAt - _ownTicks;   // the previous frame, without this hook's own work in it
                    _frames++; _frameTicks += d; if (d > _frameMax) _frameMax = d;
                }
                _frameAt = now;
                Second(now);
            }
            catch (Exception e) { Log.Write("frame: " + e); }
            _ownTicks = Stopwatch.GetTimestamp() - now;
            _secOwn += _ownTicks;
        }

        static void Second(long now)
        {
            var g = DXGame.Current;
            if (_levelAt == 0)
            {
                _levelAt = _secAt = now; _gtAt = g.GameTime; _cpuAt = Cpu(); _waitTicks = 0; _iaAt = g.IAIteration; Interlocked.Exchange(ref _tasks, 0);
                Log.Write("level started");
                return;
            }
            double sec = (now - _levelAt) / (double)Stopwatch.Frequency;
            if (_auto)
            {
                if (g.Paused && sec >= 2) g.Paused = false;   // keep the clock running through the start messages
                if (!_swarmed && sec >= _delay) { _swarmed = true; CLife.DamageAvailable = false; _pending = 1; Log.Write("damage off, final swarm x" + _factor + " requested"); }
            }
            else
            {
                bool down = (GetAsyncKeyState(0x11) & 0x8000) != 0 && (GetAsyncKeyState(0x10) & 0x8000) != 0 && (GetAsyncKeyState(0x78) & 0x8000) != 0;
                if (down && !_keyDown) { _pending = 1; Log.Write("Ctrl+Shift+F9: final swarm x" + _factor + " requested"); }
                _keyDown = down;
            }
            if (now - _secAt < Stopwatch.Frequency) return;
            int zombies = g.CountEntitiesOfType<Zombie>();
            bool stop = _auto && (zombies >= _zombies || sec >= _timeout);
            if (_terrain) _inTerrain = InTerrain();
            Row(now, sec, zombies, stop ? (zombies >= _zombies ? "end" : "timeout") : "");
            if (stop)
            {
                _done = true;
                Log.Write("done at " + sec.ToString("0", CultureInfo.InvariantCulture) + " s, " + zombies + " zombies — quitting");
                Environment.Exit(0);
            }
        }

        // ---------- check (TABBENCH_TERRAIN=1): zombies standing on cells the map marks inaccessible ----------
        // The map's inaccessible terrain (water, cliffs...) is one entity with CAccesibleLayer: its AccesibleArea grid
        // ([x, y], false = inaccessible) blocks pathfinding, and static rectangles over those cells block the physics.
        // Walks every zombie under the logic lock, so it costs frame time (taken out like the rest of this hook's work)
        // and holds the logic for a moment: for correctness runs, not for timing runs. -1: no such layer.
        static int _inTerrain = -1;
        static bool[,] _access;
        static int InTerrain()
        {
            lock (DXGame.SyncLogic)
            {
                var g = DXGame.Current;
                if (_access == null)
                    foreach (var e in g.EntitiesOfType<DXEntity>())
                    {
                        var l = e.GetComponent<CAccesibleLayer>();
                        if (l != null && l.AccesibleArea != null) { _access = l.AccesibleArea; Log.Write("terrain check: layer " + _access.GetLength(0) + "x" + _access.GetLength(1)); break; }
                    }
                if (_access == null) return -1;
                int n = 0, w = _access.GetLength(0), h = _access.GetLength(1);
                foreach (var z in g.EntitiesOfType<Zombie>())
                {
                    var c = DXWorldGrid.CellFromPoint(z.Position);
                    if (c.X >= 0 && c.Y >= 0 && c.X < w && c.Y < h && !_access[c.X, c.Y]) n++;
                }
                return n;
            }
        }

        static bool _header;
        static void Row(long now, double sec, int zombies, string end)
        {
            var c = CultureInfo.InvariantCulture;
            var g = DXGame.Current;
            double real = (now - _secAt) / (double)Stopwatch.Frequency, tf = 1000.0 / Stopwatch.Frequency;
            double gt = g.GameTime, cpu = Cpu();
            string note = _note; _note = "";
            if (end.Length > 0) note = note.Length > 0 ? note + " " + end : end;
            string line = string.Join(",", sec.ToString("0.0", c), _frames.ToString(c),
                (_frames > 0 ? _frameTicks * tf / _frames : 0).ToString("0.00", c), (_frameMax * tf).ToString("0.00", c),
                (_frames > 0 ? _waitTicks * tf / _frames : 0).ToString("0.00", c), Interlocked.Exchange(ref _tasks, 0).ToString(c), (g.IAIteration - _iaAt).ToString(c),
                zombies.ToString(c), _inTerrain.ToString(c), (gt / 1000.0).ToString("0.00", c), ((gt - _gtAt) / 1000.0 / real).ToString("0.000", c),
                ((cpu - _cpuAt) / (real * 1000.0)).ToString("0.00", c), (_secOwn * tf).ToString("0.000", c), g.Paused ? "1" : "0", note);
            if (!_header && !File.Exists(_csv))
            {
                Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(_csv)));
                File.AppendAllText(_csv, "t_s,frames,frame_ms,frame_max_ms,logic_wait_ms,logic_tasks,logic_ticks,zombies,zombies_in_terrain,game_time_s,game_speed,cpu_cores,bench_ms,paused,note\r\n");
            }
            _header = true;
            File.AppendAllText(_csv, line + "\r\n");
            _secAt = now; _gtAt = gt; _cpuAt = cpu; _iaAt = g.IAIteration;
            _frames = 0; _frameTicks = 0; _frameMax = 0; _waitTicks = 0; _secOwn = 0;
        }

        static double Cpu() { using (var p = Process.GetCurrentProcess()) return p.TotalProcessorTime.TotalMilliseconds; }
    }
}
