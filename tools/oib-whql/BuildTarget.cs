// Copyright (c) 2026 OpenInputBridge Contributors
// SPDX-License-Identifier: MIT
// Licensed under the MIT License. See LICENSE file in the project root for full license text.

namespace OibWhql;

// ドライバ 1 本。Dir は driver\ の下のフォルダ。Name は DVL の名前と、HLK 版フォルダの名前に使う。
internal sealed record Driver(string Name, string Dir)
{
    public static readonly Driver[] All =
    {
        new("oib_kbd", "keyboard"),
        new("oib_mou", "mouse"),
    };
}

// 1 回の解析の単位。bat は kbd と mou の両方をビルドするので、SARIF は CPU ごとに 1 本で、DVL をドライバごとに作る。
internal sealed record BuildTarget(string Arch)
{
    public string BuildBat => $"build_codeql_{Arch}.bat";

    public static readonly BuildTarget[] All = { new("x64"), new("arm64") };
}
