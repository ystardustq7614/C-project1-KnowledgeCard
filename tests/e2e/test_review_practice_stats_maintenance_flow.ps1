param(
    [string]$ExePath = ".\project1.exe",
    [switch]$SkipBuild,
    [switch]$KeepTemp
)

<#
[导读]
- 本脚本覆盖复习、练习、统计和维护分支的主要菜单路径。

[输入输出]
- 输入：预置 data/*.txt 和菜单输入序列。
- 输出：review_logs.txt、cards.txt、wrongs.txt 的断言结果。

[易错点]
- 预置数据用于缩短交互脚本，避免本 case 依赖卡片/错题录入流程。
- 自测练习不应写日志；日志断言只针对今日复习产生的 card/wrong 两条记录。
- 回收站恢复通过 active 字段断言，物理清理分支只走取消路径，避免破坏后续恢复断言。
#>

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\common.ps1"

$tmpRoot = Join-Path $script:E2ETmpParent "e2e_review_practice_stats_maintenance_flow"
$dataDir = Join-Path $tmpRoot "data"
$outputFile = Join-Path $tmpRoot "review_practice_stats_maintenance_output.log"

Push-Location $script:E2EProjectRoot
try {
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [Console]::InputEncoding = $utf8NoBom
    [Console]::OutputEncoding = $utf8NoBom
    $OutputEncoding = $utf8NoBom

    Resolve-E2EExecutable -ExePath $ExePath -SkipBuild:$SkipBuild

    Remove-SafeDirectory $tmpRoot
    Initialize-EmptyDataDir -DataDir $dataDir -Encoding $utf8NoBom

    $today = Get-Date -Format "yyyy-MM-dd"
    # 预置一条到期卡片、一条到期错题，以及各一条已删除记录用于维护模块。
    [System.IO.File]::WriteAllLines(
        (Join-Path $dataDir "users.txt"),
        @("1|v135_branch_user|pass|$today"),
        $utf8NoBom
    )
    [System.IO.File]::WriteAllLines(
        (Join-Path $dataDir "cards.txt"),
        @(
            "1|1|Math|Algebra|Due Card|Question one|Answer one|branch|2|50|0|0|1|$today||$today|1",
            "2|1|History|Archive|Deleted Card|Deleted question|Deleted answer|deleted|1|40|0|0|1|$today||$today|0"
        ),
        $utf8NoBom
    )
    [System.IO.File]::WriteAllLines(
        (Join-Path $dataDir "wrongs.txt"),
        @(
            "1|1|Math|Algebra|Due Wrong|Correct A|Wrong A|Reason A|calculation|-1|30|0|0|1|$today||$today|1",
            "2|1|Science|Deleted|Deleted Wrong|Correct D|Wrong D|Reason D|memory|-1|30|0|0|1|$today||$today|0"
        ),
        $utf8NoBom
    )

    # 输入序列依次进入今日复习、自测练习、统计分析和数据维护。
    $inputLines = @(
        "1",
        "v135_branch_user",
        "pass",
        "",
        "3",
        "1",
        "",
        "2",
        "",
        "3",
        "",
        "",
        "2",
        "",
        "3",
        "",
        "0",
        "4",
        "1",
        "1",
        "",
        "",
        "",
        "2",
        "",
        "",
        "",
        "",
        "0",
        "5",
        "1",
        "",
        "2",
        "",
        "3",
        "",
        "4",
        "",
        "5",
        "",
        "6",
        "",
        "7",
        "",
        "0",
        "6",
        "1",
        "",
        "2",
        "1",
        "",
        "3",
        "",
        "4",
        "1",
        "",
        "5",
        "n",
        "",
        "6",
        "",
        "0",
        "0",
        "",
        "0"
    )
    $inputText = [string]::Join("`n", $inputLines) + "`n"

    Write-E2EStep "Running review/practice/stats/maintenance branch flow"
    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir) `
        -ExpectedExitCode 0 `
        -Label "review practice stats maintenance branch flow" `
        -InputText $inputText `
        -OutputPath $outputFile

    $cardsFile = Join-Path $dataDir "cards.txt"
    $wrongsFile = Join-Path $dataDir "wrongs.txt"
    $logsFile = Join-Path $dataDir "review_logs.txt"

    $logLines = Get-RecordLines $logsFile
    Assert-Equal -Label "review log count" -Actual $logLines.Count -Expected 2
    Assert-True -Label "card review log exists" -Condition (($logLines | Where-Object { (Split-RecordLine $_)[3] -eq "card" }).Count -eq 1)
    Assert-True -Label "wrong review log exists" -Condition (($logLines | Where-Object { (Split-RecordLine $_)[3] -eq "wrong" }).Count -eq 1)

    $reviewedCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq "1" } `
        -FailureMessage "reviewed card not found"
    Assert-Equal -Label "card review count updated" -Actual $reviewedCard[10] -Expected "1"

    $reviewedWrong = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[0] -eq "1" } `
        -FailureMessage "reviewed wrong not found"
    Assert-Equal -Label "wrong review count updated" -Actual $reviewedWrong[11] -Expected "1"

    $restoredCard = Get-RequiredLine `
        -Path $cardsFile `
        -Predicate { param($parts) $parts.Count -ge 17 -and $parts[0] -eq "2" } `
        -FailureMessage "restored card not found"
    Assert-Equal -Label "deleted card restored" -Actual $restoredCard[16] -Expected "1"

    $restoredWrong = Get-RequiredLine `
        -Path $wrongsFile `
        -Predicate { param($parts) $parts.Count -ge 18 -and $parts[0] -eq "2" } `
        -FailureMessage "restored wrong not found"
    Assert-Equal -Label "deleted wrong restored" -Actual $restoredWrong[17] -Expected "1"

    Invoke-E2EProgram `
        -Arguments @("--data-dir", $dataDir, "--check-data") `
        -ExpectedExitCode 0 `
        -Label "review practice stats maintenance data check"

    Write-E2EStep "Review/practice/stats/maintenance branch flow passed"
}
finally {
    Pop-Location
    if (-not $KeepTemp) {
        Remove-SafeDirectory $tmpRoot
    }
}
