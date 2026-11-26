// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.

using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;

namespace OibWhql;

// 画面とログファイル（HLK 版フォルダの whql-run.log）に同じ内容を出す。外部コマンドの実行もここで受け持つ。
internal sealed class RunLog : IDisposable
{
    private readonly StreamWriter _w;

    public RunLog(string path)
    {
        _w = new StreamWriter(path, append: true, new UTF8Encoding(false)) { AutoFlush = true };
        Line($"==== {DateTime.Now:yyyy-MM-dd HH:mm:ss} 開始 ====");
    }

    public void Line(string s)
    {
        Console.WriteLine(s);
        _w.WriteLine(s);
    }

    // cmd.exe /c で実行し、出力を画面とログに流して、終了コードを返す。
    public int Run(string commandLine, string workDir)
    {
        Line($"> {commandLine}   (cwd: {workDir})");
        var psi = new ProcessStartInfo("cmd.exe", "/s /c \"" + commandLine + "\"")
        {
            WorkingDirectory = workDir,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        using var p = Process.Start(psi)!;
        void Pump(StreamReader r) { string? l; while ((l = r.ReadLine()) != null) lock (_w) Line(l); }
        var t1 = Task.Run(() => Pump(p.StandardOutput));
        var t2 = Task.Run(() => Pump(p.StandardError));
        p.WaitForExit();
        Task.WaitAll(t1, t2);
        Line($"  終了コード: {p.ExitCode}");
        return p.ExitCode;
    }

    public static string Sha256(string path)
    {
        using var s = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(s));
    }

    public void Dispose() => _w.Dispose();
}
