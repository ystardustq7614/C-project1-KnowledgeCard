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
- 这个脚本是“卡片模块”的端到端回归测试。
- 它会模拟一个用户从注册、登录，到新增卡片、查看卡片、修改卡片、查询卡片、删除卡片的完整操作。
- 测试重点不是看控制台文字是否完全一样，而是检查 data/cards.txt 里最终落盘的数据是否符合预期。

[PowerShell 读法]
- `$变量名` 表示变量。
- `@(...)` 表示数组。
- 反引号 `` ` `` 放在行尾表示“下一行仍然属于同一条命令”。
- `. "$PSScriptRoot\common.ps1"` 是 dot-source，引入 common.ps1 里的函数和脚本变量。
- `try/finally` 保证即使测试中途失败，也能恢复目录并清理临时文件。

[输入输出]
- 输入：`$inputLines` 里按顺序写好的菜单输入，每一项相当于用户在控制台敲一行。
- 输出：被测程序的控制台输出会写入 `$outputFile`，最终断言读取 users.txt/cards.txt。

[你以后最常改的地方]
- `$inputLines`：当菜单流程、选项编号、字段顺序变化时，主要改这里。
- 断言区的 `Get-RequiredLine` / `Assert-Equal`：当 cards.txt 字段顺序或预期结果变化时改这里。
- `$tmpRoot` 后缀：如果新增另一个 card 测试脚本，需要换一个临时目录名避免冲突。

[不建议随便改的地方]
- `$ErrorActionPreference = "Stop"`：它保证 PowerShell 错误会立刻让测试失败。
- `Push-Location` / `Pop-Location`：它保证测试始终从项目根目录运行并能恢复工作目录。
- `Remove-SafeDirectory`：清理临时目录时依赖 common.ps1 的路径安全保护。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；否则某些失败可能被继续跑下去。
$ErrorActionPreference = "Stop"

# 引入公共 helper：路径计算、编译运行程序、读取记录、断言函数都来自 common.ps1。
. "$PSScriptRoot\common.ps1"

# 为本测试创建独立临时目录；所有测试数据都放在这里，避免污染真实 data/。
# [可改] 如果复制本脚本做另一个卡片测试，应修改 "e2e_card_flow" 这个目录名。
$tmpRoot = Join-Path $script:E2ETmpParent "e2e_card_flow"

# 本次测试专用数据目录，运行程序时会通过 --data-dir 指向这里。
$dataDir = Join-Path $tmpRoot "data"

# 保存程序控制台输出的日志文件；测试失败时配合 -KeepTemp 查看。
$outputFile = Join-Path $tmpRoot "card_flow_output.log"

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
    # 两张卡片用于区分“被修改后删除”的记录和“仍保持 active”的记录。
    # [谨慎改] 改这里时不要随意删除空字符串；很多 "" 对应程序里的“按回车继续”。
    $inputLines = @(
        # 进入初始菜单：2 = 注册。
        "2"
        # 注册用户名。
        "v135_card_user"
        # 注册密码。
        "pass"
        # 确认密码。
        "pass"
        # 注册完成后的暂停回车。
        ""

        # 回到初始菜单：1 = 登录。
        "1"
        # 登录用户名。
        "v135_card_user"
        # 登录密码。
        "pass"
        # 登录完成后的暂停回车。
        ""

        # 主菜单：1 = 进入卡片管理分支。
        "1"
        # 卡片菜单：1 = 新增卡片。
        "1"
        # 第一张卡片 subject。
        "Math"
        # 第一张卡片 category。
        "Algebra"
        # 第一张卡片 title；后面会被修改成 Linear Basics Updated。
        "Linear Basics"
        # 第一张卡片 front/question。
        "What is slope?"
        # 第一张卡片 back/answer。
        "Rate of change"
        # 第一张卡片 tag。
        "linear"
        # 第一张卡片 difficulty。
        "2"
        # 新增完成后的暂停回车。
        ""

        # 卡片菜单：1 = 再新增一张卡片。
        "1"
        # 第二张卡片 subject。
        "Math"
        # 第二张卡片 category。
        "Geometry"
        # 第二张卡片 title；后面用它确认删除没有误伤其他卡片。
        "Triangle Sum"
        # 第二张卡片 front/question。
        "What is a triangle angle sum?"
        # 第二张卡片 back/answer。
        "180"
        # 第二张卡片 tag。
        "angle"
        # 第二张卡片 difficulty。
        "3"
        # 新增完成后的暂停回车。
        ""

        # 卡片菜单：9 = 查看/刷新当前卡片列表。
        "9"
        # 选择列表中的第 1 张卡片。
        "1"
        # 查看完成后的暂停回车。
        ""

        # 卡片菜单：2 = 修改卡片。
        "2"
        # 修改列表中的第 1 张卡片，即 Linear Basics。
        "1"
        # subject 留空，表示保持原值 Math。
        ""
        # category 留空，表示保持原值 Algebra。
        ""
        # title 改为 Linear Basics Updated。
        "Linear Basics Updated"
        # front/question 留空，表示保持原值。
        ""
        # back/answer 留空，表示保持原值。
        ""
        # tag 改为 linear-updated。
        "linear-updated"
        # difficulty 改为 4。
        "4"
        # 修改完成后的暂停回车。
        ""

        # 卡片菜单：5 = 关键字查询。
        "5"
        # 查询关键字 Updated，应能找到刚刚修改后的卡片。
        "Updated"
        # 选择查询结果中的第 1 条。
        "1"
        # 查询查看后的暂停回车。
        ""

        # 卡片菜单：6 = 分类查看。
        "6"
        # 选择分类列表中的第 1 类。
        "1"
        # 选择该分类下的第 1 张卡片。
        "1"
        # 查看后的暂停回车。
        ""
        # 返回/继续时的暂停回车。
        ""

        # 卡片菜单：7 = 排序查看。
        "7"
        # 排序方式：5 = 按脚本当前版本菜单定义的某个排序项。
        # [谨慎改] 如果程序排序菜单编号变化，需要同步改这个值。
        "5"
        # 排序结果查看后的暂停回车。
        ""
        # 返回/继续时的暂停回车。
        ""

        # 卡片菜单：8 = 多条件查询。
        "8"
        # 多条件 subject = Math。
        "Math"
        # 多条件 category 留空。
        ""
        # 多条件 keyword = Updated。
        "Updated"
        # 选择查询结果中的第 1 条。
        "1"
        # 查询查看后的暂停回车。
        ""

        # 卡片菜单：3 = 删除卡片。
        "3"
        # 删除列表中的第 1 张卡片，即修改后的 Linear Basics Updated。
        "1"
        # 确认删除：y = yes。
        "y"
        # 删除完成后的暂停回车。
        ""

        # 卡片菜单：0 = 返回主菜单。
        "0"
        # 主菜单：0 = 退出登录/返回初始菜单。
        "0"
        # 退出后的暂停回车。
        ""
        # 初始菜单：0 = 退出程序。
        "0"
    )

    # 把输入数组拼成一整段文本，每项之间用换行分隔。
    # 最后再补一个换行，模拟用户输入最后一项后按回车。
    $inputText = [string]::Join("`n", $inputLines) + "`n"

    # 打印当前测试步骤。
    Write-E2EStep "Running card branch flow"

    # 运行被测程序，把上面的模拟输入通过管道送进去，并把输出写到日志文件。
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "card branch flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    # 拼出本轮测试生成的 users.txt 路径。
    $usersFile = Join-Path $dataDir "users.txt"

    # 拼出本轮测试生成的 cards.txt 路径。
    $cardsFile = Join-Path $dataDir "cards.txt"

    # 从 users.txt 中找到刚才注册的用户记录。
    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and (Decode-StorageField $parts[1]) -eq "v135_card_user" } `
        -FailureMessage "card flow user not found"

    # users.txt 第 0 个字段是 userId；后面查 cards.txt 时用它限定“当前用户的卡片”。
    $userId = $userParts[0]

    # 从 cards.txt 中找到修改后的第一张卡片。
    $updatedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[1] -eq $script:userId -and (Decode-StorageField $parts[4]) -eq "Linear Basics Updated" } `
        -FailureMessage "updated card not found"

    # cards.txt 第 7 个字段是 tag；确认修改流程真的把 tag 写入了文件。
    Assert-Equal -Label "updated card tag" -Actual (Decode-StorageField $updatedCard[7]) -Expected "linear-updated"

    # cards.txt 第 8 个字段是 difficulty；确认难度从 2 改成了 4。
    Assert-Equal -Label "updated card difficulty" -Actual $updatedCard[8] -Expected "4"

    # cards.txt 第 16 个字段是 active；0 表示逻辑删除。
    Assert-Equal -Label "updated card deleted by branch flow" -Actual $updatedCard[16] -Expected "0"

    # 保留一张 active 卡片，确认删除操作没有误删当前用户其他卡片。
    $activeCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[1] -eq $script:userId -and (Decode-StorageField $parts[4]) -eq "Triangle Sum" -and $parts[16] -eq "1" } `
        -FailureMessage "remaining active card not found"

    # cards.txt 第 2 个字段是 subject；确认剩余卡片仍是 Math 学科。
    Assert-Equal -Label "remaining active card subject" -Actual (Decode-StorageField $activeCard[2]) -Expected "Math"

    # 再运行一次程序的数据自检模式，确认本轮生成的数据文件整体格式仍合法。
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "card flow data check"

    # 所有断言和数据自检都通过后，打印脚本级通过信息。
    Write-E2EStep "Card branch flow passed"
}
finally {
    # 离开测试时恢复进入脚本前的工作目录。
    Pop-Location

    # 默认删除临时目录；只有运行脚本时传 -KeepTemp 才保留现场。
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpRoot
    }
}
