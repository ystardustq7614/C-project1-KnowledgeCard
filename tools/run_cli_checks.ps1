# param 是脚本入口参数声明；运行这个工具脚本时可以从命令行传这些参数。
param(
    # [可改] 被测主程序路径；默认使用项目根目录下的 project1.exe。
    [string]$ExePath = ".\project1.exe",

    # [可改] 开关参数：传入 -SkipBuild 时跳过编译，直接使用已有 exe。
    [switch]$SkipBuild,

    # [可改] 开关参数：传入 -KeepTemp 时保留 .test_tmp/cli_checks，方便查看修复后的数据。
    [switch]$KeepTemp
)

<#
[导读]
- 这个脚本验证 project1.exe 的命令行数据检查能力。
- 覆盖范围包括：
  1. `--check-data`
  2. `--check-data --fix`
  3. `--check-data --user-id`
  4. `PROJECT1_DATA_DIR` 环境变量
  5. 错误参数退出码
- 它不是交互式 E2E，不模拟菜单输入；它直接通过命令行参数驱动主程序。

[输入输出]
- 输入：tests/fixtures 下的固定样例数据。
- 输出：
  1. .test_tmp/cli_checks 下的隔离 case 目录。
  2. 主程序退出码断言。
  3. 自动修复后的字段断言，例如 linkedCardId 是否归正为 -1。

[你以后最常改的地方]
- `$fixtureRoot`：如果 fixture 目录移动，需要改这里。
- `Copy-FixtureData`：如果 data 文件新增了持久化文件，需要同步复制。
- 主流程中的 case 列表：如果 CLI 增加新参数或新数据修复规则，可以在这里加 case。
- `ExpectedExitCode`：如果 CLI 契约改了，必须同步更新预期退出码。

[不建议随便改的地方]
- 每个 fixture 必须复制到临时目录后再运行，尤其是 `--fix` 会改写数据文件。
- `Remove-SafeDirectory` 必须保留路径安全检查，避免清理临时目录时误删项目外文件。
- 环境变量测试必须恢复 `PROJECT1_DATA_DIR`，否则会污染同一 PowerShell 进程中的后续测试。

[易错点]
- 退出码是 CLI 契约的一部分，不能只看控制台输出文本。
- 自动修复测试不仅检查退出码，还要检查字段是否真的被修正。
- `--data-dir` 显式参数和 `PROJECT1_DATA_DIR` 环境变量是两条不同入口，都要覆盖。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；例如 Copy-Item 失败后直接中断。
$ErrorActionPreference = "Stop"

# $PSScriptRoot 是当前脚本所在目录：tools。
# Split-Path -Parent 取上一级目录，也就是项目根目录。
$projectRoot = Split-Path -Parent $PSScriptRoot

# 把项目根目录转换成绝对路径，后续做安全删除校验时使用。
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)

# 所有临时测试产物统一放在项目根目录下的 .test_tmp。
$tmpParent = Join-Path $projectRoot ".test_tmp"

# 本脚本专用临时目录；每个 CLI case 会在这里创建一个子目录。
$tmpRoot = Join-Path $projectRoot ".test_tmp\cli_checks"

# fixture 根目录；里面放 clean_data、broken_link_data、invalid_field_data 等样例数据。
$fixtureRoot = Join-Path $projectRoot "tests\fixtures"

# 定义一个函数：统一打印测试步骤。
function Write-Step {
    # 接收要打印的消息文本。
    param([string]$Message)

    # [可改] 如果项目版本升级或想换日志前缀，可以改 "[v1.4.0]"。
    Write-Host "[v1.4.0] $Message"
}

