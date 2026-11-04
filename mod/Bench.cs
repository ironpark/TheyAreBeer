using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using DXVision;
using HarmonyLib;
using ZX;
using ZX.Components;
using ZX.Entities;
using ZX.GameSystems;

namespace Billions
{
    // Performance test mode, off unless engine.json has "bench": true (or TABPERF_BENCH=1). Every setting below can also be
    // given as an environment variable, which wins over engine.json (scripts\bench.ps1 uses them):
    //   bench TABPERF_BENCH · benchAuto TABPERF_BENCH_AUTO · benchSeconds TABPERF_BENCH_SECONDS · benchSwarmDelay TABPERF_BENCH_DELAY
    //   benchSwarmFactor TABPERF_BENCH_FACTOR · benchSeed TABPERF_BENCH_SEED · benchThreads TABPERF_BENCH_THREADS
    //   benchZoom TABPERF_BENCH_ZOOM ("max" | "off") · benchCsv TABPERF_BENCH_CSV (CSV path)
    //  - Ctrl+Shift+F9 in a level triggers the final swarm right away, "benchSwarmFactor" times (default 4), on the
    //    game's own event thread (ZXSystem_GameLevel.ProcessLevelEvents), as if it had come due that many times.
    //  - Every second appends a row to %APPDATA%\TABPerf\bench.csv: frames, frame time, physics steps and step time
    //    (DXPhysicWorld.Step = one Space_Step), live zombies, physics engine.
    //  - The automatic run ("benchAuto") starts a fixed survival game, zoomed out as far as the game allows (benchZoom "max",
    //    the default), so every run draws the same view.
    public static class Bench
    {
        public static bool Enabled;
        static int _factor = 4;
        // DX_SetOption 340-347 pairs (time, calls), in this order
        static readonly string CallHeader = string.Concat(Array.ConvertAll(new[] { "angle", "size", "dispose", "sensor" }, k => ",n_" + k + "_per_s,n_" + k + "_ms"));
        static readonly string QueryHeader = string.Concat(Array.ConvertAll(new[] { "area", "radius", "fast", "near", "ray", "space" },
            k => ",q_" + k + "_per_s,q_" + k + "_ms,q_" + k + "_lock_ms,q_" + k + "_cb_ms,q_" + k + "_cand,q_" + k + "_hits"));
        static string _engine;   // read in the first level: the game loads its physics DLL after the mod installs
        static bool _keyDown;
        static volatile int _pending;

        [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vk);
        const int VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_F9 = 0x78;

