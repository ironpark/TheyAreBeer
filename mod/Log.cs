using System;
using System.Globalization;
using System.IO;
using System.Text;

namespace Billions
{

    public static class Log
    {
#if PERFMOD
        public static readonly string Dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "TABPerf");
#else
        public static readonly string Dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Billions");
#endif
        public static readonly string File = Path.Combine(Dir, "billions.log");
        public const long RotateMax = 8L * 1024 * 1024;
        public const int Keep = 2;

        public const long SessionMax = 64L * 1024 * 1024;
        private static readonly object Gate = new object();
        static DateTime _day = DateTime.MinValue;
        static long _written; static bool _capped;

        public static void RotateIfLarge()
        {
            try
            {
                lock (Gate)
                {
                    var fi = new FileInfo(File);
                    if (!fi.Exists || fi.Length <= RotateMax) return;
                    string oldest = Old(Keep);
                    if (System.IO.File.Exists(oldest)) System.IO.File.Delete(oldest);
                    for (int i = Keep - 1; i >= 1; i--) { string f = Old(i); if (System.IO.File.Exists(f)) System.IO.File.Move(f, Old(i + 1)); }
                    System.IO.File.Move(File, Old(1));
                    _day = DateTime.MinValue;
                }
            }
            catch { }
        }
        static string Old(int i) { return Path.Combine(Dir, "billions." + i + ".log"); }

        public static void Info(string msg)
        {
            try
            {
                lock (Gate)
                {
                    if (_capped) return;
                    Directory.CreateDirectory(Dir);
                    var now = DateTime.Now;
                    string t = now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture);
                    string head = "";
                    if (now.Date != _day) { _day = now.Date; head = t + " ==== " + now.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture) + " ====" + Environment.NewLine; }
                    string text = head + t + " " + msg + Environment.NewLine;
                    _written += Encoding.UTF8.GetByteCount(text);
                    if (_written > SessionMax) { _capped = true; text = t + " ==== 로그가 이번 실행에서 " + (SessionMax >> 20) + " MB 를 넘어 여기서 멈춤(다음 실행 때 다시) ====" + Environment.NewLine; }
                    System.IO.File.AppendAllText(File, text, Encoding.UTF8);
                }
            }
            catch { }
        }

        public static void Error(string where, Exception e)
        {
            Info("[ERR] " + where + ": " + e);
        }
    }
}
