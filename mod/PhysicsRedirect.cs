using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using DXVision;
using HarmonyLib;

namespace Billions
{
    // Sends the game's physics calls to a DXPhysicsNative.dll in the mod folder, without touching the game files.
    //
    // The game declares its physics imports in DXVision.DXPhysicsNative ([DllImport("DXPhysicsNative.dll")], compiled into
    // TheyAreBillions.exe), and .NET Framework binds that name to the DLL in the exe's folder first - so a replacement
    // in the mod folder is never used, even when loaded before. Here the same imports are emitted again, on a dynamic
    // type whose DllImport names the replacement by its full path, and every call to the game's imports (in
    // DXPhysicBody / DXPhysicWorld) is rewritten by a Harmony transpiler to call those instead. The calls stay plain
    // P/Invoke calls with the game's own signatures and marshalling, so they cost the same as before.
    public static class PhysicsRedirect
    {
        public static IntPtr Module { get; private set; }   // the replacement DLL, or zero when the game's DLL is used
        // DX_SetOption of the replacement (emitted with the other imports), or null when the game's DLL is used
        public static Func<int, int, int> SetOption { get; private set; }
        // the game folder's DLL; DXVision is loaded from memory (no Assembly.Location), so the exe's folder is used
        public static readonly string GameDll = Path.Combine(Path.GetDirectoryName(Process.GetCurrentProcess().MainModule.FileName), "DXPhysicsNative.dll");

        // one game import: the declaration, its entry point and parameter types
        sealed class Import
        {
            public MethodInfo M; public DllImportAttribute A; public string Entry; public Type[] Types;
            public Import(MethodInfo m)
            {
                M = m;
                A = (DllImportAttribute)Attribute.GetCustomAttribute(m, typeof(DllImportAttribute));
                if (A == null) throw new InvalidOperationException("no DllImport on " + m.Name);
                Entry = string.IsNullOrEmpty(A.EntryPoint) ? m.Name : A.EntryPoint;
                Types = Array.ConvertAll(m.GetParameters(), q => q.ParameterType);
            }
        }