        public static void Install(Harmony h)
        {
            try
            {
                var m = PhysicsTime.Config();
                Enabled = Settings.Bool(m, "bench", "TABPERF_BENCH", false);
                if (!Enabled) return;
                _factor = Math.Max(1, Math.Min(20, Settings.Int(m, "benchSwarmFactor", "TABPERF_BENCH_FACTOR", 4)));
                _auto = Settings.Bool(m, "benchAuto", "TABPERF_BENCH_AUTO", false);
                _autoSeconds = Math.Max(30, Settings.Int(m, "benchSeconds", "TABPERF_BENCH_SECONDS", 240));
                _autoDelay = Math.Max(1, Settings.Int(m, "benchSwarmDelay", "TABPERF_BENCH_DELAY", 5));
                _autoSeed = Settings.Int(m, "benchSeed", "TABPERF_BENCH_SEED", 20261104);
                _threads = Settings.Int(m, "benchThreads", "TABPERF_BENCH_THREADS", -1);
                string zoom = Settings.Str(m, "benchZoom", "TABPERF_BENCH_ZOOM", "max").ToLowerInvariant();
                if (zoom != "max" && zoom != "off") { Log.Info("bench: benchZoom \"" + zoom + "\" 은 모름 — max 를 쓴다(max · off)"); zoom = "max"; }
                _zoomMax = zoom == "max";
                _csv = Settings.Str(m, "benchCsv", "TABPERF_BENCH_CSV", null);
            }
            catch (Exception e) { Log.Error("bench config", e); return; }
            var pe = AccessTools.Method(typeof(ZXSystem_GameLevel), "ProcessLevelEvents");
            var st = AccessTools.Method(typeof(DXPhysicWorld), "Step", new[] { typeof(double) });
            if (pe == null || st == null) { Log.Info("HOOK MISSING (optional): 성능 시험 — ProcessLevelEvents/DXPhysicWorld.Step 없음"); Enabled = false; return; }
            h.Patch(pe, null, new HarmonyMethod(typeof(Bench), nameof(AfterEvents)));
            h.Patch(st, new HarmonyMethod(typeof(Bench), nameof(StepPre)), new HarmonyMethod(typeof(Bench), nameof(StepPost)));
            if (_auto)
            {
                var ss = AccessTools.Method(typeof(ZXSystem_StartScreen), "ShowScene");
                if (ss == null) { Log.Info("HOOK MISSING (optional): 자동 시험 — ZXSystem_StartScreen.ShowScene 없음"); _auto = false; }
                else { h.Patch(ss, null, new HarmonyMethod(typeof(Bench), nameof(AfterStartScreen))); Log.Info("bench: 자동 시험 — 시작 화면에서 생존 게임(900% 설정, 시드 " + _autoSeed + ")을 바로 시작 · " + _autoDelay + "초 뒤 피해 끔 + 마지막 웨이브 x" + _factor + " · " + _autoSeconds + "초 뒤 게임 종료 · 줌 " + (_zoomMax ? "최대로 뺌" : "그대로")); }
                var cam = AccessTools.Method(typeof(ZXSystem_GameLevel), "OnSetCamera");
                if (_auto && _zoomMax)
                {
                    if (cam == null) Log.Info("HOOK MISSING (optional): 자동 시험 줌 — ZXSystem_GameLevel.OnSetCamera 없음");
                    else h.Patch(cam, null, new HarmonyMethod(typeof(Bench), nameof(AfterSetCamera)));
                }
            }
            Log.Info("bench: 성능 시험 모드 — Ctrl+Shift+F9 = 마지막 웨이브 x" + _factor + " 바로 · 초당 기록 " + CsvPath);
        }

        static string _csv;
        static string CsvPath { get { return _csv ?? Path.Combine(Log.Dir, "bench.csv"); } }

