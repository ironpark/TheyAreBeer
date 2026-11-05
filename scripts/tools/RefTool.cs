// Build-time reference tool (scripts\build.ps1): makes the DXVision.dll the mod compiles against.
//   RefTool.exe <TheyAreBillions.exe> <fallback DXVision.dll> <out DXVision.dll>
// 1. The DXVision the game runs is the copy embedded in its executable (Costura resource costura.dxvision.dll.compressed,
//    raw deflate), not the loader's Lib\DXVision.dll: take that one when present, else the fallback.
// 2. Publicize it for compiling only: types, fields and methods become public in the metadata tables (flags flipped in
//    place, nothing else changes). The mod declares [IgnoresAccessChecksTo("DXVision")], so code compiled against it
//    reaches DXVision's private members directly at run time (mod/Render/SceneDraw.cs). To leave existing code compiling
//    as before: a field only when its name starts with '_' or it is not private (a field-like event's private backing
//    field has the event's name), a method only when its type has no public method of that name (no new overloads).
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text;

static class RefTool
{
    static int Main(string[] args)
    {
        try
        {
            if (args.Length != 3) { Console.Error.WriteLine("usage: RefTool <game exe> <fallback DXVision.dll> <out DXVision.dll>"); return 2; }
            byte[] dll = Embedded(args[0], "costura.dxvision.dll.compressed");
            string from = dll != null ? args[0] + " (embedded)" : args[1];
            if (dll == null) dll = File.ReadAllBytes(args[1]);
            int n = Publicize(dll);
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[2])));
            File.WriteAllBytes(args[2], dll);
            Console.WriteLine("DXVision reference: " + from + ", " + n + " members made public -> " + args[2]);
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine("RefTool: " + e.Message); return 1; }
    }

    static byte[] Embedded(string exe, string resource)
    {
        if (!File.Exists(exe)) return null;
        var a = Assembly.ReflectionOnlyLoadFrom(exe);
        using (var s = a.GetManifestResourceStream(resource))
        {
            if (s == null) return null;
            using (var d = new DeflateStream(s, CompressionMode.Decompress))
            using (var m = new MemoryStream()) { d.CopyTo(m); return m.ToArray(); }
        }
    }

    // ---------- ECMA-335 metadata: TypeDef, Field and MethodDef flags ----------
    static byte[] B;
    static ushort U16(int o) { return BitConverter.ToUInt16(B, o); }
    static int I32(int o) { return BitConverter.ToInt32(B, o); }

    static int Publicize(byte[] b)
    {
        B = b;
        int pe = I32(0x3C);
        if (I32(pe) != 0x4550) throw new InvalidDataException("not a PE file");
        int coff = pe + 4, nSec = U16(coff + 2), optSize = U16(coff + 16), opt = coff + 20;
        int dirs = opt + (U16(opt) == 0x20B ? 112 : 96);
        int cliRva = I32(dirs + 14 * 8);
        if (cliRva == 0) throw new InvalidDataException("not a .NET assembly");
        int secs = opt + optSize;
        Func<int, int> off = rva =>
        {
            for (int i = 0; i < nSec; i++)
            {
                int s = secs + i * 40, va = I32(s + 12), raw = I32(s + 16), ptr = I32(s + 20), vs = I32(s + 8);
                if (rva >= va && rva < va + Math.Max(vs, raw)) return rva - va + ptr;
            }
            throw new InvalidDataException("RVA outside the sections");
        };
        int cli = off(cliRva), md = off(I32(cli + 8));
        if (I32(md) != 0x424A5342) throw new InvalidDataException("no metadata root");
        int verLen = I32(md + 12), p = md + 16 + verLen + 2, nStreams = U16(p); p += 2;
        int tables = -1, strings = -1;
        for (int i = 0; i < nStreams; i++)
        {
            int so = I32(p), sz = I32(p + 4); p += 8;
            int e = p; while (B[e] != 0) e++;
            string name = Encoding.ASCII.GetString(B, p, e - p);
            p = (e + 4) & ~3;
            if (name == "#~") tables = md + so; else if (name == "#Strings") strings = md + so; else if (name == "#-") throw new InvalidDataException("uncompressed metadata (#-) not supported");
        }
        if (tables < 0 || strings < 0) throw new InvalidDataException("no #~ or #Strings stream");
        byte heaps = B[tables + 6];
        ulong valid = BitConverter.ToUInt64(B, tables + 8);
        var rows = new int[64];
        p = tables + 24;
        for (int t = 0; t < 64; t++) if ((valid >> t & 1) != 0) { rows[t] = I32(p); p += 4; }
        if ((heaps & 0x40) != 0) p += 4;
        if (rows[0x03] > 0 || rows[0x05] > 0) throw new InvalidDataException("field/method pointer tables not supported");
        int str = (heaps & 1) != 0 ? 4 : 2, guid = (heaps & 2) != 0 ? 4 : 2, blob = (heaps & 4) != 0 ? 4 : 2;
        Func<int, int> idx = t => rows[t] < 65536 ? 2 : 4;
        Func<int, int[], int> coded = (bits, ts) => { int max = 0; foreach (int t in ts) max = Math.Max(max, rows[t]); return max < (1 << (16 - bits)) ? 2 : 4; };
        int rs = coded(2, new[] { 0x00, 0x1A, 0x23, 0x01 }), tdor = coded(2, new[] { 0x02, 0x01, 0x1B });
        int moduleRow = 2 + str + 3 * guid, typeRefRow = rs + 2 * str;
        int typeDefRow = 4 + 2 * str + tdor + idx(0x04) + idx(0x06), fieldRow = 2 + str + blob, methodRow = 8 + str + blob + idx(0x08);
        int typeDefs = p + rows[0x00] * moduleRow + rows[0x01] * typeRefRow;
        int fields = typeDefs + rows[0x02] * typeDefRow, methods = fields + rows[0x04] * fieldRow;
        Func<int, int, int> read = (o, size) => size == 2 ? U16(o) : I32(o);
        Func<int, string> name0 = si => { int s = strings + si, e = s; while (B[e] != 0) e++; return Encoding.UTF8.GetString(B, s, e - s); };

        int changed = 0;
        for (int t = 0; t < rows[0x02]; t++)
        {
            int r = typeDefs + t * typeDefRow;
            int flags = I32(r), vis = flags & 7;
            int nv = vis == 0 ? 1 : vis >= 3 ? 2 : vis;   // NotPublic -> Public, nested non-public -> NestedPublic
            if (nv != vis) { WriteI32(r, (flags & ~7) | nv); changed++; }
            int fo = 4 + 2 * str + tdor;
            int f0 = read(r + fo, idx(0x04)), m0 = read(r + fo + idx(0x04), idx(0x06));
            int f1 = t + 1 < rows[0x02] ? read(r + typeDefRow + fo, idx(0x04)) : rows[0x04] + 1;
            int m1 = t + 1 < rows[0x02] ? read(r + typeDefRow + fo + idx(0x04), idx(0x06)) : rows[0x06] + 1;
            for (int f = f0; f < f1; f++)
            {
                int fr = fields + (f - 1) * fieldRow, ff = U16(fr), acc = ff & 7;
                if (acc == 6) continue;
                if (acc == 1 && !name0(read(fr + 2, str)).StartsWith("_", StringComparison.Ordinal)) continue;
                WriteU16(fr, (ff & ~7) | 6); changed++;
            }
            var publicNames = new HashSet<string>();
            for (int m = m0; m < m1; m++) { int mr = methods + (m - 1) * methodRow; if ((U16(mr + 6) & 7) == 6) publicNames.Add(name0(read(mr + 8, str))); }
            for (int m = m0; m < m1; m++)
            {
                int mr = methods + (m - 1) * methodRow, mf = U16(mr + 6);
                if ((mf & 7) == 6 || publicNames.Contains(name0(read(mr + 8, str)))) continue;
                WriteU16(mr + 6, (mf & ~7) | 6); changed++;
            }
        }
        return changed;
    }

    static void WriteU16(int o, int v) { B[o] = (byte)v; B[o + 1] = (byte)(v >> 8); }
    static void WriteI32(int o, int v) { var x = BitConverter.GetBytes(v); Buffer.BlockCopy(x, 0, B, o, 4); }
}
