// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.

namespace OibWhql;

internal enum DvlMode { Cli, Manual }

// 1 CPU ぶんの処理: DB 作成 → 解析 → SARIF を HLK へ → ドライバごとに DVL 作成 → 検査して HLK へ。
internal sealed class Pipeline
{
    private readonly Config _c;
    private readonly RunLog _log;
    private readonly DvlMode _mode;

    public Pipeline(Config c, RunLog log, DvlMode mode) { _c = c; _log = log; _mode = mode; }

    private string Home(string name) => Path.Combine(_c.CodeQlHome, name);

    // 成功なら true。途中の失敗は理由をログに出して false。
    public bool Run(BuildTarget t, string hlkDir)
    {
        _log.Line($"\n---- {t.Arch} ----");
        var pre = Checks.HlkFolder(_c, hlkDir);
        if (!pre.Ok) { pre.Print(_log); return false; }

        // 1. 古い DB と SARIF を消す。消すのは、このツールが決めた名前（databases-oib-*）の中身だけ。
        var db = Home(_c.DbName(t.Arch));
        var sarif = Home(_c.CodeQlSarifName(t.Arch));
        if (!ClearDatabase(db)) return false;
        if (File.Exists(sarif)) File.Delete(sarif);

        // 2. DB 作成（bat で kbd と mou をビルドしながら、CodeQL が観察する）
        var bat = Path.Combine(_c.DriverRoot, t.BuildBat);
        if (_log.Run($"\"{_c.CodeQlCmd}\" database create \"{db}\" --language=cpp --source-root=\"{_c.DriverRoot}\" --command=\"{bat}\"", _c.DriverRoot) != 0)
            return Fail("database create が失敗した");
        var dbCheck = Checks.Database(_c, db);
        dbCheck.Print(_log);
        if (!dbCheck.Ok) return Fail("DB の検査で止めた");

        // 3. 解析
        if (_log.Run($"\"{_c.CodeQlCmd}\" database analyze \"{db}\" {_c.QuerySuite} --format=sarifv2.1.0 --output=\"{sarif}\"", _c.CodeQlHome) != 0)
            return Fail("database analyze が失敗した");
        var sarifCheck = Checks.Sarif(sarif);
        sarifCheck.Print(_log);
        if (!sarifCheck.Ok) return Fail("SARIF の検査で止めた");

        // 4. ドライバごとに、SARIF を HLK 版フォルダへ置き、DVL を作る
        var allOk = true;
        foreach (var d in Driver.All)
        {
            var outDir = _c.HlkDriverDir(hlkDir, d, t.Arch);
            Directory.CreateDirectory(outDir);
            var sarifDest = Path.Combine(outDir, Config.HlkSarifName(t.Arch));
            File.Copy(sarif, sarifDest, overwrite: true);
            _log.Line($"  [{d.Name}] SARIF -> {sarifDest}  SHA-256 {RunLog.Sha256(sarifDest)}");
            allOk &= MakeDvl(t, d, sarifDest, Path.Combine(outDir, $"{d.Name}.DVL.XML"));
        }

        _log.Line(allOk ? $"  完了: {t.Arch}" : $"  [失敗] {t.Arch}: DVL を作れなかったドライバがある");
        return allOk;
    }

    private bool MakeDvl(BuildTarget t, Driver d, string sarif, string dvlDest)
    {
        var driverDir = Path.Combine(_c.DriverRoot, d.Dir);
        var projDvl = Path.Combine(driverDir, $"{d.Name}.DVL.XML");
        // ドライバのフォルダには、いまの 1 本の SARIF だけを置く（dvl.exe も VS 拡張も、ここを見る）。
        CleanStage();
        File.Copy(sarif, Path.Combine(driverDir, Config.HlkSarifName(t.Arch)), overwrite: true);
        var started = DateTime.Now.AddSeconds(-1);
        try
        {
            if (_mode == DvlMode.Cli)
            {
                if (_log.Run($"\"{_c.DvlExe}\" /manualCreate {d.Name} {t.Arch}", driverDir) != 0)
                    return Fail($"[{d.Name}] dvl.exe が失敗した");
            }
            else
            {
                var proj = t.Arch == "arm64" ? $"{d.Name}_arm64" : d.Name;
                _log.Line($"  ▶ VS2022 で、{proj} プロジェクトを選び、");
                _log.Line("    Driver → Create Driver Verification Log を実行してください（SARIF が Detected になっているのを確認して Create）。");
                _log.Line("    クリップボードのエラーダイアログは × で閉じて構いません。終わったら Enter を押してください。");
                Console.ReadLine();
            }

            if (!File.Exists(projDvl) || File.GetLastWriteTime(projDvl) < started)
                return Fail($"[{d.Name}] 新しい {d.Name}.DVL.XML が見つからない（{projDvl}）");
            var check = Checks.Dvl(projDvl, d, t.Arch);
            check.Print(_log);
            if (!check.Ok) return Fail($"[{d.Name}] DVL の検査で止めた");

            File.Copy(projDvl, dvlDest, overwrite: true);
            _log.Line($"  [{d.Name}] DVL -> {dvlDest}  SHA-256 {RunLog.Sha256(dvlDest)}");
            return true;
        }
        finally { CleanStage(); }
    }

    // ドライバのフォルダから、このツールが置いた/作った SARIF と DVL を片づける（どれも .gitignore 済み）。
    private void CleanStage()
    {
        foreach (var d in Driver.All)
        {
            var dir = Path.Combine(_c.DriverRoot, d.Dir);
            foreach (var t in BuildTarget.All)
                Delete(Path.Combine(dir, Config.HlkSarifName(t.Arch)));
            Delete(Path.Combine(dir, $"{d.Name}.DVL.XML"));
            Delete(Path.Combine(dir, "DvlErrLog.txt"));
        }
    }

    private static void Delete(string p) { if (File.Exists(p)) File.Delete(p); }

    private bool ClearDatabase(string db)
    {
        var full = Path.GetFullPath(db);
        var home = Path.GetFullPath(_c.CodeQlHome).TrimEnd('\\') + "\\";
        var known = BuildTarget.All.Any(x => Home(_c.DbName(x.Arch)).Equals(full, StringComparison.OrdinalIgnoreCase));
        if (!full.StartsWith(home, StringComparison.OrdinalIgnoreCase) || !known)
            return Fail($"消そうとしたフォルダが想定外のため止めた: {full}");
        if (Directory.Exists(full))
        {
            _log.Line($"  DB を空にする: {full}");
            foreach (var d in Directory.GetDirectories(full)) Directory.Delete(d, true);
            foreach (var f in Directory.GetFiles(full)) File.Delete(f);
        }
        return true;
    }

    private bool Fail(string why) { _log.Line($"  [失敗] {why}"); return false; }
}
