using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;

namespace Billions
{
    // Measuring aid (TABPERF_RENDERDUMP=1): writes %APPDATA%\TABPerf\renderdump.txt with the game renderer's assembly
    // (DXPlatform_Desktop, embedded in the game executable): its types and methods with IL sizes, and every SlimDX member
    // its IL uses with the number of call sites. For judging what replacing the renderer would take.
    internal static class RenderDump
    {
        internal static void Run()
        {
            if (!Settings.Switch("TABPERF_RENDERDUMP", false)) return;
            try
            {
                var asm = Renderer.Type != null ? Renderer.Type.Assembly : null;
                if (asm == null) { Log.Info("renderdump: 렌더러 어셈블리 없음"); return; }
                var sb = new StringBuilder();
                sb.AppendLine("assembly " + asm.FullName);
                foreach (var r in asm.GetReferencedAssemblies()) sb.AppendLine("  ref " + r.FullName);
                var uses = new Dictionary<string, int>();
                var flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
                Type[] types; try { types = asm.GetTypes(); } catch (ReflectionTypeLoadException e) { types = e.Types.Where(t => t != null).ToArray(); }
                long total = 0;
                foreach (var t in types.OrderBy(t => t.FullName))
                {
                    var ms = t.GetMethods(flags).Cast<MethodBase>().Concat(t.GetConstructors(flags)).ToList();
                    long tsize = 0; var lines = new List<string>();
                    foreach (var m in ms)
                    {
                        byte[] il = null; try { var b = m.GetMethodBody(); il = b != null ? b.GetILAsByteArray() : null; } catch { }
                        int n = il != null ? il.Length : 0; tsize += n;
                        lines.Add("    " + n.ToString().PadLeft(6) + "  " + m.Name + "(" + string.Join(", ", m.GetParameters().Select(p => p.ParameterType.Name)) + ")");
                        if (il == null) continue;
                        for (int i = 0; i + 4 < il.Length; i++)
                        {
                            if (il[i] != 0x28 && il[i] != 0x6F && il[i] != 0x73) continue;   // call, callvirt, newobj
                            try
                            {
                                var c = t.Module.ResolveMethod(BitConverter.ToInt32(il, i + 1));
                                if (c != null && c.DeclaringType != null && c.DeclaringType.Assembly.GetName().Name == "SlimDX")
                                {
                                    string k = c.DeclaringType.FullName + "." + c.Name; int v; uses.TryGetValue(k, out v); uses[k] = v + 1;
                                }
                            }
                            catch { }
                        }
                    }
                    total += tsize;
                    sb.AppendLine(t.FullName + "  (" + ms.Count + " methods, IL " + tsize + " bytes)");
                    if (t.FullName.IndexOf("Render", StringComparison.OrdinalIgnoreCase) >= 0 || t.FullName.IndexOf("Shader", StringComparison.OrdinalIgnoreCase) >= 0 || t.FullName.IndexOf("Texture", StringComparison.OrdinalIgnoreCase) >= 0)
                        foreach (var l in lines.OrderByDescending(l => int.Parse(l.Substring(0, 10).Trim()))) sb.AppendLine(l);
                }
                sb.AppendLine("total IL " + total + " bytes, " + types.Length + " types");
                sb.AppendLine("SlimDX members used (call sites):");
                foreach (var kv in uses.OrderBy(k => k.Key)) sb.AppendLine("  " + kv.Value.ToString().PadLeft(4) + "  " + kv.Key);
                string path = Path.Combine(Log.Dir, "renderdump.txt");
                File.WriteAllText(path, sb.ToString());
                Log.Info("renderdump: " + path);
            }
            catch (Exception e) { Log.Error("renderdump", e); }
        }
    }
}
