// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.

using OibWhql;

// oib-whql: WHQL 申請用の CodeQL 解析と DVL 作成を、x64/ARM64 の両方、kbd/mou の両ドライバで同じ手順で実行する。

const string Usage = """
    使い方:
      oib-whql check  [--arch x64|arm64] [--dvl cli|manual]
      oib-whql run    --hlk <HLK 版フォルダ> [--arch x64|arm64] [--dvl cli|manual]
      oib-whql verify --hlk <HLK 版フォルダ> [--arch x64|arm64]

    版（OSS と Pro / Subsc）は、リポジトリごとに oib-whql.json の Edition で決まる。
    --layout "{arch}\{driver}": HLK 版フォルダの構成を一時的に変える（既定は oib-whql.json の HlkLayout）。
    --dvl cli（既定）: dvl.exe で DVL を作る。OIB は Code Analysis を使わないので、VS 拡張で作ったものと同じになる。
    --dvl manual     : DVL は VS2022 の拡張で作る。SARIF の配置と DVL の検査・回収は自動。
    """;

Console.OutputEncoding = System.Text.Encoding.UTF8;
if (args.Length == 0 || args[0] is "-h" or "--help") { Console.WriteLine(Usage); return 0; }

var cmd = args[0];
var opt = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
for (var i = 1; i < args.Length; i++)
{
    if (!args[i].StartsWith("--") || i + 1 >= args.Length) { Console.Error.WriteLine($"引数が不正: {args[i]}\n{Usage}"); return 2; }
    opt[args[i][2..]] = args[++i];
}

var cfg = Config.Load();
if (string.IsNullOrEmpty(cfg.RepoDir)) { Console.Error.WriteLine("リポジトリのフォルダが分からない（oib-whql.json の RepoDir を指定してください）"); return 2; }
if (!cfg.Edition.Equals("oss", StringComparison.OrdinalIgnoreCase) && !cfg.IsSubsc)
{ Console.Error.WriteLine($"Edition は oss か subsc: {cfg.Edition}"); return 2; }

// HLK 版フォルダの構成が、過去の版と違うとき（例: x64\oib_kbd）に、一時的に指定できる。
if (opt.TryGetValue("layout", out var layout) && !string.IsNullOrWhiteSpace(layout)) cfg.HlkLayout = layout;

opt.TryGetValue("arch", out var ar);
var targets = BuildTarget.All.Where(t => ar == null || t.Arch.Equals(ar, StringComparison.OrdinalIgnoreCase)).ToList();
if (targets.Count == 0) { Console.Error.WriteLine("--arch は x64 か arm64"); return 2; }

var mode = DvlMode.Cli;
if (opt.TryGetValue("dvl", out var m))
{
    if (m.Equals("manual", StringComparison.OrdinalIgnoreCase)) mode = DvlMode.Manual;
    else if (!m.Equals("cli", StringComparison.OrdinalIgnoreCase)) { Console.Error.WriteLine("--dvl は cli か manual"); return 2; }
}

string hlk = "";
if (cmd != "check")
{
    if (!opt.TryGetValue("hlk", out hlk!) || string.IsNullOrWhiteSpace(hlk)) { Console.Error.WriteLine("--hlk が必要"); return 2; }
}

switch (cmd)
{
    case "check": return Check();
    case "run": return Run();
    case "verify": return Verify();
    default: Console.Error.WriteLine(Usage); return 2;
}

int Check()
{
    using var log = new RunLog(Path.Combine(Path.GetTempPath(), "oib-whql-check.log"));
    log.Line($"版: {cfg.Edition}  リポジトリ: {cfg.RepoDir}");
    log.Line($"対象: {string.Join(", ", targets.Select(t => t.Arch))}  ドライバ: {string.Join(", ", Driver.All.Select(d => d.Name))}");
    var r = Checks.Preflight(cfg, targets, needDvlExe: mode == DvlMode.Cli);
    r.Print(log, "");
    log.Line(r.Ok ? "前提の検査: OK" : "前提の検査: NG");
    return r.Ok ? 0 : 1;
}

int Run()
{
    var pre = Checks.Preflight(cfg, targets, needDvlExe: mode == DvlMode.Cli);
    if (!pre.Ok) { foreach (var e in pre.Errors) Console.Error.WriteLine($"[NG] {e}"); return 1; }
    var folder = Checks.HlkFolder(cfg, hlk);
    if (!folder.Ok) { foreach (var e in folder.Errors) Console.Error.WriteLine($"[NG] {e}"); return 1; }

    var results = new List<(BuildTarget t, bool ok)>();
    using (var log = new RunLog(Path.Combine(hlk, "whql-run.log")))
    {
        log.Line($"版: {cfg.Edition}  リポジトリ: {cfg.RepoDir}  DVL: {mode}");
        var pipe = new Pipeline(cfg, log, mode);
        foreach (var t in targets)
        {
            bool ok;
            try { ok = pipe.Run(t, hlk); }
            catch (Exception e) { log.Line($"  [失敗] 例外: {e.Message}"); ok = false; }
            results.Add((t, ok));
            if (!ok) log.Line($"  {t.Arch} は失敗。残りの CPU は続けます。");
        }
    }

    Console.WriteLine("\n==== 結果 ====");
    foreach (var (t, ok) in results) Console.WriteLine($"  {(ok ? "OK" : "NG")}  {t.Arch}");
    return results.All(r => r.ok) ? 0 : 1;
}

int Verify()
{
    var allOk = true;
    var folder = Checks.HlkFolder(cfg, hlk);
    if (!folder.Ok) { foreach (var e in folder.Errors) Console.WriteLine($"[NG] {e}"); return 1; }
    foreach (var t in targets)
    {
        // DB は CPU ごとに 1 つ。残っていれば、取り違えがないか見る（申請後は使い回されるので、警告にとどめる）。
        var db = Path.Combine(cfg.CodeQlHome, cfg.DbName(t.Arch));
        var dbWarn = new List<string>();
        if (Directory.Exists(db))
        {
            var dbr = Checks.Database(cfg, db);
            dbWarn.AddRange(dbr.Errors.Select(e => "DB（作業後に使い回されたなら問題なし）: " + e));
            dbWarn.AddRange(dbr.Warnings);
        }

        foreach (var d in Driver.All)
        {
            var dir = cfg.HlkDriverDir(hlk, d, t.Arch);
            var r = new CheckResult();
            if (!Directory.Exists(dir)) r.Errors.Add($"フォルダが無い: {dir}");
            else
            {
                foreach (var (label, names) in new[] { ("driver", new[] { "driver", "drivers" }), ("Symbol", new[] { "Symbol" }) })
                {
                    var sub = Checks.FindSubDir(dir, names);
                    if (sub == null || !Directory.EnumerateFileSystemEntries(sub).Any()) r.Errors.Add($"{label}\\ が無いか空");
                }
                r.Add(Checks.Sarif(Path.Combine(dir, Config.HlkSarifName(t.Arch))));
                r.Add(Checks.Dvl(Path.Combine(dir, $"{d.Name}.DVL.XML"), d, t.Arch));
                r.Warnings.AddRange(dbWarn);
            }
            Console.WriteLine($"{(r.Ok ? "OK" : "NG")}  {d.Name} {t.Arch}  ({dir})");
            foreach (var e in r.Errors) Console.WriteLine($"    [NG] {e}");
            foreach (var w in r.Warnings) Console.WriteLine($"    [注意] {w}");
            allOk &= r.Ok;
        }
    }
    return allOk ? 0 : 1;
}
