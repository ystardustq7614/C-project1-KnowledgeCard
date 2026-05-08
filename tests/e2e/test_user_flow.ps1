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
- 这个脚本是“用户账号流程”的端到端回归测试。
- 它会模拟注册、错误密码登录、正确密码登录、修改密码、旧密码登录失败、新密码登录成功。
- 测试重点是检查 users.txt 的最终落盘结果，而不是逐字匹配控制台输出。

[PowerShell 读法]
- `$变量名` 表示变量。
- `@(...)` 表示数组。
- 反引号 `` ` `` 放在行尾表示“下一行仍然属于同一条命令”。
- `. "$PSScriptRoot\common.ps1"` 是 dot-source，引入 common.ps1 里的公共函数和脚本变量。
- `try/finally` 保证即使测试失败，也能恢复目录并按需清理临时文件。

[输入输出]
- 输入：`$inputLines` 里按顺序写好的菜单输入，每一项相当于用户在控制台敲一行。
- 输出：被测程序的控制台输出会写入 `$outputFile`，最终断言读取 users.txt。

[你以后最常改的地方]
- `$inputLines`：当欢迎菜单、主菜单、改密输入顺序或暂停回车数量变化时，主要改这里。
- 断言区的 `Get-RequiredLine` / `Assert-Equal` / `Assert-True`：当 users.txt 字段顺序或预期账号结果变化时改这里。
- `$tmpRoot` 后缀：如果新增另一个用户流程测试，需要换一个临时目录名避免冲突。

[不建议随便改的地方]
- `$ErrorActionPreference = "Stop"`：它保证 PowerShell 错误会立刻让测试失败。
- `Push-Location` / `Pop-Location`：它保证测试始终从项目根目录运行并能恢复工作目录。
- `Remove-SafeDirectory`：清理临时目录时依赖 common.ps1 的路径安全保护。

[易错点]
- 输入序列中的空字符串 `""` 多数对应 pauseScreen 的“按回车继续”，不能随意删除。
- 本脚本先跑失败路径，再跑成功路径，避免只覆盖 happy path。
- users.txt 第 2 个字段是 password；改密后必须是 updated_pass。
- 最终用户记录数必须仍为 1，防止注册/改密流程误写重复账号。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；否则某些失败可能被继续跑下去。
$ErrorActionPreference = "Stop"

# 引入公共 helper：路径计算、编译运行程序、读取记录、断言函数都来自 common.ps1。
. "$PSScriptRoot\common.ps1"

# 为本测试创建独立临时目录；所有测试数据都放在这里，避免污染真实 data/。
# [可改] 如果复制本脚本做另一个用户测试，应修改 "e2e_user_flow" 这个目录名。
$tmpRoot = Join-Path $script:E2ETmpParent "e2e_user_flow"

# 本次测试专用数据目录，运行程序时会通过 --data-dir 指向这里。
$dataDir = Join-Path $tmpRoot "data"

# 保存程序控制台输出的日志文件；测试失败时配合 -KeepTemp 查看。
$outputFile = Join-Path $tmpRoot "user_flow_output.log"

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

    # 菜单脚本先验证失败路径，再验证成功路径，防止只覆盖 happy path。
    # [谨慎改] 改这里时不要随意删除空字符串；很多 "" 对应程序里的“按回车继续”。
    $inputLines = @(
        # 初始菜单：2 = 用户注册。
        "2",
        # 注册用户名。
        "v135_user",
        # 注册密码。
        "initial_pass",
        # 确认注册密码。
        "initial_pass",
        # 注册成功后的暂停回车。
        "",

        # 初始菜单：1 = 用户登录。
        "1",
        # 登录用户名。
        "v135_user",
        # 故意输入错误密码，用来验证失败路径。
        "bad_pass",
        # 登录失败后的暂停回车。
        "",

        # 初始菜单：1 = 再次用户登录。
        "1",
        # 登录用户名。
        "v135_user",
        # 输入注册时的正确旧密码。
        "initial_pass",
        # 登录成功后的暂停回车；随后进入主菜单。
        "",

        # 主菜单：7 = 修改密码。
        "7",
        # 修改密码：输入旧密码。
        "initial_pass",
        # 修改密码：输入新密码。
        "updated_pass",
        # 修改密码：确认新密码。
        "updated_pass",
        # 修改成功后的暂停回车。
        "",

        # 主菜单：0 = 退出登录，回到初始菜单。
        "0",
        # 退出登录后的暂停回车。
        "",

        # 初始菜单：1 = 用户登录。
        "1",
        # 登录用户名。
        "v135_user",
        # 故意使用旧密码登录，应失败。
        "initial_pass",
        # 旧密码登录失败后的暂停回车。
        "",

        # 初始菜单：1 = 再次用户登录。
        "1",
        # 登录用户名。
        "v135_user",
        # 使用新密码登录，应成功。
        "updated_pass",
        # 新密码登录成功后的暂停回车；随后进入主菜单。
        "",

        # 主菜单：0 = 退出登录，回到初始菜单。
        "0",
        # 退出登录后的暂停回车。
        "",

        # 初始菜单：0 = 退出程序。
        "0"
    )

    # 把输入数组拼成一整段文本，每项之间用换行分隔。
    # 最后再补一个换行，模拟用户输入最后一项后按回车。
    $inputText = [string]::Join("`n", $inputLines) + "`n"

    # 打印当前测试步骤。
    Write-E2EStep "Running user branch flow"

    # 运行被测程序，把上面的模拟输入通过管道送进去，并把输出写到日志文件。
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "user branch flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    # 拼出本轮测试生成的 users.txt 路径。
    $usersFile = Join-Path $dataDir "users.txt"

    # 从 users.txt 中找到刚才注册的用户记录。
    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and (Decode-StorageField $parts[1]) -eq "v135_user" } `
        -FailureMessage "v1.4.0 user not found"

    # users.txt 第 2 个字段是 password；确认修改密码真正持久化到文件。
    Assert-Equal -Label "user password changed" -Actual (Decode-StorageField $userParts[2]) -Expected "updated_pass"

    # 确认整个流程没有生成重复用户记录；注册一次后应只有一行有效用户。
    Assert-True -Label "single user record kept" -Condition ((Get-RecordLines $usersFile).Count -eq 1)

    # 再运行一次程序的数据自检模式，确认本轮生成的数据文件整体格式仍合法。
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "user flow data check"

    # 所有断言和数据自检都通过后，打印脚本级通过信息。
    Write-E2EStep "User branch flow passed"
}
finally {
    # 离开测试时恢复进入脚本前的工作目录。
    Pop-Location

    # 默认删除临时目录；只有运行脚本时传 -KeepTemp 才保留现场。
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpRoot
    }
}
