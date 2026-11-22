# Attestation Signing (Partner Center / Hardware Dashboard) に提出する、Windows 10用ドライバの
# CABを作る。oib_kbd_w10.cab と oib_mou_w10.cab を、出力先 (既定: <packaging>\attestation) に作る。
#
# 入力は、ソリューションの oib_kbd_win10 / oib_mou_win10 プロジェクトのビルド出力
# (driver\<keyboard|mouse>\x64\Release_Win10\<name>_win10\ の .inf / .sys / .cat)。
# 先にフルビルド(またはその2プロジェクト)をReleaseでビルドしておくこと。
#
# CABの構造は、Microsoftの手順(code-signing-attestation)に従う: ルートにファイルを置かず、
# ドライバーパッケージごとにサブフォルダを作る (oib_kbd_w10.cab の中は oib_kbd\ 、oib_mou_w10.cab の
# 中は oib_mou\ )。.inf/.sys/.cat/.pdb を入れる。.pdbは自動クラッシュ分析に必要とされるため、
# 既定で入れる (-NoSymbols で外せる)。.catは会社の検証用で、Microsoftが作り直して置き換える。
#
# -SignContents を付けると、CABに入れる .sys と .cat にもEV署名する (WHQL申請と同じ流れ)。手順上は
# 必須ではない (.catはMicrosoftが作り直す) が、.catは会社の検証に使われるため、付けておくと安全。
# ビルド出力は変えず、一時フォルダのコピーに署名してからCABに詰める。署名してもカタログ内の
# ハッシュは変わらない (Authenticodeのハッシュは署名部分を除く)。
#
# 提出CABは、EV証明書で署名されている必要がある (Hardware Dashboardの要件)。-Sign を付けると、
# sign.mak と同じ証明書 (サブジェクト "Applet LLC"、USBトークン) とタイムスタンプで署名する。
# 証明書が見つからなければ、sign.mak のように黙って飛ばさず、エラーにする。
#
# 使い方:
#   .\Make-AttestationCab.ps1                  CABを作る (未署名)
#   .\Make-AttestationCab.ps1 -Sign            CABを作って、EV署名する
#   .\Make-AttestationCab.ps1 -SignContents -Sign   .sys/.cat にもEV署名してから、CABを作って署名する (推奨)
#   .\Make-AttestationCab.ps1 -NoSymbols       .pdb を入れない
#   .\Make-AttestationCab.ps1 -OutputDir D:\submit
#
# 返ってきた署名済みパッケージ (.inf/.sys/.cat、あれば.pdb) は、packaging\Signed\oib_kbd\x64_win10\ と
# packaging\Signed\oib_mou\x64_win10\ に置くと、sign.mak の whql が署名済みとして使う。
param(
    [string] $OutputDir = "",
    [switch] $Sign,
    [switch] $SignContents,
    [switch] $NoSymbols
)

$ErrorActionPreference = "Stop"

$CertSubject = "Applet LLC"
$TimestampUrl = "http://timestamp.globalsign.com/tsa/r45standard"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ($OutputDir -eq "") { $OutputDir = Join-Path $PSScriptRoot "attestation" }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

# CABの名前、ドライバ名、プロジェクトのフォルダ名
$packages = @(
    @{ Cab = "oib_kbd_w10.cab"; Name = "oib_kbd"; Dir = "keyboard" },
    @{ Cab = "oib_mou_w10.cab"; Name = "oib_mou"; Dir = "mouse" }
)

function Get-SigntoolExe {
    $binRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    if (Test-Path $binRoot) {
        $verDirs = Get-ChildItem -LiteralPath $binRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object { $_.Name } -Descending
        foreach ($d in $verDirs) {
            $candidate = Join-Path $d.FullName "x64\signtool.exe"
            if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
        }
    }
    throw "signtool.exe not found under Windows Kits 10. Install the Windows SDK."
}

function Invoke-EvSign {
    param([string[]] $Paths)
    $cert = Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -like "*$CertSubject*" -and $_.NotAfter -gt (Get-Date) } |
        Select-Object -First 1
    if (-not $cert) {
        throw "EV certificate '$CertSubject' not found (USB token disconnected?). Nothing was signed."
    }
    $signtool = Get-SigntoolExe
    & $signtool sign /n $CertSubject /tr $TimestampUrl /td sha256 /fd sha256 @Paths | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
}

