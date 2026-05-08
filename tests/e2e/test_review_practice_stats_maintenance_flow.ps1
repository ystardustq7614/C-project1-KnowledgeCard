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
- 这个脚本是一个组合型端到端回归测试，覆盖四个登录后的主菜单分支：
  1. 今日复习
  2. 自测练习中心
  3. 统计分析
  4. 数据维护
- 它不走“新增卡片/新增错题”的交互流程，而是先直接写入 fixture 数据，缩短测试输入长度。
- 测试重点是检查 review_logs.txt、cards.txt、wrongs.txt 的最终落盘结果。

[PowerShell 读法]
- `$变量名` 表示变量。
- `@(...)` 表示数组。
- 反引号 `` ` `` 放在行尾表示“下一行仍然属于同一条命令”。
- `[System.IO.File]::WriteAllLines(...)` 表示调用 .NET 静态方法一次写入多行文件。
- `. "$PSScriptRoot\common.ps1"` 是 dot-source，引入 common.ps1 里的公共函数和脚本变量。

[输入输出]
- 输入：预置 data/*.txt，加上 `$inputLines` 里按顺序写好的菜单输入。
- 输出：程序控制台日志写入 `$outputFile`；断言读取 review_logs.txt、cards.txt、wrongs.txt。

[你以后最常改的地方]
- 预置数据区：如果 cards.txt/wrongs.txt/review_logs.txt 字段顺序变了，需要同步改 fixture 行。
- `$inputLines`：如果复习、练习、统计、维护菜单编号或暂停回车数量变了，主要改这里。
- 断言区：如果复习日志或 active/reviewCount 字段位置变了，需要同步改字段下标。

[不建议随便改的地方]
- `$ErrorActionPreference = "Stop"`：它保证 PowerShell 错误会立刻让测试失败。
- `Push-Location` / `Pop-Location`：它保证测试始终从项目根目录运行并能恢复工作目录。
- `Remove-SafeDirectory`：清理临时目录时依赖 common.ps1 的路径安全保护。

[易错点]
- 自测练习只统计本次练习结果，不应写 review_logs.txt，也不应修改 mastery/nextReviewDate。
- 今日复习会复习一张卡片和一道错题，因此 review_logs.txt 最终应有两条记录。
- 数据维护里的“彻底清空回收站”本脚本只输入 n 走取消路径，避免物理删除影响后续断言。
- 恢复卡片/错题只检查 active 字段从 0 变 1，不要求重算复习计划。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；否则某些失败可能被继续跑下去。
$ErrorActionPreference = "Stop"

# 引入公共 helper：路径计算、编译运行程序、读取记录、断言函数都来自 common.ps1。
. "$PSScriptRoot\common.ps1"

# 为本测试创建独立临时目录；所有测试数据都放在这里，避免污染真实 data/。
# [可改] 如果拆出新的组合流程测试，应修改这个目录名，避免多个脚本共用同一临时目录。
$tmpRoot = Join-Path $script:E2ETmpParent "e2e_review_practice_stats_maintenance_flow"

# 本次测试专用数据目录，运行程序时会通过 --data-dir 指向这里。
$dataDir = Join-Path $tmpRoot "data"

# 保存程序控制台输出的日志文件；测试失败时配合 -KeepTemp 查看。
$outputFile = Join-Path $tmpRoot "review_practice_stats_maintenance_output.log"

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

    # 使用当天日期写 fixture，确保“今日复习”和“今日待复习统计”都能命中这些记录。
    $today = Get-Date -Format "yyyy-MM-dd"

    # 预置一条到期卡片、一条到期错题，以及各一条已删除记录用于维护模块。
    # [谨慎改] 这些行必须和 storage.cpp 保存格式保持一致。
    [System.IO.File]::WriteAllLines(
        # users.txt 字段：userId|username|password|createDate。
        (Join-Path $dataDir "users.txt"),
        @("1|v135_branch_user|pass|$today"),
        $utf8NoBom
    )

    [System.IO.File]::WriteAllLines(
        # cards.txt 字段：
        # cardId|userId|subject|chapter|title|front|back|tags|difficulty|mastery|reviewCount|correctStreak|intervalDays|createDate|lastReviewDate|nextReviewDate|active
        (Join-Path $dataDir "cards.txt"),
        @(
            # active=1 且 nextReviewDate=$today：用于今日复习。
            "1|1|Math|Algebra|Due Card|Question one|Answer one|branch|2|50|0|0|1|$today||$today|1",
            # active=0：用于数据维护里的“查看已删除卡片”和“恢复知识卡片”。
            "2|1|History|Archive|Deleted Card|Deleted question|Deleted answer|deleted|1|40|0|0|1|$today||$today|0"
        ),
        $utf8NoBom
    )

    [System.IO.File]::WriteAllLines(
        # wrongs.txt 字段：
        # wrongId|userId|subject|chapter|question|correctAnswer|wrongAnswer|reason|errorType|linkedCardId|mastery|reviewCount|correctStreak|intervalDays|createDate|lastReviewDate|nextReviewDate|active
        (Join-Path $dataDir "wrongs.txt"),
        @(
            # active=1 且 nextReviewDate=$today：用于今日复习；linkedCardId=-1 表示未转成卡片。
            "1|1|Math|Algebra|Due Wrong|Correct A|Wrong A|Reason A|calculation|-1|30|0|0|1|$today||$today|1",
            # active=0：用于数据维护里的“查看已删除错题”和“恢复错题”。
            "2|1|Science|Deleted|Deleted Wrong|Correct D|Wrong D|Reason D|memory|-1|30|0|0|1|$today||$today|0"
        ),
        $utf8NoBom
    )

    # $inputLines 是本脚本最核心的部分：每个字符串都代表一次用户输入。
    # 输入序列依次进入今日复习、自测练习、统计分析和数据维护。
    # [谨慎改] 改这里时不要随意删除空字符串；很多 "" 对应程序里的“按回车继续”。
    $inputLines = @(
        # 初始菜单：1 = 登录。
        "1",
        # 登录用户名。
        "v135_branch_user",
        # 登录密码。
        "pass",
        # 登录完成后的暂停回车。
        "",

        # 主菜单：3 = 今日复习。
        "3",
        # 今日复习菜单：1 = 查看今日待复习列表。
        "1",
        # 查看列表后的暂停回车。
        "",

        # 今日复习菜单：2 = 开始复习。
        "2",
        # 第 1 项复习：按回车查看答案/正确答案。
        "",
        # 第 1 项复习评分：3 = 记牢。
        "3",
        # 继续下一项？直接回车继续。
        "",
        # 第 2 项复习：按回车查看答案/正确答案。
        "",
        # 第 2 项复习评分：2 = 模糊。
        "2",
        # 复习会话结束后的暂停回车。
        "",

        # 今日复习菜单：3 = 查看复习历史。
        "3",
        # 查看历史后的暂停回车。
        "",
        # 今日复习菜单：0 = 返回主菜单。
        "0",

        # 主菜单：4 = 自测练习中心。
        "4",
        # 自测练习菜单：1 = 随机抽查测试。
        "1",
        # 抽取数量：1 张；避免测试输入过长。
        "1",
        # 随机自测第 1 张：按回车查看答案。
        "",
        # 你答对了吗？直接回车默认算对。
        "",
        # 随机自测结束后的暂停回车。
        "",

        # 自测练习菜单：2 = 薄弱点专项突破。
        "2",
        # 找到薄弱章节卡片后，按回车开始专项练习。
        "",
        # 专项练习第 1 张：按回车查看答案。
        "",
        # 你答对了吗？直接回车默认算对。
        "",
        # 专项练习结束后的暂停回车。
        "",
        # 自测练习菜单：0 = 返回主菜单。
        "0",

        # 主菜单：5 = 统计分析。
        "5",
        # 统计菜单：1 = 卡片总数统计。
        "1",
        # 查看统计后的暂停回车。
        "",
        # 统计菜单：2 = 错题总数统计。
        "2",
        # 查看统计后的暂停回车。
        "",
        # 统计菜单：3 = 各学科分布统计。
        "3",
        # 查看统计后的暂停回车。
        "",
        # 统计菜单：4 = 今日待复习统计。
        "4",
        # 查看统计后的暂停回车。
        "",
        # 统计菜单：5 = 掌握情况统计。
        "5",
        # 查看统计后的暂停回车。
        "",
        # 统计菜单：6 = 复习频率统计。
        "6",
        # 查看统计后的暂停回车。
        "",
        # 统计菜单：7 = 错因分类统计。
        "7",
        # 查看统计后的暂停回车。
        "",
        # 统计菜单：0 = 返回主菜单。
        "0",

        # 主菜单：6 = 数据维护。
        "6",
        # 数据维护菜单：1 = 查看已删除卡片；会生成 currentDeletedCardMap。
        "1",
        # 查看已删除卡片后的暂停回车。
        "",
        # 数据维护菜单：2 = 恢复知识卡片。
        "2",
        # 恢复第 1 条已删除卡片。
        "1",
        # 恢复卡片后的暂停回车。
        "",
        # 数据维护菜单：3 = 查看已删除错题；会生成 currentDeletedWrongMap。
        "3",
        # 查看已删除错题后的暂停回车。
        "",
        # 数据维护菜单：4 = 恢复错题。
        "4",
        # 恢复第 1 条已删除错题。
        "1",
        # 恢复错题后的暂停回车。
        "",
        # 数据维护菜单：5 = 彻底清空回收站。
        "5",
        # 确认物理删除？n = 取消，避免破坏本轮恢复断言。
        "n",
        # 取消物理删除后的暂停回车。
        "",
        # 数据维护菜单：6 = 数据一致性检查。
        "6",
        # 检查完成后的暂停回车。
        "",
        # 数据维护菜单：0 = 返回主菜单。
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
    Write-E2EStep "Running review/practice/stats/maintenance branch flow"

    # 运行被测程序，把上面的模拟输入通过管道送进去，并把输出写到日志文件。
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "review practice stats maintenance branch flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    # 拼出本轮测试生成的 cards.txt 路径。
    $cardsFile = Join-Path $dataDir "cards.txt"

    # 拼出本轮测试生成的 wrongs.txt 路径。
    $wrongsFile = Join-Path $dataDir "wrongs.txt"

    # 拼出本轮测试生成的 review_logs.txt 路径。
    $logsFile = Join-Path $dataDir "review_logs.txt"

    # 读取复习日志有效行；自测练习不写日志，因此这里应该只有今日复习产生的两条。
    $logLines = Get-RecordLines $logsFile

    # 今日复习 fixture 包含一张卡片和一道错题，所以日志数量应为 2。
    Assert-Equal -Label "review log count" -Actual $logLines.Count -Expected 2

    # review_logs.txt 第 3 个字段是 itemType；确认存在一条 card 日志。
    Assert-True -Label "card review log exists" -Condition (($logLines | Where-Object { (Split-RecordLine $_)[3] -eq "card" }).Count -eq 1)

    # review_logs.txt 第 3 个字段是 itemType；确认存在一条 wrong 日志。
    Assert-True -Label "wrong review log exists" -Condition (($logLines | Where-Object { (Split-RecordLine $_)[3] -eq "wrong" }).Count -eq 1)

    # 找到被复习过的卡片记录，cardId=1 来自上面的 fixture。
    $reviewedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq "1" } `
        -FailureMessage "reviewed card not found"

    # cards.txt 第 10 个字段是 reviewCount；今日复习后应从 0 增加到 1。
    Assert-Equal -Label "card review count updated" -Actual $reviewedCard[10] -Expected "1"

    # 找到被复习过的错题记录，wrongId=1 来自上面的 fixture。
    $reviewedWrong = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[0] -eq "1" } `
        -FailureMessage "reviewed wrong not found"

    # wrongs.txt 第 11 个字段是 reviewCount；今日复习后应从 0 增加到 1。
    Assert-Equal -Label "wrong review count updated" -Actual $reviewedWrong[11] -Expected "1"

    # 找到原本 active=0 的已删除卡片，cardId=2 来自上面的 fixture。
    $restoredCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq "2" } `
        -FailureMessage "restored card not found"

    # cards.txt 第 16 个字段是 active；恢复后应变成 1。
    Assert-Equal -Label "deleted card restored" -Actual $restoredCard[16] -Expected "1"

    # 找到原本 active=0 的已删除错题，wrongId=2 来自上面的 fixture。
    $restoredWrong = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[0] -eq "2" } `
        -FailureMessage "restored wrong not found"

    # wrongs.txt 第 17 个字段是 active；恢复后应变成 1。
    Assert-Equal -Label "deleted wrong restored" -Actual $restoredWrong[17] -Expected "1"

    # 再运行一次程序的数据自检模式，确认本轮生成的数据文件整体格式和关联仍合法。
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "review practice stats maintenance data check"

    # 所有断言和数据自检都通过后，打印脚本级通过信息。
    Write-E2EStep "Review/practice/stats/maintenance branch flow passed"
}
finally {
    # 离开测试时恢复进入脚本前的工作目录。
    Pop-Location

    # 默认删除临时目录；只有运行脚本时传 -KeepTemp 才保留现场。
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpRoot
    }
}
