# param 是脚本入口参数声明；运行这个工具脚本时可以从命令行传这些参数。
param(
    # [可改] 被驱动的主程序路径；默认使用项目根目录下的 project1.exe。
    [string]$ExePath = ".\project1.exe",

    # [可改] 自动化输入文件路径；默认读取 tools/sample_login_zh_input.txt。
    [string]$InputFile = ".\tools\sample_login_zh_input.txt"
)

<#
[导读]
- 这个脚本用 UTF-8 文本文件驱动 project1.exe，主要用于手动复现中文输入/输出问题。
- 它不是严格的断言型测试：不会检查 users.txt/cards.txt，也不会判断业务结果是否正确。
- 它的价值是把“中文文本 -> PowerShell 管道 -> 原生 C++ exe”这条链路稳定跑起来。

[和 tests/test_chinese_input_flow.ps1 的区别]
- 本脚本：偏手动工具，只负责喂输入、显示输出、转发退出码。
- test_chinese_input_flow.ps1：偏自动化测试，会创建隔离 data 目录并断言中文字段落盘正确。

[输入输出]
- 输入：UTF-8 自动化输入文件，例如 tools/sample_login_zh_input.txt。
- 输出：project1.exe 的原始控制台输出，以及 project1.exe 的原始退出码。

[你以后最常改的地方]
- `$ExePath`：如果主程序 exe 名称或位置变化，可以改默认值。
- `$InputFile`：如果想用另一份中文菜单输入文件，可以改默认值或运行时传参。
- 输入文本文件本身：要保留空行，因为空行通常对应程序里的“按回车继续”。

[不建议随便改的地方]
- 三处 UTF-8 编码设置：中文通过 PowerShell 管道传给原生 exe 时很容易受默认编码影响。
- `exit $LASTEXITCODE`：它把 project1.exe 的退出码原样传出去，方便外层脚本判断成功/失败。
#>

# 让 PowerShell 遇到非终止错误时也停止执行；例如 Resolve-Path 找不到文件时直接失败。
$ErrorActionPreference = "Stop"

# $PSScriptRoot 是当前脚本所在目录：tools。
# Split-Path -Parent 取上一级目录，也就是项目根目录。
$projectRoot = Split-Path -Parent $PSScriptRoot

# 切换到项目根目录，让默认的 .\project1.exe 和 .\tools\sample_login_zh_input.txt 都能正确解析。
Push-Location $projectRoot

try {
    # 创建 UTF-8 无 BOM 编码对象；false 表示写出/读取时不主动带 BOM。
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)

    # PowerShell 管道向原生 exe 传中文时需要显式设置三处编码。
    # 输入编码：影响原生程序从 stdin 接收文本时如何解码。
    [Console]::InputEncoding = $utf8NoBom

    # 输出编码：影响原生程序输出中文时 PowerShell 控制台如何显示。
    [Console]::OutputEncoding = $utf8NoBom

    # PowerShell 管道编码：影响 `$inputText | & exe` 这条管道传输。
    $OutputEncoding = $utf8NoBom

    # Resolve-Path 把 exe 路径转成绝对路径；文件不存在会抛错。
    $resolvedExe = (Resolve-Path $ExePath).Path

    # Resolve-Path 把输入文件路径转成绝对路径；文件不存在会抛错。
    $resolvedInput = (Resolve-Path $InputFile).Path

    # 一次性读取完整输入文件。
    # 这里不用 Get-Content，是为了保留原始换行和空行，避免菜单输入错位。
    $inputText = [System.IO.File]::ReadAllText($resolvedInput, $utf8NoBom)

    # 把输入文本通过管道送给 C++ 程序，模拟用户逐行在控制台输入。
    $inputText | & $resolvedExe

    # 把 project1.exe 的退出码原样作为本脚本退出码。
    exit $LASTEXITCODE
}
finally {
    # 无论成功还是失败，都恢复调用脚本前的工作目录。
    Pop-Location
}
