# param 是脚本入口参数声明；运行这个 E2E 汇总脚本时可以从命令行传这些参数。
param(
    # [可改] 主程序 exe 输出路径或已有 exe 路径；默认使用项目根目录下的 project1.exe。
    [string]$ExePath = ".\project1.exe",

    # [可改] C++ 编译器命令；默认使用 g++。
    [string]$Compiler = "g++",

    # [可改] 开关参数：传入 -SkipBuild 时跳过编译，直接使用已有 exe。
    [switch]$SkipBuild,

    # [可改] 开关参数：传入 -KeepTemp 时保留各 E2E case 的临时 data 和输出日志。
    [switch]$KeepTemp
)

<#
[导读]
- 这个脚本是“E2E 回归测试套件”的汇总入口。
- 它负责构建或复用 project1.exe，然后依次运行所有交互式 E2E case。
- 它本身不写具体业务断言；断言分散在 tests/test_e2e_flow.ps1、tests/test_chinese_input_flow.ps1 和 tests/e2e/*.ps1 中。

[当前执行的 E2E case]
1. core business flow：核心业务闭环。
2. Chinese input flow：中文输入和中文落盘。
3. user branch flow：用户注册、登录、改密。
4. card branch flow：卡片增删改查。
5. wrong branch flow：错题增删改查和错题转卡。
6. review practice stats maintenance branch flow：复习、练习、统计、维护组合流程。

[输入输出]
- 输入：
  1. 已构建或待构建的 project1.exe。
  2. 各 E2E 测试脚本。
  3. tests/fixtures/e2e_inputs 下的中文输入 fixture。
- 输出：
  1. 每个 case 自己创建的 .test_tmp 子目录。
  2. 每个 case 的程序输出日志和断言结果。
  3. 本脚本最终退出码：全部通过为 0，任一失败为 1。

[你以后最常改的地方]
- `Invoke-E2ECase` 调用列表：新增 E2E case 时，在主流程里加一行。
- `$Compiler` / `$ExePath`：如果编译器或 exe 名称变化，可以改默认值。
- 子脚本路径：如果 tests 目录结构调整，需要同步改 Join-Path。

[不建议随便改的地方]
- 每个 case 都必须接收同一个已解析 exe，并自行初始化独立 data 目录。
- `SkipBuild = $true` 传给子 case，避免每个 case 都重复编译主程序。
- `.test_tmp` 只在空目录时删除，避免误删其他失败现场。

[易错点]
- 这个脚本是汇总入口，不代表所有测试；算法单元测试和 CLI 检查由其他 tools 脚本负责。
- 子 case 的输入里包含大量 pauseScreen 空行，失败时应优先查看对应 case 的输出日志。
- 传入 -KeepTemp 后，临时目录不会清理，适合定位哪个 case 数据落盘异常。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；任一 case 失败都应中断 E2E 汇总。
$ErrorActionPreference = "Stop"

# $PSScriptRoot 是当前脚本所在目录：tools。
# Split-Path -Parent 取上一级目录，也就是项目根目录。
$projectRoot = Split-Path -Parent $PSScriptRoot

# 把项目根目录转换成绝对路径，后续做安全删除校验时使用。
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)

# 所有 E2E 临时目录统一放在项目根目录下的 .test_tmp。
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

# 定义一个函数：运行一个具体 E2E case。
function Invoke-E2ECase {
    # Name 是 case 名称；ScriptPath 是 case 脚本路径；ResolvedExe 是已解析的主程序 exe 绝对路径。
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string]$ResolvedExe
    )

    # 打印 case 开始信息，方便定位失败卡在哪个流程。
    Write-Step "START: $Name"

    # 子 case 接收同一个已构建 exe，但必须自行初始化独立数据目录。
    # @{} 是哈希表；后面用 @caseParams 展开成命名参数。
    $caseParams = @{
        ExePath = $ResolvedExe
        SkipBuild = $true
    }

    # 如果本汇总脚本要求保留临时目录，也把 KeepTemp 透传给子 case。
    if ($KeepTemp) {
        $caseParams.KeepTemp = $true
    }

    # 调用具体 E2E case 脚本。
    & $ScriptPath @caseParams

    # 子 case 返回非 0 时，当前 case 失败并中断汇总流程。
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }

    # 子 case 通过。
    Write-Step "PASS: $Name"
}

# 切换到项目根目录，确保 src/*.cpp、tests/... 相对路径都能正确解析。
Push-Location $projectRoot
try {
    # 创建 UTF-8 无 BOM 编码对象；用于统一控制台输入/输出编码。
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)

    # 设置控制台输入编码，避免中文 E2E 输入通过管道时乱码。
    [Console]::InputEncoding = $utf8NoBom

    # 设置控制台输出编码，避免中文测试输出乱码。
    [Console]::OutputEncoding = $utf8NoBom

    # 设置 PowerShell 管道输出编码，保持中文输入链路稳定。
    $OutputEncoding = $utf8NoBom

    # 如果没有传 -SkipBuild，就先编译主程序。
    if (-not $SkipBuild) {
        # 打印编译开始。
        Write-Step "START: build main program"

        # 编译所有 src/*.cpp 到 ExePath。
        & $Compiler -std=c++17 -o $ExePath src/*.cpp

        # 编译失败时中断 E2E 汇总。
        if ($LASTEXITCODE -ne 0) {
            throw "Main program build failed with exit code $LASTEXITCODE"
        }

        # 编译通过。
        Write-Step "PASS: build main program"
    }

    # 把 exe 路径解析成绝对路径，传给每个 E2E case 复用。
    $resolvedExe = (Resolve-Path $ExePath).Path

    # 核心业务闭环：注册、登录、新增卡片、新增错题、错题转卡片、数据检查。
    Invoke-E2ECase -Name "core business flow" -ScriptPath (Join-Path $projectRoot "tests\test_e2e_flow.ps1") -ResolvedExe $resolvedExe

    # 中文输入链路：从 UTF-8 fixture 输入中文，并断言中文字段正确落盘。
    Invoke-E2ECase -Name "Chinese input flow" -ScriptPath (Join-Path $projectRoot "tests\test_chinese_input_flow.ps1") -ResolvedExe $resolvedExe

    # 用户分支：注册、失败登录、成功登录、修改密码、旧密码失败、新密码成功。
    Invoke-E2ECase -Name "user branch flow" -ScriptPath (Join-Path $projectRoot "tests\e2e\test_user_flow.ps1") -ResolvedExe $resolvedExe

    # 卡片分支：卡片新增、查看、修改、查询、分类、排序、多条件、逻辑删除。
    Invoke-E2ECase -Name "card branch flow" -ScriptPath (Join-Path $projectRoot "tests\e2e\test_card_flow.ps1") -ResolvedExe $resolvedExe

    # 错题分支：错题新增、修改、查询、分类、多条件、转卡片、重复转卡保护、逻辑删除。
    Invoke-E2ECase -Name "wrong branch flow" -ScriptPath (Join-Path $projectRoot "tests\e2e\test_wrong_flow.ps1") -ResolvedExe $resolvedExe

    # 复习/练习/统计/维护组合分支：覆盖登录后的多个非 CRUD 菜单。
    Invoke-E2ECase -Name "review practice stats maintenance branch flow" -ScriptPath (Join-Path $projectRoot "tests\e2e\test_review_practice_stats_maintenance_flow.ps1") -ResolvedExe $resolvedExe

    # 如果没有要求保留临时文件，并且 .test_tmp 存在，则尝试清理空的 .test_tmp。
    if (-not $KeepTemp -and (Test-Path -LiteralPath $tmpParent)) {
        # 先做路径安全检查。
        $tmpParentFull = Assert-PathInsideProject $tmpParent

        # 只有 .test_tmp 已经空了才删除父目录；各 case 自己负责删除自己的临时目录。
        if (-not (Get-ChildItem -LiteralPath $tmpParentFull -Force)) {
            Remove-Item -LiteralPath $tmpParentFull -Force
        }
    }

    # 所有 E2E case 都通过。
    Write-Step "All E2E tests passed"

    # 汇总脚本成功退出。
    exit 0
}
catch {
    # 任一 case 或构建阶段抛错都会进入这里。
    Write-Error $_

    # 汇总脚本失败退出。
    exit 1
}
finally {
    # 无论成功还是失败，都恢复调用脚本前的工作目录。
    Pop-Location
}
