param(
    [string]$ExePath = ".\project1.exe",
    [switch]$SkipBuild,
    [switch]$KeepTemp
)

<#
脚本职责：
- 使用 UTF-8 fixture 验证中文注册、登录、卡片、错题和错题转卡片内容可正确落盘。

关键约束：
- 中文输入从 tests/fixtures/e2e_inputs 读取，避免 PowerShell 脚本源码编码影响测试数据。
- 断言必须先解码存储字段，再与 expected fixture 比较。
- 该脚本覆盖中文链路，不负责穷举所有菜单分支。
#>

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)
$tmpParent = Join-Path $projectRoot ".test_tmp"
$tmpRoot = Join-Path $tmpParent "chinese_input_flow"
$dataDir = Join-Path $tmpRoot "data"
$outputFile = Join-Path $tmpRoot "chinese_output.log"
$fixtureDir = Join-Path $projectRoot "tests\fixtures\e2e_inputs"
$inputFixture = Join-Path $fixtureDir "chinese_business_flow.txt"
$expectedFixture = Join-Path $fixtureDir "chinese_expected_fields.txt"

function Write-Step {
    param([string]$Message)
    Write-Host "[v1.4.4] $Message"
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

function Split-RecordLine {
    param([string]$Line)
    return $Line -split "\|", -1
}

function ConvertFrom-StorageField {
    param([string]$Value)

    # 与 storage.cpp 的 encodeStorageField/decodeStorageField 保持同一兼容范围。
    $builder = [System.Text.StringBuilder]::new()
    $i = 0
    while ($i -lt $Value.Length) {
        if ($Value[$i] -eq '%' -and $i + 2 -lt $Value.Length) {
            $code = $Value.Substring($i + 1, 2).ToUpperInvariant()
            if ($code -eq "25") {
                [void]$builder.Append('%')
                $i += 3
                continue
            }
            if ($code -eq "7C") {
                [void]$builder.Append('|')
                $i += 3
                continue
            }
            if ($code -eq "0D") {
                [void]$builder.Append("`r")
                $i += 3
                continue
            }
            if ($code -eq "0A") {
                [void]$builder.Append("`n")
                $i += 3
                continue
            }
        }
        [void]$builder.Append($Value[$i])
        $i++
    }
    return $builder.ToString()
}

function Decode-StorageField {
    param([string]$Value)
    return ConvertFrom-StorageField $Value
}

function Read-ExpectedFields {
    param(
        [string]$Path,
        [System.Text.Encoding]$Encoding
    )

    # expected fixture 使用 key=value，允许中文值直接存放在 UTF-8 文本中。
    $map = @{}
    $text = [System.IO.File]::ReadAllText($Path, $Encoding)
    $lines = $text -split "`r?`n"
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) {
            continue
        }
        $eq = $trimmed.IndexOf("=")
        if ($eq -le 0) {
            throw "Invalid expected field line: $trimmed"
        }
        $key = $trimmed.Substring(0, $eq)
        $value = $trimmed.Substring($eq + 1)
        $map[$key] = $value
    }
    return $map
}

function Get-RequiredLine {
    param(
        [string]$Path,
        [scriptblock]$Predicate,
        [string]$FailureMessage
    )

    $lines = Get-Content -LiteralPath $Path -Encoding UTF8
    foreach ($line in $lines) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $parts = Split-RecordLine $line
        if (& $Predicate $parts) {
            return $parts
        }
    }
    throw $FailureMessage
}

function Assert-Equal {
    param(
        [string]$Label,
        [object]$Actual,
        [object]$Expected
    )
    if ($Actual -ne $Expected) {
        throw "$Label failed: expected '$Expected', got '$Actual'"
    }
    Write-Step "$Label passed"
}

function Assert-True {
    param(
        [string]$Label,
        [bool]$Condition
    )
    if (-not $Condition) {
        throw "$Label failed"
    }
    Write-Step "$Label passed"
}

