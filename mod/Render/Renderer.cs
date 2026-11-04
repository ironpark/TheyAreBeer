using System;
using System.Reflection;
using HarmonyLib;

namespace Billions
{
    // The game's Direct3D 9 renderer, DXPlatform_Desktop.DXRender_SlimDX (in DXPlatform_Desktop.dll, which the game embeds
    // with Costura and loads from memory): the type and the private members the render patches use, looked up by name in
    // one place. Each lookup is null when the renderer or the member is missing, and the patch then stays off.
    internal static class Renderer
    {
        // fields of DXRender_SlimDX
        internal const string Pending = "_NPendingObjects";          // sprites in the batch
        internal const string LastDrawn = "_LastObjectDrawn";        // last DXObject added to the batch
        internal const string Vertices = "_ObjectArrayVertices";     // the sprite batch, TransformedTextured[60000] (6 per sprite)
        internal const int VertexSize = 96;                          // Marshal.SizeOf(TransformedTextured)

        internal static readonly Type Type = AccessTools.TypeByName("DXPlatform_Desktop.DXRender_SlimDX");
        internal static readonly FieldInfo VerticesField = Field(Vertices);
        static readonly FieldInfo _device = Field("_D3DDevice");

        internal static FieldInfo Field(string name) { return Type != null ? AccessTools.Field(Type, name) : null; }
        internal static MethodInfo Method(string name) { return Type != null ? AccessTools.Method(Type, name) : null; }
        internal static Type Nested(string name) { return Type != null ? Type.GetNestedType(name, BindingFlags.NonPublic) : null; }

        // SlimDX's Device type (the type of _D3DDevice); its assembly holds the other SlimDX types.
        internal static Type DeviceType { get { return _device != null ? _device.FieldType : null; } }

        // The batch's vertex type (check its size against VertexSize before writing into the batch).
        internal static Type VertexType { get { return VerticesField != null ? VerticesField.FieldType.GetElementType() : null; } }
    }
}
