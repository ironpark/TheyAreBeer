using System;

namespace Billions
{

    public sealed class EnergyLinks
    {
        public const int Yes = 1, No = 0, Unknown = -1;
        int[] _label = new int[0];
        int[] _queue = new int[0];
        int _w, _h;
        public int Groups { get; private set; }

        public void Build(uint[,] g)
        {
            int w = g.GetLength(0), h = g.GetLength(1), n = w * h;
            if (_label.Length != n) { _label = new int[n]; _queue = new int[n]; } else Array.Clear(_label, 0, n);
            _w = w; _h = h;
            int next = 0;
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++)
                {
                    if (g[x, y] == 0 || _label[x * h + y] != 0) continue;
                    next++;
                    int head = 0, tail = 0;
                    _label[x * h + y] = next; _queue[tail++] = x * h + y;
                    while (head < tail)
                    {
                        int c = _queue[head++], cx = c / h, cy = c - cx * h;
                        if (cx > 0 && g[cx - 1, cy] != 0 && _label[c - h] == 0) { _label[c - h] = next; _queue[tail++] = c - h; }
                        if (cx < w - 1 && g[cx + 1, cy] != 0 && _label[c + h] == 0) { _label[c + h] = next; _queue[tail++] = c + h; }
                        if (cy > 0 && g[cx, cy - 1] != 0 && _label[c - 1] == 0) { _label[c - 1] = next; _queue[tail++] = c - 1; }
                        if (cy < h - 1 && g[cx, cy + 1] != 0 && _label[c + 1] == 0) { _label[c + 1] = next; _queue[tail++] = c + 1; }
                    }
                }
            Groups = next;
        }

        public int Connected(uint[,] g, int ox, int oy, int dx, int dy)
        {
            if (ox == dx && oy == dy) return Yes;
            if (g.GetLength(0) != _w || g.GetLength(1) != _h) return Unknown;
            if (dx < 0 || dy < 0 || dx >= _w || dy >= _h || ox < 0 || oy < 0 || ox >= _w || oy >= _h) return No;
            if (g[dx, dy] == 0) return No;
            int ld = _label[dx * _h + dy];
            if (ld == 0) return Unknown;
            bool stale = false;
            if (ox > 0 && Hit(g, ox - 1, oy, ld, ref stale)) return Yes;
            if (ox < _w - 1 && Hit(g, ox + 1, oy, ld, ref stale)) return Yes;
            if (oy > 0 && Hit(g, ox, oy - 1, ld, ref stale)) return Yes;
            if (oy < _h - 1 && Hit(g, ox, oy + 1, ld, ref stale)) return Yes;
            return stale ? Unknown : No;
        }

        bool Hit(uint[,] g, int x, int y, int ld, ref bool stale)
        {
            if (g[x, y] == 0) return false;
            int l = _label[x * _h + y];
            if (l == 0) { stale = true; return false; }
            return l == ld;
        }
    }
}
