# param 是脚本入口参数声明；运行这个总测试脚本时可以从命令行传这些参数。
param(
    # [可改] 主程序 exe 输出路径；默认在项目根目录生成 project1.exe。
    [string]$ExePath = ".\project1.exe",

    # [可改] C++ 编译器命令；默认使用 g++。
    [string]$Compiler = "g++",

    # [可改] 开关参数：传入 -KeepTemp 时保留 .test_tmp，方便排查失败现场。
    [switch]$KeepTemp
)

<#
[导读]
- 这个脚本是项目总验收入口，相当于“一键跑完整测试套件”。
- 它按顺序串联四个阶段：
  1. 编译主程序 project1.exe
  2. 运行 CLI 数据一致性检查
  3. 运行算法/日期 C++ 单元测试
  4. 运行 E2E 回归测试套件
- 任一阶段失败都会抛错并最终返回退出码 1；全部通过则返回 0。

[输入输出]
- 输入：
  1. src/*.cpp 源码
  2. tests/fixtures 测试数据
  3. tests/*.cpp 单元测试源码
  4. tests/*.ps1 和 tests/e2e/*.ps1 交互测试脚本
- 输出：
  1. project1.exe
  2. .test_tmp 下的各类临时测试目录
  3. 子测试日志和最终退出码

[你以后最常改的地方]
- `$Compiler` / `$ExePath`：如果编译器或 exe 名称变化，可以改默认值。
- `Invoke-TestStep` 阶段列表：如果新增一类测试入口，可以在主流程中加一个阶段。
- 子脚本路径：如果 tools/run_cli_checks.ps1 等文件移动，需要同步改 Join-Path。

[不建议随便改的地方]
- 阶段顺序：先构建主程序，再把同一个 exe 传给 CLI/E2E，能避免重复编译造成定位噪声。
- `SkipBuild = $true`：子脚本复用已构建 exe，避免同一轮总测试中多次编译主程序。
- `.test_tmp` 清理逻辑：必须保留路径安全检查，避免误删项目外文件。

[易错点]
- 这个脚本是总入口，不直接写具体断言；具体断言在子脚本或 C++ 测试文件里。
- 子测试必须使用独立临时目录或 `--data-dir`，不能读写真实 data/。
- PowerShell 默认编码会影响中文 E2E，所以总入口也统一设置 UTF-8。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；任一阶段失败都应中断总测试。
$ErrorActionPreference = "Stop"

# $PSScriptRoot 是当前脚本所在目录：tools。
# Split-Path -Parent 取上一级目录，也就是项目根目录。
$projectRoot = Split-Path -Parent $PSScriptRoot

# 把项目根目录转换成绝对路径，后续做安全删除校验时使用。
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)

# 所有测试临时目录统一放在项目根目录下的 .test_tmp。
$tmpParent = Join-Path $projectRoot ".test_tmp"

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

# 定义一个函数：统一执行一个测试阶段。
function Invoke-TestStep {
    # Name 是阶段名称；Action 是要执行的脚本块。
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    # 打印阶段开始，方便定位失败卡在哪一步。
    Write-Step "START: $Name"

    # 子步骤自行输出细节；这里统一负责失败冒泡和阶段性 PASS 标记。
    & $Action

    # 如果 Action 没有抛错，说明阶段通过。
    Write-Step "PASS: $Name"
}

# 切换到项目根目录，确保 src/*.cpp、tools/...、tests/... 相对路径都能正确解析。
Push-Location $projectRoot
try {
    # 创建 UTF-8 无 BOM 编码对象；用于统一控制台输入/输出编码。
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)

    # PowerShell 管道默认编码会影响中文 E2E，入口处统一 UTF-8。
    [Console]::InputEncoding = $utf8NoBom

    # 设置控制台输出编码，避免中文测试输出乱码。
    [Console]::OutputEncoding = $utf8NoBom

    # 设置 PowerShell 管道输出编码，保持子测试中文输入链路稳定。
    $OutputEncoding = $utf8NoBom

    # 默认先清理整个 .test_tmp，保证总测试从干净临时目录开始。
    # 传入 -KeepTemp 时跳过清理，保留失败现场。
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpParent
    }

    # 阶段 1：编译主程序。
    Invoke-TestStep -Name "build main program" -Action {
        # 编译所有 src/*.cpp 到 ExePath。
        & $Compiler -std=c++17 -o $ExePath src/*.cpp

        # 编译失败时抛错，终止后续测试。
        if ($LASTEXITCODE -ne 0) {
            throw "Main program build failed with exit code $LASTEXITCODE"
        }
    }

    # 把刚编译出的 exe 解析成绝对路径，传给后续子测试复用。
    $resolvedExe = (Resolve-Path $ExePath).Path

    # 阶段 2：运行 CLI 数据一致性检查。
    Invoke-TestStep -Name "CLI data consistency checks" -Action {
        # 用哈希表收集子脚本参数；后面用 @stepParams 展开。
        $stepParams = @{
            ExePath = $resolvedExe
            SkipBuild = $true
        }

        # 如果总入口要求保留临时目录，也把 KeepTemp 传给子脚本。
        if ($KeepTemp) {
            $stepParams.KeepTemp = $true
        }

        # 调用 CLI 检查脚本；它会复制 fixture 到临时目录并验证 --check-data/--fix 等行为。
        & (Join-Path $projectRoot "tools\run_cli_checks.ps1") @stepParams

        # 子脚本返回非 0 时，当前阶段失败。
        if ($LASTEXITCODE -ne 0) {
            throw "CLI checks failed with exit code $LASTEXITCODE"
        }
    }

    # 阶段 3：运行算法和日期单元测试。
    Invoke-TestStep -Name "algorithm unit tests" -Action {
        # 算法测试不复用主程序 exe，而是自己编译 C++ 单元测试 exe。
        $stepParams = @{
            Compiler = $Compiler
        }

        # 透传 KeepTemp，允许保留算法测试临时 exe。
        if ($KeepTemp) {
            $stepParams.KeepTemp = $true
        }

        # 调用算法测试调度脚本。
        & (Join-Path $projectRoot "tools\run_algorithm_tests.ps1") @stepParams

        # 子脚本返回非 0 时，当前阶段失败。
        if ($LASTEXITCODE -ne 0) {
            throw "Algorithm tests failed with exit code $LASTEXITCODE"
        }
    }

    # 阶段 4：运行 E2E 回归套件。
    Invoke-TestStep -Name "E2E regression suite" -Action {
        # E2E 复用阶段 1 已构建的主程序 exe，避免重复编译。
        $stepParams = @{
            ExePath = $resolvedExe
            Compiler = $Compiler
            SkipBuild = $true
        }

        # 透传 KeepTemp，允许保留各 E2E case 的临时 data 和输出日志。
        if ($KeepTemp) {
            $stepParams.KeepTemp = $true
        }

        # 调用 E2E 汇总脚本；它会进一步执行各个具体 E2E case。
        & (Join-Path $projectRoot "tools\run_e2e_tests.ps1") @stepParams

        # 子脚本返回非 0 时，当前阶段失败。
        if ($LASTEXITCODE -ne 0) {
            throw "E2E regression suite failed with exit code $LASTEXITCODE"
        }
    }

    # 所有阶段都通过后，如果没有要求保留临时文件，就清理 .test_tmp。
    if (-not $KeepTemp -and (Test-Path -LiteralPath $tmpParent)) {
        Remove-SafeDirectory $tmpParent
    }

    # 打印总通过信息。
    Write-Step "All tests passed"

    # 总测试成功，退出码 0。
    exit 0
}
catch {
    # 任一阶段抛错都会进入这里；Write-Error 输出错误对象。
    Write-Error $_

    # 总测试失败，退出码 1。
    exit 1
}
finally {
    # 无论成功还是失败，都恢复调用脚本前的工作目录。
    Pop-Location
}
