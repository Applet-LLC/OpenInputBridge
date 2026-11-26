// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.

namespace OibWhql;

// HLK 版フォルダを作り、ドライバとシンボルを置く。run の前に 1 回実行する。
//   <HLK 版フォルダ>\<ドライバ>\<cpu>\driver\   ← cat / inf / sys（署名前）
//                                      Signed\   ← 空（WHQL 署名が戻ったら、ここに置く）
//                                      Symbol\   ← pdb
internal static class Prepare
{
    // 元のファイルが 1 つでも足りなければ、何もコピーせずに止める。既に配置済みのときは、force が無ければ止める。
    public static bool Run(Config c, RunLog log, IEnumerable<BuildTarget> targets, string hlkDir, bool force)
    {
        var plan = new List<(string Src, string Dest)>();
        var errors = new List<string>();
        var signedDirs = new List<string>();

        foreach (var t in targets)
            foreach (var d in Driver.All)
            {
                var (_, files, pdb) = c.DriverSource(d, t.Arch);
                var outDir = c.HlkDriverDir(hlkDir, d, t.Arch);
                var driverDir = Path.Combine(outDir, "driver");
                var symbolDir = Path.Combine(outDir, "Symbol");
                foreach (var f in files.Append(pdb))
                    if (!File.Exists(f)) errors.Add($"元のファイルが無い: {f}");
                if (!force && Directory.Exists(driverDir) && Directory.EnumerateFileSystemEntries(driverDir).Any())
                    errors.Add($"配置済み（上書きするなら --force）: {driverDir}");
                foreach (var f in files) plan.Add((f, Path.Combine(driverDir, Path.GetFileName(f))));
                plan.Add((pdb, Path.Combine(symbolDir, Path.GetFileName(pdb))));
                signedDirs.Add(Path.Combine(outDir, "Signed"));
            }

        if (errors.Count > 0)
        {
            foreach (var e in errors) log.Line($"  [NG] {e}");
            log.Line("  [失敗] 何もコピーしていない。");
            return false;
        }

        foreach (var (src, dest) in plan)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            File.Copy(src, dest, overwrite: true);
            log.Line($"  {src}  ({File.GetLastWriteTime(src):yyyy-MM-dd HH:mm})\n    -> {dest}  SHA-256 {RunLog.Sha256(dest)}");
        }
        foreach (var s in signedDirs) Directory.CreateDirectory(s);
        log.Line($"  完了: {plan.Count} ファイルを配置した。");
        return true;
    }
}
