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
- 这个脚本是“错题模块”的端到端回归测试。
- 它会模拟一个用户从注册、登录，到记录错题、查看错题、修改错题、查询错题、分类查看、错题转卡片、重复转卡保护、删除错题的完整流程。
- 测试重点是检查 wrongs.txt 和 cards.txt 的最终落盘结果，而不是逐字匹配控制台输出。

[PowerShell 读法]
- `$变量名` 表示变量。
- `@(...)` 表示数组。
- 反引号 `` ` `` 放在行尾表示“下一行仍然属于同一条命令”。
- `. "$PSScriptRoot\common.ps1"` 是 dot-source，引入 common.ps1 里的函数和脚本变量。
- `try/finally` 保证即使测试失败，也能恢复目录并按需清理临时文件。

[输入输出]
- 输入：`$inputLines` 里按顺序写好的菜单输入，每一项相当于用户在控制台敲一行。
- 输出：被测程序的控制台输出会写入 `$outputFile`，最终断言读取 users.txt/wrongs.txt/cards.txt。

[你以后最常改的地方]
- `$inputLines`：当错题菜单编号、字段顺序、暂停回车数量变化时，主要改这里。
- 断言区的 `Get-RequiredLine` / `Assert-Equal` / `Assert-True`：当 wrongs.txt 或 cards.txt 字段顺序变化时改这里。
- `$tmpRoot` 后缀：如果新增另一个 wrong 测试脚本，需要换一个临时目录名避免冲突。

[不建议随便改的地方]
- `$ErrorActionPreference = "Stop"`：它保证 PowerShell 错误会立刻让测试失败。
- `Push-Location` / `Pop-Location`：它保证测试始终从项目根目录运行并能恢复工作目录。
- `Remove-SafeDirectory`：清理临时目录时依赖 common.ps1 的路径安全保护。

[易错点]
- 错题菜单 `8` 是“错题转知识卡片”，本脚本连续执行两次，用来验证“重复转卡保护”。
- 修改错题时的 `n` 是回答“是否修改错因类型？(y/n)”，表示不修改。
- wrongs.txt 第 9 个字段是 linkedCardId；非 -1 表示错题已经关联到一张卡片。
- wrongs.txt 第 17 个字段是 active；0 表示逻辑删除，1 表示仍有效。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；否则某些失败可能被继续跑下去。
$ErrorActionPreference = "Stop"

# 引入公共 helper：路径计算、编译运行程序、读取记录、断言函数都来自 common.ps1。
. "$PSScriptRoot\common.ps1"

# 为本测试创建独立临时目录；所有测试数据都放在这里，避免污染真实 data/。
# [可改] 如果复制本脚本做另一个错题测试，应修改 "e2e_wrong_flow" 这个目录名。
$tmpRoot = Join-Path $script:E2ETmpParent "e2e_wrong_flow"

# 本次测试专用数据目录，运行程序时会通过 --data-dir 指向这里。
$dataDir = Join-Path $tmpRoot "data"

# 保存程序控制台输出的日志文件；测试失败时配合 -KeepTemp 查看。
$outputFile = Join-Path $tmpRoot "wrong_flow_output.log"

# 切换到项目根目录，确保相对路径如 src/*.cpp、.\project1.exe 都按项目根目录解析。
Push-Location $script:E2EProjectRoot
try {
    # 创建 UTF-8 无 BOM 编码对象，用来写测试数据文件；false 表示不带 BOM。
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)

    # 设置控制台输入编码，避免中文或特殊字符通过管道输入时被错误解码。
    [Console]::InputEncoding = $utf8NoBom

    # 设置控制台输出编码，避免程序输出被 PowerShell 错误转码。
    [Console]::OutputEncoding = $utf8NoBom

    # 设置 PowerShell 管道输出编码，影响重定向和管道传输。
    $OutputEncoding = $utf8NoBom

    # 编译并解析被测 exe 的绝对路径；如果运行脚本时传 -SkipBuild，则只解析现有 exe。
    Resolve-E2EExecutable -ExePath $ExePath -SkipBuild:$SkipBuild

    # 清理上一次同名测试留下的临时目录，保证本轮从干净状态开始。
    Remove-SafeDirectory $tmpRoot

    # 创建空 data 目录和空 users/cards/wrongs/review_logs 文件。
    Initialize-EmptyDataDir -DataDir $dataDir -Encoding $utf8NoBom

    # $inputLines 是本脚本最核心的部分：每个字符串都代表一次用户输入。
    # 第一道错题走修改、转卡、重复转卡和删除；第二道错题用于确认其他记录仍 active。
    # [谨慎改] 改这里时不要随意删除空字符串；很多 "" 对应程序里的“按回车继续”。
    $inputLines = @(
        # 初始菜单：2 = 注册。
        "2",
        # 注册用户名。
        "v135_wrong_user",
        # 注册密码。
        "pass",
        # 确认密码。
        "pass",
        # 注册完成后的暂停回车。
        "",

        # 初始菜单：1 = 登录。
        "1",
        # 登录用户名。
        "v135_wrong_user",
        # 登录密码。
        "pass",
        # 登录完成后的暂停回车。
        "",

        # 主菜单：2 = 进入错题管理分支。
        "2",
        # 错题菜单：1 = 记录错题。
        "1",
        # 第一题学科 subject。
        "Math",
        # 第一题章节 chapter。
        "Algebra",
        # 第一题题目 question；后面会被修改为 Updated wrong question。
        "Original wrong question",
        # 第一题正确答案 correctAnswer。
        "x=2",
        # 第一题用户错误答案 wrongAnswer。
        "x=3",
        # 第一题错因分析 reason。
        "sign mistake",
        # 第一题错因类型：5 = 计算错误。
        "5",
        # 新增完成后的暂停回车。
        "",

        # 错题菜单：1 = 再记录一题。
        "1",
        # 第二题学科 subject；后面用它确认删除没有误伤其他错题。
        "Physics",
        # 第二题章节 chapter。
        "Mechanics",
        # 第二题题目 question。
        "Second wrong question",
        # 第二题正确答案 correctAnswer。
        "F=ma",
        # 第二题用户错误答案 wrongAnswer。
        "F=m/a",
        # 第二题错因分析 reason。
        "formula confusion",
        # 第二题错因类型：2 = 记忆错误。
        "2",
        # 新增完成后的暂停回车。
        "",

        # 错题菜单：9 = 查看全部错题，并刷新 currentWrongMap 展示列表。
        "9",
        # 查看列表中的第 1 道错题。
        "1",
        # 查看详情后的暂停回车。
        "",

        # 错题菜单：2 = 修改错题。
        "2",
        # 修改最近展示列表中的第 1 道错题。
        "1",
        # subject 留空，表示保持原值 Math。
        "",
        # chapter 留空，表示保持原值 Algebra。
        "",
        # question 改为 Updated wrong question。
        "Updated wrong question",
        # correctAnswer 留空，表示保持原值 x=2。
        "",
        # wrongAnswer 留空，表示保持原值 x=3。
        "",
        # reason 留空，表示保持原值 sign mistake。
        "",
        # 是否修改错因类型？n = 不修改。
        "n",
        # 修改完成后的暂停回车。
        "",

        # 错题菜单：5 = 按关键字查询。
        "5",
        # 查询关键字 Updated，应能找到刚刚修改后的错题。
        "Updated",
        # 选择查询结果中的第 1 条。
        "1",
        # 查询查看后的暂停回车。
        "",

        # 错题菜单：6 = 分类查看。
        "6",
        # 分类方式：1 = 按学科查看。
        "1",
        # 选择学科列表中的第 1 项。
        "1",
        # 直接回车返回，不打开该分类下的具体详情。
        "",
        # 分类查看完成后的暂停回车。
        "",

        # 错题菜单：7 = 多条件组合查询。
        "7",
        # 多条件 subject = Math。
        "Math",
        # 多条件 chapter 留空。
        "",
        # 多条件 keyword = Updated。
        "Updated",
        # 选择查询结果中的第 1 条。
        "1",
        # 查询查看后的暂停回车。
        "",

        # 错题菜单：8 = 错题转知识卡片。
        "8",
        # 选择最近结果中的第 1 条错题进行转换。
        "1",
        # 转换成功后的暂停回车。
        "",

        # 错题菜单：8 = 再次尝试把同一道错题转成卡片，用来验证重复转卡保护。
        "8",
        # 仍选择第 1 条；程序应识别已关联卡片，不应再生成第二张关联卡。
        "1",
        # 重复转换提示后的暂停回车。
        "",

        # 错题菜单：3 = 删除错题。
        "3",
        # 删除最近展示列表中的第 1 条，即修改后的 Updated wrong question。
        "1",
        # 确认删除：y = yes。
        "y",
        # 删除完成后的暂停回车。
        "",

        # 错题菜单：0 = 返回主菜单。
        "0",
        # 主菜单：0 = 退出登录/返回初始菜单。
        "0",
        # 退出后的暂停回车。
        "",
        # 初始菜单：0 = 退出程序。
        "0"
    )

    # 把输入数组拼成一整段文本，每项之间用换行分隔。
    # 最后再补一个换行，模拟用户输入最后一项后按回车。
    $inputText = [string]::Join("`n", $inputLines) + "`n"

    # 打印当前测试步骤。
    Write-E2EStep "Running wrong branch flow"

    # 运行被测程序，把上面的模拟输入通过管道送进去，并把输出写到日志文件。
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "wrong branch flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    # 拼出本轮测试生成的 users.txt 路径。
    $usersFile = Join-Path $dataDir "users.txt"

    # 拼出本轮测试生成的 cards.txt 路径；错题转卡片后会写入这里。
    $cardsFile = Join-Path $dataDir "cards.txt"

    # 拼出本轮测试生成的 wrongs.txt 路径。
    $wrongsFile = Join-Path $dataDir "wrongs.txt"

    # 从 users.txt 中找到刚才注册的用户记录。
    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and (Decode-StorageField $parts[1]) -eq "v135_wrong_user" } `
        -FailureMessage "wrong flow user not found"

    # users.txt 第 0 个字段是 userId；后面查 wrongs.txt/cards.txt 时用它限定当前用户。
    $userId = $userParts[0]

    # 从 wrongs.txt 中找到修改后的第一道错题。
    $updatedWrong = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[1] -eq $script:userId -and (Decode-StorageField $parts[4]) -eq "Updated wrong question" } `
        -FailureMessage "updated wrong not found"

    # wrongs.txt 第 9 个字段是 linkedCardId；非 -1 表示已经成功转成过卡片。
    Assert-True -Label "updated wrong converted once" -Condition ($updatedWrong[9] -ne "-1")

    # wrongs.txt 第 17 个字段是 active；0 表示删除流程做了逻辑删除。
    Assert-Equal -Label "updated wrong deleted by branch flow" -Actual $updatedWrong[17] -Expected "0"

    # linkedCardId 指向的卡片必须存在且 active，才能证明错题转卡片链路完整。
    $convertedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq $script:updatedWrong[9] -and $parts[1] -eq $script:userId -and $parts[16] -eq "1" } `
        -FailureMessage "converted card from wrong flow not found"

    # cards.txt 第 5 个字段是 front；错题转卡片时应把错题 question 写到卡片正面。
    Assert-Equal -Label "converted card front from wrong" -Actual (Decode-StorageField $convertedCard[5]) -Expected "Updated wrong question"

    # 确认第二道错题仍然 active，证明删除第 1 道错题没有误伤其他记录。
    $remainingWrong = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[1] -eq $script:userId -and (Decode-StorageField $parts[4]) -eq "Second wrong question" -and $parts[17] -eq "1" } `
        -FailureMessage "remaining active wrong not found"

    # wrongs.txt 第 2 个字段是 subject；确认剩余错题仍是 Physics 学科。
    Assert-Equal -Label "remaining wrong subject" -Actual (Decode-StorageField $remainingWrong[2]) -Expected "Physics"

    # 再运行一次程序的数据自检模式，确认本轮生成的数据文件整体格式和跨文件关联仍合法。
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "wrong flow data check"

    # 所有断言和数据自检都通过后，打印脚本级通过信息。
    Write-E2EStep "Wrong branch flow passed"
}
finally {
    # 离开测试时恢复进入脚本前的工作目录。
    Pop-Location

    # 默认删除临时目录；只有运行脚本时传 -KeepTemp 才保留现场。
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpRoot
    }
}