        static Dictionary<MethodInfo, MethodInfo> _map;
        static readonly List<MethodBase> _patched = new List<MethodBase>();

        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
        [DllImport("kernel32", CharSet = CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module, string name);
        // An export of the replacement DLL as a delegate, or null (game's DLL, or no such export).
        internal static T Proc<T>(string name) where T : class
        {
            IntPtr p = Module != IntPtr.Zero ? GetProcAddress(Module, name) : IntPtr.Zero;
            return p != IntPtr.Zero ? Marshal.GetDelegateForFunctionPointer(p, typeof(T)) as T : null;
        }
        const uint LOAD_WITH_ALTERED_SEARCH_PATH = 8;   // the DLL's own folder is searched for its dependencies (rapier2d_ffi.dll)

        public static bool Install(Harmony h, string dll)
        {
            var sw = Stopwatch.StartNew();
            Type native = typeof(DXPhysicBody).Assembly.GetType("DXVision.DXPhysicsNative");
            if (native == null) { Log.Info("물리 연결: DXVision.DXPhysicsNative 를 못 찾음 — 게임 폴더의 물리 DLL 을 쓴다"); return false; }
            var imports = new List<Import>();
            foreach (var m in native.GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
                if ((m.Attributes & MethodAttributes.PinvokeImpl) != 0) imports.Add(new Import(m));

            IntPtr mod = LoadLibraryExW(dll, IntPtr.Zero, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (mod == IntPtr.Zero) { Log.Info("물리 연결: " + dll + " 을 못 올림(오류 " + Marshal.GetLastWin32Error() + ") — 게임 폴더의 물리 DLL 을 쓴다"); return false; }
            // The replacement must have every function the game's DLL has (the game declares a few that neither has, e.g.
            // Body_GetCategory; calling those fails the same way with either DLL). Unknown game DLL: require all.
            HashSet<string> game = null;
            try { game = Exports(GameDll); } catch (Exception e) { Log.Info("물리 연결: 게임 폴더 물리 DLL 의 함수 목록을 못 읽음(" + e.Message + ") — 선언된 함수가 모두 있어야 한다"); }
            int missingBoth = 0;
            foreach (var m in imports)
            {
                string ep = m.Entry;
                if (GetProcAddress(mod, ep) != IntPtr.Zero) continue;
                if (game != null && !game.Contains(ep)) { missingBoth++; continue; }
                Log.Info("물리 연결: " + dll + " 에 " + ep + " 이 없다 — 게임 폴더의 물리 DLL 을 쓴다");
                return false;
            }
            if (missingBoth > 0) Log.Info("물리 연결: 게임이 선언만 하고 두 DLL 모두에 없는 함수 " + missingBoth + "개는 그대로 둔다(원작에서도 부르면 실패)");

            MethodInfo setOption;
            _map = Emit(imports, dll, GetProcAddress(mod, "DX_SetOption") != IntPtr.Zero, out setOption);
            var opt = setOption != null ? (Func<int, int, int>)Delegate.CreateDelegate(typeof(Func<int, int, int>), setOption) : null;
            // the simple body getters/setters of the Rapier replacement go to managed code (PhysicsFast)
            int fast = PhysicsFast.Apply(_map, opt);
            if (fast > 0) Log.Info("물리 연결: 몸체 값 읽기·쓰기 " + fast + "종은 DLL 호출 없이 몸체 기록을 직접 읽고 쓴다");

            // callers: every method of the game assembly whose IL calls one of the imports
            var targets = Callers(native.Assembly, imports);
            var tp = new HarmonyMethod(typeof(PhysicsRedirect), nameof(Transpiler));
            try
            {
                foreach (var t in targets) { h.Patch(t, null, null, tp); _patched.Add(t); }
            }
            catch (Exception e)
            {
                // all or nothing: mixing the two DLLs would pass one DLL's handles to the other
                foreach (var t in _patched) try { h.Unpatch(t, HarmonyPatchType.Transpiler, h.Id); } catch { }
                _patched.Clear();
                Log.Error("물리 연결 패치 — 게임 폴더의 물리 DLL 을 쓴다", e);
                return false;
            }
            Module = mod;
            SetOption = opt;
            if (GameDllLoaded()) Log.Info("물리 연결: 게임 폴더의 물리 DLL 이 이미 올라와 있다(모드가 실리기 전에 불렸다) — 그 뒤 호출만 모드 쪽으로 간다");
            var names = targets.ConvertAll(t => t.DeclaringType.Name + "." + t.Name);
            Log.Info("물리 연결: 게임의 물리 호출 " + imports.Count + "종을 " + dll + " 로 돌림 — 호출하는 메서드 " + targets.Count + "개 (" + sw.ElapsedMilliseconds + " ms): " + string.Join(", ", names.ToArray()));
            return true;
        }

        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
        // whether the game folder's DXPhysicsNative.dll is loaded as well (a call that went past the redirect loads it)
        public static bool GameDllLoaded() { return GetModuleHandleW(GameDll) != IntPtr.Zero; }

        // Export names of a PE file, read from the file (loading the game's DLL here would run its initialization).
        static HashSet<string> Exports(string path)
        {
            byte[] d = File.ReadAllBytes(path);
            int pe = BitConverter.ToInt32(d, 0x3c), opt = pe + 24;
            if (BitConverter.ToUInt32(d, pe) != 0x4550) throw new InvalidDataException("not a PE file");
            bool pe64 = BitConverter.ToUInt16(d, opt) == 0x20b;
            int rva = BitConverter.ToInt32(d, opt + (pe64 ? 112 : 96));
            int nsec = BitConverter.ToUInt16(d, pe + 6), sec = opt + BitConverter.ToUInt16(d, pe + 20);
            Func<int, int> off = r =>
            {
                for (int i = 0; i < nsec; i++)
                {
                    int s = sec + i * 40, vsize = BitConverter.ToInt32(d, s + 8), va = BitConverter.ToInt32(d, s + 12), rsize = BitConverter.ToInt32(d, s + 16), raw = BitConverter.ToInt32(d, s + 20);
                    if (r >= va && r < va + Math.Max(vsize, rsize)) return r - va + raw;
                }
                throw new InvalidDataException("bad RVA");
            };
            var names = new HashSet<string>();
            if (rva == 0) return names;
            int e = off(rva), n = BitConverter.ToInt32(d, e + 24), table = off(BitConverter.ToInt32(d, e + 32));
            for (int i = 0; i < n; i++)
            {
                int o = off(BitConverter.ToInt32(d, table + 4 * i)), end = Array.IndexOf(d, (byte)0, o);
                names.Add(System.Text.Encoding.ASCII.GetString(d, o, end - o));
            }
            return names;
        }

        // The imports again, same names, signatures, calling conventions and parameter marshalling, bound to the full path;
        // plus DX_SetOption (the replacements' own option call, not declared by the game) when the DLL has it.
        static Dictionary<MethodInfo, MethodInfo> Emit(List<Import> imports, string dll, bool withSetOption, out MethodInfo setOption)
        {
            var ab = AppDomain.CurrentDomain.DefineDynamicAssembly(new AssemblyName("TABPerf.PhysicsImports"), AssemblyBuilderAccess.Run);
            var tb = ab.DefineDynamicModule("TABPerf.PhysicsImports").DefineType("DXPhysicsImports", TypeAttributes.Public | TypeAttributes.Abstract | TypeAttributes.Sealed);
            const MethodAttributes attrs = MethodAttributes.Public | MethodAttributes.Static | MethodAttributes.HideBySig | MethodAttributes.PinvokeImpl;
            // Without SuppressUnmanagedCodeSecurity every call also runs the runtime's unmanaged-code permission check
            // (a stack walk); the game makes several calls per body per step. In the game's final swarm (70,000 zombies)
            // the per-body work around the step went from 10.1 to 3.9 ms a step with it. TABPERF_SUCS=0 leaves it off.
            if (Environment.GetEnvironmentVariable("TABPERF_SUCS") != "0")
                tb.SetCustomAttribute(new CustomAttributeBuilder(typeof(System.Security.SuppressUnmanagedCodeSecurityAttribute).GetConstructor(Type.EmptyTypes), new object[0]));
            foreach (var m in imports)
            {
                var pm = tb.DefinePInvokeMethod(m.M.Name, dll, m.Entry, attrs, CallingConventions.Standard, m.M.ReturnType, m.Types, m.A.CallingConvention, m.A.CharSet);
                pm.SetImplementationFlags(pm.GetMethodImplementationFlags() | (m.A.PreserveSig ? MethodImplAttributes.PreserveSig : 0));
                Marshalling(pm.DefineParameter(0, ParameterAttributes.Retval, null), m.M.ReturnParameter);
                var ps = m.M.GetParameters();
                for (int i = 0; i < ps.Length; i++) Marshalling(pm.DefineParameter(i + 1, ps[i].Attributes, ps[i].Name), ps[i]);
            }
            var opt = new[] { typeof(int), typeof(int) };
            if (withSetOption)
            {
                var pm = tb.DefinePInvokeMethod("DX_SetOption", dll, "DX_SetOption", attrs, CallingConventions.Standard, typeof(int), opt, CallingConvention.Cdecl, CharSet.Ansi);
                pm.SetImplementationFlags(pm.GetMethodImplementationFlags() | MethodImplAttributes.PreserveSig);
            }
            Type t = tb.CreateType();
            var map = new Dictionary<MethodInfo, MethodInfo>();
            foreach (var m in imports) map[m.M] = t.GetMethod(m.M.Name, BindingFlags.Static | BindingFlags.Public, null, m.Types, null);
            setOption = withSetOption ? t.GetMethod("DX_SetOption", BindingFlags.Static | BindingFlags.Public, null, opt, null) : null;
            return map;
        }

        static readonly FieldInfo[] MarshalFields = typeof(MarshalAsAttribute).GetFields(BindingFlags.Public | BindingFlags.Instance);
        static void Marshalling(ParameterBuilder pb, ParameterInfo p)
        {
            var ma = (MarshalAsAttribute)Attribute.GetCustomAttribute(p, typeof(MarshalAsAttribute));
            if (ma == null) return;
            var fields = new List<FieldInfo>(); var values = new List<object>();
            foreach (var f in MarshalFields)
            {
                // only the fields that are set (a default 0 ArraySubType or SizeParamIndex would change the marshalling)
                object v = f.GetValue(ma);
                if (v == null || (v is string && (string)v == "") || (v is bool && !(bool)v) || (v.GetType().IsValueType && Convert.ToInt64(v) == 0)) continue;
                fields.Add(f); values.Add(v);
            }
            pb.SetCustomAttribute(new CustomAttributeBuilder(typeof(MarshalAsAttribute).GetConstructor(new[] { typeof(UnmanagedType) }), new object[] { ma.Value }, fields.ToArray(), values.ToArray()));
        }

        // Methods whose IL has "call <import>": scans the IL bytes for the call opcode followed by an import's token
        // (a stray match only costs an extra transpiler pass that changes nothing).
        static List<MethodBase> Callers(Assembly asm, List<Import> imports)
        {
            var tokens = new HashSet<int>(); foreach (var m in imports) tokens.Add(m.M.MetadataToken);
            var found = new List<MethodBase>();
            Type[] types;
            try { types = asm.GetTypes(); } catch (ReflectionTypeLoadException e) { types = e.Types; }
            const BindingFlags all = BindingFlags.Static | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            var methods = new List<MethodBase>();
            foreach (var t in types)
            {
                if (t == null || t.IsGenericTypeDefinition) continue;
                methods.Clear();
                try { methods.AddRange(t.GetMethods(all)); methods.AddRange(t.GetConstructors(all)); } catch { continue; }
                foreach (var m in methods)
                {
                    if (m.IsAbstract || m.IsGenericMethodDefinition || (m.Attributes & MethodAttributes.PinvokeImpl) != 0 || (m.IsStatic && m is ConstructorInfo)) continue;
                    byte[] il;
                    try { var b = m.GetMethodBody(); il = b != null ? b.GetILAsByteArray() : null; } catch { continue; }
                    if (il == null) continue;
                    for (int i = 0; i + 4 < il.Length; i++)
                        if (il[i] == 0x28 && tokens.Contains(BitConverter.ToInt32(il, i + 1))) { found.Add(m); break; }
                }
            }
            return found;
        }

        static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> code)
        {
            foreach (var ins in code)
            {
                MethodInfo to;
                if ((ins.opcode == OpCodes.Call || ins.opcode == OpCodes.Callvirt) && ins.operand is MethodInfo mi && _map.TryGetValue(mi, out to)) ins.operand = to;
                yield return ins;
            }
        }
    }
}
