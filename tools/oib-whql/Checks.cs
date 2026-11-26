// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.

using System.Diagnostics;
using System.IO.Compression;
using System.Text.Json;
using System.Xml.Linq;

namespace OibWhql;

// 取り違えと失敗を、その場で止めるための検査。エラーは「止めるべきもの」、警告は「見て判断するもの」。
internal sealed class CheckResult
{
    public List<string> Errors { get; } = new();
    public List<string> Warnings { get; } = new();
    public bool Ok => Errors.Count == 0;

    public void Add(CheckResult o) { Errors.AddRange(o.Errors); Warnings.AddRange(o.Warnings); }

    public void Print(RunLog log, string prefix = "  ")
    {
        foreach (var e in Errors) log.Line($"{prefix}[NG] {e}");
        foreach (var w in Warnings) log.Line($"{prefix}[注意] {w}");
    }
}

internal static class Checks
{
    // 外部コマンドの標準出力を文字列で受け取る（バージョン確認用）。
    private static string Capture(string file, string args)
    {
        var psi = new ProcessStartInfo("cmd.exe", $"/s /c \"\"{file}\" {args}\"")
        { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
        using var p = Process.Start(psi)!;
        var o = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
        p.WaitForExit();
        return o.Trim();
    }

    // 前提の検査。1 本も動かす前に、足りないものを全部まとめて出す。
    public static CheckResult Preflight(Config c, IEnumerable<BuildTarget> targets, bool needDvlExe)
    {
        var r = new CheckResult();
        if (!File.Exists(c.CodeQlCmd)) r.Errors.Add($"codeql.cmd が無い: {c.CodeQlCmd}");
        else
        {
            var v = Capture(c.CodeQlCmd, "version --format=terse");
            if (!v.Contains(c.CodeQlVersion)) r.Errors.Add($"CodeQL の版が違う（期待 {c.CodeQlVersion} / 実際 {v}）");
        }
        if (!Directory.Exists(c.CodeQlHome)) r.Errors.Add($"CodeQL のフォルダが無い: {c.CodeQlHome}");
        if (!File.Exists(c.BuildEnvCmd)) r.Errors.Add($"ビルド環境の cmd が無い（bat が呼ぶ）: {c.BuildEnvCmd}");
        if (needDvlExe && !File.Exists(c.DvlExe)) r.Errors.Add($"dvl.exe が無い: {c.DvlExe}");
        if (!Directory.Exists(c.DriverRoot)) { r.Errors.Add($"ドライバのフォルダが無い: {c.DriverRoot}"); return r; }
        foreach (var d in Driver.All)
            if (!Directory.Exists(Path.Combine(c.DriverRoot, d.Dir))) r.Errors.Add($"ドライバのフォルダが無い: driver\\{d.Dir}");
        foreach (var t in targets)
            if (!File.Exists(Path.Combine(c.DriverRoot, t.BuildBat))) r.Errors.Add($"ビルド用の bat が無い: {t.BuildBat}");
        return r;
    }

    // HLK 版フォルダの名前と版の食い違い（OSS の成果物を Subsc のフォルダに入れる事故）を止める。
    // Subsc のフォルダには Subsc が入り、OSS（Pro）のフォルダには入らない。
    public static CheckResult HlkFolder(Config c, string hlkDir)
    {
        if (!Directory.Exists(hlkDir)) { var r0 = new CheckResult(); r0.Errors.Add($"HLK 版フォルダが無い: {hlkDir}"); return r0; }
        return HlkFolderName(c, hlkDir);
    }

    // フォルダ名だけの検査（prepare は、フォルダを作る前にこれで版の取り違えを止める）。
    public static CheckResult HlkFolderName(Config c, string hlkDir)
    {
        var r = new CheckResult();
        var name = Path.GetFileName(hlkDir.TrimEnd('\\', '/'));
        var hasSubsc = name.Contains("Subsc", StringComparison.OrdinalIgnoreCase);
        if (c.IsSubsc && !hasSubsc) r.Errors.Add($"版が合わない: このリポジトリは Subsc だが、フォルダ名に Subsc が無い: {name}");
        if (!c.IsSubsc && hasSubsc) r.Errors.Add($"版が合わない: このリポジトリは OSS（Pro）だが、フォルダ名に Subsc がある: {name}");
        return r;
    }

    // DB の取り違え検査: ソースの場所、kbd と mou の両方があること、Subsc だけの license.c の有無。
    public static CheckResult Database(Config c, string dbDir)
    {
        var r = new CheckResult();
        var yml = Path.Combine(dbDir, "codeql-database.yml");
        if (!File.Exists(yml)) { r.Errors.Add($"codeql-database.yml が無い（DB が作れていない）: {dbDir}"); return r; }

        var prefix = File.ReadAllLines(yml).FirstOrDefault(l => l.TrimStart().StartsWith("sourceLocationPrefix:"));
        var want = Path.GetFullPath(c.DriverRoot).TrimEnd('\\');
        if (prefix == null || !prefix.Replace("\\\\", "\\").Contains(want, StringComparison.OrdinalIgnoreCase))
            r.Errors.Add($"DB のソースの場所が違う（期待 {want} / yml: {prefix?.Trim() ?? "なし"}）");

        var zip = Path.Combine(dbDir, "src.zip");
        if (!File.Exists(zip)) { r.Errors.Add("src.zip が無い"); return r; }
        using var z = ZipFile.OpenRead(zip);
        bool Has(string tail) => z.Entries.Any(e => e.FullName.EndsWith(tail, StringComparison.OrdinalIgnoreCase));
        if (z.Entries.Count == 0) r.Errors.Add("src.zip が空");
        if (!Has("/keyboard/kbdfilter.c")) r.Errors.Add("kbd のソース（keyboard/kbdfilter.c）が DB に無い");
        if (!Has("/mouse/mousefilter.c")) r.Errors.Add("mou のソース（mouse/mousefilter.c）が DB に無い");
        var hasLicense = Has("/common/license.c");
        if (c.IsSubsc && !hasLicense) r.Errors.Add("Subsc なのに license.c が DB に無い（OSS のビルドが混ざった可能性）");
        if (!c.IsSubsc && hasLicense) r.Errors.Add("OSS なのに license.c が DB に入っている（Subsc のビルドが混ざった可能性）");
        return r;
    }

    public static CheckResult Sarif(string path)
    {
        var r = new CheckResult();
        if (!File.Exists(path)) { r.Errors.Add($"SARIF が無い: {path}"); return r; }
        var len = new FileInfo(path).Length;
        if (len < 1024) { r.Errors.Add($"SARIF が小さすぎる（{len} バイト）"); return r; }
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllBytes(path));
            var run = doc.RootElement.GetProperty("runs")[0];
            var tool = run.GetProperty("tool").GetProperty("driver").GetProperty("name").GetString() ?? "";
            if (!tool.Contains("CodeQL", StringComparison.OrdinalIgnoreCase)) r.Errors.Add($"CodeQL の SARIF ではない（tool: {tool}）");
            var n = run.TryGetProperty("results", out var res) ? res.GetArrayLength() : 0;
            if (n == 0) r.Warnings.Add("SARIF の結果が 0 件（欠陥なしなら正常。想定外なら、解析が動いていない）");
        }
        catch (Exception e) when (e is JsonException or KeyNotFoundException or InvalidOperationException)
        {
            r.Errors.Add($"SARIF を読めない: {e.Message}");
        }
        return r;
    }

    // DVL の検査。項目名のドライバ名と CPU が対象と合うか、Semmle の項目とチェックサムがあるか。
    // kbd の DVL に mou の項目が混ざる、x64 の DVL に arm64 の項目が混ざる、といった取り違えはエラーにする。
    public static CheckResult Dvl(string path, Driver d, string arch)
    {
        var r = new CheckResult();
        if (!File.Exists(path)) { r.Errors.Add($"DVL.XML が無い: {path}"); return r; }
        List<(string Name, string Value, string Unit)> items;
        try
        {
            items = XDocument.Load(path).Descendants("AssessmentScore")
                .Select(e => ((string?)e.Attribute("ScoreName") ?? "", (string?)e.Attribute("ScoreValue") ?? "", (string?)e.Attribute("ScoreUnit") ?? ""))
                .ToList();
        }
        catch (System.Xml.XmlException e) { r.Errors.Add($"DVL.XML を読めない: {e.Message}"); return r; }

        if (items.Count == 0) { r.Errors.Add("DVL.XML に項目が無い"); return r; }
        var prefix = $"{d.Name}.{arch}.";
        var other = items.Where(i => !i.Name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)).ToList();
        if (other.Count > 0)
        {
            var wrong = BuildTarget.All.SelectMany(t => Driver.All.Select(x => $"{x.Name}.{t.Arch}."))
                .Where(p => !p.Equals(prefix, StringComparison.OrdinalIgnoreCase))
                .Any(p => other.Any(i => i.Name.StartsWith(p, StringComparison.OrdinalIgnoreCase)));
            (wrong ? r.Errors : r.Warnings).Add($"項目名が {prefix} で始まらない: {other[0].Name}");
        }
        var summary = items.FirstOrDefault(i => i.Name.EndsWith(".Semmle.Summary"));
        if (summary.Name == null) r.Errors.Add("Semmle.Summary が無い（SARIF が反映されていない）");
        if (!items.Any(i => i.Name.EndsWith(".General.Checksum"))) r.Errors.Add("General.Checksum が無い");
        var skipped = items.Count(i => i.Unit == "SEMMLE_MUSTRUN_SKIPPED");
        if (skipped > 0)
            r.Warnings.Add($"未実行の必須クエリが {skipped} 件（SEMMLE_MUSTRUN_SKIPPED）。クエリスイートが mustrun.qls か確認すること");
        return r;
    }

    // HLK 版フォルダ内で、名前の大文字小文字や単複が版ごとに違うサブフォルダ（driver/drivers、Symbol/symbol）を探す。
    public static string? FindSubDir(string dir, params string[] names)
    {
        if (!Directory.Exists(dir)) return null;
        return Directory.EnumerateDirectories(dir)
            .FirstOrDefault(p => names.Any(n => Path.GetFileName(p).Equals(n, StringComparison.OrdinalIgnoreCase)));
    }
}
