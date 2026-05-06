param(
    [string]$ExePath = ".\project1.exe",
    [switch]$SkipBuild,
    [switch]$KeepTemp
)

<#
脚本职责：
- 覆盖错题分支：新增、查看、修改、查询、分类、多条件、错题转卡片、重复转卡保护和逻辑删除。

关键约束：
- 同一道错题重复执行转卡应只保留一个有效 linkedCardId，不应生成第二张关联卡。
- 断言同时检查 wrongs.txt 和 cards.txt，确保跨模块联动真实落盘。
#>

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$tmpRoot = Join-Path $script:E2ETmpParent "e2e_wrong_flow"
$dataDir = Join-Path $tmpRoot "data"
$outputFile = Join-Path $tmpRoot "wrong_flow_output.log"

Push-Location $script:E2EProjectRoot
try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom

    Resolve-E2EExecutable -ExePath $ExePath -SkipBuild:$SkipBuild

    Remove-SafeDirectory $tmpRoot
    Initialize-EmptyDataDir -DataDir $dataDir -Encoding $utf8NoBom

    # 第一道错题走修改、转卡、重复转卡和删除；第二道错题用于确认其他记录仍 active。
    $inputLines = @(
        "2",
        "v135_wrong_user",
        "pass",
        "pass",
        "",
        "1",
        "v135_wrong_user",
        "pass",
        "",
        "2",
        "1",
        "Math",
        "Algebra",
        "Original wrong question",
        "x=2",
        "x=3",
        "sign mistake",
        "5",
        "",
        "1",
        "Physics",
        "Mechanics",
        "Second wrong question",
        "F=ma",
        "F=m/a",
        "formula confusion",
        "2",
        "",
        "9",
        "1",
        "",
        "2",
        "1",
        "",
        "",
        "Updated wrong question",
        "",
        "",
        "",
        "n",
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
        "Math",
        "",
        "Updated",
        "1",
        "",
        "8",
        "1",
        "",
        "8",
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

    Write-E2EStep "Running wrong branch flow"
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "wrong branch flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    $usersFile = Join-Path $dataDir "users.txt"
    $cardsFile = Join-Path $dataDir "cards.txt"
    $wrongsFile = Join-Path $dataDir "wrongs.txt"
    $userParts = Get-RequiredLine `
        -Path $usersFile `
        -Predicate { param($parts) $parts.Count -ge 4 -and (Decode-StorageField $parts[1]) -eq "v135_wrong_user" } `
        -FailureMessage "wrong flow user not found"
    $userId = $userParts[0]

    $updatedWrong = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[1] -eq $script:userId -and (Decode-StorageField $parts[4]) -eq "Updated wrong question" } `
        -FailureMessage "updated wrong not found"
    Assert-True -Label "updated wrong converted once" -Condition ($updatedWrong[9] -ne "-1")
    Assert-Equal -Label "updated wrong deleted by branch flow" -Actual $updatedWrong[17] -Expected "0"

    # linkedCardId 指向的卡片必须存在且 active，才能证明错题转卡片链路完整。
    $convertedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq $script:updatedWrong[9] -and $parts[1] -eq $script:userId -and $parts[16] -eq "1" } `
        -FailureMessage "converted card from wrong flow not found"
    Assert-Equal -Label "converted card front from wrong" -Actual (Decode-StorageField $convertedCard[5]) -Expected "Updated wrong question"

    $remainingWrong = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[1] -eq $script:userId -and (Decode-StorageField $parts[4]) -eq "Second wrong question" -and $parts[17] -eq "1" } `
        -FailureMessage "remaining active wrong not found"
    Assert-Equal -Label "remaining wrong subject" -Actual (Decode-StorageField $remainingWrong[2]) -Expected "Physics"

    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "wrong flow data check"

    Write-E2EStep "Wrong branch flow passed"
}
finally {
    Pop-Location
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpRoot
    }
}
