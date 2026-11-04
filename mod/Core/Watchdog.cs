using System;
using System.Diagnostics;
using System.Threading;
using DXVision;
using HarmonyLib;

namespace Billions
{

    public static class Watchdog
    {
        public static int StallMs = 6000;
        static long _beat; static volatile bool _inLevel; static Thread _main, _logic; static bool _reported;
        static AccessTools.FieldRef<DXGame, Thread> _phys;
        public static Thread MainThread { get { return _main; } } public static Thread LogicThread { get { return _logic; } }
        public static volatile string Phase = "";

        public static void Start()
        {
            try { _phys = AccessTools.FieldRefAccess<DXGame, Thread>("_ThreadPhysic"); } catch { }
            var t = new Thread(Loop) { IsBackground = true, Name = "tab-watchdog", Priority = ThreadPriority.AboveNormal }; t.Start();
            Log.Info("watchdog: 그리기 " + StallMs / 1000 + "초 멈춤 시 스택 기록");
        }

        public static void Beat() { _main = Thread.CurrentThread; _inLevel = true; Interlocked.Exchange(ref _beat, Clock.Ms); }
        public static void Logic() { _logic = Thread.CurrentThread; }
        public static void LeftLevel() { _inLevel = false; }

        static void Loop()
        {
            while (true)
            {
                Thread.Sleep(1000);
                try
                {
                    long gap = Clock.Ms - Interlocked.Read(ref _beat);
                    if (!_inLevel || gap < StallMs) { _reported = false; continue; }
                    if (_reported) continue; _reported = true;
                    Thread phys = null; try { if (DXGame.Current != null && _phys != null) phys = _phys(DXGame.Current); } catch { }
                    string a = Grab(_main), b = Grab(phys), c = _logic != null && _logic != _main ? Grab(_logic) : "(같음/없음)";
                    Log.Info("WATCHDOG 그리기 " + gap + " ms 멈춤 · 먼 좀비 단계 [" + Phase + "]\n── 그리기 스레드:\n" + a + "\n── 물리 스레드:\n" + b + "\n── 논리 스레드:\n" + c);
                }
                catch (Exception e) { try { Log.Error("watchdog", e); } catch { } }
            }
        }

#pragma warning disable 618
        static string Grab(Thread t)
        {
            if (t == null) return "(없음)";
            if (!t.IsAlive) return "(끝남)";
            StackTrace st = null; bool sus = false;
            try { t.Suspend(); sus = true; st = new StackTrace(t, false); }
            catch (Exception e) { return "(스택 못 받음: " + e.GetType().Name + ")"; }
            finally { if (sus) try { t.Resume(); } catch { } }
            return st.ToString();
        }
#pragma warning restore 618
    }
}
