using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Threading;
using DXVision;
using HarmonyLib;

namespace Billions
{
    // TABModLoader v1.1.7: DXPhysicBody keeps the GCHandle it gives the physics DLL as the body's user data
    // (_PrivateUserDataHandle) and frees it in Dispose, right after Body_Dispose (v1.1.6 never freed it). The game's world
    // queries (DXPhysicWorld.GetNearestEntity, EntitiesAt, ...) run on logic threads without DXPhysicWorld.Sync and turn
    // the user data the DLL hands back into the body with GCHandle.FromIntPtr; a query that read the pointer just before
    // a disposal on the physics thread could then use a freed (or already reused) handle. Here Dispose leaves the handle
    // alone and it is freed at the next physics update's DXGame.SyncLogic section (PhysicsTime), when no logic runs.
    // Installed only when the field exists, i.e. on v1.1.7 and later.
    public static class BodyHandles
    {
        public static int Installed;
        static AccessTools.FieldRef<DXPhysicBody, GCHandle> _handle;
        static AccessTools.FieldRef<DXPhysicBody, bool> _disposed;
        static readonly ConcurrentQueue<GCHandle> _queue = new ConcurrentQueue<GCHandle>();
        public static long Deferred;

        public static void Install(Harmony h)
        {
            try
            {
                if (AccessTools.Field(typeof(DXPhysicBody), "_PrivateUserDataHandle") == null || AccessTools.Field(typeof(DXPhysicBody), "_Disposed") == null)
                    return;   // v1.1.6: Dispose does not free the handle
                if (PhysicsTime.Installed != 1) { Log.Info("몸체 핸들 늦게 풀기: 물리 보간 기준(PhysicsTime)이 없어 쓰지 않음 — 원작대로 바로 풂"); return; }
                var dispose = AccessTools.Method(typeof(DXPhysicBody), "Dispose", Type.EmptyTypes);
                if (dispose == null) { Log.Info("HOOK MISSING (optional): DXPhysicBody.Dispose"); return; }
                _handle = AccessTools.FieldRefAccess<DXPhysicBody, GCHandle>("_PrivateUserDataHandle");
                _disposed = AccessTools.FieldRefAccess<DXPhysicBody, bool>("_Disposed");
                h.Patch(dispose, new HarmonyMethod(typeof(BodyHandles), nameof(Pre)), null, null, new HarmonyMethod(typeof(BodyHandles), nameof(Fin)));
                Installed = 1;
                Log.Info("hook: DXPhysicBody.Dispose (사용자 데이터 GCHandle 을 다음 물리 갱신의 SyncLogic 구간에서 풂)");
            }
            catch (Exception e) { Installed = 0; Log.Error("body handles install", e); }
        }

        // Dispose runs under DXPhysicWorld.Sync; taking it first (reentrant) keeps the handle swap and Dispose together
        static void Pre(DXPhysicBody __instance, out GCHandle __state)
        {
            Monitor.Enter(DXPhysicWorld.Sync);
            __state = _handle(__instance);
            _handle(__instance) = default(GCHandle);
        }

        static Exception Fin(DXPhysicBody __instance, GCHandle __state, Exception __exception)
        {
            try
            {
                if (__state.IsAllocated)
                {
                    if (_disposed(__instance)) { _queue.Enqueue(__state); Interlocked.Increment(ref Deferred); }
                    else if (!_handle(__instance).IsAllocated) _handle(__instance) = __state;   // disposal deferred until after the step: keep it
                    else __state.Free();
                }
            }
            finally { Monitor.Exit(DXPhysicWorld.Sync); }
            return __exception;
        }

        // Called by PhysicsTime inside lock (DXGame.SyncLogic), once per physics update: frees the handles queued before
        // the previous update (so a query on a thread outside the logic lock, such as the render thread, has had a whole
        // update to finish with a pointer it read).
        static int _ready;
        public static void FreeQueued()
        {
            if (Installed != 1) return;
            int n = _ready;
            GCHandle g;
            while (n-- > 0 && _queue.TryDequeue(out g)) { try { if (g.IsAllocated) g.Free(); } catch { } }
            _ready = _queue.Count;
        }
    }
}