        // ---------- automatic run: start screen -> survival game -> swarm -> exit ----------
        static bool _auto, _autoStarted; static int _autoSeconds, _autoDelay, _autoSeed, _threads;
        static long _levelAt; static bool _swarmed;
        static bool _zoomMax, _zoomLogged;
        static void AfterStartScreen()
        {
            if (!_auto || _autoStarted) return;
            _autoStarted = true;
            // The start screen is shown from inside the game's first scene change; a second ChangeScene before that one
            // ends loses its fade callback (one fade slot) and never loads. Wait until it is over and the menu settled.
            new Thread(() =>
            {
                try
                {
                    int calm = 0;
                    for (int i = 0; i < 600 && calm < 20; i++) { Thread.Sleep(100); calm = ZXGame.get_Current().ChangingScene || ZXGame.get_Current().IsLoading ? 0 : calm + 1; }
                    DXGame.Current.InvokeOnStartFrame(StartSurvival);
                }
                catch (Exception e) { Log.Error("bench auto wait", e); }
            }) { IsBackground = true, Name = "TABPerf bench start" }.Start();
        }
        // The "Start Game" button of the survival window (ZXSystem_StartScreen.NewSurvivalGame) with fixed settings:
        // most infected (150%), shortest duration (80 days), and the map theme with the highest score factor -> the
        // game's highest score factor; a fixed seed makes the same map every run.
        static void StartSurvival()
        {
            try
            {
                const string name = "TABPerfBench";
                ZXGame.get_Current().DeleteSaveGames(name);
                var p = new ZXRandomLevelParams();
                ZXMapTheme best = null;
                foreach (var t in ZXMapTheme.get_AllThemes().Values) if (best == null || t.ScoreFactor > best.ScoreFactor) best = t;
                p.ThemeType = best.MapThemeType; p.FactorZombiePopulation = 1.5f; p.FactorGameDuration = 0.8f;
                p.Seed = _autoSeed; p.Name = name; p.NCells = 256; p.DifficultyType = (ZXSurvivalDifficultyType)3;
                Log.Info("bench: 자동 시험 — 생존 게임 시작 · 테마 " + best.Name + " · 점수 배율 " + Math.Round(p.ScoreFactor * 100) + "% · 시드 " + p.Seed);
                ZXGame.get_Current().ChangeScene(20, delegate
                {
                    ZXGameState.set_Current(new ZXGameState(name));
                    ZXGameState.get_Current().GameMode = ZXGameModeType.Survival;
                    ZXGameState.get_Current().SurvivalModeParams = p;
                    ZXLevelState.set_Current(new ZXLevelState());
                    ZXLevelState.get_Current().WriteCurrentGameState();
                    ZXGame.get_Current().CurrentGameSystem = DXSystem.Load<ZXSystem_GameLevel>(false);
                    (ZXGame.get_Current().CurrentGameSystem as ZXSystem_GameLevel).LoadLevel(ZXLevelGenerator.Create(p));
                }, delegate { ZXGame.get_Current().CurrentGameSystem.ShowScene(); }, new List<string> { "TABPerf bench" }, true);
            }
            catch (Exception e) { Log.Error("bench auto start", e); }
        }
        // Called every level frame in automatic mode.
        static void AutoFrame(long now)
        {
            if (_levelAt == 0)
            {
                _levelAt = now; Log.Info("bench: 자동 시험 — 레벨 시작");
                // benchThreads: physics worker threads for this run (DX_SetOption(0); the DLL default is automatic)
                if (_threads >= 0) try { Log.Info("bench: 물리 작업 스레드 " + _threads + " → " + PerfMod.PhysOpt(0, _threads)); } catch (Exception e) { Log.Error("bench threads", e); }
            }
            double sec = (now - _levelAt) / (double)Stopwatch.Frequency;
            if (DXGame.Current.Paused && sec >= 2) DXGame.Current.Paused = false;   // keep the clock running through start messages
            if (!_swarmed && sec >= _autoDelay)
            {
                _swarmed = true;
                CLife.DamageAvailable = false;   // the game's debug switch: nothing dies, so the crowd only grows and every engine gets the same load
                _pending = 1;
                Log.Info("bench: 자동 시험 — 피해 끔 · 마지막 웨이브 x" + _factor + " 요청");
            }
            if (sec >= _autoDelay + _autoSeconds)
            {
                Row("end");
                PostProf.Dump();
                if (PhysicsRedirect.Module != IntPtr.Zero)
                    Log.Info("bench: 게임 폴더 물리 DLL " + (PhysicsRedirect.GameDllLoaded() ? "도 올라와 있다(게임 시작 때 확인 호출 등)" : "은 올라오지 않았다 — 모든 물리 호출이 모드 쪽으로 갔다"));
                Log.Info("bench: 자동 시험 끝 — 게임 종료");
                Environment.Exit(0);
            }
        }

        // ---------- zoom (automatic run, benchZoom "max") ----------
        // After the game's camera update: the camera at the widest view the game allows (the limit of its own zoom-out,
        // same formula as ZXSystem_GameLevel.OnSetCamera), around the same center. Only while input is on, so the
        // new-game camera animation runs as usual first.
        static void AfterSetCamera(ZXSystem_GameLevel __instance)
        {
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
                if (!_zoomLogged) { _zoomLogged = true; Log.Info("bench: 자동 시험 — 줌 최대 (카메라 폭 " + cur.Width.ToString("0.0", CultureInfo.InvariantCulture) + " → " + max.ToString("0.0", CultureInfo.InvariantCulture) + ")"); }
            }
            catch (Exception e) { Log.Error("bench zoom", e); _zoomMax = false; }
        }

