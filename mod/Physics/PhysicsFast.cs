using System;
using System.Collections.Generic;
using System.Drawing;
using System.Reflection;
using System.Threading;

namespace Billions
{
    // Managed versions of the replacement Rapier DLL's simple body getters and setters. The game's body handle is the
    // DLL's body record (DxBody *), and Body_GetPosition / GetVelocity / GetForce / GetMass / GetAngle / GetUserData and
    // Body_SetForce / SetVelocity only read or write fields of it (lock-free, the same plain loads and stores). The game
    // makes several of these calls per moving body per step (about half a million per step with 70,000 zombies), and
    // each P/Invoke transition costs far more than the load itself, so these do the same memory access here.
    // The DLL reports the record layout (DX_SetOption 206-216); anything unexpected keeps the native calls.
    // TABPERF_FASTBODY=0 turns this off.
    public static unsafe class PhysicsFast
    {
        const int LayoutVersion = 1;
        static int _pos, _vel, _force, _angle, _dirty, _type, _mass, _user, _dvel;
        public static bool On { get; private set; }

        // Replaces the redirect targets of the covered imports when the DLL is the Rapier replacement with the
        // expected layout. Returns how many imports now go to managed code.
        internal static int Apply(Dictionary<MethodInfo, MethodInfo> map, Func<int, int, int> opt)
        {
            On = false;
            if (opt == null || !Settings.Switch("TABPERF_FASTBODY", true)) return 0;
            try
            {
                if (opt(200, 0) != 2 || opt(206, 0) != LayoutVersion) return 0;
                int size = opt(207, 0);
                _pos = opt(208, 0); _vel = opt(209, 0); _force = opt(210, 0); _angle = opt(211, 0);
                _dirty = opt(212, 0); _type = opt(213, 0); _mass = opt(214, 0); _user = opt(215, 0); _dvel = opt(216, 0);
                // 8-byte fields aligned (one atomic load/store, like the DLL's), 4-byte fields aligned, all inside the record
                if (size <= 0 || size > 4096 || _dvel <= 0) return 0;
                foreach (int o in new[] { _pos, _vel, _force, _user }) if (o < 0 || o + 8 > size || (o & 7) != 0) return 0;
                foreach (int o in new[] { _angle, _dirty, _type, _mass }) if (o < 0 || o + 4 > size || (o & 3) != 0) return 0;
            }
            catch (Exception e) { Log.Error("물리 빠른 접근 배치 확인", e); return 0; }

            int n = 0;
            var keys = new List<MethodInfo>(map.Keys);
            foreach (var k in keys)
            {
                var to = typeof(PhysicsFast).GetMethod(k.Name, BindingFlags.Public | BindingFlags.Static, null, Array.ConvertAll(k.GetParameters(), p => p.ParameterType), null);
                if (to == null || to.ReturnType != k.ReturnType) continue;
                map[k] = to; n++;
            }
            On = n > 0;
            return n;
        }

        static PointF Pt(long v) { float* f = (float*)&v; return new PointF(f[0], f[1]); }
        static long Pk(float x, float y) { long v; float* f = (float*)&v; f[0] = x; f[1] = y; return v; }
        static long Load(IntPtr b, int off) { return Volatile.Read(ref *(long*)((byte*)b + off)); }

        public static PointF Body_GetPosition(IntPtr body) { return body == IntPtr.Zero ? new PointF(0f, 0f) : Pt(Load(body, _pos)); }
        public static PointF Body_GetVelocity(IntPtr body) { return body == IntPtr.Zero ? new PointF(0f, 0f) : Pt(Load(body, _vel)); }
        public static PointF Body_GetForce(IntPtr body) { return body == IntPtr.Zero ? new PointF(0f, 0f) : Pt(Load(body, _force)); }
        public static float Body_GetAngle(IntPtr body) { return body == IntPtr.Zero ? 0f : Volatile.Read(ref *(float*)((byte*)body + _angle)); }
        public static IntPtr Body_GetUserData(IntPtr body) { return body == IntPtr.Zero ? IntPtr.Zero : Volatile.Read(ref *(IntPtr*)((byte*)body + _user)); }
        public static float Body_GetMass(IntPtr body)
        {
            if (body == IntPtr.Zero) return 0f;
            return Volatile.Read(ref *(int*)((byte*)body + _type)) == 2 ? *(float*)((byte*)body + _mass) : float.PositiveInfinity;
        }
        // force: a plain store, read by the next step (no dirty flag, like the DLL)
        public static void Body_SetForce(IntPtr body, float x, float y) { if (body != IntPtr.Zero) Volatile.Write(ref *(long*)((byte*)body + _force), Pk(x, y)); }
        // velocity: store, then mark it for the next push (InterlockedOr in the DLL)
        public static void Body_SetVelocity(IntPtr body, float x, float y)
        {
            if (body == IntPtr.Zero) return;
            Volatile.Write(ref *(long*)((byte*)body + _vel), Pk(x, y));
            int* d = (int*)((byte*)body + _dirty);
            int o;
            do { o = *d; } while (Interlocked.CompareExchange(ref *d, o | _dvel, o) != o);
        }
    }
}