# 定义一个函数：确认某个路径仍在项目根目录内。
function Assert-PathInsideProject {
    # 传入要检查的路径。
    param([string]$Path)

    # 先转成绝对路径，避免 "../" 这类相对路径绕过检查。
    $fullPath = [System.IO.Path]::GetFullPath($Path)

    # StartsWith 判断目标路径是否以项目根目录开头；OrdinalIgnoreCase 表示忽略大小写。
    if (-not $fullPath.StartsWith($projectRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        # 路径不在项目根目录下就直接失败，避免删除用户其他目录。
        throw "Refusing to operate outside project root: $fullPath"
    }

    # 返回规范化后的绝对路径。
    return $fullPath
}

# 定义一个函数：安全删除目录。
function Remove-SafeDirectory {
    # 传入准备删除的目录路径。
    param([string]$Path)

    # 删除前先做项目根目录边界检查。
    $fullPath = Assert-PathInsideProject $Path

    # Test-Path 判断路径是否存在；-LiteralPath 按原样解释路径，不把 []、* 当通配符。
    if (Test-Path -LiteralPath $fullPath) {
        # [谨慎改] 这里是真正的递归删除，必须依赖上面的路径保护。
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}

# 定义一个函数：把某套 fixture 数据复制成一个独立 case 目录。
function Copy-FixtureData {
    # FixtureName 是 tests/fixtures 下的目录名；CaseName 是 .test_tmp/cli_checks 下的 case 目录名。
    param(
        [string]$FixtureName,
        [string]$CaseName
    )

    # fixture 只读，测试用例必须复制到临时目录后再运行 --fix。
    $fixtureDir = Join-Path $fixtureRoot $FixtureName

    # 如果 fixture 目录不存在，直接失败，说明测试材料缺失。
    if (-not (Test-Path -LiteralPath $fixtureDir)) {
        throw "Fixture not found: $fixtureDir"
    }

    # 每个 case 一个独立目录，避免不同测试互相污染。
    $caseDir = Join-Path $tmpRoot $CaseName

    # 主程序需要看到 data 目录，因此 fixture 文件复制到 caseDir/data 下。
    $dataDir = Join-Path $caseDir "data"

    # 创建 data 目录；-Force 表示已存在也不报错。
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

    # 复制用户数据。
    Copy-Item -LiteralPath (Join-Path $fixtureDir "users.txt") -Destination $dataDir -Force

    # 复制卡片数据。
    Copy-Item -LiteralPath (Join-Path $fixtureDir "cards.txt") -Destination $dataDir -Force

    # 复制错题数据。
    Copy-Item -LiteralPath (Join-Path $fixtureDir "wrongs.txt") -Destination $dataDir -Force

    # 复制复习日志数据。
    Copy-Item -LiteralPath (Join-Path $fixtureDir "review_logs.txt") -Destination $dataDir -Force

    # 返回 case 根目录，后续 Invoke-ProjectCase 会从这里拼 data 路径。
    return $caseDir
}

# 定义一个函数：通过 --data-dir 显式指定数据目录运行主程序。
function Invoke-ProjectCase {
    # 参数说明：
    # - CaseDir：Copy-FixtureData 返回的 case 目录。
    # - Arguments：传给 project1.exe 的 CLI 参数。
    # - ExpectedExitCode：期望退出码。
    # - Label：本 case 的可读名称。
    param(
        [string]$CaseDir,
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label
    )

    # 每个 case 的真实数据目录固定是 CaseDir/data。
    $dataDir = Join-Path $CaseDir "data"

    # 切到项目根目录，保证主程序相对路径稳定。
    Push-Location $projectRoot
    try {
        # --data-dir 追加在最后，确保测试数据目录显式覆盖默认 data/。
        & $script:ResolvedExe @Arguments --data-dir $dataDir

        # 保存主程序退出码。
        $actualExitCode = $LASTEXITCODE
    }
    finally {
        # 无论成功还是失败，都恢复原工作目录。
        Pop-Location
    }

    # 检查实际退出码是否符合 CLI 契约。
    if ($actualExitCode -ne $ExpectedExitCode) {
        throw "$Label failed: expected exit $ExpectedExitCode, got $actualExitCode"
    }

    # 退出码符合预期时打印通过信息。
    Write-Step "$Label passed (exit=$actualExitCode)"
}

# 定义一个函数：通过 PROJECT1_DATA_DIR 环境变量指定数据目录运行主程序。
function Invoke-ProjectCaseWithEnvDataDir {
    # 参数含义与 Invoke-ProjectCase 类似，只是数据目录来源换成环境变量。
    param(
        [string]$CaseDir,
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label
    )

    # 每个 case 的真实数据目录固定是 CaseDir/data。
    $dataDir = Join-Path $CaseDir "data"

    # 先保存旧环境变量，finally 里必须恢复，避免影响后续测试。
    $previousDataDir = $env:PROJECT1_DATA_DIR

    # 切到项目根目录，保证主程序相对路径稳定。
    Push-Location $projectRoot
    try {
        # 设置环境变量，让主程序从这里读取 data 目录。
        $env:PROJECT1_DATA_DIR = $dataDir

        # 运行主程序；这里故意不传 --data-dir，用来验证环境变量入口。
        & $script:ResolvedExe @Arguments

        # 保存主程序退出码。
        $actualExitCode = $LASTEXITCODE
    }
    finally {
        # 恢复环境变量，避免影响同一 PowerShell 进程中的后续测试。
        if ($null -eq $previousDataDir) {
            # 原本没有这个环境变量，就删除它。
            Remove-Item Env:PROJECT1_DATA_DIR -ErrorAction SilentlyContinue
        }
        else {
            # 原本有这个环境变量，就恢复旧值。
            $env:PROJECT1_DATA_DIR = $previousDataDir
        }

        # 恢复原工作目录。
        Pop-Location
    }

    # 检查实际退出码是否符合预期。
    if ($actualExitCode -ne $ExpectedExitCode) {
        throw "$Label failed: expected exit $ExpectedExitCode, got $actualExitCode"
    }

    # 退出码符合预期时打印通过信息。
    Write-Step "$Label passed (exit=$actualExitCode)"
}

# 定义一个函数：读取 wrongs.txt 第一条错题的 linkedCardId 字段。
function Get-WrongLinkedCardId {
    # CaseDir 是某个临时 case 目录。
    param([string]$CaseDir)

    # wrongs.txt 位于 caseDir/data/wrongs.txt。
    $wrongFile = Join-Path $CaseDir "data\wrongs.txt"

    # 读取第一行错题记录。
    $line = Get-Content -LiteralPath $wrongFile -Encoding UTF8 | Select-Object -First 1

    # 按 | 切字段；linkedCardId 是 wrongs.txt 第 9 个字段。
    $parts = $line -split "\|", -1

    # 返回 linkedCardId。
    return $parts[9]
}

# 主流程开始：切换到项目根目录。
Push-Location $projectRoot
try {
    # 创建 UTF-8 无 BOM 编码对象；用于统一控制台输入/输出编码。
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)

    # 设置控制台输入编码，保持测试入口一致。
    [Console]::InputEncoding = $utf8NoBom

    # 设置控制台输出编码，避免主程序输出中文时显示乱码。
    [Console]::OutputEncoding = $utf8NoBom

    # 设置 PowerShell 管道输出编码，保持测试入口一致。
    $OutputEncoding = $utf8NoBom

    # 如果没有传 -SkipBuild，就先编译主程序。
    if (-not $SkipBuild) {
        # 打印编译步骤。
        Write-Step "Building project"

        # 编译所有 src/*.cpp 到 ExePath。
        & g++ -std=c++17 -o $ExePath src/*.cpp

        # 编译失败时中止测试。
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE"
        }
    }

    # 把 exe 路径解析成绝对路径，后续运行时不受 Push-Location 影响。
    $script:ResolvedExe = (Resolve-Path $ExePath).Path

    # 清理旧的 CLI 检查临时目录，保证本轮从干净状态开始。
    Remove-SafeDirectory $tmpRoot

    # 创建本轮 CLI 检查临时目录。
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null

    # clean_data：正常数据，--check-data 应通过。
    $cleanCase = Copy-FixtureData -FixtureName "clean_data" -CaseName "clean"

    # 验证正常数据全量检查，期望退出码 0。
    Invoke-ProjectCase -CaseDir $cleanCase -Arguments @("--check-data") -ExpectedExitCode 0 -Label "clean data check"

    # 验证正常数据按用户范围检查，userId=1 存在，期望退出码 0。
    Invoke-ProjectCase -CaseDir $cleanCase -Arguments @("--check-data", "--user-id", "1") -ExpectedExitCode 0 -Label "clean data user scoped check"

    # 验证 PROJECT1_DATA_DIR 环境变量入口，期望退出码 0。
    Invoke-ProjectCaseWithEnvDataDir -CaseDir $cleanCase -Arguments @("--check-data") -ExpectedExitCode 0 -Label "environment data dir check"

    # 验证错误参数：单独传 --fix 没有 --check-data，应返回参数错误退出码 2。
    Invoke-ProjectCase -CaseDir $cleanCase -Arguments @("--fix") -ExpectedExitCode 2 -Label "invalid parameter check"

    # 验证缺失用户参数：userId=999 不存在，应返回参数错误退出码 2。
    Invoke-ProjectCase -CaseDir $cleanCase -Arguments @("--check-data", "--user-id", "999") -ExpectedExitCode 2 -Label "missing user parameter check"

    # broken_link_data：错题 linkedCardId 指向不存在/不可见卡片，--check-data 应检测失败。
    $brokenCase = Copy-FixtureData -FixtureName "broken_link_data" -CaseName "broken_link"

    # 验证坏关联能被检测出来，期望退出码 1。
    Invoke-ProjectCase -CaseDir $brokenCase -Arguments @("--check-data") -ExpectedExitCode 1 -Label "broken link detection"

    # 复制同一套坏关联 fixture 到另一个 case，用来测试 --fix。
    $brokenFixCase = Copy-FixtureData -FixtureName "broken_link_data" -CaseName "broken_link_fix"

    # 验证 --check-data --fix 能自动修复坏关联，期望退出码 0。
    Invoke-ProjectCase -CaseDir $brokenFixCase -Arguments @("--check-data", "--fix") -ExpectedExitCode 0 -Label "broken link auto fix"

    # 修复后再次检查，应通过。
    Invoke-ProjectCase -CaseDir $brokenFixCase -Arguments @("--check-data") -ExpectedExitCode 0 -Label "broken link post-fix check"

    # 读取修复后的 linkedCardId，确认不只是退出码正确，字段也真的被修复。
    $linkedCardId = Get-WrongLinkedCardId $brokenFixCase

    # 坏关联修复策略是把 linkedCardId 重置为 -1。
    if ($linkedCardId -ne "-1") {
        throw "broken link auto fix failed: expected linkedCardId -1, got $linkedCardId"
    }

    # 字段验证通过。
    Write-Step "broken link field verification passed"

    # invalid_field_data：字段范围/日期非法，--check-data 应检测失败。
    $invalidCase = Copy-FixtureData -FixtureName "invalid_field_data" -CaseName "invalid_field"

    # 验证非法字段能被检测出来，期望退出码 1。
    Invoke-ProjectCase -CaseDir $invalidCase -Arguments @("--check-data") -ExpectedExitCode 1 -Label "invalid field detection"

    # 复制同一套非法字段 fixture 到另一个 case，用来测试 --fix。
    $invalidFixCase = Copy-FixtureData -FixtureName "invalid_field_data" -CaseName "invalid_field_fix"

    # 验证 --check-data --fix 能自动修复可修复字段，期望退出码 0。
    Invoke-ProjectCase -CaseDir $invalidFixCase -Arguments @("--check-data", "--fix") -ExpectedExitCode 0 -Label "invalid field auto fix"

    # 修复后再次检查，应通过。
    Invoke-ProjectCase -CaseDir $invalidFixCase -Arguments @("--check-data") -ExpectedExitCode 0 -Label "invalid field post-fix check"

    # 所有 CLI 检查通过。
    Write-Step "All CLI checks passed"
}
finally {
    # 无论成功还是失败，都恢复调用脚本前的工作目录。
    Pop-Location

    # 默认清理临时目录；传入 -KeepTemp 时保留现场用于排查。
    if (-not $KeepTemp) {
        # 删除 .test_tmp/cli_checks。
        Remove-SafeDirectory $tmpRoot

        # 如果 .test_tmp 已经空了，也顺手删除父目录。
        $tmpParentFull = Assert-PathInsideProject $tmpParent
        if ((Test-Path -LiteralPath $tmpParentFull) -and -not (Get-ChildItem -LiteralPath $tmpParentFull -Force)) {
            Remove-Item -LiteralPath $tmpParentFull -Force
        }
    }
}