        // ---------- final swarm trigger (event thread) ----------
        static void AfterEvents()
        {
            try
            {
                if (_pending == 0) return;
                _pending = 0;
                var ls = ZXLevelState.get_Current(); if (ls == null || ls.LevelEvents == null) return;
                ZXLevelEvent fin = null;
                foreach (var e in ls.LevelEvents) if (e != null && e.FinalSwarm) { fin = e; break; }
                if (fin == null) { Log.Info("bench: 이 레벨에는 마지막 웨이브(FinalSwarm) 이벤트가 없다"); return; }
                if (!fin.Created) fin.Create();
                int before = fin.NTimesTriggered;
                for (int i = 0; i < _factor; i++) { fin.NTimesTriggered = before; fin.Trigger(); }   // each one sized like a first trigger
                fin.NTimesTriggered = before;   // the scheduled final swarm still comes at its time
                int per = 0; try { per = fin.GetInfectedCount(); } catch { }
                Log.Info("bench: 마지막 웨이브 x" + _factor + " 발생 (한 번에 약 " + per + "마리 · 생성기 " + (fin.CurrentUnitGenerators != null ? fin.CurrentUnitGenerators.Count : 0) + "곳)");
                Row("swarm x" + _factor);
            }
            catch (Exception e) { Log.Error("bench swarm", e); }
        }

        // ---------- physics step timing (physics thread) ----------
        [ThreadStatic] static long _t0;
        static long _steps, _stepTicks, _stepMax;
        static void StepPre() { _t0 = Stopwatch.GetTimestamp(); }
        static void StepPost()
        {
            long d = Stopwatch.GetTimestamp() - _t0;
            Interlocked.Increment(ref _steps); Interlocked.Add(ref _stepTicks, d);
            long m; while (d > (m = Interlocked.Read(ref _stepMax)) && Interlocked.CompareExchange(ref _stepMax, d, m) != m) { }
        }

        // ---------- per-frame / per-second (called from PerfHooks.OnStartFrame) ----------
        static long _frameAt, _secAt, _frames, _frameTicks, _frameMax;
        public static void Frame()
        {
            long now = Stopwatch.GetTimestamp();
            bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
            if (down && !_keyDown) { _pending = 1; Log.Info("bench: Ctrl+Shift+F9 — 다음 이벤트 처리에서 마지막 웨이브 x" + _factor); }
            _keyDown = down;
            if (_auto) AutoFrame(now);
            if (_frameAt != 0) { long d = now - _frameAt; _frames++; _frameTicks += d; if (d > _frameMax) _frameMax = d; }
            _frameAt = now;
            if (_secAt == 0) { _secAt = now; return; }
            if (now - _secAt < Stopwatch.Frequency) return;
            _secAt = now;
            Row("");
        }

