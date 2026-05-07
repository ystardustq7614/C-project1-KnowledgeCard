param(
    [string]$ExePath = ".\project1.exe",
    [switch]$SkipBuild,
    [switch]$KeepTemp
)

<#
[导读]
- 本脚本覆盖卡片分支：新增、查看、修改、关键字查询、分类查看、排序查看、多条件查询和逻辑删除。

[输入输出]
- 输入：脚本生成的菜单输入序列。
- 输出：cards.txt 字段断言和 case 退出码。

[易错点]
- 断言使用 cards.txt 字段确认修改和删除状态，不依赖控制台输出文本。
- 测试通过展示序号操作卡片，保护 currentCardMap 映射相关流程。
#>

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$tmpRoot = Join-Path $script:E2ETmpParent "e2e_card_flow"
$dataDir = Join-Path $tmpRoot "data"
$outputFile = Join-Path $tmpRoot "card_flow_output.log"

Push-Location $script:E2EProjectRoot
try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom

    Resolve-E2EExecutable -ExePath $ExePath -SkipBuild:$SkipBuild

    Remove-SafeDirectory $tmpRoot
    Initialize-EmptyDataDir -DataDir $dataDir -Encoding $utf8NoBom

    # 两张卡片用于区分“被修改后删除”的记录和“仍保持 active”的记录。
    $inputLines = @(
        "2",
        "v135_card_user",
        "pass",
        "pass",
        "",
        "1",
        "v135_card_user",
        "pass",
        "",
        "1",
        "1",
        "Math",
        "Algebra",
        "Linear Basics",
        "What is slope?",
        "Rate of change",
        "linear",
        "2",
        "",
        "1",
        "Math",
        "Geometry",
        "Triangle Sum",
        "What is a triangle angle sum?",
        "180",
        "angle",
        "3",
        "",
        "9",
        "1",
        "",
        "2",
        "1",
        "",
        "",
        "Linear Basics Updated",
        "",
        "",
        "linear-updated",
        "4",
        "",
        "5",
        "Updated",
        "1",
        "",
        "6",
        "1",
        "1",
        "",
        "",
        "7",
        "5",
        "",
        "",
        "8",
        "Math",
        "",
        "Updated",
        "1",
        "",
        "3",
        "1",
        "y",
        "",
        "0",
        "0",
        "",
        "0"
    )
    $inputText = [string]::Join("`n", $inputLines) + "`n"

    Write-E2EStep "Running card branch flow"
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "card branch flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    $usersFile = Join-Path $dataDir "users.txt"
    $cardsFile = Join-Path $dataDir "cards.txt"
    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and (Decode-StorageField $parts[1]) -eq "v135_card_user" } `
        -FailureMessage "card flow user not found"
    $userId = $userParts[0]

    $updatedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[1] -eq $script:userId -and (Decode-StorageField $parts[4]) -eq "Linear Basics Updated" } `
        -FailureMessage "updated card not found"
    Assert-Equal -Label "updated card tag" -Actual (Decode-StorageField $updatedCard[7]) -Expected "linear-updated"
    Assert-Equal -Label "updated card difficulty" -Actual $updatedCard[8] -Expected "4"
    Assert-Equal -Label "updated card deleted by branch flow" -Actual $updatedCard[16] -Expected "0"

    # 保留一张 active 卡片，确认删除操作没有误删当前用户其他卡片。
    $activeCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[1] -eq $script:userId -and (Decode-StorageField $parts[4]) -eq "Triangle Sum" -and $parts[16] -eq "1" } `
        -FailureMessage "remaining active card not found"
    Assert-Equal -Label "remaining active card subject" -Actual (Decode-StorageField $activeCard[2]) -Expected "Math"

    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "card flow data check"

    Write-E2EStep "Card branch flow passed"
}
finally {
    Pop-Location
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpRoot
    }
}
