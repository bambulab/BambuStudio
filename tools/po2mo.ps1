<#
.SYNOPSIS
  Dependency-free gettext .po -> .mo compiler (msgfmt replacement) for Windows PowerShell 5.1 / PowerShell 7.

.DESCRIPTION
  powershell -ExecutionPolicy Bypass -File tools\po2mo.ps1 -Po <in.po> -Mo <out.mo> [-Check] [-Dump] [-DumpCount N]

  -Check  After writing, re-read the .mo, verify magic/count/order/terminators and print written/skipped
          counts plus the Plural-Forms and Content-Type header lines.
  -Dump   Print entry count and the first N (default 5) key/value pairs of the .mo.
          Without -Po, -Dump just reads the existing -Mo (e.g. a shipped catalog).

  Behaviour matches msgfmt defaults: fuzzy entries and entries with empty translations are skipped
  (the header, msgid "", is always kept). Exit code: 0 ok, 1 parse/duplicate/check errors, 2 usage/IO error.
  Heavy lifting is done in an embedded C# class (Add-Type; compiler ships with .NET Framework, C# 5 only).
#>
[CmdletBinding()]
param(
    [string]$Po,
    [string]$Mo,
    [switch]$Check,
    [switch]$Dump,
    [int]$DumpCount = 5
)

$ErrorActionPreference = 'Stop'