        static bool _header; static long _iaLast, _cpuWall; static double _gtLast, _cpuLast;
        static void Row(string note)
        {
            try
            {
                if (_engine == null)
                {
                    _engine = PerfMod.PhysicsDll().Replace(',', ' '); Log.Info("bench: 물리 DLL — " + _engine);
                    // Rapier's stage counters cost about 7% of the step, so they are on only with DX_RAPIER_COUNTERS=1 (bench.ps1 -Counters)
                    if (PerfMod.PhysOpt(200, 0) == 2) Log.Info("bench: Rapier 단계 카운터 " + (PerfMod.PhysOpt(204, -1) == 1 ? "켬" : "끔"));
                    // the wrapper's timed call and query diagnostics (n_angle_* ... q_*) read clocks on hot paths: on for the bench only
                    if (PerfMod.PhysOpt(200, 0) == 2) PerfMod.PhysOpt(356, 1);
                    int adapt = PerfMod.PhysOpt(200, 0) == 2 ? PerfMod.PhysOpt(205, 0) : -1;
                    if (adapt >= 0) Log.Info("bench: Rapier 하위 단계 " + (adapt > 0 ? "자동 " + adapt / 100 + "~" + adapt % 100 : "고정"));
                }
                int zombies = 0; foreach (var z in DXGame.Current.EntitiesOfType<Zombie>()) zombies++;
                long steps = 0, st = 0, sm = 0, frames = 0, ft = 0, fm = 0;
                bool tick = note.Length == 0;   // a marker row (note) leaves the running second alone
                if (tick)
                {
                    steps = Interlocked.Exchange(ref _steps, 0); st = Interlocked.Exchange(ref _stepTicks, 0); sm = Interlocked.Exchange(ref _stepMax, 0);
                    frames = _frames; ft = _frameTicks; fm = _frameMax; _frames = 0; _frameTicks = 0; _frameMax = 0;
                }
                var c = CultureInfo.InvariantCulture;
                // step sections (push, r2Step, pull, contact pairs, query index), ms per step this second, then solver substeps
                // and narrow-phase contact pairs per step (DX_SetOption(308, 309)): only the Rapier
                // DLL counts them (DX_SetOption(305) = steps counted; the other DLLs return -1 for unknown options)
                // then Rapier's own stage times and counts per step (DX_SetOption(310 + k), on with DX_SetOption(204, 1))
                string sections = ",,,,,," + new string(',', Stages.Length);
                // the game's own work around the step, ms per step: queued post methods, the per-body pre-step update
                // (StartUpdatePhysics), and the per-body position read-back (OnNewPositionComputed)
                // then the physics thread's waits, ms per step: for DXPhysicWorld.Sync (held by body creation on other
                // threads) and for DXGame.SyncLogic (held through each game logic update); and the logic updates this second
                // (the game wants 30) with the duration of the last one (IALogicUsageFactor = ms * 30 / 1000)
                string game = ",,,,,,,,,,,,,,,,,,,,,,,,,,,,,";
                if (tick && steps > 0)
                {
                    double f = 1000.0 / Stopwatch.Frequency / steps;
                    var g = DXGame.Current;
                    long ia = g.IAIteration, dia = _iaLast > 0 ? ia - _iaLast : 0; _iaLast = ia;
                    game = string.Join(",", (Interlocked.Exchange(ref PhysicsTime.PostMethodTicks, 0) * f).ToString("0.00", c),
                        (Interlocked.Exchange(ref PhysicsTime.PreTicks, 0) * f).ToString("0.00", c), (Interlocked.Exchange(ref PhysicsTime.PostLoopTicks, 0) * f).ToString("0.00", c),
                        (Interlocked.Exchange(ref PhysicsTime.SyncWaitTicks, 0) * f).ToString("0.00", c), (Interlocked.Exchange(ref PhysicsTime.PostTicks, 0) * f).ToString("0.00", c),
                        dia.ToString(c), (g.IALogicUsageFactor * 1000.0 / 30.0).ToString("0.0", c));
                    // Rapier body creations and type changes this second, with the native time per call (µs; DX_SetOption 330-333)
                    int nc = PerfMod.PhysOpt(331, 0), nt = PerfMod.PhysOpt(333, 0);
                    game += "," + (nc >= 0 ? nc.ToString(c) + "," + (nc > 0 ? (PerfMod.PhysOpt(330, 0) / (double)nc).ToString("0.0", c) : "") : ",")
                        + "," + (nt >= 0 ? nt.ToString(c) + "," + (nt > 0 ? (PerfMod.PhysOpt(332, 0) / (double)nt).ToString("0.0", c) : "") : ",")
                        // per creation: Rapier body insert, collider insert (µs); then the query-index lock wait this second (ms)
                        + "," + (nc > 0 ? (PerfMod.PhysOpt(334, 0) / (double)nc).ToString("0.0", c) + "," + (PerfMod.PhysOpt(335, 0) / (double)nc).ToString("0.0", c) : ",")
                        + "," + (nc >= 0 ? (PerfMod.PhysOpt(336, 0) / 1000.0).ToString("0.0", c) : "");
                    // the game clock: the whole physics update (ms per step), game speed, the usage factors that slow the
                    // clock when above 1 (physics: update ms * 12 * speed / 1000, logic: update ms * 30 * speed / 1000),
                    // and game seconds per real second
                    double gt = g.GameTime, rate = _gtLast > 0 ? (gt - _gtLast) / 1000.0 : 0; _gtLast = gt;
                    game += "," + (Interlocked.Exchange(ref PhysicsTime.UpdateTicks, 0) * f).ToString("0.00", c) + "," + g.GameSpeed.ToString("0.00", c)
                        + "," + g.PhysicsUsageFactor.ToString("0.00", c) + "," + g.IALogicUsageFactor.ToString("0.00", c) + "," + rate.ToString("0.00", c);
                    // the logic rate (LogicProf), this second: UpdateGameLogic calls and their average wait for the previous
                    // task (ms), logic tasks with average start delay and run time (ms), logic ticks run, tasks that ran no
                    // tick, and the game clock loop's time held back by logic / by physics / let through (ms)
                    double tf = 1000.0 / Stopwatch.Frequency;
                    long calls = Interlocked.Exchange(ref LogicProf.Calls, 0), tasks = Interlocked.Exchange(ref LogicProf.Tasks, 0);
                    double callMs = Interlocked.Exchange(ref LogicProf.CallTicks, 0) * tf, taskMs = Interlocked.Exchange(ref LogicProf.TaskTicks, 0) * tf, delayMs = Interlocked.Exchange(ref LogicProf.StartDelayTicks, 0) * tf;
                    game += "," + calls.ToString(c) + "," + (calls > 0 ? callMs / calls : 0).ToString("0.00", c) + "," + tasks.ToString(c)
                        + "," + (tasks > 0 ? delayMs / tasks : 0).ToString("0.00", c) + "," + (tasks > 0 ? taskMs / tasks : 0).ToString("0.00", c)
                        + "," + Interlocked.Exchange(ref LogicProf.TicksRun, 0).ToString(c) + "," + Interlocked.Exchange(ref LogicProf.IdleTasks, 0).ToString(c)
                        + "," + (Interlocked.Exchange(ref LogicProf.BlockIaUs, 0) / 1000.0).ToString("0", c) + "," + (Interlocked.Exchange(ref LogicProf.BlockPhysUs, 0) / 1000.0).ToString("0", c)
                        + "," + (Interlocked.Exchange(ref LogicProf.RunUs, 0) / 1000.0).ToString("0", c);
                    // the whole process's CPU time this second, in cores (all threads: game, render, logic, physics workers)
                    long wall = Stopwatch.GetTimestamp(); double cpu;
                    using (var pr = Process.GetCurrentProcess()) cpu = pr.TotalProcessorTime.TotalMilliseconds;
                    double cores = _cpuWall > 0 ? (cpu - _cpuLast) / ((wall - _cpuWall) * 1000.0 / Stopwatch.Frequency) : 0;
                    _cpuLast = cpu; _cpuWall = wall;
                    game += "," + cores.ToString("0.00", c);
                    // Rapier wrapper calls this second (DX_SetOption 340-349): Body_SetAngle / SetSize / Dispose / AddSensor
                    // calls and total ms (with the wait for the world lock a step holds), pending index entries per rebuild
                    var lk = new int[13]; for (int k = 0; k < 13; k++) lk[k] = PerfMod.PhysOpt(340 + k, 0);
                    for (int k = 0; k < 8; k += 2) game += "," + lk[k + 1].ToString(c) + "," + (lk[k] / 1000.0).ToString("0.0", c);
                    game += "," + (lk[9] > 0 ? (double)lk[8] / lk[9] : 0).ToString("0", c) + "," + lk[10].ToString(c) + "," + lk[11].ToString(c) + "," + lk[12].ToString(c);   // ray walk check (DX_RAPIER_RAYWALK=2), category changes
                    // Rapier's parallel solver this second (DX_SetOption 400-409, with RAPIER_SOLVER_DIAG set): barrier wait and
                    // worker run time summed over the workers (ms), barrier waits, worker 0's serial overflow time (ms), then per
                    // solve: colors, overflow chunks, chunks, workers
                    int sd = PerfMod.PhysOpt(400, 0), solves = sd > 8 ? PerfMod.PhysOpt(409, 0) : 0;
                    game += "," + (sd > 8 ? PerfMod.PhysOpt(401, 0) / 1000.0 : 0).ToString("0", c) + "," + (sd > 8 ? PerfMod.PhysOpt(404, 0) / 1000.0 : 0).ToString("0", c)
                        + "," + (sd > 8 ? PerfMod.PhysOpt(402, 0) : 0).ToString(c) + "," + (sd > 8 ? PerfMod.PhysOpt(403, 0) / 1000.0 : 0).ToString("0.0", c);
                    for (int k = 405; k <= 408; k++) game += "," + (solves > 0 ? PerfMod.PhysOpt(k, 0) / (double)solves : 0).ToString("0.0", c);
                    // queries by kind (DX_SetOption 360 + kind * 6 + field): calls, total ms, ms waiting for the index lock,
                    // ms in the game's callbacks, index entries looked at and hits per call
                    for (int q = 0; q < 6; q++)
                    {
                        int n = PerfMod.PhysOpt(360 + q * 6, 0);
                        game += "," + n.ToString(c);
                        for (int j = 1; j <= 3; j++) game += "," + (PerfMod.PhysOpt(360 + q * 6 + j, 0) / 1000.0).ToString("0.0", c);
                        for (int j = 4; j <= 5; j++) game += "," + (n > 0 ? PerfMod.PhysOpt(360 + q * 6 + j, 0) / (double)n : 0).ToString("0.0", c);
                    }
                }
                if (tick)
                {
                    int n = PerfMod.PhysOpt(305, 0);
                    if (n > 0)
                    {
                        var v = new string[7 + Stages.Length];
                        for (int k = 0; k < 5; k++) v[k] = (PerfMod.PhysOpt(300 + k, 0) / 1000.0 / n).ToString("0.00", c);
                        v[5] = ((double)PerfMod.PhysOpt(308, 0) / n).ToString("0.0", c);
                        v[6] = ((double)PerfMod.PhysOpt(309, 0) / n).ToString("0", c);
                        bool counted = PerfMod.PhysOpt(204, -1) == 1;
                        for (int k = 0; k < Stages.Length; k++)
                            v[7 + k] = !counted ? "" : (PerfMod.PhysOpt(310 + Stages[k], 0) / (Stages[k] < 17 ? 1000.0 : 1.0) / n).ToString(Stages[k] < 17 ? "0.00" : "0", c);
                        sections = string.Join(",", v);
                    }
                    PerfMod.PhysOpt(399, 0);
                }
                string line = string.Join(",", DateTime.Now.ToString("HH:mm:ss", c), _engine, DXGame.Current.Paused ? "1" : "0",
                    frames.ToString(c), (frames > 0 ? Ms(ft) / frames : 0).ToString("0.00", c), Ms(fm).ToString("0.00", c),
                    steps.ToString(c), (steps > 0 ? Ms(st) / steps : 0).ToString("0.00", c), Ms(sm).ToString("0.00", c),
                    zombies.ToString(c), note, sections, game);
                Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(CsvPath)));
                if (!_header && !File.Exists(CsvPath)) File.AppendAllText(CsvPath, "time,engine,paused,frames,frame_avg_ms,frame_max_ms,phys_steps,step_avg_ms,step_max_ms,zombies,note,r_push_ms,r_step_ms,r_pull_ms,r_pairs_ms,r_index_ms,r_substeps,r_contact_pairs," + StageHeader + ",g_postmethods_ms,g_pre_ms,g_post_ms,g_syncwait_ms,g_logicwait_ms,g_ia_per_s,g_ia_ms,n_create_per_s,n_create_us,n_settype_per_s,n_settype_us,n_insert_us,n_collider_us,n_indexwait_ms,g_update_ms,g_speed,g_phys_usage,g_ia_usage,g_clock_rate,l_calls,l_wait_ms,l_tasks,l_start_ms,l_task_ms,l_ticks,l_idle,l_block_ia_ms,l_block_phys_ms,l_run_ms,g_cpu_cores" + CallHeader + ",n_pending,n_ray_diff,n_ray_tie,n_catset,s_wait_ms,s_run_ms,s_waits,s_overflow_ms,s_colors,s_overflow_chunks,s_chunks,s_workers" + QueryHeader + "\r\n");
                _header = true;
                File.AppendAllText(CsvPath, line + "\r\n");
            }
            catch (Exception e) { Log.Error("bench row", e); }
        }
        // Rapier stage counters in the CSV: DX_SetOption(310 + k) index, column name
        static readonly int[] Stages = { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 13, 14, 15, 16, 17, 19 };
        const string StageHeader = "rs_total_ms,rs_user_ms,rs_update_ms,rs_bp_ms,rs_bp_final_ms,rs_np_ms,rs_island_ms,rs_collect_ms,rs_solver_ms,rs_vassembly_ms,rs_vsolve_ms,rs_vupdate_ms,rs_writeback_ms,rs_ccd_ms,rs_pairs,rs_contacts";
        static double Ms(long ticks) { return ticks * 1000.0 / Stopwatch.Frequency; }
    }
}
