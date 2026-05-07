param(
    [string]$ExePath = ".\project1.exe",
    [switch]$SkipBuild,
    [switch]$KeepTemp
)

<#
[导读]
- 本脚本从空数据目录跑通核心业务主链路：注册、登录、新增卡片、新增错题、错题转卡片、数据一致性检查。

[输入输出]
- 输入：脚本生成的交互式菜单文本。
- 输出：临时 data/*.txt 文件和 --check-data 退出码。

[易错点]
- 该脚本验证“完整业务闭环”，不是覆盖所有菜单分支；细分分支由 tests/e2e/*.ps1 覆盖。
- 输入文本必须包含每个 pauseScreen 所需空行，否则后续菜单选择会整体错位。
- 数据断言以持久化文件为准，确保交互结束后数据真正落盘。
#>

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)
$tmpParent = Join-Path $projectRoot ".test_tmp"
$tmpRoot = Join-Path $tmpParent "e2e_flow"
$caseDir = Join-Path $tmpRoot "case"
$dataDir = Join-Path $caseDir "data"
$inputFile = Join-Path $tmpRoot "e2e_input.txt"
$outputFile = Join-Path $tmpRoot "e2e_output.log"

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

function Invoke-ProjectCase {
    param(
        [string]$WorkingDirectory,
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label,
        [string]$InputText = $null,
        [string]$OutputPath = $null
    )

    Push-Location $WorkingDirectory
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

function Split-RecordLine {
    param([string]$Line)
    return $Line -split "\|", -1
}

function Decode-StorageField {
    param([string]$Value)

    # 与 storage.cpp 保持同一解码范围：只还原 %, |, CR, LF。
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

    # 每轮主链路测试从空文件启动，避免历史数据影响 cardId/wrongId 和断言定位。
    Remove-SafeDirectory $tmpRoot
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    foreach ($name in @("users.txt", "cards.txt", "wrongs.txt", "review_logs.txt")) {
        [System.IO.File]::WriteAllText((Join-Path $dataDir $name), "", $utf8NoBom)
    }

    $expectedCardFront = "What is a function?`nEach input | maps to one output."
    $expectedCardBack = "A relation where each input | has one output."
    $expectedWrongQuestion = "If y=2x and x=3, what is y? | explain"
    $expectedWrongCorrect = "6"
    $expectedWrongAnswer = "5 | arithmetic slip"
    $expectedWrongReason = "multiplied incorrectly`nmissed coefficient | 2"
    # 输入序列按菜单路径组织；空字符串表示“按回车继续”或“直接回车返回”。
    $inputLines = @(
        "2",
        "e2e_user",
        "e2e_pass",
        "e2e_pass",
        "",
        "1",
        "e2e_user",
        "e2e_pass",
        "",
        "1",
        "1",
        "Math",
        "Linear Function",
        "Function Definition",
        ".multi",
        "What is a function?",
        "Each input | maps to one output.",
        ".end",
        "A relation where each input | has one output.",
        "function",
        "2",
        "",
        "0",
        "2",
        "1",
        "Math",
        "Linear Function",
        "If y=2x and x=3, what is y? | explain",
        "6",
        "5 | arithmetic slip",
        ".multi",
        "multiplied incorrectly",
        "missed coefficient | 2",
        ".end",
        "5",
        "",
        "8",
        "1",
        "",
        "0",
        "1",
        "9",
        "",
        "",
        "0",
        "0",
        "",
        "0"
    )
    $inputText = [string]::Join("`n", $inputLines) + "`n"
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null
    [System.IO.File]::WriteAllText($inputFile, $inputText, $utf8NoBom)

    Write-Step "Running interactive business flow"
    Invoke-ProjectCase `
        -WorkingDirectory $projectRoot `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "interactive business flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    $usersFile = Join-Path $dataDir "users.txt"
    $cardsFile = Join-Path $dataDir "cards.txt"
    $wrongsFile = Join-Path $dataDir "wrongs.txt"

    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and $parts[1] -eq "e2e_user" } `
        -FailureMessage "registered user not found"
    $userId = $userParts[0]

    # 断言文本字段同时验证解码值和原始文件中的转义形态，覆盖多行与 | 的持久化契约。
    $manualCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[1] -eq $userId -and $parts[4] -eq "Function Definition" -and $parts[16] -eq "1" } `
        -FailureMessage "manual card not found"
    Assert-Equal -Label "manual card subject" -Actual $manualCard[2] -Expected "Math"
    Assert-Equal -Label "manual card front decodes multiline pipe" -Actual (Decode-StorageField $manualCard[5]) -Expected $expectedCardFront
    Assert-Equal -Label "manual card back decodes pipe" -Actual (Decode-StorageField $manualCard[6]) -Expected $expectedCardBack
    Assert-True -Label "manual card front stores escaped newline" -Condition ($manualCard[5].Contains("%0A"))
    Assert-True -Label "manual card front stores escaped pipe" -Condition ($manualCard[5].Contains("%7C"))

    $wrongParts = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[1] -eq $userId -and (Decode-StorageField $parts[4]) -eq $script:expectedWrongQuestion -and $parts[17] -eq "1" } `
        -FailureMessage "created wrong question not found"
    Assert-True -Label "wrong question error type was selected" -Condition (-not [string]::IsNullOrWhiteSpace($wrongParts[8]))
    Assert-Equal -Label "wrong question correct answer decodes" -Actual (Decode-StorageField $wrongParts[5]) -Expected $expectedWrongCorrect
    Assert-Equal -Label "wrong question wrong answer decodes pipe" -Actual (Decode-StorageField $wrongParts[6]) -Expected $expectedWrongAnswer
    Assert-Equal -Label "wrong question reason decodes multiline pipe" -Actual (Decode-StorageField $wrongParts[7]) -Expected $expectedWrongReason

    $linkedCardId = $wrongParts[9]
    Assert-True -Label "wrong question has linked card" -Condition ($linkedCardId -ne "-1")

    # 错题转卡片需要同时满足 wrongs.txt 的 linkedCardId 和 cards.txt 中真实可见卡片。
    $convertedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq $linkedCardId -and $parts[1] -eq $userId -and $parts[16] -eq "1" } `
        -FailureMessage "converted card referenced by linkedCardId not found"
    Assert-Equal -Label "converted card front matches wrong question" -Actual (Decode-StorageField $convertedCard[5]) -Expected $expectedWrongQuestion
    $convertedBackDecoded = Decode-StorageField $convertedCard[6]
    Assert-True -Label "converted card back starts with correct answer" -Condition ($convertedBackDecoded.StartsWith($expectedWrongCorrect))
    Assert-True -Label "converted card back carries reason" -Condition ($convertedBackDecoded.Contains($expectedWrongReason))
    Assert-True -Label "converted card tag is present" -Condition (-not [string]::IsNullOrWhiteSpace($convertedCard[7]))

    Write-Step "Running post-flow data consistency check"
    Invoke-ProjectCase `
        -WorkingDirectory $projectRoot `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "post-flow data check"

    Write-Step "E2E business flow passed"
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
