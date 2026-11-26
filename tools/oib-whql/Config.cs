// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.

using System.Text.Json;

namespace OibWhql;

// 設定。パスは oib-whql.json で変えられる（無ければ既定値）。RepoDir が空なら、実行ファイルから上へ辿って探す。
internal sealed class Config
{
    public string Edition { get; set; } = "oss";   // "oss"（OSS と、WHQL 署名を取る Pro）| "subsc"
    public string CodeQlHome { get; set; } = @"E:\codeql-home";
    public string CodeQlCmd { get; set; } = @"E:\codeql-home\codeql-2.20.1\codeql.cmd";
    public string CodeQlVersion { get; set; } = "2.20.1";
    public string QuerySuite { get; set; } = "microsoft/windows-drivers@1.8.0:windows-driver-suites/mustrun.qls";
    public string BuildEnvCmd { get; set; } = @"K:\BuildEnv\SetupBuildEnv.cmd";
    public string DvlExe { get; set; } = @"C:\Program Files (x86)\Windows Kits\10\Tools\dvl\dvl.exe";
    public string RepoDir { get; set; } = "";
    // HLK 版フォルダの中の、ドライバごとの置き場所。{driver} は oib_kbd/oib_mou、{arch} は x64/arm64。
    public string HlkLayout { get; set; } = @"{driver}\{arch}";

    public bool IsSubsc => Edition.Equals("subsc", StringComparison.OrdinalIgnoreCase);
    public string DriverRoot => Path.Combine(RepoDir, "driver");

    // kasane や、もう一方の OIB リポジトリと E:\codeql-home を共有するので、DB と SARIF の名前は版と CPU で分ける。
    public string DbName(string arch) => IsSubsc ? $"databases-oib-subsc-{arch}" : $"databases-oib-{arch}";
    public string CodeQlSarifName(string arch) => IsSubsc ? $"OpenInputBridge-Subscription-{arch}.sarif" : $"OpenInputBridge-{arch}.sarif";
    // HLK 版フォルダとドライバのフォルダに置く SARIF の名前（従来どおり、版によらず同じ）。
    public static string HlkSarifName(string arch) => $"OpenInputBridge-{arch}.sarif";

    public string HlkDriverDir(string hlkDir, Driver d, string arch) =>
        Path.Combine(hlkDir, HlkLayout.Replace("{driver}", d.Name).Replace("{arch}", arch));

    public static Config Load()
    {
        var cfg = new Config();
        var path = Path.Combine(AppContext.BaseDirectory, "oib-whql.json");
        if (File.Exists(path))
            cfg = JsonSerializer.Deserialize<Config>(File.ReadAllText(path)) ?? cfg;
        if (string.IsNullOrWhiteSpace(cfg.RepoDir))
            cfg.RepoDir = FindRepoDir() ?? "";
        return cfg;
    }

    // 実行ファイルの場所から上に辿り、driver\build_codeql_x64.bat のあるフォルダ（リポジトリのルート）を返す。
    private static string? FindRepoDir()
    {
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
            if (File.Exists(Path.Combine(d.FullName, "driver", "build_codeql_x64.bat")))
                return d.FullName;
        return null;
    }
}
