param(
    [string]$ExePath = ".\project1.exe",
    [switch]$SkipBuild,
    [switch]$KeepTemp
)

<#
[导读]
- 本脚本验证 --check-data、--fix、--user-id、PROJECT1_DATA_DIR 和参数错误退出码。

[输入输出]
- 输入：tests/fixtures 下的样例数据。
- 输出：.test_tmp/cli_checks 下的隔离运行目录，以及主程序退出码断言。

[易错点]
- 每个 case 必须复制到临时目录后运行，避免污染真实 data/。
- 自动修复测试不仅检查退出码，还检查 linkedCardId 等关键字段是否被实际归正。
- 退出码是 CLI 契约的一部分，不能只看输出文本。
#>

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)
$tmpParent = Join-Path $projectRoot ".test_tmp"
$tmpRoot = Join-Path $projectRoot ".test_tmp\cli_checks"
$fixtureRoot = Join-Path $projectRoot "tests\fixtures"

function Write-Step {
    param([string]$Message)
    Write-Host "[v1.4.0] $Message"
}

function Assert-PathInsideProject {
    param([string]$Path)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($projectRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate outside project root: $fullPath"
    }
    return $fullPath
}

function Remove-SafeDirectory {
    param([string]$Path)
    $fullPath = Assert-PathInsideProject $Path
    if (Test-Path -LiteralPath $fullPath) {
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}

function Copy-FixtureData {
    param(
        [string]$FixtureName,
        [string]$CaseName
    )

    # fixture 只读，测试用例必须复制到临时目录后再运行 --fix。
    $fixtureDir = Join-Path $fixtureRoot $FixtureName
    if (-not (Test-Path -LiteralPath $fixtureDir)) {
        throw "Fixture not found: $fixtureDir"
    }

    $caseDir = Join-Path $tmpRoot $CaseName
    $dataDir = Join-Path $caseDir "data"
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $fixtureDir "users.txt") -Destination $dataDir -Force
    Copy-Item -LiteralPath (Join-Path $fixtureDir "cards.txt") -Destination $dataDir -Force
    Copy-Item -LiteralPath (Join-Path $fixtureDir "wrongs.txt") -Destination $dataDir -Force
    Copy-Item -LiteralPath (Join-Path $fixtureDir "review_logs.txt") -Destination $dataDir -Force
    return $caseDir
}

function Invoke-ProjectCase {
    param(
        [string]$CaseDir,
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label
    )

    $dataDir = Join-Path $CaseDir "data"
    Push-Location $projectRoot
    try {
        # --data-dir 追加在最后，确保测试数据目录显式覆盖默认 data/。
        & $script:ResolvedExe @Arguments --data-dir $dataDir
        $actualExitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if ($actualExitCode -ne $ExpectedExitCode) {
        throw "$Label failed: expected exit $ExpectedExitCode, got $actualExitCode"
    }
    Write-Step "$Label passed (exit=$actualExitCode)"
}

function Invoke-ProjectCaseWithEnvDataDir {
    param(
        [string]$CaseDir,
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label
    )

    $dataDir = Join-Path $CaseDir "data"
    $previousDataDir = $env:PROJECT1_DATA_DIR
    Push-Location $projectRoot
    try {
        $env:PROJECT1_DATA_DIR = $dataDir
        & $script:ResolvedExe @Arguments
        $actualExitCode = $LASTEXITCODE
    }
    finally {
        # 恢复环境变量，避免影响同一 PowerShell 进程中的后续测试。
        if ($null -eq $previousDataDir) {
            Remove-Item Env:PROJECT1_DATA_DIR -ErrorAction SilentlyContinue
        }
        else {
            $env:PROJECT1_DATA_DIR = $previousDataDir
        }
        Pop-Location
    }

    if ($actualExitCode -ne $ExpectedExitCode) {
        throw "$Label failed: expected exit $ExpectedExitCode, got $actualExitCode"
    }
    Write-Step "$Label passed (exit=$actualExitCode)"
}

function Get-WrongLinkedCardId {
    param([string]$CaseDir)
    $wrongFile = Join-Path $CaseDir "data\wrongs.txt"
    $line = Get-Content -LiteralPath $wrongFile -Encoding UTF8 | Select-Object -First 1
    $parts = $line -split "\|", -1
    return $parts[9]
}

Push-Location $projectRoot
try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom

    if (-not $SkipBuild) {
        Write-Step "Building project"
        & g++ -std=c++17 -o $ExePath src/*.cpp
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE"
        }
    }

    $script:ResolvedExe = (Resolve-Path $ExePath).Path
    Remove-SafeDirectory $tmpRoot
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null

    $cleanCase = Copy-FixtureData -FixtureName "clean_data" -CaseName "clean"
    Invoke-ProjectCase -CaseDir $cleanCase -Arguments @("--check-data") -ExpectedExitCode 0 -Label "clean data check"
    Invoke-ProjectCase -CaseDir $cleanCase -Arguments @("--check-data", "--user-id", "1") -ExpectedExitCode 0 -Label "clean data user scoped check"
    Invoke-ProjectCaseWithEnvDataDir -CaseDir $cleanCase -Arguments @("--check-data") -ExpectedExitCode 0 -Label "environment data dir check"
    Invoke-ProjectCase -CaseDir $cleanCase -Arguments @("--fix") -ExpectedExitCode 2 -Label "invalid parameter check"
    Invoke-ProjectCase -CaseDir $cleanCase -Arguments @("--check-data", "--user-id", "999") -ExpectedExitCode 2 -Label "missing user parameter check"

    $brokenCase = Copy-FixtureData -FixtureName "broken_link_data" -CaseName "broken_link"
    Invoke-ProjectCase -CaseDir $brokenCase -Arguments @("--check-data") -ExpectedExitCode 1 -Label "broken link detection"

    $brokenFixCase = Copy-FixtureData -FixtureName "broken_link_data" -CaseName "broken_link_fix"
    Invoke-ProjectCase -CaseDir $brokenFixCase -Arguments @("--check-data", "--fix") -ExpectedExitCode 0 -Label "broken link auto fix"
    Invoke-ProjectCase -CaseDir $brokenFixCase -Arguments @("--check-data") -ExpectedExitCode 0 -Label "broken link post-fix check"
    $linkedCardId = Get-WrongLinkedCardId $brokenFixCase
    if ($linkedCardId -ne "-1") {
        throw "broken link auto fix failed: expected linkedCardId -1, got $linkedCardId"
    }
    Write-Step "broken link field verification passed"

    $invalidCase = Copy-FixtureData -FixtureName "invalid_field_data" -CaseName "invalid_field"
    Invoke-ProjectCase -CaseDir $invalidCase -Arguments @("--check-data") -ExpectedExitCode 1 -Label "invalid field detection"

    $invalidFixCase = Copy-FixtureData -FixtureName "invalid_field_data" -CaseName "invalid_field_fix"
    Invoke-ProjectCase -CaseDir $invalidFixCase -Arguments @("--check-data", "--fix") -ExpectedExitCode 0 -Label "invalid field auto fix"
    Invoke-ProjectCase -CaseDir $invalidFixCase -Arguments @("--check-data") -ExpectedExitCode 0 -Label "invalid field post-fix check"

    Write-Step "All CLI checks passed"
}
finally {
    Pop-Location
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpRoot
        $tmpParentFull = Assert-PathInsideProject $tmpParent
        if ((Test-Path -LiteralPath $tmpParentFull) -and -not (Get-ChildItem -LiteralPath $tmpParentFull -Force)) {
            Remove-Item -LiteralPath $tmpParentFull -Force
        }
    }
}
