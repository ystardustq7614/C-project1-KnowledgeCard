param(
    [string]$ExePath = ".\project1.exe",
    [string]$Compiler = "g++",
    [switch]$SkipBuild,
    [switch]$KeepTemp
)

<#
脚本职责：
- 汇总所有交互式 E2E case，覆盖核心主链路、中文输入和主要菜单分支。

关键约束：
- 默认只构建一次主程序，然后把已解析 exe 路径传给各 case，避免重复编译造成定位噪声。
- 每个 case 必须使用自己的临时 data 目录；case 之间不能共享运行状态。
- -KeepTemp 用于失败排查，正常运行结束后应清理 .test_tmp。
#>

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)
$tmpParent = Join-Path $projectRoot ".test_tmp"

function Write-Step {
    param([string]$Message)
    Write-Host "[v1.3.8] $Message"
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

function Invoke-E2ECase {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string]$ResolvedExe
    )

    Write-Step "START: $Name"
    # 子 case 接收同一个已构建 exe，但必须自行初始化独立数据目录。
    $caseParams = @{
        ExePath = $ResolvedExe
        SkipBuild = $true
    }
    if ($KeepTemp) {
        $caseParams.KeepTemp = $true
    }
    & $ScriptPath @caseParams
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
    Write-Step "PASS: $Name"
}

Push-Location $projectRoot
try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom

    if (-not $SkipBuild) {
        Write-Step "START: build main program"
        & $Compiler -std=c++17 -o $ExePath src/*.cpp
        if ($LASTEXITCODE -ne 0) {
            throw "Main program build failed with exit code $LASTEXITCODE"
        }
        Write-Step "PASS: build main program"
    }

    $resolvedExe = (Resolve-Path $ExePath).Path

    Invoke-E2ECase -Name "core business flow" -ScriptPath (Join-Path $projectRoot "tests\test_e2e_flow.ps1") -ResolvedExe $resolvedExe
    Invoke-E2ECase -Name "Chinese input flow" -ScriptPath (Join-Path $projectRoot "tests\test_chinese_input_flow.ps1") -ResolvedExe $resolvedExe
    Invoke-E2ECase -Name "user branch flow" -ScriptPath (Join-Path $projectRoot "tests\e2e\test_user_flow.ps1") -ResolvedExe $resolvedExe
    Invoke-E2ECase -Name "card branch flow" -ScriptPath (Join-Path $projectRoot "tests\e2e\test_card_flow.ps1") -ResolvedExe $resolvedExe
    Invoke-E2ECase -Name "wrong branch flow" -ScriptPath (Join-Path $projectRoot "tests\e2e\test_wrong_flow.ps1") -ResolvedExe $resolvedExe
    Invoke-E2ECase -Name "review practice stats maintenance branch flow" -ScriptPath (Join-Path $projectRoot "tests\e2e\test_review_practice_stats_maintenance_flow.ps1") -ResolvedExe $resolvedExe

    if (-not $KeepTemp -and (Test-Path -LiteralPath $tmpParent)) {
        $tmpParentFull = Assert-PathInsideProject $tmpParent
        if (-not (Get-ChildItem -LiteralPath $tmpParentFull -Force)) {
            Remove-Item -LiteralPath $tmpParentFull -Force
        }
    }

    Write-Step "All E2E tests passed"
    exit 0
}
catch {
    Write-Error $_
    exit 1
}
finally {
    Pop-Location
}
