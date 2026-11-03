using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Billions
{

    public static class MiniJson
    {
        public const int MaxDepth = 64;
        public static object Parse(string s)
        {
            int i = 0; var v = ParseValue(s, ref i, 0); SkipWs(s, ref i);
            if (i != s.Length) throw new FormatException("trailing at " + i);
            return v;
        }

        static void SkipWs(string s, ref int i) { while (i < s.Length && char.IsWhiteSpace(s[i])) i++; }

        static object ParseValue(string s, ref int i, int depth)
        {
            SkipWs(s, ref i);
            if (i >= s.Length) throw new FormatException("eof");
            char c = s[i];
            if ((c == '{' || c == '[') && depth >= MaxDepth) throw new FormatException("too deep at " + i);
            if (c == '{') return ParseObject(s, ref i, depth + 1);
            if (c == '[') return ParseArray(s, ref i, depth + 1);
            if (c == '"') return ParseString(s, ref i);
            if (s.Length - i >= 4 && string.CompareOrdinal(s, i, "true", 0, 4) == 0) { i += 4; return true; }
            if (s.Length - i >= 5 && string.CompareOrdinal(s, i, "false", 0, 5) == 0) { i += 5; return false; }
            if (s.Length - i >= 4 && string.CompareOrdinal(s, i, "null", 0, 4) == 0) { i += 4; return null; }
            return ParseNumber(s, ref i);
        }

        static Dictionary<string, object> ParseObject(string s, ref int i, int depth)
        {
            var d = new Dictionary<string, object>(); i++;
            SkipWs(s, ref i);
            if (i < s.Length && s[i] == '}') { i++; return d; }
            for (;;)
            {
                SkipWs(s, ref i);
                if (i >= s.Length || s[i] != '"') throw new FormatException("key at " + i);
                string k = ParseString(s, ref i);
                SkipWs(s, ref i);
                if (i >= s.Length || s[i] != ':') throw new FormatException("colon at " + i);
                i++;
                d[k] = ParseValue(s, ref i, depth);
                SkipWs(s, ref i);
                if (i < s.Length && s[i] == ',') { i++; continue; }
                if (i < s.Length && s[i] == '}') { i++; return d; }
                throw new FormatException("object at " + i);
            }
        }

        static List<object> ParseArray(string s, ref int i, int depth)
        {
            var l = new List<object>(); i++;
            SkipWs(s, ref i);
            if (i < s.Length && s[i] == ']') { i++; return l; }
            for (;;)
            {
                l.Add(ParseValue(s, ref i, depth));
                SkipWs(s, ref i);
                if (i < s.Length && s[i] == ',') { i++; continue; }
                if (i < s.Length && s[i] == ']') { i++; return l; }
                throw new FormatException("array at " + i);
            }
        }

        static string ParseString(string s, ref int i)
        {
            var sb = new StringBuilder(); i++;
            while (i < s.Length)
            {
                char c = s[i++];
                if (c == '"') return sb.ToString();
                if (c != '\\') { sb.Append(c); continue; }
                if (i >= s.Length) break;
                char e = s[i++];
                switch (e)
                {
                    case '"': sb.Append('"'); break;
                    case '\\': sb.Append('\\'); break;
                    case '/': sb.Append('/'); break;
                    case 'b': sb.Append('\b'); break;
                    case 'f': sb.Append('\f'); break;
                    case 'n': sb.Append('\n'); break;
                    case 'r': sb.Append('\r'); break;
                    case 't': sb.Append('\t'); break;
                    case 'u':
                        if (i + 4 > s.Length) throw new FormatException("\\u");
                        sb.Append((char)Convert.ToInt32(s.Substring(i, 4), 16)); i += 4; break;
                    default: throw new FormatException("escape");
                }
            }
            throw new FormatException("unterminated string");
        }

        static double ParseNumber(string s, ref int i)
        {
            int st = i;
            while (i < s.Length && "+-0123456789.eE".IndexOf(s[i]) >= 0) i++;
            if (st == i) throw new FormatException("value at " + i);
            return double.Parse(s.Substring(st, i - st), NumberStyles.Float, CultureInfo.InvariantCulture);
        }

        public static string Serialize(object v)
        {
            var sb = new StringBuilder(); Write(sb, v); return sb.ToString();
        }

        static void Write(StringBuilder sb, object v)
        {
            if (v == null) { sb.Append("null"); return; }
            if (v is string) { WriteString(sb, (string)v); return; }
            if (v is bool) { sb.Append((bool)v ? "true" : "false"); return; }
            if (v is int || v is long || v is short || v is byte || v is uint || v is ulong) { sb.Append(Convert.ToString(v, CultureInfo.InvariantCulture)); return; }
            if (v is double || v is float || v is decimal)
            {
                double d = Convert.ToDouble(v, CultureInfo.InvariantCulture);
                if (double.IsNaN(d) || double.IsInfinity(d)) { sb.Append("null"); return; }
                sb.Append(d.ToString("R", CultureInfo.InvariantCulture)); return;
            }
            if (v is IDictionary)
            {
                sb.Append('{'); bool first = true;
                foreach (DictionaryEntry e in (IDictionary)v)
                {
                    if (!first) sb.Append(','); first = false;
                    WriteString(sb, Convert.ToString(e.Key)); sb.Append(':'); Write(sb, e.Value);
                }
                sb.Append('}'); return;
            }
            if (v is IEnumerable)
            {
                sb.Append('['); bool first = true;
                foreach (object o in (IEnumerable)v) { if (!first) sb.Append(','); first = false; Write(sb, o); }
                sb.Append(']'); return;
            }
            WriteString(sb, v.ToString());
        }

        static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4")); else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }

        public static string Str(Dictionary<string, object> d, string k, string def = null) { object v; return d != null && d.TryGetValue(k, out v) && v is string ? (string)v : def; }
        public static int Int(Dictionary<string, object> d, string k, int def = 0) { object v; return d != null && d.TryGetValue(k, out v) && v is double ? (int)Math.Round((double)v) : def; }
        public static bool Has(Dictionary<string, object> d, string k) { return d != null && d.ContainsKey(k); }
        public static Dictionary<string, object> Obj(Dictionary<string, object> d, string k) { object v; return d != null && d.TryGetValue(k, out v) ? v as Dictionary<string, object> : null; }
    }
}
