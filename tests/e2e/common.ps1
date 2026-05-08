# $PSScriptRoot 是 PowerShell 自动变量，表示当前脚本所在目录：tests/e2e。
# Split-Path -Parent 取上一级目录；连续取两次就是项目根目录。
# [谨慎改] 如果以后移动 tests/e2e 目录层级，才需要同步调整这里。
$script:E2EProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# 把项目根目录转换成绝对路径，后面做安全校验时用。
# [勿随意改] 路径安全检查依赖这个值，改错会导致临时目录清理不安全或测试误失败。
$script:E2EProjectRootFull = [System.IO.Path]::GetFullPath($script:E2EProjectRoot)

# Join-Path 用系统安全的方式拼接路径；这里统一把 e2e 临时文件放到项目根目录下的 .test_tmp。
# [可改] 如果你想换一个测试临时目录名，可以只改 ".test_tmp"，但仍建议保留在项目根目录内。
$script:E2ETmpParent = Join-Path $script:E2EProjectRoot ".test_tmp"

<#
[导读]
- 这个文件是 tests/e2e 下所有 PowerShell 端到端测试共享的 helper。
- 其他测试脚本会用 `. "$PSScriptRoot\common.ps1"` 引入这里的变量和函数。
- 主要职责：
  1. 计算项目根目录和临时目录。
  2. 防止测试清理逻辑误删项目外文件。
  3. 准备空的 data/*.txt 测试数据文件。
  4. 编译并运行 C++ 主程序。
  5. 读取、拆分、解码存储文件里的记录。
  6. 提供最小断言函数，让测试失败时直接抛错。

[你以后最常改的地方]
- Initialize-EmptyDataDir 里的数据文件名列表：
  当项目新增持久化文件时，在这里补一个空文件。
- Resolve-E2EExecutable 里的 g++ 编译命令：
  当源码目录、C++ 标准或编译参数变化时改这里。
- ConvertFrom-StorageField 里的解码规则：
  只有当 storage.cpp 的转义规则变化时才改。
- Assert-Equal / Assert-True：
  如果想统一改变断言输出格式，可以改这两个函数。

[不建议随便改的地方]
- Assert-PathInsideProject / Remove-SafeDirectory：
  这是防止测试删除项目外目录的保护逻辑。
- Invoke-E2EProgram 里的 Push-Location / Pop-Location：
  这是为了保证测试在固定工作目录运行，并在结束后恢复原目录。
#>

# 定义一个函数：统一打印 e2e 测试步骤。
function Write-E2EStep {
    # param 声明函数参数；这里接收一段要打印的消息文本。
    param([string]$Message)

    # Write-Host 直接输出到控制台；前缀用于标识当前测试日志版本。
    # [可改] 如果项目版本升级或想换日志前缀，可以改 "[v1.4.0]"。
    Write-Host "[v1.4.0] $Message"
}

# 定义一个函数：确认某个路径仍在项目根目录里面。
function Assert-PathInsideProject {
    # 传入要检查的路径。
    param([string]$Path)

    # 把传入路径转成绝对路径，避免 "../" 这类相对路径绕过检查。
    $fullPath = [System.IO.Path]::GetFullPath($Path)

    # StartsWith 判断目标路径是否以项目根目录开头；OrdinalIgnoreCase 表示忽略大小写比较。
    # -not 是 PowerShell 的逻辑非。
    if (-not $fullPath.StartsWith($script:E2EProjectRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        # throw 会让当前测试失败并停止执行，避免继续做危险操作。
        throw "Refusing to operate outside project root: $fullPath"
    }

    # return 把规范化后的绝对路径交给调用方继续使用。
    return $fullPath
}

# 定义一个函数：安全删除目录。
function Remove-SafeDirectory {
    # 传入准备删除的目录路径。
    param([string]$Path)

    # 删除前先调用安全检查，确认目标仍在项目根目录内。
    $fullPath = Assert-PathInsideProject $Path

    # Test-Path 判断路径是否存在；-LiteralPath 表示按原样解释路径，不把 []、* 当通配符。
    if (Test-Path -LiteralPath $fullPath) {
        # Remove-Item 删除目录；-Recurse 递归删除子文件；-Force 允许删除隐藏/只读项。
        # [谨慎改] 这里是实际删除动作，改动前必须确认 Assert-PathInsideProject 仍然生效。
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}

# 定义一个函数：初始化一个空的数据目录。
function Initialize-EmptyDataDir {
    # 这个函数需要两个参数：数据目录路径、写文件时使用的文本编码。
    param(
        [string]$DataDir,
        [System.Text.Encoding]$Encoding
    )

    # 创建 data 目录；-Force 表示目录已存在也不报错；Out-Null 丢弃命令输出，保持测试日志干净。
    New-Item -ItemType Directory -Force -Path $DataDir | Out-Null

    # 依次创建项目当前使用的四个存储文件。
    # [可改] 如果项目新增了 data 下的持久化文件，就在这个数组里补文件名。
    foreach ($name in @("users.txt", "cards.txt", "wrongs.txt", "review_logs.txt")) {
        # WriteAllText 写入空字符串，相当于创建一个空文件或清空已有文件。
        [System.IO.File]::WriteAllText((Join-Path $DataDir $name), "", $Encoding)
    }
}

# 定义一个函数：得到可执行文件路径；默认会先编译项目。
function Resolve-E2EExecutable {
    # $ExePath 是输出 exe 路径；$SkipBuild 是开关参数，传入时跳过编译。
    param(
        [string]$ExePath,
        [switch]$SkipBuild
    )

    # 切换到项目根目录，让后面的 g++ 命令和 src/*.cpp 路径稳定可用。
    Push-Location $script:E2EProjectRoot
    try {
        # 如果没有传 -SkipBuild，就先编译 C++ 项目。
        if (-not $SkipBuild) {
            # 打印当前步骤，方便定位测试卡在哪一步。
            Write-E2EStep "Building project"

            # & 是 PowerShell 的调用运算符，用来执行外部命令。
            # [可改] 如果 C++ 标准、输出文件名或源码组织变化，可以改这一行。
            & g++ -std=c++17 -o $ExePath src/*.cpp

            # $LASTEXITCODE 是上一个原生命令的退出码；非 0 通常表示失败。
            if ($LASTEXITCODE -ne 0) {
                # 编译失败就立即终止测试，并带上实际退出码。
                throw "Build failed with exit code $LASTEXITCODE"
            }
        }

        # Resolve-Path 把 exe 路径解析成绝对路径；.Path 取字符串形式。
        # 后续 Invoke-E2EProgram 都使用绝对 exe 路径，避免 Push-Location 改变相对路径解析结果。
        $script:ResolvedExe = (Resolve-Path $ExePath).Path
    }
    finally {
        # finally 无论成功还是失败都会执行，确保离开函数时回到调用前目录。
        Pop-Location
    }
}

# 定义一个函数：运行被测 C++ 程序，并检查退出码。
function Invoke-E2EProgram {
    # 参数说明：
    # - Arguments：传给 exe 的命令行参数数组。
    # - ExpectedExitCode：期望退出码。
    # - Label：本次运行的可读名称，用于错误和日志。
    # - InputText：模拟用户在控制台输入的完整文本。
    # - OutputPath：如果提供，就把程序输出重定向到这个文件。
    param(
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label,
        [string]$InputText = $null,
        [string]$OutputPath = $null
    )

    # 切换到项目根目录，保证程序运行时看到的相对路径一致。
    Push-Location $script:E2EProjectRoot
    try {
        # 如果调用方提供了 InputText，就通过管道把文本送进原生 exe，模拟控制台交互。
        if ($null -ne $InputText) {
            # 如果提供了输出文件路径，就把标准输出和标准错误都写入文件。
            if ($OutputPath) {
                # @Arguments 是 splatting，会把数组展开成多个命令行参数。
                # > 重定向标准输出；2>&1 把标准错误合并到标准输出。
                $InputText | & $script:ResolvedExe @Arguments > $OutputPath 2>&1
            }
            else {
                # 不保存输出文件时，程序输出直接显示在当前控制台。
                $InputText | & $script:ResolvedExe @Arguments
            }
        }
        else {
            # 如果没有输入文本，就直接运行 exe。
            if ($OutputPath) {
                # 无输入、有输出文件：适合 --check-data 这类非交互命令。
                & $script:ResolvedExe @Arguments > $OutputPath 2>&1
            }
            else {
                # 无输入、无输出文件：输出直接显示在控制台。
                & $script:ResolvedExe @Arguments
            }
        }

        # 保存程序退出码；必须在下一次原生命令执行前读取。
        $actualExitCode = $LASTEXITCODE
    }
    finally {
        # 无论程序成功还是失败，都恢复之前的工作目录。
        Pop-Location
    }

    # 比较实际退出码和期望退出码；-ne 表示 not equal。
    if ($actualExitCode -ne $ExpectedExitCode) {
        # 退出码不符合预期时，让测试失败。
        throw "$Label failed: expected exit $ExpectedExitCode, got $actualExitCode"
    }

    # 退出码符合预期时，打印通过信息。
    Write-E2EStep "$Label passed (exit=$actualExitCode)"
}

# 定义一个函数：把一行存储记录按字段分隔符拆开。
function Split-RecordLine {
    # 传入 users.txt/cards.txt/wrongs.txt/review_logs.txt 里的一行文本。
    param([string]$Line)

    # -split "\|" 按竖线拆字段；竖线在正则里有特殊含义，所以要写成 \|。
    # 最后的 -1 表示保留末尾空字段，避免字段数量被 PowerShell 自动压缩。
    # 存储层已经把字段内部的 | 转成 %7C，所以这里可以安全按原始 | 切分。
    return $Line -split "\|", -1
}

# 定义一个函数：把存储文件里的字段转回原始文本。
function ConvertFrom-StorageField {
    # 传入单个字段值，例如 "hello%7Cworld"。
    param([string]$Value)

    # StringBuilder 用于高效拼接字符，避免循环中频繁创建新字符串。
    $builder = [System.Text.StringBuilder]::new()

    # $i 是当前扫描到的字符下标，从 0 开始。
    $i = 0

    # while 循环：只要还没扫描完整个字符串就继续。
    while ($i -lt $Value.Length) {
        # 如果当前字符是 %，并且后面至少还有两个字符，就可能是转义序列。
        if ($Value[$i] -eq '%' -and $i + 2 -lt $Value.Length) {
            # 取 % 后面的两个字符作为转义码，并统一转成大写。
            $code = $Value.Substring($i + 1, 2).ToUpperInvariant()

            # %25 表示原始百分号 %。
            if ($code -eq "25") {
                # [void] 表示忽略 Append 的返回值，只保留对 builder 的修改。
                [void]$builder.Append('%')
                # 已消费 "%25" 三个字符，所以下标前进 3。
                $i += 3
                # continue 跳过本轮剩余逻辑，进入下一轮循环。
                continue
            }

            # %7C 表示原始竖线 |。
            if ($code -eq "7C") {
                [void]$builder.Append('|')
                $i += 3
                continue
            }

            # %0D 表示回车符 CR，也就是 `r。
            if ($code -eq "0D") {
                [void]$builder.Append("`r")
                $i += 3
                continue
            }

            # %0A 表示换行符 LF，也就是 `n。
            if ($code -eq "0A") {
                [void]$builder.Append("`n")
                $i += 3
                continue
            }
        }

        # 如果不是项目定义的四种转义，就把当前字符原样加入结果。
        [void]$builder.Append($Value[$i])

        # 普通字符只消费一个字符。
        $i++
    }

    # ToString 把 StringBuilder 内容转换成普通字符串并返回。
    return $builder.ToString()
}

# 定义一个兼容旧名字的解码函数。
function Decode-StorageField {
    # 传入要解码的字段值。
    param([string]$Value)

    # 新脚本可以继续用 Decode-StorageField 表达“解码存储字段”的测试意图。
    # [可改] 如果以后统一改函数名，需要同步更新调用方；现在保留它能减少测试脚本改动。
    return ConvertFrom-StorageField $Value
}

# 定义一个函数：读取数据文件里的有效记录行。
function Get-RecordLines {
    # 传入要读取的文件路径。
    param([string]$Path)

    # 如果数据文件不存在，直接失败；这样比返回空数组更容易定位问题。
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing data file: $Path"
    }

    # Get-Content 读取 UTF-8 文本；Where-Object 过滤空行和全空白行。
    # @(...) 强制把结果包装成数组，即使只有一行也能安全使用 .Count。
    return @(Get-Content -LiteralPath $Path -Encoding UTF8 | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

# 定义一个函数：从数据文件中找一条必须存在的记录。
function Get-RequiredLine {
    # 参数说明：
    # - Path：要查找的数据文件。
    # - Predicate：调用方传入的判断条件脚本块。
    # - FailureMessage：找不到记录时抛出的错误消息。
    param(
        [string]$Path,
        [scriptblock]$Predicate,
        [string]$FailureMessage
    )

    # 逐行读取有效记录。
    foreach ($line in (Get-RecordLines $Path)) {
        # 先按 | 拆成字段数组。
        $parts = Split-RecordLine $line

        # & $Predicate $parts 表示执行调用方传进来的判断逻辑，并把字段数组传进去。
        if (& $Predicate $parts) {
            # 找到第一条满足条件的记录就返回字段数组。
            return $parts
        }
    }

    # 找不到目标记录就直接失败，避免后续断言在空值上报出误导性错误。
    throw $FailureMessage
}

# 定义一个函数：断言两个值相等。
function Assert-Equal {
    # Label 是断言名称；Actual 是实际值；Expected 是期望值。
    param(
        [string]$Label,
        [object]$Actual,
        [object]$Expected
    )

    # -ne 表示不相等；不相等就抛错让测试失败。
    if ($Actual -ne $Expected) {
        throw "$Label failed: expected '$Expected', got '$Actual'"
    }

    # 相等就打印通过信息。
    Write-E2EStep "$Label passed"
}

# 定义一个函数：断言某个条件为真。
function Assert-True {
    # Label 是断言名称；Condition 是布尔条件。
    param(
        [string]$Label,
        [bool]$Condition
    )

    # -not 表示取反；条件不成立就抛错。
    if (-not $Condition) {
        throw "$Label failed"
    }

    # 条件成立就打印通过信息。
    Write-E2EStep "$Label passed"
}
