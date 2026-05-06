param(
    [string]$ExePath = ".\project1.exe",
    [string]$InputFile = ".\tools\sample_login_zh_input.txt"
)

<#
脚本职责：
- 用 UTF-8 文本文件驱动 project1.exe，供手动复现中文输入问题。

关键约束：
- 该脚本不是断言型测试入口，只负责稳定设置 PowerShell 输入/输出编码并转发退出码。
- 输入文件需要保留菜单中的“按回车继续”空行，否则交互流程会错位。
#>

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot

try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    # PowerShell 管道向原生 exe 传中文时需要显式设置三处编码。
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom

    $resolvedExe = (Resolve-Path $ExePath).Path
    $resolvedInput = (Resolve-Path $InputFile).Path
    $inputText = [System.IO.File]::ReadAllText($resolvedInput, $utf8NoBom)

    $inputText | & $resolvedExe
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