function Invoke-ProjectCase {
    param(
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label,
        [string]$InputText = $null,
        [string]$OutputPath = $null
    )

    Push-Location $projectRoot
    try {
        if ($null -ne $InputText) {
            if ($OutputPath) {
                $InputText | & $script:ResolvedExe @Arguments > $OutputPath 2>&1
            }
            else {
                $InputText | & $script:ResolvedExe @Arguments
            }
        }
        else {
            if ($OutputPath) {
                & $script:ResolvedExe @Arguments > $OutputPath 2>&1
            }
            else {
                & $script:ResolvedExe @Arguments
            }
        }
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

    # 中文 E2E 必须从空数据目录启动，避免同名中文用户导致注册分支变更。
    Remove-SafeDirectory $tmpRoot
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    foreach ($name in @("users.txt", "cards.txt", "wrongs.txt", "review_logs.txt")) {
        [System.IO.File]::WriteAllText((Join-Path $dataDir $name), "", $utf8NoBom)
    }

    $inputText = [System.IO.File]::ReadAllText($inputFixture, $utf8NoBom)
    $expected = Read-ExpectedFields -Path $expectedFixture -Encoding $utf8NoBom

    Write-Step "Running chinese input business flow"
    Invoke-ProjectCase `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "chinese input business flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    $usersFile = Join-Path $dataDir "users.txt"
    $cardsFile = Join-Path $dataDir "cards.txt"
    $wrongsFile = Join-Path $dataDir "wrongs.txt"

    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and (Decode-StorageField $parts[1]) -eq $script:expected["username"] } `
        -FailureMessage "registered chinese user not found"
    $userId = $userParts[0]
    Assert-Equal -Label "chinese username preserved" -Actual (Decode-StorageField $userParts[1]) -Expected $expected["username"]
    Assert-Equal -Label "chinese user password preserved" -Actual (Decode-StorageField $userParts[2]) -Expected $expected["password"]

    $manualCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[1] -eq $userId -and (Decode-StorageField $parts[4]) -eq $script:expected["card_title"] -and $parts[16] -eq "1" } `
        -FailureMessage "chinese card not found"
    Assert-Equal -Label "chinese card subject preserved" -Actual (Decode-StorageField $manualCard[2]) -Expected $expected["card_subject"]
    Assert-Equal -Label "chinese card chapter preserved" -Actual (Decode-StorageField $manualCard[3]) -Expected $expected["card_chapter"]
    Assert-Equal -Label "chinese card front preserved" -Actual (Decode-StorageField $manualCard[5]) -Expected $expected["card_front"]
    Assert-Equal -Label "chinese card back preserved" -Actual (Decode-StorageField $manualCard[6]) -Expected $expected["card_back"]
    Assert-Equal -Label "chinese card tags preserved" -Actual (Decode-StorageField $manualCard[7]) -Expected $expected["card_tags"]

    $wrongParts = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[1] -eq $userId -and (Decode-StorageField $parts[4]) -eq $script:expected["wrong_question"] -and $parts[17] -eq "1" } `
        -FailureMessage "chinese wrong question not found"
    Assert-Equal -Label "chinese wrong correct answer preserved" -Actual (Decode-StorageField $wrongParts[5]) -Expected $expected["wrong_correct"]
    Assert-Equal -Label "chinese wrong answer preserved" -Actual (Decode-StorageField $wrongParts[6]) -Expected $expected["wrong_answer"]
    Assert-Equal -Label "chinese wrong reason preserved" -Actual (Decode-StorageField $wrongParts[7]) -Expected $expected["wrong_reason"]

    $linkedCardId = $wrongParts[9]
    Assert-True -Label "chinese wrong has linked card" -Condition ($linkedCardId -ne "-1")

    # 转换卡片的正面来自错题题目，背面必须同时保留正确答案和错因分析。
    $convertedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq $linkedCardId -and $parts[1] -eq $userId -and $parts[16] -eq "1" } `
        -FailureMessage "converted card for chinese wrong not found"
    Assert-Equal -Label "converted chinese card front preserved" -Actual (Decode-StorageField $convertedCard[5]) -Expected $expected["wrong_question"]
    $convertedBackDecoded = Decode-StorageField $convertedCard[6]
    Assert-True -Label "converted chinese card back starts with answer" -Condition ($convertedBackDecoded.StartsWith($expected["wrong_correct"]))
    Assert-True -Label "converted chinese card back carries reason" -Condition ($convertedBackDecoded.Contains($expected["wrong_reason"]))

    Write-Step "Running post-flow data consistency check"
    Invoke-ProjectCase `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "chinese post-flow data check"

    Write-Step "Chinese input flow passed"
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
