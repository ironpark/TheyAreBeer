using System.Runtime.CompilerServices;

// Lets the mod use DXVision's private members directly: it is compiled against a publicized copy of the DXVision the
// game runs (scripts\tools\RefTool.cs), and the runtime skips the access checks for the assembly named here.
[assembly: IgnoresAccessChecksTo("DXVision")]

namespace System.Runtime.CompilerServices
{
    [AttributeUsage(AttributeTargets.Assembly, AllowMultiple = true)]
    internal sealed class IgnoresAccessChecksToAttribute : Attribute { public IgnoresAccessChecksToAttribute(string assemblyName) { AssemblyName = assemblyName; } public string AssemblyName { get; } }
}
