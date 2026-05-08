param(
    [string]$ExePath = ".\project1.exe",
    [switch]$SkipBuild,
    [switch]$KeepTemp
)

<#
脚本职责：
- 覆盖用户分支：注册、错误登录、正确登录、修改密码、旧密码失败、新密码成功。

关键约束：
- 断言以 users.txt 为准，确认改密真正持久化且未产生重复用户记录。
- 输入序列中的空字符串对应 pauseScreen 或返回菜单，不能随意删除。
#>

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$tmpRoot = Join-Path $script:E2ETmpParent "e2e_user_flow"
$dataDir = Join-Path $tmpRoot "data"
$outputFile = Join-Path $tmpRoot "user_flow_output.log"

Push-Location $script:E2EProjectRoot
try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom

    Resolve-E2EExecutable -ExePath $ExePath -SkipBuild:$SkipBuild

    Remove-SafeDirectory $tmpRoot
    Initialize-EmptyDataDir -DataDir $dataDir -Encoding $utf8NoBom

    # 菜单脚本先验证失败路径，再验证成功路径，防止只覆盖 happy path。
    $inputLines = @(
        "2",
        "v135_user",
        "initial_pass",
        "initial_pass",
        "",
        "1",
        "v135_user",
        "bad_pass",
        "",
        "1",
        "v135_user",
        "initial_pass",
        "",
        "7",
        "initial_pass",
        "updated_pass",
        "updated_pass",
        "",
        "0",
        "",
        "1",
        "v135_user",
        "initial_pass",
        "",
        "1",
        "v135_user",
        "updated_pass",
        "",
        "0",
        "",
        "0"
    )
    $inputText = [string]::Join("`n", $inputLines) + "`n"

    Write-E2EStep "Running user branch flow"
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "user branch flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    $usersFile = Join-Path $dataDir "users.txt"
    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and (Decode-StorageField $parts[1]) -eq "v135_user" } `
        -FailureMessage "v1.3.8 user not found"

    Assert-Equal -Label "user password changed" -Actual (Decode-StorageField $userParts[2]) -Expected "updated_pass"
    Assert-True -Label "single user record kept" -Condition ((Get-RecordLines $usersFile).Count -eq 1)

    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "user flow data check"

    Write-E2EStep "User branch flow passed"
}
finally {
    Pop-Location
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpRoot
    }
}
