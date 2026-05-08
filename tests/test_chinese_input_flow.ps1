# param 是脚本入口参数声明；运行这个测试脚本时可以从命令行传这些参数。
param(
    # [可改] 被测程序的 exe 输出路径；默认在项目根目录生成 project1.exe。
    [string]$ExePath = ".\project1.exe",

    # [可改] 开关参数：传入 -SkipBuild 时跳过编译，直接使用已有 exe。
    [switch]$SkipBuild,

    # [可改] 开关参数：传入 -KeepTemp 时保留 .test_tmp 下的临时数据和输出日志，方便排查失败。
    [switch]$KeepTemp
)

<#
[导读]
- 这个脚本是“中文输入链路”的端到端回归测试。
- 它用 UTF-8 文本 fixture 驱动 project1.exe，验证中文用户名、卡片、错题、错题转卡片内容能正确落盘。
- 和 tests/e2e/*.ps1 不同，它不把菜单输入直接写在脚本里，而是从 tests/fixtures/e2e_inputs/chinese_business_flow.txt 读取。

[PowerShell 读法]
- `$变量名` 表示变量。
- `@(...)` 表示数组。
- `@{}` 表示哈希表，类似 key-value 字典。
- 反引号 `` ` `` 放在行尾表示“下一行仍然属于同一条命令”。
- `[System.IO.File]::ReadAllText(...)` 表示调用 .NET 静态方法读取整个文件。
- `$script:xxx` 表示脚本作用域变量，传进 scriptblock 后仍能访问。

[输入输出]
- 输入：
  1. `tests/fixtures/e2e_inputs/chinese_business_flow.txt`：中文菜单输入流程。
  2. `tests/fixtures/e2e_inputs/chinese_expected_fields.txt`：预期落盘字段。
- 输出：
  1. `.test_tmp/chinese_input_flow/data/*.txt`：隔离测试数据。
  2. `.test_tmp/chinese_input_flow/chinese_output.log`：程序控制台输出。
  3. 多个字段断言结果。

[你以后最常改的地方]
- `$fixtureDir` / `$inputFixture` / `$expectedFixture`：如果 fixture 文件移动或重命名，需要改这里。
- `Read-ExpectedFields`：如果 expected fixture 格式从 key=value 改成 JSON/YAML，需要改这里。
- 断言区字段下标：如果 users.txt/cards.txt/wrongs.txt 的存储格式变化，需要同步改。

[不建议随便改的地方]
- 三处编码设置：中文输入通过管道进入原生 exe，编码设置少一处都可能乱码。
- `ConvertFrom-StorageField`：必须和 storage.cpp 的转义规则保持一致。
- `Remove-SafeDirectory`：负责清理临时目录，必须保留路径安全检查。

[易错点]
- 中文输入从 fixture 读取，是为了避免 PowerShell 脚本源码编码影响测试数据。
- 断言必须先 Decode-StorageField，再和 expected fixture 比较；落盘文件里可能有 `%7C/%0A` 这类转义。
- 这个脚本只覆盖中文链路，不负责穷举所有菜单分支；菜单分支覆盖在 tests/e2e/*.ps1。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；否则某些失败可能被继续跑下去。
$ErrorActionPreference = "Stop"

# $PSScriptRoot 是当前脚本所在目录：tests。
# Split-Path -Parent 取上一级目录，也就是项目根目录。
$projectRoot = Split-Path -Parent $PSScriptRoot

# 把项目根目录转成绝对路径，后续做安全删除校验时使用。
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)

# 所有临时测试目录统一放在项目根目录下的 .test_tmp。
$tmpParent = Join-Path $projectRoot ".test_tmp"

# 本脚本专用临时目录，避免和其他测试 case 混用数据。
$tmpRoot = Join-Path $tmpParent "chinese_input_flow"

# 被测程序运行时使用的隔离 data 目录。
$dataDir = Join-Path $tmpRoot "data"

# 保存程序控制台输出的日志文件；失败时配合 -KeepTemp 查看。
$outputFile = Join-Path $tmpRoot "chinese_output.log"

# 中文输入和预期字段 fixture 所在目录。
$fixtureDir = Join-Path $projectRoot "tests\fixtures\e2e_inputs"

# 中文业务流程输入文件；每一行相当于用户在控制台输入一行。
$inputFixture = Join-Path $fixtureDir "chinese_business_flow.txt"

# 预期字段文件；格式是 key=value。
$expectedFixture = Join-Path $fixtureDir "chinese_expected_fields.txt"

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
        # 路径不在项目根目录下就直接失败，避免删除或操作用户其他目录。
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

# 定义一个函数：把存储文件中的一行按 | 拆成字段数组。
function Split-RecordLine {
    # 传入 users.txt/cards.txt/wrongs.txt 里的一行文本。
    param([string]$Line)

    # -split "\|" 按竖线拆字段；最后的 -1 表示保留末尾空字段。
    return $Line -split "\|", -1
}

# 定义一个函数：把存储字段从转义格式还原成原始文本。
function ConvertFrom-StorageField {
    # 传入单个字段值，例如 "中文%7C标签"。
    param([string]$Value)

    # StringBuilder 用于循环拼接字符，避免频繁创建新字符串。
    $builder = [System.Text.StringBuilder]::new()

    # 当前扫描到的字符下标。
    $i = 0

    # 逐字符扫描整个字段。
    while ($i -lt $Value.Length) {
        # 如果当前字符是 %，并且后面还有两个字符，就可能是项目存储转义。
        if ($Value[$i] -eq '%' -and $i + 2 -lt $Value.Length) {
            # 取 % 后面的两个字符作为转义码，并统一转成大写。
            $code = $Value.Substring($i + 1, 2).ToUpperInvariant()

            # %25 表示原始百分号 %。
            if ($code -eq "25") {
                [void]$builder.Append('%')
                $i += 3
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

        # 如果不是项目定义的转义，就原样追加当前字符。
        [void]$builder.Append($Value[$i])

        # 普通字符只前进一位。
        $i++
    }

    # 返回解码后的字符串。
    return $builder.ToString()
}

# 定义一个语义更直观的别名函数：解码存储字段。
function Decode-StorageField {
    # 传入要解码的字段值。
    param([string]$Value)

    # 实际逻辑复用 ConvertFrom-StorageField。
    return ConvertFrom-StorageField $Value
}

# 定义一个函数：读取 expected fixture 中的 key=value 预期值。
function Read-ExpectedFields {
    # Path 是 expected fixture 路径；Encoding 是读取文本时使用的编码。
    param(
        [string]$Path,
        [System.Text.Encoding]$Encoding
    )

    # expected fixture 使用 key=value，允许中文值直接存放在 UTF-8 文本中。
    # @{} 是哈希表，用来保存 key -> expected value。
    $map = @{}

    # 一次性读取整个 expected fixture。
    $text = [System.IO.File]::ReadAllText($Path, $Encoding)

    # 按 CRLF 或 LF 拆分成多行；这里用正则写法兼容两种换行。
    $lines = $text -split "`r?`n"

    # 遍历每一行 expected 配置。
    foreach ($line in $lines) {
        # Trim 去掉行首尾空白。
        $trimmed = $line.Trim()

        # 跳过空行和以 # 开头的注释行。
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) {
            continue
        }

        # 找到第一个等号，用它把 key 和 value 分开。
        $eq = $trimmed.IndexOf("=")

        # 等号不存在或在开头，都说明 fixture 格式错误。
        if ($eq -le 0) {
            throw "Invalid expected field line: $trimmed"
        }

        # 等号左边是 key。
        $key = $trimmed.Substring(0, $eq)

        # 等号右边是 value；允许 value 本身包含中文和空格。
        $value = $trimmed.Substring($eq + 1)

        # 写入哈希表，后续用 $expected["username"] 这类方式读取。
        $map[$key] = $value
    }

    # 返回所有预期字段。
    return $map
}

# 定义一个函数：从数据文件中找一条必须存在的记录。
function Get-RequiredLine {
    # Predicate 是调用方传进来的判断脚本块；FailureMessage 是找不到时的错误。
    param(
        [string]$Path,
        [scriptblock]$Predicate,
        [string]$FailureMessage
    )

    # 按 UTF-8 读取数据文件。
    $lines = Get-Content -LiteralPath $Path -Encoding UTF8

    # 逐行查找。
    foreach ($line in $lines) {
        # 跳过空行。
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        # 按 | 拆成字段数组。
        $parts = Split-RecordLine $line

        # 执行调用方传入的判断条件；如果匹配就返回字段数组。
        if (& $Predicate $parts) {
            return $parts
        }
    }

    # 找不到目标记录就让测试失败。
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

    # 不相等就抛错。
    if ($Actual -ne $Expected) {
        throw "$Label failed: expected '$Expected', got '$Actual'"
    }

    # 相等则打印通过信息。
    Write-Step "$Label passed"
}

# 定义一个函数：断言某个布尔条件为真。
function Assert-True {
    # Label 是断言名称；Condition 是布尔条件。
    param(
        [string]$Label,
        [bool]$Condition
    )

    # 条件为假就抛错。
    if (-not $Condition) {
        throw "$Label failed"
    }

    # 条件为真则打印通过信息。
    Write-Step "$Label passed"
}

# 定义一个函数：运行被测程序，并检查退出码。
function Invoke-ProjectCase {
    # 参数说明：
    # - Arguments：传给 exe 的命令行参数数组。
    # - ExpectedExitCode：期望退出码。
    # - Label：本次运行的可读名称。
    # - InputText：模拟用户输入的完整文本。
    # - OutputPath：如果提供，就把程序输出重定向到这个文件。
    param(
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label,
        [string]$InputText = $null,
        [string]$OutputPath = $null
    )

    # 切换到项目根目录，保证程序内相对路径稳定。
    Push-Location $projectRoot
    try {
        # 如果提供了 InputText，就通过管道送给原生 exe。
        if ($null -ne $InputText) {
            # 如果提供了输出路径，就把 stdout 和 stderr 都写入日志文件。
            if ($OutputPath) {
                $InputText | & $script:ResolvedExe @Arguments > $OutputPath 2>&1
            }
            else {
                # 不保存日志时，输出直接显示在控制台。
                $InputText | & $script:ResolvedExe @Arguments
            }
        }
        else {
            # 没有 InputText 时，直接运行程序。
            if ($OutputPath) {
                & $script:ResolvedExe @Arguments > $OutputPath 2>&1
            }
            else {
                & $script:ResolvedExe @Arguments
            }
        }

        # 保存原生命令退出码；必须在下一次原生命令前读取。
        $actualExitCode = $LASTEXITCODE
    }
    finally {
        # 无论运行成功还是失败，都恢复原工作目录。
        Pop-Location
    }

    # 检查退出码是否符合预期。
    if ($actualExitCode -ne $ExpectedExitCode) {
        throw "$Label failed: expected exit $ExpectedExitCode, got $actualExitCode"
    }

    # 退出码符合预期时输出通过信息。
    Write-Step "$Label passed (exit=$actualExitCode)"
}

# 主流程开始：切换到项目根目录。
Push-Location $projectRoot
try {
    # 创建 UTF-8 无 BOM 编码对象；这里用于 fixture 读取和测试数据写入。
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)

    # 设置控制台输入编码，确保中文通过管道进入 exe 时不乱码。
    [Console]::InputEncoding = $utf8NoBom

    # 设置控制台输出编码，确保 exe 输出中文时不被 PowerShell 错误解码。
    [Console]::OutputEncoding = $utf8NoBom

    # 设置 PowerShell 管道输出编码，影响 `$inputText | & exe` 这条链路。
    $OutputEncoding = $utf8NoBom

    # 如果没有传 -SkipBuild，就先编译主程序。
    if (-not $SkipBuild) {
        # 打印编译步骤。
        Write-Step "Building project"

        # 调用 g++ 编译所有 src/*.cpp 到指定 exe 路径。
        & g++ -std=c++17 -o $ExePath src/*.cpp

        # 编译失败时直接中止测试。
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE"
        }
    }

    # 把 exe 路径解析成绝对路径，后续运行时不受 Push-Location 影响。
    $script:ResolvedExe = (Resolve-Path $ExePath).Path

    # 中文 E2E 必须从空数据目录启动，避免同名中文用户导致注册分支变更。
    Remove-SafeDirectory $tmpRoot

    # 创建本轮测试专用 data 目录。
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

    # 初始化四个数据文件为空文件，模拟一个干净的新用户环境。
    foreach ($name in @("users.txt", "cards.txt", "wrongs.txt", "review_logs.txt")) {
        [System.IO.File]::WriteAllText((Join-Path $dataDir $name), "", $utf8NoBom)
    }

    # 读取中文菜单输入 fixture；这里不用 Get-Content，是为了保留原始换行和空行。
    $inputText = [System.IO.File]::ReadAllText($inputFixture, $utf8NoBom)

    # 读取 expected fixture，得到 key-value 预期字段表。
    $expected = Read-ExpectedFields -Path $expectedFixture -Encoding $utf8NoBom

    # 打印当前测试步骤。
    Write-Step "Running chinese input business flow"

    # 运行被测程序：使用隔离 data 目录，输入中文业务流程，把输出写到日志文件。
    Invoke-ProjectCase `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "chinese input business flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    # 拼出本轮测试生成的 users.txt 路径。
    $usersFile = Join-Path $dataDir "users.txt"

    # 拼出本轮测试生成的 cards.txt 路径。
    $cardsFile = Join-Path $dataDir "cards.txt"

    # 拼出本轮测试生成的 wrongs.txt 路径。
    $wrongsFile = Join-Path $dataDir "wrongs.txt"

    # 从 users.txt 中找到中文用户名对应的用户记录。
    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and (Decode-StorageField $parts[1]) -eq $script:expected["username"] } `
        -FailureMessage "registered chinese user not found"

    # users.txt 第 0 个字段是 userId；后续查卡片/错题时用它限定当前用户。
    $userId = $userParts[0]

    # users.txt 第 1 个字段是 username；确认中文用户名保存后可解码还原。
    Assert-Equal -Label "chinese username preserved" -Actual (Decode-StorageField $userParts[1]) -Expected $expected["username"]

    # users.txt 第 2 个字段是 password；确认中文流程下密码字段也没有被破坏。
    Assert-Equal -Label "chinese user password preserved" -Actual (Decode-StorageField $userParts[2]) -Expected $expected["password"]

    # 从 cards.txt 中找到手动新增的中文卡片。
    $manualCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[1] -eq $userId -and (Decode-StorageField $parts[4]) -eq $script:expected["card_title"] -and $parts[16] -eq "1" } `
        -FailureMessage "chinese card not found"

    # cards.txt 第 2 个字段是 subject；确认中文学科保存正确。
    Assert-Equal -Label "chinese card subject preserved" -Actual (Decode-StorageField $manualCard[2]) -Expected $expected["card_subject"]

    # cards.txt 第 3 个字段是 chapter；确认中文章节保存正确。
    Assert-Equal -Label "chinese card chapter preserved" -Actual (Decode-StorageField $manualCard[3]) -Expected $expected["card_chapter"]

    # cards.txt 第 5 个字段是 front；确认中文问题正文保存正确。
    Assert-Equal -Label "chinese card front preserved" -Actual (Decode-StorageField $manualCard[5]) -Expected $expected["card_front"]

    # cards.txt 第 6 个字段是 back；确认中文答案保存正确。
    Assert-Equal -Label "chinese card back preserved" -Actual (Decode-StorageField $manualCard[6]) -Expected $expected["card_back"]

    # cards.txt 第 7 个字段是 tags；确认中文标签保存正确。
    Assert-Equal -Label "chinese card tags preserved" -Actual (Decode-StorageField $manualCard[7]) -Expected $expected["card_tags"]

    # 从 wrongs.txt 中找到手动新增的中文错题。
    $wrongParts = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[1] -eq $userId -and (Decode-StorageField $parts[4]) -eq $script:expected["wrong_question"] -and $parts[17] -eq "1" } `
        -FailureMessage "chinese wrong question not found"

    # wrongs.txt 第 5 个字段是 correctAnswer；确认中文错题正确答案保存正确。
    Assert-Equal -Label "chinese wrong correct answer preserved" -Actual (Decode-StorageField $wrongParts[5]) -Expected $expected["wrong_correct"]

    # wrongs.txt 第 6 个字段是 wrongAnswer；确认用户错误答案保存正确。
    Assert-Equal -Label "chinese wrong answer preserved" -Actual (Decode-StorageField $wrongParts[6]) -Expected $expected["wrong_answer"]

    # wrongs.txt 第 7 个字段是 reason；确认中文错因分析保存正确。
    Assert-Equal -Label "chinese wrong reason preserved" -Actual (Decode-StorageField $wrongParts[7]) -Expected $expected["wrong_reason"]

    # wrongs.txt 第 9 个字段是 linkedCardId；错题转卡片后应不再是 -1。
    $linkedCardId = $wrongParts[9]

    # 确认中文错题确实生成并关联了一张卡片。
    Assert-True -Label "chinese wrong has linked card" -Condition ($linkedCardId -ne "-1")

    # 转换卡片的正面来自错题题目，背面必须同时保留正确答案和错因分析。
    $convertedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq $linkedCardId -and $parts[1] -eq $userId -and $parts[16] -eq "1" } `
        -FailureMessage "converted card for chinese wrong not found"

    # 转换卡片的 front 应等于原错题题目。
    Assert-Equal -Label "converted chinese card front preserved" -Actual (Decode-StorageField $convertedCard[5]) -Expected $expected["wrong_question"]

    # 解码转换卡片的 back，后面要检查它同时包含正确答案和错因分析。
    $convertedBackDecoded = Decode-StorageField $convertedCard[6]

    # 转换卡片背面应以正确答案开头。
    Assert-True -Label "converted chinese card back starts with answer" -Condition ($convertedBackDecoded.StartsWith($expected["wrong_correct"]))

    # 转换卡片背面还应包含错因分析。
    Assert-True -Label "converted chinese card back carries reason" -Condition ($convertedBackDecoded.Contains($expected["wrong_reason"]))

    # 打印数据一致性检查步骤。
    Write-Step "Running post-flow data consistency check"

    # 再运行一次 --check-data，确认中文流程生成的数据整体格式和关联合法。
    Invoke-ProjectCase `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "chinese post-flow data check"

    # 所有断言和数据检查通过后，打印脚本级通过信息。
    Write-Step "Chinese input flow passed"
}
finally {
    # 离开脚本时恢复进入脚本前的工作目录。
    Pop-Location

    # 默认清理临时目录；传入 -KeepTemp 时保留现场用于排查。
    if (-not $KeepTemp) {
        # 删除本脚本的临时目录。
        Remove-SafeDirectory $tmpRoot

        # 如果 .test_tmp 已经空了，也顺手删除父目录。
        $tmpParentFull = Assert-PathInsideProject $tmpParent
        if ((Test-Path -LiteralPath $tmpParentFull) -and -not (Get-ChildItem -LiteralPath $tmpParentFull -Force)) {
            Remove-Item -LiteralPath $tmpParentFull -Force
        }
    }
}
