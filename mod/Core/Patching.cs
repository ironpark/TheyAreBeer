using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;

namespace Billions
{
    // Small Harmony helpers shared by the patches.
    internal static class Patching
    {
        // For a transpiler: calls to a key of `map` become static calls to its value (same arguments, the instance first).
        // n: the sites.
        internal static List<CodeInstruction> Redirect(IEnumerable<CodeInstruction> code, IDictionary<MethodInfo, MethodInfo> map, out int n)
        {
            var list = new List<CodeInstruction>(code); n = 0;
            foreach (var ci in list)
            {
                MethodInfo to;
                if ((ci.opcode == OpCodes.Callvirt || ci.opcode == OpCodes.Call) && ci.operand is MethodInfo from && map.TryGetValue(from, out to)) { ci.opcode = OpCodes.Call; ci.operand = to; n++; }
            }
            return list;
        }

        internal static List<CodeInstruction> Redirect(IEnumerable<CodeInstruction> code, MethodInfo from, MethodInfo to, out int n)
        {
            return Redirect(code, new Dictionary<MethodInfo, MethodInfo> { { from, to } }, out n);
        }
    }
}
