# param 是脚本入口参数声明；运行这个 E2E case 时可以从命令行传这些参数。
param(
    # [可改] 主程序 exe 输出路径或已有 exe 路径；默认使用项目根目录下的 project1.exe。
    [string]$ExePath = ".\project1.exe",

    # [可改] 开关参数：传入 -SkipBuild 时跳过编译，直接使用已有 exe。
    [switch]$SkipBuild,

    # [可改] 开关参数：传入 -KeepTemp 时保留 .test_tmp/e2e_flow，方便查看输入、输出和落盘数据。
    [switch]$KeepTemp
)

<#
[导读]
- 这个脚本是“核心业务闭环”的端到端测试。
- 它从空数据目录开始，跑通：
  1. 用户注册
  2. 用户登录
  3. 新增一张知识卡片
  4. 新增一道错题
  5. 错题转知识卡片
  6. 查看卡片列表
  7. 数据一致性检查
- 它不覆盖所有菜单分支；细分分支由 tests/e2e/*.ps1 覆盖。

[PowerShell 读法]
- `$变量名` 表示变量。
- `@(...)` 表示数组。
- 反引号 `` ` `` 放在行尾表示“下一行仍然属于同一条命令”。
- `$script:xxx` 是脚本作用域变量，可以在 Predicate 脚本块里访问。
- `scriptblock` 是一段可执行代码，常用于把判断逻辑作为参数传给函数。

[输入输出]
- 输入：
  1. 脚本生成的 `$inputLines` 交互式菜单文本。
  2. 空的临时 data/*.txt 数据文件。
- 输出：
  1. `.test_tmp/e2e_flow/e2e_input.txt`：本轮实际输入文本。
  2. `.test_tmp/e2e_flow/e2e_output.log`：主程序控制台输出。
  3. `.test_tmp/e2e_flow/case/data/*.txt`：落盘数据。
  4. 字段断言和 `--check-data` 退出码。

[你以后最常改的地方]
- `$inputLines`：如果菜单编号、字段顺序或 pauseScreen 数量变化，主要改这里。
- `$expected...` 变量：如果业务输入文本变化，需要同步改预期值。
- 断言区字段下标：如果 users.txt/cards.txt/wrongs.txt 存储格式变化，需要同步改。

[不建议随便改的地方]
- `Decode-StorageField`：必须和 storage.cpp 的存储转义规则保持一致。
- 临时目录清理逻辑：必须保留路径安全检查，避免误删项目外文件。
- 本脚本从空数据目录启动，这保证 cardId/wrongId 和断言定位稳定。

[易错点]
- `$inputLines` 中的空字符串 `""` 多数对应“按回车继续”，不能随意删除。
- `.multi` / `.end` 是业务程序的多行输入协议，用来测试多行文本能否正确落盘。
- 输入中故意包含 `|`，用来验证存储层是否把字段分隔符正确转义为 `%7C`。
- 数据断言以持久化文件为准，确保交互结束后数据真正落盘。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；任一断言失败都应中断本 case。
$ErrorActionPreference = "Stop"

# $PSScriptRoot 是当前脚本所在目录：tests。
# Split-Path -Parent 取上一级目录，也就是项目根目录。
$projectRoot = Split-Path -Parent $PSScriptRoot

# 把项目根目录转换成绝对路径，后续做安全删除校验时使用。
$projectRootFull = [System.IO.Path]::GetFullPath($projectRoot)

# 所有临时测试目录统一放在项目根目录下的 .test_tmp。
$tmpParent = Join-Path $projectRoot ".test_tmp"

# 本脚本专用临时根目录。
$tmpRoot = Join-Path $tmpParent "e2e_flow"

# 本轮业务 case 目录。
$caseDir = Join-Path $tmpRoot "case"

# 主程序运行时使用的隔离 data 目录。
$dataDir = Join-Path $caseDir "data"

# 保存本轮自动生成输入文本的文件，方便 -KeepTemp 后复查菜单输入。
$inputFile = Join-Path $tmpRoot "e2e_input.txt"

# 保存主程序控制台输出的日志文件。
$outputFile = Join-Path $tmpRoot "e2e_output.log"

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

# 定义一个函数：运行被测程序，并检查退出码。
function Invoke-ProjectCase {
    # 参数说明：
    # - WorkingDirectory：运行程序前切换到的目录。
    # - Arguments：传给 exe 的命令行参数。
    # - ExpectedExitCode：期望退出码。
    # - Label：本次运行的可读名称。
    # - InputText：模拟用户输入的完整文本。
    # - OutputPath：如果提供，就把程序输出重定向到这个文件。
    param(
        [string]$WorkingDirectory,
        [string[]]$Arguments,
        [int]$ExpectedExitCode,
        [string]$Label,
        [string]$InputText = $null,
        [string]$OutputPath = $null
    )

    # 切换到指定工作目录，保证程序内相对路径稳定。
    Push-Location $WorkingDirectory
    try {
        # 如果提供了 InputText，就通过管道送给原生 exe。
        if ($null -ne $InputText) {
            # 如果提供输出文件，则把 stdout 和 stderr 都写入日志。
            if ($OutputPath) {
                $InputText | & $script:ResolvedExe @Arguments > $OutputPath 2>&1
            }
            else {
                # 不保存日志时，输出直接显示到控制台。
                $InputText | & $script:ResolvedExe @Arguments
            }
        }
        else {
            # 没有输入文本时，直接运行程序；适合 --check-data。
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
        # 无论成功还是失败，都恢复原工作目录。
        Pop-Location
    }

    # 检查实际退出码是否符合预期。
    if ($actualExitCode -ne $ExpectedExitCode) {
        throw "$Label failed: expected exit $ExpectedExitCode, got $actualExitCode"
    }

    # 退出码符合预期时打印通过信息。
    Write-Step "$Label passed (exit=$actualExitCode)"
}

# 定义一个函数：把存储文件中的一行按 | 拆成字段数组。
function Split-RecordLine {
    # 传入 users.txt/cards.txt/wrongs.txt 里的一行文本。
    param([string]$Line)

    # -split "\|" 按竖线拆字段；最后的 -1 表示保留末尾空字段。
    return $Line -split "\|", -1
}

# 定义一个函数：把存储字段从转义格式还原成原始文本。
function Decode-StorageField {
    # 传入单个字段值。
    param([string]$Value)

    # 与 storage.cpp 保持同一解码范围：只还原 %, |, CR, LF。
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

# 主流程开始：切换到项目根目录。
Push-Location $projectRoot
try {
    # 创建 UTF-8 无 BOM 编码对象；用于控制台编码和临时文本写入。
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)

    # 设置控制台输入编码，避免输入通过管道进入 exe 时乱码。
    [Console]::InputEncoding = $utf8NoBom

    # 设置控制台输出编码，避免程序输出中文或特殊字符时乱码。
    [Console]::OutputEncoding = $utf8NoBom

    # 设置 PowerShell 管道输出编码。
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

    # 每轮主链路测试从空文件启动，避免历史数据影响 cardId/wrongId 和断言定位。
    Remove-SafeDirectory $tmpRoot

    # 创建本轮测试专用 data 目录。
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

    # 初始化四个数据文件为空文件，模拟全新用户环境。
    foreach ($name in @("users.txt", "cards.txt", "wrongs.txt", "review_logs.txt")) {
        [System.IO.File]::WriteAllText((Join-Path $dataDir $name), "", $utf8NoBom)
    }

    # 下面这些 expected 变量用于断言落盘数据解码后是否等于原始输入。
    # 卡片正面包含换行和 |，用于验证多行文本和字段分隔符转义。
    $expectedCardFront = "What is a function?`nEach input | maps to one output."

    # 卡片背面包含 |，用于验证单行文本里的字段分隔符转义。
    $expectedCardBack = "A relation where each input | has one output."

    # 错题题目包含 |，用于验证 wrongs.txt 的字段转义。
    $expectedWrongQuestion = "If y=2x and x=3, what is y? | explain"

    # 错题正确答案。
    $expectedWrongCorrect = "6"

    # 错题用户错误答案包含 |。
    $expectedWrongAnswer = "5 | arithmetic slip"

    # 错因分析包含换行和 |。
    $expectedWrongReason = "multiplied incorrectly`nmissed coefficient | 2"

    # 输入序列按菜单路径组织；空字符串表示“按回车继续”或“直接回车返回”。
    # [谨慎改] 改这里时必须同步维护上面的 expected 变量和后续断言。
    $inputLines = @(
        # 初始菜单：2 = 用户注册。
        "2",
        # 注册用户名。
        "e2e_user",
        # 注册密码。
        "e2e_pass",
        # 确认注册密码。
        "e2e_pass",
        # 注册成功后的暂停回车。
        "",

        # 初始菜单：1 = 用户登录。
        "1",
        # 登录用户名。
        "e2e_user",
        # 登录密码。
        "e2e_pass",
        # 登录成功后的暂停回车；随后进入主菜单。
        "",

        # 主菜单：1 = 知识卡片管理。
        "1",
        # 卡片菜单：1 = 新增卡片。
        "1",
        # 卡片 subject。
        "Math",
        # 卡片 chapter。
        "Linear Function",
        # 卡片 title。
        "Function Definition",
        # 卡片 front 使用多行输入模式。
        ".multi",
        # front 第 1 行。
        "What is a function?",
        # front 第 2 行，故意包含 |。
        "Each input | maps to one output.",
        # 结束 front 多行输入。
        ".end",
        # 卡片 back，故意包含 |。
        "A relation where each input | has one output.",
        # 卡片 tags。
        "function",
        # 卡片 difficulty。
        "2",
        # 新增卡片完成后的暂停回车。
        "",
        # 卡片菜单：0 = 返回主菜单。
        "0",

        # 主菜单：2 = 错题管理。
        "2",
        # 错题菜单：1 = 记录错题。
        "1",
        # 错题 subject。
        "Math",
        # 错题 chapter。
        "Linear Function",
        # 错题 question，故意包含 |。
        "If y=2x and x=3, what is y? | explain",
        # 错题 correctAnswer。
        "6",
        # 错题 wrongAnswer，故意包含 |。
        "5 | arithmetic slip",
        # 错因分析 reason 使用多行输入模式。
        ".multi",
        # reason 第 1 行。
        "multiplied incorrectly",
        # reason 第 2 行，故意包含 |。
        "missed coefficient | 2",
        # 结束 reason 多行输入。
        ".end",
        # 错因类型：5 = 计算错误。
        "5",
        # 新增错题完成后的暂停回车。
        "",

        # 错题菜单：8 = 错题转知识卡片。
        "8",
        # 选择第 1 道错题转换。
        "1",
        # 转换成功后的暂停回车。
        "",
        # 错题菜单：0 = 返回主菜单。
        "0",

        # 主菜单：1 = 再次进入知识卡片管理。
        "1",
        # 卡片菜单：9 = 查看全部卡片，验证转换卡片也进入卡片列表。
        "9",
        # 查看全部卡片时直接回车返回，不打开详情。
        "",
        # 查看全部卡片后的暂停回车。
        "",
        # 卡片菜单：0 = 返回主菜单。
        "0",

        # 主菜单：0 = 退出登录。
        "0",
        # 退出登录后的暂停回车。
        "",
        # 初始菜单：0 = 退出程序。
        "0"
    )

    # 把输入数组拼成一整段文本，每项之间用换行分隔。
    # 最后再补一个换行，模拟用户输入最后一项后按回车。
    $inputText = [string]::Join("`n", $inputLines) + "`n"

    # 创建 tmpRoot，用于保存输入文本和输出日志。
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null

    # 把本轮输入文本写入 e2e_input.txt，方便 -KeepTemp 后复查。
    [System.IO.File]::WriteAllText($inputFile, $inputText, $utf8NoBom)

    # 打印当前测试步骤。
    Write-Step "Running interactive business flow"

    # 运行被测程序：指定隔离 data 目录，把自动输入通过管道送入，并把输出写到日志文件。
    Invoke-ProjectCase `
        -WorkingDirectory $projectRoot `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "interactive business flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    # 拼出本轮测试生成的 users.txt 路径。
    $usersFile = Join-Path $dataDir "users.txt"

    # 拼出本轮测试生成的 cards.txt 路径。
    $cardsFile = Join-Path $dataDir "cards.txt"

    # 拼出本轮测试生成的 wrongs.txt 路径。
    $wrongsFile = Join-Path $dataDir "wrongs.txt"

    # 从 users.txt 中找到刚注册的用户。
    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and $parts[1] -eq "e2e_user" } `
        -FailureMessage "registered user not found"

    # users.txt 第 0 个字段是 userId；后续用它限定当前用户的卡片/错题。
    $userId = $userParts[0]

    # 断言文本字段同时验证解码值和原始文件中的转义形态，覆盖多行与 | 的持久化契约。
    $manualCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[1] -eq $userId -and $parts[4] -eq "Function Definition" -and $parts[16] -eq "1" } `
        -FailureMessage "manual card not found"

    # cards.txt 第 2 个字段是 subject。
    Assert-Equal -Label "manual card subject" -Actual $manualCard[2] -Expected "Math"

    # cards.txt 第 5 个字段是 front；解码后应恢复多行和 |。
    Assert-Equal -Label "manual card front decodes multiline pipe" -Actual (Decode-StorageField $manualCard[5]) -Expected $expectedCardFront

    # cards.txt 第 6 个字段是 back；解码后应恢复 |。
    Assert-Equal -Label "manual card back decodes pipe" -Actual (Decode-StorageField $manualCard[6]) -Expected $expectedCardBack

    # 原始落盘字段中应包含 %0A，证明换行被转义保存。
    Assert-True -Label "manual card front stores escaped newline" -Condition ($manualCard[5].Contains("%0A"))

    # 原始落盘字段中应包含 %7C，证明 | 被转义保存。
    Assert-True -Label "manual card front stores escaped pipe" -Condition ($manualCard[5].Contains("%7C"))

    # 从 wrongs.txt 中找到刚创建的错题。
    $wrongParts = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[1] -eq $userId -and (Decode-StorageField $parts[4]) -eq $script:expectedWrongQuestion -and $parts[17] -eq "1" } `
        -FailureMessage "created wrong question not found"

    # wrongs.txt 第 8 个字段是 errorType；本轮选择了错因类型，不能为空。
    Assert-True -Label "wrong question error type was selected" -Condition (-not [string]::IsNullOrWhiteSpace($wrongParts[8]))

    # wrongs.txt 第 5 个字段是 correctAnswer。
    Assert-Equal -Label "wrong question correct answer decodes" -Actual (Decode-StorageField $wrongParts[5]) -Expected $expectedWrongCorrect

    # wrongs.txt 第 6 个字段是 wrongAnswer；解码后应恢复 |。
    Assert-Equal -Label "wrong question wrong answer decodes pipe" -Actual (Decode-StorageField $wrongParts[6]) -Expected $expectedWrongAnswer

    # wrongs.txt 第 7 个字段是 reason；解码后应恢复多行和 |。
    Assert-Equal -Label "wrong question reason decodes multiline pipe" -Actual (Decode-StorageField $wrongParts[7]) -Expected $expectedWrongReason

    # wrongs.txt 第 9 个字段是 linkedCardId；错题转卡片后应不再是 -1。
    $linkedCardId = $wrongParts[9]

    # 确认错题确实关联到了转换生成的卡片。
    Assert-True -Label "wrong question has linked card" -Condition ($linkedCardId -ne "-1")

    # 错题转卡片需要同时满足 wrongs.txt 的 linkedCardId 和 cards.txt 中真实可见卡片。
    $convertedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq $linkedCardId -and $parts[1] -eq $userId -and $parts[16] -eq "1" } `
        -FailureMessage "converted card referenced by linkedCardId not found"

    # 转换卡片的 front 应等于错题题目。
    Assert-Equal -Label "converted card front matches wrong question" -Actual (Decode-StorageField $convertedCard[5]) -Expected $expectedWrongQuestion

    # 解码转换卡片的 back，后续检查它包含正确答案和错因分析。
    $convertedBackDecoded = Decode-StorageField $convertedCard[6]

    # 转换卡片背面应以正确答案开头。
    Assert-True -Label "converted card back starts with correct answer" -Condition ($convertedBackDecoded.StartsWith($expectedWrongCorrect))

    # 转换卡片背面应包含错因分析。
    Assert-True -Label "converted card back carries reason" -Condition ($convertedBackDecoded.Contains($expectedWrongReason))

    # 转换卡片 tag 应存在，证明转换流程补充了默认标签。
    Assert-True -Label "converted card tag is present" -Condition (-not [string]::IsNullOrWhiteSpace($convertedCard[7]))

    # 打印数据一致性检查步骤。
    Write-Step "Running post-flow data consistency check"

    # 再运行一次 --check-data，确认交互流程生成的数据整体格式和关联合法。
    Invoke-ProjectCase `
        -WorkingDirectory $projectRoot `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "post-flow data check"

    # 所有断言和数据检查通过后，打印脚本级通过信息。
    Write-Step "E2E business flow passed"
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
