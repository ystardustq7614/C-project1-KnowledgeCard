param(
    [string]$Compiler = "g++",
    [switch]$KeepTemp
)

<#
脚本职责：
- 编译并运行日期工具测试和算法纯函数测试。

关键约束：
- 测试程序编译到 .test_tmp/algorithm_tests，不能与主程序或真实数据目录耦合。
- 算法测试只链接 algo_* 和 date_utils，不链接 main/storage，保证纯函数层没有隐藏全局依赖。
#>

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)
$tmpParent = Join-Path $projectRoot ".test_tmp"
$tmpRoot = Join-Path $tmpParent "algorithm_tests"
$dateTestExe = Join-Path $tmpRoot "test_date_utils.exe"
$algorithmTestExe = Join-Path $tmpRoot "test_algorithms.exe"

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

Push-Location $projectRoot
try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom

    Remove-SafeDirectory $tmpRoot
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null

    # 日期工具单独编译，确保其边界问题能独立于算法测试定位。
    Write-Step "Building date unit tests"
    & $Compiler `
        -std=c++17 `
        -I src `
        -o $dateTestExe `
        tests/test_date_utils.cpp `
        src/date_utils.cpp

    if ($LASTEXITCODE -ne 0) {
        throw "Date test build failed with exit code $LASTEXITCODE"
    }

    Write-Step "Running date unit tests"
    & $dateTestExe
    if ($LASTEXITCODE -ne 0) {
        throw "Date tests failed with exit code $LASTEXITCODE"
    }

    # 算法测试只链接纯函数实现，避免误引入文件读写或登录状态依赖。
    Write-Step "Building algorithm unit tests"
    & $Compiler `
        -std=c++17 `
        -I src `
        -o $algorithmTestExe `
        tests/test_algorithms.cpp `
        src/algo_sm2.cpp `
        src/algo_decay.cpp `
        src/algo_recommend.cpp `
        src/date_utils.cpp

    if ($LASTEXITCODE -ne 0) {
        throw "Algorithm test build failed with exit code $LASTEXITCODE"
    }

    Write-Step "Running algorithm unit tests"
    & $algorithmTestExe
    if ($LASTEXITCODE -ne 0) {
        throw "Algorithm tests failed with exit code $LASTEXITCODE"
    }

    Write-Step "All algorithm tests passed"
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
