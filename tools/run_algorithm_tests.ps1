# param 是脚本入口参数声明；运行这个工具脚本时可以从命令行传这些参数。
param(
    # [可改] C++ 编译器命令；默认使用 g++。
    [string]$Compiler = "g++",

    # [可改] 开关参数：传入 -KeepTemp 时保留 .test_tmp/algorithm_tests，方便查看临时 exe。
    [switch]$KeepTemp
)

<#
[导读]
- 这个脚本是“算法/日期 C++ 单元测试”的 PowerShell 调度入口。
- 它本身不写算法断言，而是负责编译并运行两个 C++ 测试程序：
  1. tests/test_date_utils.cpp
  2. tests/test_algorithms.cpp
- 这类测试用 C++ 写，是因为它们要直接调用 C++ 纯函数，而不是模拟控制台菜单。

[输入输出]
- 输入：
  1. tests/test_date_utils.cpp
  2. tests/test_algorithms.cpp
  3. src/date_utils.cpp
  4. src/algo_sm2.cpp / src/algo_decay.cpp / src/algo_recommend.cpp
- 输出：
  1. .test_tmp/algorithm_tests/test_date_utils.exe
  2. .test_tmp/algorithm_tests/test_algorithms.exe
  3. 两个测试程序的退出码。

[你以后最常改的地方]
- `$Compiler`：如果换成 clang++ 或指定完整编译器路径，可以改默认值或运行时传参。
- 编译命令里的源码列表：如果新增算法模块或测试文件，需要在对应 g++ 命令里补文件。
- `-std=c++17`：如果项目 C++ 标准升级，需要和主程序编译参数同步。

[不建议随便改的地方]
- 不要把 main.cpp、storage.cpp、user.cpp 链进算法测试：否则纯函数测试会混入全局状态和文件读写依赖。
- `Remove-SafeDirectory` 必须保留路径安全检查，避免清理临时目录时误删项目外文件。
- 两个测试 exe 放在 .test_tmp/algorithm_tests 下，不应放到项目根目录污染源码目录。

[易错点]
- 日期工具测试单独编译，便于日期边界失败时快速定位。
- 算法测试只链接 algo_* 和 date_utils，验证的是算法层纯函数契约。
- PowerShell 在这里负责“编译和运行”，真正的断言在 C++ 测试文件中。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；例如编译命令失败后直接进入异常流程。
$ErrorActionPreference = "Stop"

# $PSScriptRoot 是当前脚本所在目录：tools。
# Split-Path -Parent 取上一级目录，也就是项目根目录。
$projectRoot = Split-Path -Parent $PSScriptRoot

# 把项目根目录转换成绝对路径，后续做安全删除校验时使用。
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)

# 所有临时测试产物统一放在项目根目录下的 .test_tmp。
$tmpParent = Join-Path $projectRoot ".test_tmp"

# 本脚本专用临时目录，用于存放编译出来的两个测试 exe。
$tmpRoot = Join-Path $tmpParent "algorithm_tests"

# 日期工具测试编译后的 exe 路径。
$dateTestExe = Join-Path $tmpRoot "test_date_utils.exe"

# 算法测试编译后的 exe 路径。
$algorithmTestExe = Join-Path $tmpRoot "test_algorithms.exe"

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

# 切换到项目根目录，确保 tests/... 和 src/... 这些相对路径都能正确解析。
Push-Location $projectRoot
try {
    # 创建 UTF-8 无 BOM 编码对象；主要用于控制台输入/输出编码设置。
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)

    # 设置控制台输入编码，保持和其他测试入口一致。
    [Console]::InputEncoding = $utf8NoBom

    # 设置控制台输出编码，避免测试程序输出中文时显示乱码。
    [Console]::OutputEncoding = $utf8NoBom

    # 设置 PowerShell 管道输出编码，保持测试入口行为一致。
    $OutputEncoding = $utf8NoBom

    # 每次运行前清理旧的 algorithm_tests 临时目录，避免旧 exe 干扰新测试。
    Remove-SafeDirectory $tmpRoot

    # 创建干净的临时目录；Out-Null 丢弃 New-Item 的对象输出，让日志更干净。
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null

    # 日期工具单独编译，确保其边界问题能独立于算法测试定位。
    Write-Step "Building date unit tests"

    # & 是 PowerShell 调用运算符；这里调用 g++/clang++ 等外部编译器。
    # 反引号 ` 表示命令续行，方便把编译参数分多行写清楚。
    & $Compiler `
        -std=c++17 `
        -I src `
        -o $dateTestExe `
        tests/test_date_utils.cpp `
        src/date_utils.cpp

    # $LASTEXITCODE 是上一条原生命令的退出码；非 0 表示编译失败。
    if ($LASTEXITCODE -ne 0) {
        throw "Date test build failed with exit code $LASTEXITCODE"
    }

    # 运行日期工具测试 exe。
    Write-Step "Running date unit tests"
    & $dateTestExe

    # 日期测试返回非 0 时，让整个脚本失败。
    if ($LASTEXITCODE -ne 0) {
        throw "Date tests failed with exit code $LASTEXITCODE"
    }

    # 算法测试只链接纯函数实现，避免误引入文件读写或登录状态依赖。
    Write-Step "Building algorithm unit tests"

    # 这里链接：
    # - tests/test_algorithms.cpp：测试 main 和断言
    # - src/algo_sm2.cpp：复习间隔算法
    # - src/algo_decay.cpp：记忆衰减算法
    # - src/algo_recommend.cpp：薄弱章节推荐算法
    # - src/date_utils.cpp：衰减算法依赖日期差计算
    & $Compiler `
        -std=c++17 `
        -I src `
        -o $algorithmTestExe `
        tests/test_algorithms.cpp `
        src/algo_sm2.cpp `
        src/algo_decay.cpp `
        src/algo_recommend.cpp `
        src/date_utils.cpp

    # 检查算法测试编译是否成功。
    if ($LASTEXITCODE -ne 0) {
        throw "Algorithm test build failed with exit code $LASTEXITCODE"
    }

    # 运行算法测试 exe。
    Write-Step "Running algorithm unit tests"
    & $algorithmTestExe

    # 算法测试返回非 0 时，让整个脚本失败。
    if ($LASTEXITCODE -ne 0) {
        throw "Algorithm tests failed with exit code $LASTEXITCODE"
    }

    # 两组测试都通过后，打印总通过信息。
    Write-Step "All algorithm tests passed"
}
finally {
    # 无论成功还是失败，都恢复调用脚本前的工作目录。
    Pop-Location

    # 默认清理临时测试 exe；传入 -KeepTemp 时保留，方便手动复跑或排查。
    if (-not $KeepTemp) {
        # 删除 .test_tmp/algorithm_tests。
        Remove-SafeDirectory $tmpRoot

        # 如果 .test_tmp 已经空了，也顺手删除父目录。
        $tmpParentFull = Assert-PathInsideProject $tmpParent
        if ((Test-Path -LiteralPath $tmpParentFull) -and -not (Get-ChildItem -LiteralPath $tmpParentFull -Force)) {
            Remove-Item -LiteralPath $tmpParentFull -Force
        }
    }
}