function New-PackageCab {
    param([hashtable] $Package)

    $name = $Package.Name
    $srcDir = Join-Path $repoRoot "driver\$($Package.Dir)\x64\Release_Win10\${name}_win10"
    $files = @("$name.inf", "$name.sys", "$name.cat")
    foreach ($f in $files) {
        if (-not (Test-Path -LiteralPath (Join-Path $srcDir $f))) {
            throw "[$name] $f not found in $srcDir`nBuild the ${name}_win10 project first (Release|x64, EWDK environment)."
        }
    }

    # Win10 1607の古いINFパーサーや提出ツールを避けるため、.infに非ASCII文字が無いことを確認する。
    $infBytes = [System.IO.File]::ReadAllBytes((Join-Path $srcDir "$name.inf"))
    if (@($infBytes | Where-Object { $_ -gt 127 }).Count -gt 0) {
        Write-Warning "[$name] $name.inf contains non-ASCII bytes (the .inx should be ASCII only)."
    }

    $paths = @($files | ForEach-Object { Join-Path $srcDir $_ })
    $workDir = $null
    if ($SignContents) {
        # ビルド出力を汚さないよう、コピーに署名する。
        $workDir = Join-Path ([System.IO.Path]::GetTempPath()) "oib_attest_$name"
        if (Test-Path -LiteralPath $workDir) { Remove-Item -LiteralPath $workDir -Recurse -Force }
        New-Item -ItemType Directory -Path $workDir | Out-Null
        foreach ($f in $files) { Copy-Item -LiteralPath (Join-Path $srcDir $f) -Destination $workDir }
        Invoke-EvSign -Paths @((Join-Path $workDir "$name.sys"), (Join-Path $workDir "$name.cat"))
        $paths = @($files | ForEach-Object { Join-Path $workDir $_ })
    }
    if (-not $NoSymbols) {
        $pdb = Join-Path $repoRoot "driver\$($Package.Dir)\x64\Release_Win10\$name.pdb"
        if (Test-Path -LiteralPath $pdb) { $paths += $pdb }
        else { Write-Warning "[$name] $name.pdb not found: $pdb" }
    }

    $cabPath = Join-Path $OutputDir $Package.Cab
    if (Test-Path -LiteralPath $cabPath) { Remove-Item -LiteralPath $cabPath -Force }

    # makecab のDDF。ファイルは、ドライバー名のサブフォルダに置く (ルートには置かない)。
    $ddf = New-Object System.Collections.Generic.List[string]
    $ddf.Add(".OPTION EXPLICIT")
    $ddf.Add(".Set CabinetFileCountThreshold=0")
    $ddf.Add(".Set FolderFileCountThreshold=0")
    $ddf.Add(".Set FolderSizeThreshold=0")
    $ddf.Add(".Set MaxCabinetSize=0")
    $ddf.Add(".Set MaxDiskFileCount=0")
    $ddf.Add(".Set MaxDiskSize=0")
    $ddf.Add(".Set CompressionType=MSZIP")
    $ddf.Add(".Set Cabinet=on")
    $ddf.Add(".Set Compress=on")
    $ddf.Add(".Set CabinetNameTemplate=$($Package.Cab)")
    $ddf.Add(".Set DiskDirectoryTemplate=`"$OutputDir`"")
    $ddf.Add(".Set DestinationDir=$name")
    foreach ($p in $paths) { $ddf.Add("`"$p`"") }

    $ddfPath = Join-Path ([System.IO.Path]::GetTempPath()) "$name`_w10.ddf"
    [System.IO.File]::WriteAllLines($ddfPath, $ddf, (New-Object System.Text.UTF8Encoding($false)))
    try {
        $log = & makecab.exe /F $ddfPath 2>&1
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $cabPath)) {
            throw "[$name] makecab failed (exit $LASTEXITCODE):`n$($log -join "`n")"
        }
    } finally {
        if ($workDir) { Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue }
        Remove-Item -LiteralPath $ddfPath -Force -ErrorAction SilentlyContinue
        # makecab が作業ディレクトリに残す setup.inf / setup.rpt を消す。
        foreach ($leftover in "setup.inf", "setup.rpt") {
            Remove-Item -LiteralPath (Join-Path (Get-Location) $leftover) -Force -ErrorAction SilentlyContinue
        }
    }

    Write-Host "[$name] created: $cabPath ($((Get-Item -LiteralPath $cabPath).Length) bytes, $($paths.Count) files)"
    return $cabPath
}

$cabs = @()
foreach ($pkg in $packages) { $cabs += New-PackageCab -Package $pkg }

if ($Sign) {
    Invoke-EvSign -Paths $cabs
    foreach ($c in $cabs) {
        $status = (Get-AuthenticodeSignature -LiteralPath $c).Status
        Write-Host "signed: $c ($status)"
    }
} else {
    Write-Host "Not signed (add -Sign to EV-sign the CABs; Hardware Dashboard requires a signed submission)."
}