$src = @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Po2MoNs
{
    // Ordinal byte-wise comparison: required because GNU gettext does binary search on the original table.
    public class ByteArrayComparer : IComparer<byte[]>
    {
        public int Compare(byte[] a, byte[] b)
        {
            int n = Math.Min(a.Length, b.Length);
            for (int i = 0; i < n; i++)
            {
                if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
            }
            if (a.Length == b.Length) return 0;
            return a.Length < b.Length ? -1 : 1;
        }
    }

    public class Entry
    {
        public byte[] Key;
        public byte[] Value;
        public int Line;
    }

    public class PoCompiler
    {
        public List<Entry> Entries = new List<Entry>();
        public List<string> Errors = new List<string>();
        public int SkippedFuzzy;
        public int SkippedEmpty;

        private string file;
        private Dictionary<string, int> seen = new Dictionary<string, int>();

        // Current entry builder state
        private bool hasCtxt, hasId, hasPlural, hasStr, fuzzy;
        private int startLine;
        private List<byte> ctxt, id, plural, cur;
        private SortedDictionary<int, List<byte>> strs;

        public PoCompiler(string fileName) { file = fileName; Reset(); }

        private void Reset()
        {
            hasCtxt = hasId = hasPlural = hasStr = fuzzy = false;
            startLine = 0;
            ctxt = new List<byte>(); id = new List<byte>(); plural = new List<byte>();
            strs = new SortedDictionary<int, List<byte>>();
            cur = null;
        }

        private void Err(int line, string msg)
        {
            Errors.Add(file + "(" + line + "): error: " + msg);
        }

        public void Parse(string text)
        {
            if (text.Length > 0 && text[0] == (char)0xFEFF) text = text.Substring(1);
            string[] lines = text.Split('\n');
            for (int i = 0; i < lines.Length; i++)
            {
                int ln = i + 1;
                string s = lines[i].Trim(); // also strips the CR of CRLF
                if (s.Length == 0) continue;

                if (s[0] == '#')
                {
                    // A comment after msgstr starts the next entry.
                    if (hasStr) Flush();
                    if (s.Length > 1 && s[1] == ',')
                    {
                        string[] flags = s.Substring(2).Split(new char[] { ',', ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                        foreach (string f in flags) if (f == "fuzzy") fuzzy = true;
                    }
                    continue;
                }

                if (s[0] == '"')
                {
                    if (cur == null) { Err(ln, "string continuation without preceding keyword"); continue; }
                    AppendQuoted(ln, s, cur);
                    continue;
                }

                int kwEnd = 0;
                while (kwEnd < s.Length && (char.IsLetter(s[kwEnd]) || s[kwEnd] == '_')) kwEnd++;
                string kw = s.Substring(0, kwEnd);
                string rest = s.Substring(kwEnd).TrimStart();

                if (kw == "msgctxt")
                {
                    if (hasStr) Flush();
                    if (hasId || hasCtxt) Err(ln, "msgctxt in unexpected position");
                    hasCtxt = true; if (startLine == 0) startLine = ln;
                    cur = ctxt;
                    AppendQuoted(ln, rest, cur);
                }
                else if (kw == "msgid")
                {
                    if (hasStr) Flush();
                    if (hasId) Err(ln, "duplicate msgid within entry");
                    hasId = true; if (startLine == 0) startLine = ln;
                    cur = id;
                    AppendQuoted(ln, rest, cur);
                }
                else if (kw == "msgid_plural")
                {
                    if (!hasId || hasStr || hasPlural) { Err(ln, "msgid_plural in unexpected position"); }
                    hasPlural = true;
                    cur = plural;
                    AppendQuoted(ln, rest, cur);
                }
                else if (kw == "msgstr")
                {
                    if (!hasId) Err(ln, "msgstr without preceding msgid");
                    int idx = 0;
                    if (rest.Length > 0 && rest[0] == '[')
                    {
                        int close = rest.IndexOf(']');
                        if (close < 0 || !int.TryParse(rest.Substring(1, close - 1), out idx) || idx < 0)
                        {
                            Err(ln, "malformed msgstr[N]");
                            continue;
                        }
                        if (!hasPlural) Err(ln, "msgstr[N] without msgid_plural");
                        rest = rest.Substring(close + 1).TrimStart();
                    }
                    else if (hasPlural)
                    {
                        Err(ln, "plain msgstr in plural entry (expected msgstr[N])");
                    }
                    hasStr = true;
                    if (strs.ContainsKey(idx)) Err(ln, "duplicate msgstr[" + idx + "]");
                    List<byte> l = new List<byte>();
                    strs[idx] = l;
                    cur = l;
                    AppendQuoted(ln, rest, cur);
                }
                else
                {
                    Err(ln, "unexpected line: '" + (s.Length > 40 ? s.Substring(0, 40) + "..." : s) + "'");
                }
            }
            if (hasStr) Flush();
            else if (hasId) Err(startLine, "msgid without msgstr at end of file");
        }

        private static bool IsHex(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        private static int HexVal(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        }

        // Parses one "..." literal, appending unescaped bytes to dst. Escapes such as \303 or \xC3 emit raw
        // bytes; everything else is UTF-8 encoded from the (already decoded) text.
        private bool AppendQuoted(int ln, string s, List<byte> dst)
        {
            if (s.Length == 0 || s[0] != '"') { Err(ln, "expected a quoted string"); return false; }
            int i = 1, runStart = 1;
            bool closed = false;
            while (i < s.Length)
            {
                char c = s[i];
                if (c == '"')
                {
                    if (i > runStart) dst.AddRange(Encoding.UTF8.GetBytes(s.Substring(runStart, i - runStart)));
                    closed = true; i++;
                    break;
                }
                if (c == '\\')
                {
                    if (i > runStart) dst.AddRange(Encoding.UTF8.GetBytes(s.Substring(runStart, i - runStart)));
                    i++;
                    if (i >= s.Length) { Err(ln, "dangling backslash"); return false; }
                    char e = s[i];
                    switch (e)
                    {
                        case 'n': dst.Add(10); i++; break;
                        case 't': dst.Add(9); i++; break;
                        case 'r': dst.Add(13); i++; break;
                        case 'a': dst.Add(7); i++; break;
                        case 'b': dst.Add(8); i++; break;
                        case 'f': dst.Add(12); i++; break;
                        case 'v': dst.Add(11); i++; break;
                        case '"': dst.Add((byte)'"'); i++; break;
                        case '\\': dst.Add((byte)'\\'); i++; break;
                        case '\'': dst.Add((byte)'\''); i++; break;
                        case '?': dst.Add((byte)'?'); i++; break;
                        case 'x':
                            {
                                i++;
                                int v = 0, cnt = 0;
                                while (i < s.Length && cnt < 2 && IsHex(s[i])) { v = v * 16 + HexVal(s[i]); i++; cnt++; }
                                if (cnt == 0) { Err(ln, "\\x escape without hex digits"); return false; }
                                dst.Add((byte)v);
                                break;
                            }
                        default:
                            if (e >= '0' && e <= '7')
                            {
                                int v = 0, cnt = 0;
                                while (i < s.Length && cnt < 3 && s[i] >= '0' && s[i] <= '7') { v = v * 8 + (s[i] - '0'); i++; cnt++; }
                                dst.Add((byte)(v & 0xFF));
                            }
                            else
                            {
                                Err(ln, "invalid escape sequence '\\" + e + "'");
                                return false;
                            }
                            break;
                    }
                    runStart = i;
                    continue;
                }
                i++;
            }
            if (!closed) { Err(ln, "unterminated string"); return false; }
            if (s.Substring(i).Trim().Length != 0) { Err(ln, "unexpected text after closing quote"); return false; }
            return true;
        }

        private static byte[] Concat(List<byte> a, int sep, List<byte> b)
        {
            List<byte> r = new List<byte>(a.Count + b.Count + 1);
            r.AddRange(a); r.Add((byte)sep); r.AddRange(b);
            return r.ToArray();
        }

        private void Flush()
        {
            try
            {
                if (!hasId) return;
                bool isHeader = !hasCtxt && !hasPlural && id.Count == 0;

                if (fuzzy && !isHeader) { SkippedFuzzy++; return; }

                // Build key
                List<byte> key = new List<byte>();
                if (hasCtxt) { key.AddRange(ctxt); key.Add(4); }
                key.AddRange(id);
                if (hasPlural) { key.Add(0); key.AddRange(plural); }

                // Build value
                List<byte> val = new List<byte>();
                bool allEmpty = true;
                if (hasPlural)
                {
                    int expect = 0;
                    foreach (KeyValuePair<int, List<byte>> kv in strs)
                    {
                        if (kv.Key != expect) { Err(startLine, "msgstr[" + expect + "] missing in plural entry"); return; }
                        if (expect > 0) val.Add(0);
                        val.AddRange(kv.Value);
                        if (kv.Value.Count > 0) allEmpty = false;
                        expect++;
                    }
                    if (expect == 0) { Err(startLine, "plural entry without msgstr[N]"); return; }
                }
                else
                {
                    List<byte> v0;
                    if (!strs.TryGetValue(0, out v0)) { Err(startLine, "entry without msgstr"); return; }
                    val.AddRange(v0);
                    allEmpty = v0.Count == 0;
                }

                if (allEmpty && !isHeader) { SkippedEmpty++; return; }

                byte[] kb = key.ToArray();
                string k64 = Convert.ToBase64String(kb);
                int first;
                if (seen.TryGetValue(k64, out first))
                {
                    Err(startLine, "duplicate message definition (first defined at line " + first + ")");
                    return;
                }
                seen[k64] = startLine;
                Entry en = new Entry();
                en.Key = kb; en.Value = val.ToArray(); en.Line = startLine;
                Entries.Add(en);
            }
            finally { Reset(); }
        }

        public void Write(string path)
        {
            List<Entry> sorted = new List<Entry>(Entries);
            ByteArrayComparer cmp = new ByteArrayComparer();
            sorted.Sort(delegate(Entry x, Entry y) { return cmp.Compare(x.Key, y.Key); });

            uint n = (uint)sorted.Count;
            uint origTab = 28;
            uint transTab = 28 + 8 * n;
            uint hashOff = 28 + 16 * n;      // hash table size 0: string data starts right here
            uint off = hashOff;

            using (FileStream fs = new FileStream(path, FileMode.Create, FileAccess.Write))
            using (BinaryWriter bw = new BinaryWriter(fs))   // BinaryWriter is always little-endian
            {
                bw.Write((uint)0x950412de);
                bw.Write((uint)0);
                bw.Write(n);
                bw.Write(origTab);
                bw.Write(transTab);
                bw.Write((uint)0);
                bw.Write(hashOff);
                foreach (Entry e in sorted)
                {
                    bw.Write((uint)e.Key.Length); bw.Write(off);
                    off += (uint)e.Key.Length + 1;
                }
                foreach (Entry e in sorted)
                {
                    bw.Write((uint)e.Value.Length); bw.Write(off);
                    off += (uint)e.Value.Length + 1;
                }
                foreach (Entry e in sorted) { bw.Write(e.Key); bw.Write((byte)0); }
                foreach (Entry e in sorted) { bw.Write(e.Value); bw.Write((byte)0); }
            }
        }
    }

    public class MoFile
    {
        public List<byte[]> Keys = new List<byte[]>();
        public List<byte[]> Values = new List<byte[]>();
        public bool BigEndian;
        public uint Revision;
        public bool Sorted = true;
        public uint HashSize;

        private static uint U32(byte[] d, long pos, bool be)
        {
            if (pos < 0 || pos + 4 > d.Length) throw new InvalidDataException("offset out of range: " + pos);
            if (be) return ((uint)d[pos] << 24) | ((uint)d[pos + 1] << 16) | ((uint)d[pos + 2] << 8) | d[pos + 3];
            return ((uint)d[pos + 3] << 24) | ((uint)d[pos + 2] << 16) | ((uint)d[pos + 1] << 8) | d[pos];
        }

        public static MoFile Read(string path)
        {
            byte[] d = File.ReadAllBytes(path);
            if (d.Length < 28) throw new InvalidDataException("file too short for a .mo header");
            MoFile m = new MoFile();
            uint magicLE = U32(d, 0, false);
            if (magicLE == 0x950412de) m.BigEndian = false;
            else if (magicLE == 0xde120495) m.BigEndian = true;
            else throw new InvalidDataException("bad magic 0x" + magicLE.ToString("x8"));
            bool be = m.BigEndian;
            m.Revision = U32(d, 4, be);
            if ((m.Revision >> 16) > 1) throw new InvalidDataException("unsupported revision " + m.Revision);
            uint n = U32(d, 8, be);
            uint o = U32(d, 12, be), t = U32(d, 16, be);
            m.HashSize = U32(d, 20, be);
            if ((long)o + 8L * n > d.Length || (long)t + 8L * n > d.Length) throw new InvalidDataException("string tables exceed file size");
            ByteArrayComparer cmp = new ByteArrayComparer();
            for (uint i = 0; i < n; i++)
            {
                uint ol = U32(d, o + 8L * i, be), oo = U32(d, o + 8L * i + 4, be);
                uint tl = U32(d, t + 8L * i, be), to = U32(d, t + 8L * i + 4, be);
                if ((long)oo + ol >= d.Length || d[oo + ol] != 0) throw new InvalidDataException("original string " + i + " not NUL-terminated / out of range");
                if ((long)to + tl >= d.Length || d[to + tl] != 0) throw new InvalidDataException("translation string " + i + " not NUL-terminated / out of range");
                byte[] k = new byte[ol]; Array.Copy(d, oo, k, 0, ol);
                byte[] v = new byte[tl]; Array.Copy(d, to, v, 0, tl);
                if (m.Keys.Count > 0 && cmp.Compare(m.Keys[m.Keys.Count - 1], k) >= 0) m.Sorted = false;
                m.Keys.Add(k); m.Values.Add(v);
            }
            return m;
        }

        public string HeaderText()
        {
            for (int i = 0; i < Keys.Count; i++)
                if (Keys[i].Length == 0) return new UTF8Encoding(false).GetString(Values[i]);
            return null;
        }

        // Human-readable form: control chars escaped, NUL -> \0, context separator -> <EOT>.
        public static string Show(byte[] b, int max)
        {
            string s = new UTF8Encoding(false).GetString(b);
            StringBuilder sb = new StringBuilder();
            foreach (char c in s)
            {
                if (c == '\0') sb.Append("\\0");
                else if (c == '\u0004') sb.Append("<EOT>");
                else if (c == '\n') sb.Append("\\n");
                else if (c == '\r') sb.Append("\\r");
                else if (c == '\t') sb.Append("\\t");
                else sb.Append(c);
            }
            string r = sb.ToString();
            if (max > 0 && r.Length > max) r = r.Substring(0, max) + "...";
            return r;
        }
    }

    public static class Tool
    {
        public static PoCompiler CompileFile(string poPath)
        {
            PoCompiler c = new PoCompiler(poPath);
            byte[] raw = File.ReadAllBytes(poPath);
            string text;
            try { text = new UTF8Encoding(false, true).GetString(raw); }
            catch (ArgumentException ex) { c.Errors.Add(poPath + "(1): error: file is not valid UTF-8: " + ex.Message); return c; }
            c.Parse(text);
            return c;
        }
    }
}
'@

if (-not ('Po2MoNs.Tool' -as [type])) {
    Add-Type -TypeDefinition $src -Language CSharp
}

# Best effort: make Hebrew etc. print correctly in the console.
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

function Resolve-PathLoose([string]$p) {
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($p)
}

function Show-Dump([string]$moPath, [int]$count) {
    $m = [Po2MoNs.MoFile]::Read($moPath)
    $endian = 'LE'
    if ($m.BigEndian) { $endian = 'BE' }
    Write-Output ("Dump of {0}: N = {1} entries ({2}, revision {3}, hash size {4}, sorted = {5})" -f $moPath, $m.Keys.Count, $endian, $m.Revision, $m.HashSize, $m.Sorted)
    $lim = [Math]::Min($count, $m.Keys.Count)
    for ($i = 0; $i -lt $lim; $i++) {
        Write-Output ("  [{0}] key   = {1}" -f $i, [Po2MoNs.MoFile]::Show($m.Keys[$i], 160))
        Write-Output ("      value = {0}" -f [Po2MoNs.MoFile]::Show($m.Values[$i], 160))
    }
}

try {
    if (-not $Po -and -not ($Dump -and $Mo)) {
        [Console]::Error.WriteLine('usage: po2mo.ps1 -Po <in.po> -Mo <out.mo> [-Check] | -Dump -Mo <file.mo> [-DumpCount N]')
        exit 2
    }

    if ($Po) {
        if (-not $Mo) { [Console]::Error.WriteLine('error: -Mo is required with -Po'); exit 2 }
        $poPath = Resolve-PathLoose $Po
        $moPath = Resolve-PathLoose $Mo
        if (-not (Test-Path -LiteralPath $poPath)) { [Console]::Error.WriteLine("error: $poPath not found"); exit 2 }

        $c = [Po2MoNs.Tool]::CompileFile($poPath)
        if ($c.Errors.Count -gt 0) {
            foreach ($e in $c.Errors) { [Console]::Error.WriteLine($e) }
            [Console]::Error.WriteLine(("{0} error(s); {1} not written" -f $c.Errors.Count, $moPath))
            exit 1
        }

        $c.Write($moPath)
        $skipped = $c.SkippedFuzzy + $c.SkippedEmpty
        Write-Output ("Wrote {0}: {1} entries, {2} skipped (fuzzy {3}, empty {4})" -f $moPath, $c.Entries.Count, $skipped, $c.SkippedFuzzy, $c.SkippedEmpty)

        if ($Check) {
            $bad = 0
            try {
                $m = [Po2MoNs.MoFile]::Read($moPath)
            } catch {
                [Console]::Error.WriteLine("check failed: " + $_.Exception.Message)
                exit 1
            }
            if ($m.Keys.Count -ne $c.Entries.Count) {
                [Console]::Error.WriteLine(("check failed: count mismatch (read {0}, wrote {1})" -f $m.Keys.Count, $c.Entries.Count)); $bad++
            }
            if (-not $m.Sorted) { [Console]::Error.WriteLine('check failed: originals not strictly sorted'); $bad++ }
            Write-Output ("Check: magic OK, count {0}, sorted {1}" -f $m.Keys.Count, $m.Sorted)
            Write-Output ("Entries written: {0}" -f $c.Entries.Count)
            Write-Output ("Entries skipped: {0} (fuzzy {1}, empty {2})" -f $skipped, $c.SkippedFuzzy, $c.SkippedEmpty)
            $hdr = $m.HeaderText()
            if ($null -eq $hdr) {
                Write-Output 'Header: (none)'
            } else {
                $pf = $null; $ct = $null
                foreach ($l in ($hdr -split "`n")) {
                    if ($l -like 'Plural-Forms:*') { $pf = $l }
                    elseif ($l -like 'Content-Type:*') { $ct = $l }
                }
                if ($pf) { Write-Output $pf } else { Write-Output 'Plural-Forms: (missing)' }
                if ($ct) { Write-Output $ct } else { Write-Output 'Content-Type: (missing)' }
            }
            if ($bad -gt 0) { exit 1 }
        }
        if ($Dump) { Show-Dump $moPath $DumpCount }
    } else {
        $moPath = Resolve-PathLoose $Mo
        if (-not (Test-Path -LiteralPath $moPath)) { [Console]::Error.WriteLine("error: $moPath not found"); exit 2 }
        Show-Dump $moPath $DumpCount
    }
    exit 0
} catch {
    [Console]::Error.WriteLine("error: " + $_.Exception.Message)
    exit 2
}
