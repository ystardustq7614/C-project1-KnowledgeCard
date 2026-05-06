$script:E2EProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:E2EProjectRootFull = [System.IO.Path]::GetFullPath($script:E2EProjectRoot)
$script:E2ETmpParent = Join-Path $script:E2EProjectRoot ".test_tmp"

<#
模块职责：
- 为 tests/e2e 下的分支回归脚本提供统一路径保护、临时数据初始化、程序调用和数据断言 helper。

关键约束：
- 所有删除操作必须先确认目标仍在项目根目录内，避免测试清理误删用户目录。
- helper 返回的是文本文件字段数组，调用方需要按存储格式字段顺序断言。
- 解码逻辑必须与 storage.cpp 的百分号转义规则保持一致。
#>

function Write-E2EStep {
    param([string]$Message)
    Write-Host "[v1.3.7] $Message"
}

function Assert-PathInsideProject {
    param([string]$Path)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($script:E2EProjectRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
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

function Initialize-EmptyDataDir {
    param(
        [string]$DataDir,
        [System.Text.Encoding]$Encoding
    )

    # E2E 以空数据目录启动，确保 case 不依赖真实 data/ 或其他 case 的遗留数据。
    New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
    foreach ($name in @("users.txt", "cards.txt", "wrongs.txt", "review_logs.txt")) {
        [System.IO.File]::WriteAllText((Join-Path $DataDir $name), "", $Encoding)
    }
}

function Resolve-E2EExecutable {
    param(
        [string]$ExePath,
        [switch]$SkipBuild
    )

    Push-Location $script:E2EProjectRoot
    try {
        if (-not $SkipBuild) {
            Write-E2EStep "Building project"
            & g++ -std=c++17 -o $ExePath src/*.cpp
            if ($LASTEXITCODE -ne 0) {
                throw "Build failed with exit code $LASTEXITCODE"
            }
        }
        # 后续 Invoke-E2EProgram 都使用绝对 exe 路径，避免 Push-Location 改变解析结果。
        $script:ResolvedExe = (Resolve-Path $ExePath).Path
    }
    finally {
        Pop-Location
    }
}

function Invoke-E2EProgram {
    param(
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label,
        [string]$InputText = $null,
        [string]$OutputPath = $null
    )

    Push-Location $script:E2EProjectRoot
    try {
        # 输入通过管道送入原生 exe，菜单中的暂停空行必须由调用方显式提供。
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
    Write-E2EStep "$Label passed (exit=$actualExitCode)"
}

function Split-RecordLine {
    param([string]$Line)
    # 存储层已把字段内的 | 转为 %7C，因此测试可按原始分隔符切字段。
    return $Line -split "\|", -1
}

function ConvertFrom-StorageField {
    param([string]$Value)

    # 只解码项目定义的四种转义，避免把普通 %XX 文本误判为存储编码。
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
    # 兼容旧测试脚本命名；新脚本可继续使用 Decode-StorageField 表达测试意图。
    return ConvertFrom-StorageField $Value
}

function Get-RecordLines {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing data file: $Path"
    }
    return @(Get-Content -LiteralPath $Path -Encoding UTF8 | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Get-RequiredLine {
    param(
        [string]$Path,
        [scriptblock]$Predicate,
        [string]$FailureMessage
    )

    # 找不到目标记录直接失败，避免后续断言在空数组上给出误导性错误。
    foreach ($line in (Get-RecordLines $Path)) {
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
    Write-E2EStep "$Label passed"
}

function Assert-True {
    param(
        [string]$Label,
        [bool]$Condition
    )
    if (-not $Condition) {
        throw "$Label failed"
    }
    Write-E2EStep "$Label passed"
}
