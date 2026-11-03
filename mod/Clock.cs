using System.Diagnostics;

namespace Billions
{

    public static class Clock
    {
        public const long StartMs = 3600000;
        static readonly Stopwatch Sw = Stopwatch.StartNew();
        public static long Ms { get { return StartMs + Sw.ElapsedMilliseconds; } }
    }
}
