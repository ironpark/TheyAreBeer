using System;
using System.Collections.Generic;

namespace Billions
{

    public static class ZOrder
    {
        static int[] _count = new int[0]; static long[] _keys = new long[0];

        public static bool Sort<T>(int[] zs, T[] items, int n, IList<T> dst)
        {
            int minZ = int.MaxValue, maxZ = int.MinValue;
            for (int i = 0; i < n; i++) { int z = zs[i]; if (z < minZ) minZ = z; if (z > maxZ) maxZ = z; }
            long range = (long)maxZ - minZ + 1;
            if (n > 0 && range <= Math.Max(4L * n, 65536L) && range <= (1L << 22))
            {
                int r = (int)range;
                if (_count.Length < r + 1) _count = new int[Math.Max(r + 1, _count.Length * 2)];
                Array.Clear(_count, 0, r + 1);
                for (int i = 0; i < n; i++) _count[zs[i] - minZ + 1]++;
                for (int k = 1; k <= r; k++) _count[k] += _count[k - 1];
                for (int i = 0; i < n; i++) dst[_count[zs[i] - minZ]++] = items[i];
                return true;
            }
            if (_keys.Length < n) _keys = new long[n * 2];
            for (int i = 0; i < n; i++) _keys[i] = ((long)zs[i] << 32) | (uint)i;
            Array.Sort(_keys, items, 0, n);
            for (int i = 0; i < n; i++) dst[i] = items[i];
            return false;
        }
    }
}
