param(
    [string]$ExePath = ".\project1.exe",
    [string]$Compiler = "g++",
    [switch]$KeepTemp
)

<#
[导读]
- 本脚本是项目总验收入口，串联主程序编译、CLI 数据检查、算法单元测试和 E2E 回归。

[输入输出]
- 输入：源码、测试 fixture 和可选 ExePath/Compiler/KeepTemp 参数。
- 输出：project1.exe、测试阶段输出和最终退出码。

[易错点]
- 默认清理 .test_tmp，保证每次测试从干净临时数据开始；传入 -KeepTemp 时保留现场用于排查。
- 所有子测试必须使用独立 --data-dir 或临时目录，不能读写真实 data/。
- 任一步失败都抛出异常并返回非 0，便于 CI 或人工脚本判断。
#>

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)
$tmpParent = Join-Path $projectRoot ".test_tmp"

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

function Invoke-TestStep {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Step "START: $Name"
    # 子步骤自行输出细节；这里统一负责失败冒泡和阶段性 PASS 标记。
    & $Action
    Write-Step "PASS: $Name"
}

Push-Location $projectRoot
try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    # PowerShell 管道默认编码会影响中文 E2E，入口处统一 UTF-8。
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom

    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpParent
    }

    Invoke-TestStep -Name "build main program" -Action {
        & $Compiler -std=c++17 -o $ExePath src/*.cpp
        if ($LASTEXITCODE -ne 0) {
            throw "Main program build failed with exit code $LASTEXITCODE"
        }
    }

    $resolvedExe = (Resolve-Path $ExePath).Path

    Invoke-TestStep -Name "CLI data consistency checks" -Action {
        $stepParams = @{
            ExePath = $resolvedExe
            SkipBuild = $true
        }
        if ($KeepTemp) {
            $stepParams.KeepTemp = $true
        }
        & (Join-Path $projectRoot "tools\run_cli_checks.ps1") @stepParams
        if ($LASTEXITCODE -ne 0) {
            throw "CLI checks failed with exit code $LASTEXITCODE"
        }
    }

    Invoke-TestStep -Name "algorithm unit tests" -Action {
        $stepParams = @{
            Compiler = $Compiler
        }
        if ($KeepTemp) {
            $stepParams.KeepTemp = $true
        }
        & (Join-Path $projectRoot "tools\run_algorithm_tests.ps1") @stepParams
        if ($LASTEXITCODE -ne 0) {
            throw "Algorithm tests failed with exit code $LASTEXITCODE"
        }
    }

    Invoke-TestStep -Name "E2E regression suite" -Action {
        $stepParams = @{
            ExePath = $resolvedExe
            Compiler = $Compiler
            SkipBuild = $true
        }
        if ($KeepTemp) {
            $stepParams.KeepTemp = $true
        }
        & (Join-Path $projectRoot "tools\run_e2e_tests.ps1") @stepParams
        if ($LASTEXITCODE -ne 0) {
            throw "E2E regression suite failed with exit code $LASTEXITCODE"
        }
    }

    if (-not $KeepTemp -and (Test-Path -LiteralPath $tmpParent)) {
        Remove-SafeDirectory $tmpParent
    }

    Write-Step "All tests passed"
    exit 0
}
catch {
    Write-Error $_
    exit 1
}
finally {
    Pop-Location
}
