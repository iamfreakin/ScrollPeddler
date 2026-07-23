<#
.SYNOPSIS
패키지 M1 적대적 멀티플레이 행렬을 실행하고 PR 첨부용 결과를 만든다.

.DESCRIPTION
1-4인, RTT 0/80/150ms, 패킷 손실 0/2% 조합의 listen server 24세션을 순차 실행한다.
이 스크립트가 시작하고 추적한 PID만 종료한다. 호스트 기동 또는 클라이언트 접속
인프라 실패만 한 번 재시도하며 게임 판정 실패는 재시도하지 않는다.

.PARAMETER PackageRoot
내부 ScrollPeddler.exe를 찾을 패키지 디렉터리다.

.PARAMETER Executable
직접 지정할 패키지 게임 실행 파일이며 PackageRoot 탐색보다 우선한다.

.PARAMETER RunId
명령행 메타데이터와 출력 디렉터리에 사용할 고유 ASCII 토큰이다.

.PARAMETER CommitSha
로그와 보고서에 기록할 commit 식별자다. 생략하면 현재 Git HEAD와 dirty 상태를 사용한다.

.PARAMETER Seed
fixture와 요청 ID를 결정적으로 재현할 양의 정수 seed다.

.PARAMETER BasePort
24세션과 인프라 재시도에 사용할 포트 범위의 시작값이다.

.PARAMETER SessionTimeoutSeconds
기동과 접속을 포함한 단일 세션 시도의 최대 시간이다.

.PARAMETER BootTimeoutSeconds
호스트 SP_ADV_SUITE_READY marker를 기다리는 최대 시간이다.

.PARAMETER JoinTimeoutSeconds
호스트 roster와 클라이언트 준비 marker를 기다리는 최대 시간이다.
#>
[CmdletBinding()]
param(
    [Parameter()]
    [string]$PackageRoot,

    [Parameter()]
    [string]$Executable,

    [Parameter()]
    [string]$RunId,

    [Parameter()]
    [string]$CommitSha,

    [Parameter()]
    [ValidateRange(1, 2147483647)]
    [int]$Seed = 1337,

    [Parameter()]
    [ValidateRange(1024, 65488)]
    [int]$BasePort = 17777,

    [Parameter()]
    [ValidateRange(10, 3600)]
    [int]$SessionTimeoutSeconds = 240,

    [Parameter()]
    [ValidateRange(5, 300)]
    [int]$BootTimeoutSeconds = 30,

    [Parameter()]
    [ValidateRange(5, 300)]
    [int]$JoinTimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = Join-Path $ProjectRoot 'Saved\Packages\Windows'
}
$ExpectedScenarios = @(
    'OutOfRange',
    'Obstructed',
    'InventoryFull',
    'InactivePlayer',
    'OwnershipSpoof',
    'ConcurrentClaim',
    'PickupReplay',
    'PickupRequestIdConflict',
    'UseNotOwned',
    'UseReplay',
    'PositiveSettlement'
)
$SinglePlayerSkippedScenarios = @('OwnershipSpoof', 'ConcurrentClaim')
$RttValues = @(0, 80, 150)
$LossValues = @(0, 2)

function Resolve-GameExecutable {
    param(
        [string]$ExplicitExecutable,
        [string]$Root
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitExecutable)) {
        if (-not (Test-Path -LiteralPath $ExplicitExecutable -PathType Leaf)) {
            throw "Executable을 찾을 수 없습니다: $ExplicitExecutable"
        }

        $ResolvedExecutable = (Resolve-Path -LiteralPath $ExplicitExecutable).Path
        if ([System.IO.Path]::GetExtension($ResolvedExecutable) -ine '.exe') {
            throw "Executable은 .exe 파일이어야 합니다: $ResolvedExecutable"
        }

        return $ResolvedExecutable
    }

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "PackageRoot를 찾을 수 없습니다: $Root"
    }

    $ResolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $PreferredCandidates = @(
        (Join-Path $ResolvedRoot 'ScrollPeddler\Binaries\Win64\ScrollPeddler.exe'),
        (Join-Path $ResolvedRoot 'Windows\ScrollPeddler\Binaries\Win64\ScrollPeddler.exe'),
        (Join-Path $ResolvedRoot 'ScrollPeddler.exe'),
        (Join-Path $ResolvedRoot 'Windows\ScrollPeddler.exe')
    )

    foreach ($Candidate in $PreferredCandidates) {
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }

    $RecursiveCandidates = @(
        Get-ChildItem -LiteralPath $ResolvedRoot -Filter 'ScrollPeddler.exe' -File -Recurse -ErrorAction SilentlyContinue
    )
    if ($RecursiveCandidates.Count -eq 1) {
        return $RecursiveCandidates[0].FullName
    }
    if ($RecursiveCandidates.Count -gt 1) {
        $CandidateList = ($RecursiveCandidates.FullName | Sort-Object) -join [Environment]::NewLine
        throw "PackageRoot 아래 실행 파일이 여러 개입니다. -Executable로 하나를 지정하세요.$([Environment]::NewLine)$CandidateList"
    }

    throw "PackageRoot 아래에서 ScrollPeddler.exe를 찾을 수 없습니다: $ResolvedRoot"
}

function ConvertTo-SafeToken {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $Trimmed = $Value.Trim()
    if ([string]::IsNullOrWhiteSpace($Trimmed)) {
        throw "$Name 값은 비어 있을 수 없습니다."
    }
    if ($Trimmed -notmatch '^[A-Za-z0-9._-]+$') {
        throw "$Name 값에는 영문자, 숫자, 점, 밑줄, 하이픈만 사용할 수 있습니다: $Trimmed"
    }

    return $Trimmed
}

function Resolve-CommitToken {
    param(
        [string]$ExplicitCommitSha,
        [Parameter(Mandatory = $true)][string]$Root
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitCommitSha)) {
        return ConvertTo-SafeToken -Value $ExplicitCommitSha -Name 'CommitSha'
    }

    $GitCommand = Get-Command git -ErrorAction SilentlyContinue
    if ($null -eq $GitCommand) {
        return 'unknown'
    }

    $Head = @(& $GitCommand.Source -C $Root rev-parse --short=12 HEAD 2>$null)
    if ($LASTEXITCODE -ne 0 -or $Head.Count -ne 1) {
        return 'unknown'
    }

    $Token = ([string]$Head[0]).Trim()
    $Dirty = @(& $GitCommand.Source -C $Root status --porcelain --untracked-files=no 2>$null)
    if ($LASTEXITCODE -eq 0 -and $Dirty.Count -gt 0) {
        $Token = "$Token-dirty"
    }
    return ConvertTo-SafeToken -Value $Token -Name 'CommitSha'
}

function ConvertTo-WindowsCommandLineArgument {
    param([Parameter(Mandatory = $true)][string]$Value)

    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') {
        return $Value
    }

    $Builder = New-Object System.Text.StringBuilder
    [void]$Builder.Append('"')
    $BackslashCount = 0
    foreach ($Character in $Value.ToCharArray()) {
        if ($Character -eq '\') {
            $BackslashCount++
            continue
        }

        if ($Character -eq '"') {
            [void]$Builder.Append(('\' * (($BackslashCount * 2) + 1)))
            [void]$Builder.Append('"')
            $BackslashCount = 0
            continue
        }

        if ($BackslashCount -gt 0) {
            [void]$Builder.Append(('\' * $BackslashCount))
            $BackslashCount = 0
        }
        [void]$Builder.Append($Character)
    }

    if ($BackslashCount -gt 0) {
        [void]$Builder.Append(('\' * ($BackslashCount * 2)))
    }
    [void]$Builder.Append('"')
    return $Builder.ToString()
}

function Start-GameProcess {
    param(
        [Parameter(Mandatory = $true)][string]$GameExecutable,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$Role,
        [Parameter(Mandatory = $true)][int]$ClientIndex,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    $ArgumentLine = ($Arguments | ForEach-Object {
        ConvertTo-WindowsCommandLineArgument -Value $_
    }) -join ' '

    $Process = Start-Process `
        -FilePath $GameExecutable `
        -ArgumentList $ArgumentLine `
        -PassThru `
        -WindowStyle Hidden

    return [pscustomobject]@{
        Role = $Role
        ClientIndex = $ClientIndex
        LogPath = $LogPath
        Process = $Process
        Pid = $Process.Id
    }
}

function Stop-TrackedProcesses {
    param([object[]]$TrackedProcesses)

    foreach ($Tracked in @($TrackedProcesses | Sort-Object ClientIndex -Descending)) {
        if ($null -eq $Tracked -or $null -eq $Tracked.Process) {
            continue
        }

        try {
            $Tracked.Process.Refresh()
            if (-not $Tracked.Process.HasExited) {
                Stop-Process -Id $Tracked.Pid -Force -ErrorAction Stop
            }
        }
        catch {
            Write-Warning "추적 PID $($Tracked.Pid) 종료 중 오류: $($_.Exception.Message)"
        }
    }

    foreach ($Tracked in @($TrackedProcesses)) {
        if ($null -eq $Tracked -or $null -eq $Tracked.Process) {
            continue
        }

        try {
            if (-not $Tracked.Process.HasExited) {
                [void]$Tracked.Process.WaitForExit(5000)
            }
            $Tracked.Process.Refresh()
        }
        catch {
            Write-Warning "추적 PID $($Tracked.Pid) 종료 확인 중 오류: $($_.Exception.Message)"
        }
    }
}

function Get-SharedLogText {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }

    try {
        $Stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite
        )
        try {
            $Reader = New-Object System.IO.StreamReader($Stream, [System.Text.Encoding]::UTF8, $true)
            try {
                return $Reader.ReadToEnd()
            }
            finally {
                $Reader.Dispose()
            }
        }
        finally {
            $Stream.Dispose()
        }
    }
    catch {
        return ''
    }
}

function Test-LogPattern {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$Paths,
        [Parameter(Mandatory = $true)][string]$Pattern
    )

    foreach ($Path in $Paths) {
        $Text = Get-SharedLogText -Path $Path
        if ($Text -match $Pattern) {
            return $true
        }
    }
    return $false
}

function Get-LogPatternCount {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern
    )

    $Text = Get-SharedLogText -Path $Path
    if ([string]::IsNullOrEmpty($Text)) {
        return 0
    }
    return [regex]::Matches($Text, $Pattern).Count
}

function Test-AnyProcessExited {
    param([object[]]$TrackedProcesses)

    foreach ($Tracked in @($TrackedProcesses)) {
        try {
            $Tracked.Process.Refresh()
            if ($Tracked.Process.HasExited) {
                return $true
            }
        }
        catch {
            return $true
        }
    }
    return $false
}

function Wait-ForLogPattern {
    param(
        [Parameter(Mandatory = $true)][string[]]$Paths,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][object[]]$TrackedProcesses,
        [Parameter(Mandatory = $true)][datetime]$Deadline
    )

    while ([datetime]::UtcNow -lt $Deadline) {
        if (Test-LogPattern -Paths $Paths -Pattern $Pattern) {
            return [pscustomobject]@{ Matched = $true; ProcessExited = $false }
        }
        if (Test-AnyProcessExited -TrackedProcesses $TrackedProcesses) {
            return [pscustomobject]@{ Matched = $false; ProcessExited = $true }
        }

        Start-Sleep -Milliseconds 250
    }

    return [pscustomobject]@{ Matched = $false; ProcessExited = $false }
}

function Get-MinimumDeadline {
    param(
        [Parameter(Mandatory = $true)][datetime]$First,
        [Parameter(Mandatory = $true)][datetime]$Second
    )

    if ($First -lt $Second) {
        return $First
    }
    return $Second
}

function Get-ProcessSnapshots {
    param([object[]]$TrackedProcesses)

    $Snapshots = @()
    foreach ($Tracked in @($TrackedProcesses | Sort-Object ClientIndex)) {
        $HasExited = $false
        $ExitCode = $null
        try {
            $Tracked.Process.Refresh()
            $HasExited = $Tracked.Process.HasExited
            if ($HasExited) {
                $ExitCode = $Tracked.Process.ExitCode
            }
        }
        catch {
            $HasExited = $true
        }

        $Snapshots += [pscustomobject]@{
            role = $Tracked.Role
            clientIndex = $Tracked.ClientIndex
            pid = $Tracked.Pid
            hasExited = $HasExited
            exitCode = $ExitCode
            logPath = $Tracked.LogPath
        }
    }
    return @($Snapshots)
}

function Get-CommonGameArguments {
    param(
        [Parameter(Mandatory = $true)][string]$SafeRunId,
        [Parameter(Mandatory = $true)][string]$CommitToken,
        [Parameter(Mandatory = $true)][int]$RunSeed,
        [Parameter(Mandatory = $true)][int]$PartySize,
        [Parameter(Mandatory = $true)][int]$RttMs,
        [Parameter(Mandatory = $true)][int]$LossPct,
        [Parameter(Mandatory = $true)][int]$ClientIndex,
        [Parameter(Mandatory = $true)][string]$ProfileSlot,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    $HalfRttMs = [int]($RttMs / 2)
    return @(
        '-unattended',
        '-nop4',
        '-nosplash',
        '-nullrhi',
        '-NoSound',
        '-NoSplash',
        '-NoCrashReports',
        '-forcelogflush',
        '-log',
        '-SPAdversarialSuite',
        "-SPAdversarialRunId=$SafeRunId",
        "-SPAdversarialCommitSha=$CommitToken",
        "-SPAdversarialSeed=$RunSeed",
        "-SPClientIndex=$ClientIndex",
        "-SPAdversarialPartySize=$PartySize",
        "-SPAdversarialRttMs=$RttMs",
        "-SPAdversarialLossPct=$LossPct",
        "-SPProfileSlot=$ProfileSlot",
        "-PktLag=$HalfRttMs",
        "-PktLoss=$LossPct",
        "-abslog=$LogPath"
    )
}

function Invoke-SessionAttempt {
    param(
        [Parameter(Mandatory = $true)][string]$GameExecutable,
        [Parameter(Mandatory = $true)][string]$SafeRunId,
        [Parameter(Mandatory = $true)][string]$CommitToken,
        [Parameter(Mandatory = $true)][int]$RunSeed,
        [Parameter(Mandatory = $true)][int]$SessionOrdinal,
        [Parameter(Mandatory = $true)][string]$SessionKey,
        [Parameter(Mandatory = $true)][string]$SessionDirectory,
        [Parameter(Mandatory = $true)][int]$PartySize,
        [Parameter(Mandatory = $true)][int]$RttMs,
        [Parameter(Mandatory = $true)][int]$LossPct,
        [Parameter(Mandatory = $true)][int]$AttemptNumber,
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][int]$AttemptTimeoutSeconds,
        [Parameter(Mandatory = $true)][int]$HostBootTimeoutSeconds,
        [Parameter(Mandatory = $true)][int]$RosterTimeoutSeconds
    )

    $AttemptDirectory = Join-Path $SessionDirectory ("attempt-{0}" -f $AttemptNumber)
    [void](New-Item -ItemType Directory -Path $AttemptDirectory -Force)
    $TrackedProcesses = @()
    $AttemptStartedAt = [datetime]::UtcNow
    $SessionDeadline = $AttemptStartedAt.AddSeconds($AttemptTimeoutSeconds)
    $InfraFailure = $false
    $InfraStage = $null
    $CompletionReason = $null

    try {
        $HostLogPath = Join-Path $AttemptDirectory 'host.log'
        $HostProfileSlot = "SPAdv_{0}_{1}_a{2}_c0" -f $SafeRunId, $SessionKey, $AttemptNumber
        $HostArguments = @(
            "/Game/Maps/TechSpike?listen?ExpectedPlayers=$PartySize",
            "-port=$Port"
        ) + (Get-CommonGameArguments `
            -SafeRunId $SafeRunId `
            -CommitToken $CommitToken `
            -RunSeed $RunSeed `
            -PartySize $PartySize `
            -RttMs $RttMs `
            -LossPct $LossPct `
            -ClientIndex 0 `
            -ProfileSlot $HostProfileSlot `
            -LogPath $HostLogPath)

        try {
            $HostProcess = Start-GameProcess `
                -GameExecutable $GameExecutable `
                -Arguments $HostArguments `
                -Role 'host' `
                -ClientIndex 0 `
                -LogPath $HostLogPath
            $TrackedProcesses += $HostProcess
        }
        catch {
            $InfraFailure = $true
            $InfraStage = 'boot'
            $CompletionReason = "host_start_failed: $($_.Exception.Message)"
            return [pscustomobject]@{
                attempt = $AttemptNumber
                port = $Port
                startedAtUtc = $AttemptStartedAt.ToString('o')
                finishedAtUtc = [datetime]::UtcNow.ToString('o')
                infrastructureFailure = $InfraFailure
                infrastructureStage = $InfraStage
                completionReason = $CompletionReason
                attemptDirectory = $AttemptDirectory
                processes = @()
            }
        }

        $BootDeadline = Get-MinimumDeadline `
            -First $SessionDeadline `
            -Second ([datetime]::UtcNow.AddSeconds($HostBootTimeoutSeconds))
        $BootWait = Wait-ForLogPattern `
            -Paths @($HostLogPath) `
            -Pattern 'SP_ADV_SUITE_READY\s+role=host(?:\s|$)' `
            -TrackedProcesses @($HostProcess) `
            -Deadline $BootDeadline
        if (-not $BootWait.Matched) {
            $InfraFailure = $true
            $InfraStage = 'boot'
            if ($BootWait.ProcessExited) {
                $CompletionReason = 'host_exited_before_ready'
            }
            else {
                $CompletionReason = 'host_ready_timeout'
            }
        }

        if (-not $InfraFailure) {
            for ($ClientIndex = 1; $ClientIndex -lt $PartySize; $ClientIndex++) {
                $ClientLogPath = Join-Path $AttemptDirectory ("client-{0:d2}.log" -f $ClientIndex)
                $ClientProfileSlot = "SPAdv_{0}_{1}_a{2}_c{3}" -f `
                    $SafeRunId, $SessionKey, $AttemptNumber, $ClientIndex
                $ClientArguments = @("127.0.0.1:$Port") + (Get-CommonGameArguments `
                    -SafeRunId $SafeRunId `
                    -CommitToken $CommitToken `
                    -RunSeed $RunSeed `
                    -PartySize $PartySize `
                    -RttMs $RttMs `
                    -LossPct $LossPct `
                    -ClientIndex $ClientIndex `
                    -ProfileSlot $ClientProfileSlot `
                    -LogPath $ClientLogPath)

                try {
                    $ClientProcess = Start-GameProcess `
                        -GameExecutable $GameExecutable `
                        -Arguments $ClientArguments `
                        -Role 'client' `
                        -ClientIndex $ClientIndex `
                        -LogPath $ClientLogPath
                    $TrackedProcesses += $ClientProcess
                }
                catch {
                    $InfraFailure = $true
                    $InfraStage = 'join'
                    $CompletionReason = "client_${ClientIndex}_start_failed: $($_.Exception.Message)"
                    break
                }
            }
        }

        if (-not $InfraFailure) {
            $RosterDeadline = Get-MinimumDeadline `
                -First $SessionDeadline `
                -Second ([datetime]::UtcNow.AddSeconds($RosterTimeoutSeconds))
            $RosterWait = Wait-ForLogPattern `
                -Paths @($HostLogPath) `
                -Pattern 'SP_ADV_ROSTER_READY(?:\s|$)' `
                -TrackedProcesses $TrackedProcesses `
                -Deadline $RosterDeadline
            if (-not $RosterWait.Matched) {
                $InfraFailure = $true
                $InfraStage = 'join'
                if ($RosterWait.ProcessExited) {
                    $CompletionReason = 'process_exited_before_roster_ready'
                }
                else {
                    $CompletionReason = 'roster_ready_timeout'
                }
            }

            if (-not $InfraFailure -and $PartySize -gt 1) {
                foreach ($TrackedClient in @($TrackedProcesses | Where-Object {
                    $_.Role -ceq 'client'
                })) {
                    $ClientReadyWait = Wait-ForLogPattern `
                        -Paths @($TrackedClient.LogPath) `
                        -Pattern 'SP_ADV_SUITE_READY\s+role=client(?:\s|$)' `
                        -TrackedProcesses @($TrackedClient) `
                        -Deadline $RosterDeadline
                    if (-not $ClientReadyWait.Matched) {
                        $InfraFailure = $true
                        $InfraStage = 'join'
                        if ($ClientReadyWait.ProcessExited) {
                            $CompletionReason =
                                "client_$($TrackedClient.ClientIndex)_exited_before_ready"
                        }
                        else {
                            $CompletionReason =
                                "client_$($TrackedClient.ClientIndex)_ready_timeout"
                        }
                        break
                    }
                }
            }
        }

        if (-not $InfraFailure) {
            while ([datetime]::UtcNow -lt $SessionDeadline) {
                if (Test-LogPattern `
                    -Paths @($HostLogPath) `
                    -Pattern 'SP_ADV_SUITE_RESULT(?:\s|$)') {
                    $CompletionReason = 'suite_result_observed'
                    break
                }

                try {
                    $HostProcess.Process.Refresh()
                    if ($HostProcess.Process.HasExited) {
                        $CompletionReason = 'host_exited_before_suite_result'
                        break
                    }
                }
                catch {
                    $CompletionReason = 'host_process_unavailable'
                    break
                }

                Start-Sleep -Milliseconds 250
            }

            if ([string]::IsNullOrWhiteSpace($CompletionReason)) {
                $CompletionReason = 'session_timeout'
            }
        }

        $Snapshots = Get-ProcessSnapshots -TrackedProcesses $TrackedProcesses
        return [pscustomobject]@{
            attempt = $AttemptNumber
            port = $Port
            startedAtUtc = $AttemptStartedAt.ToString('o')
            finishedAtUtc = [datetime]::UtcNow.ToString('o')
            infrastructureFailure = $InfraFailure
            infrastructureStage = $InfraStage
            completionReason = $CompletionReason
            attemptDirectory = $AttemptDirectory
            processes = @($Snapshots)
        }
    }
    finally {
        Stop-TrackedProcesses -TrackedProcesses $TrackedProcesses
    }
}

function ConvertFrom-KeyValuePayload {
    param([Parameter(Mandatory = $true)][string]$Payload)

    $Fields = [ordered]@{}
    $DuplicateKeys = @()
    foreach ($Match in [regex]::Matches(
        $Payload,
        '(?<key>[A-Za-z_][A-Za-z0-9_]*)=(?<value>\S+)'
    )) {
        $Key = $Match.Groups['key'].Value
        $Value = $Match.Groups['value'].Value
        if ($Fields.Contains($Key)) {
            $DuplicateKeys += $Key
        }
        else {
            $Fields[$Key] = $Value
        }
    }

    return [pscustomobject]@{
        fields = $Fields
        duplicateKeys = @($DuplicateKeys)
    }
}

function Get-AdversarialLogRecords {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$LogPaths
    )

    $ActionRecords = @()
    $CaseRecords = @()
    $SuiteRecords = @()
    foreach ($LogPath in $LogPaths) {
        $Text = Get-SharedLogText -Path $LogPath
        foreach ($Match in [regex]::Matches(
            $Text,
            '(?m)SP_ADV_ACTION_RESULT\s+(?<payload>[^\r\n]*)'
        )) {
            $Parsed = ConvertFrom-KeyValuePayload -Payload $Match.Groups['payload'].Value.Trim()
            $ActionRecords += [pscustomobject]@{
                sourceLog = $LogPath
                raw = $Match.Value.Trim()
                fields = $Parsed.fields
                duplicateKeys = @($Parsed.duplicateKeys)
            }
        }
        foreach ($Match in [regex]::Matches(
            $Text,
            '(?m)SP_ADV_CASE_RESULT\s+(?<payload>[^\r\n]*)'
        )) {
            $Parsed = ConvertFrom-KeyValuePayload -Payload $Match.Groups['payload'].Value.Trim()
            $CaseRecords += [pscustomobject]@{
                sourceLog = $LogPath
                raw = $Match.Value.Trim()
                fields = $Parsed.fields
                duplicateKeys = @($Parsed.duplicateKeys)
            }
        }
        foreach ($Match in [regex]::Matches(
            $Text,
            '(?m)SP_ADV_SUITE_RESULT\s+(?<payload>[^\r\n]*)'
        )) {
            $Parsed = ConvertFrom-KeyValuePayload -Payload $Match.Groups['payload'].Value.Trim()
            $SuiteRecords += [pscustomobject]@{
                sourceLog = $LogPath
                raw = $Match.Value.Trim()
                fields = $Parsed.fields
                duplicateKeys = @($Parsed.duplicateKeys)
            }
        }
    }

    return [pscustomobject]@{
        actions = @($ActionRecords)
        cases = @($CaseRecords)
        suites = @($SuiteRecords)
    }
}

function Test-IntegerField {
    param(
        [System.Collections.IDictionary]$Fields,
        [string]$Name,
        [ref]$Value
    )

    if (-not $Fields.Contains($Name)) {
        return $false
    }

    $ParsedValue = 0
    if (-not [int]::TryParse([string]$Fields[$Name], [ref]$ParsedValue)) {
        return $false
    }

    $Value.Value = $ParsedValue
    return $true
}

function Test-RequiredMetadata {
    param(
        [System.Collections.IDictionary]$Fields,
        [string]$FieldName,
        [string]$ExpectedValue,
        [string]$Context,
        [System.Collections.Generic.List[string]]$Errors
    )

    if (-not $Fields.Contains($FieldName)) {
        $Errors.Add("$Context metadata is missing: $FieldName")
        return
    }
    if ([string]$Fields[$FieldName] -cne $ExpectedValue) {
        $Errors.Add(
            "$Context metadata mismatch: $FieldName=$($Fields[$FieldName]), expected=$ExpectedValue"
        )
    }
}

function Test-RequiredField {
    param(
        [System.Collections.IDictionary]$Fields,
        [string]$FieldName,
        [string]$Context,
        [System.Collections.Generic.List[string]]$Errors
    )

    if (-not $Fields.Contains($FieldName) -or
        [string]::IsNullOrWhiteSpace([string]$Fields[$FieldName])) {
        $Errors.Add("$Context field is missing or empty: $FieldName")
    }
}

function Test-SessionAssertions {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$LogPaths,
        [Parameter(Mandatory = $true)][string]$SafeRunId,
        [Parameter(Mandatory = $true)][string]$CommitToken,
        [Parameter(Mandatory = $true)][int]$RunSeed,
        [Parameter(Mandatory = $true)][int]$PartySize,
        [Parameter(Mandatory = $true)][int]$RttMs,
        [Parameter(Mandatory = $true)][int]$LossPct
    )

    $Records = Get-AdversarialLogRecords -LogPaths $LogPaths
    $Errors = New-Object 'System.Collections.Generic.List[string]'
    $NormalizedActions = @()
    $NormalizedCases = @()

    $HostLogPaths = @($LogPaths | Where-Object {
        [System.IO.Path]::GetFileName($_) -ceq 'host.log'
    })
    if ($HostLogPaths.Count -ne 1) {
        $Errors.Add("host log must appear exactly once; observed=$($HostLogPaths.Count)")
    }
    elseif (-not (Test-LogPattern -Paths $HostLogPaths -Pattern 'SP_ADV_ROSTER_READY(?:\s|$)')) {
        $Errors.Add('host log is missing SP_ADV_ROSTER_READY')
    }

    if ($PartySize -gt 1) {
        if ($LogPaths.Count -ne $PartySize) {
            $Errors.Add(
                "process log count mismatch: observed=$($LogPaths.Count), expected=$PartySize"
            )
        }

        $HalfRttMs = [int]($RttMs / 2)
        $ExpectedLagLog = [regex]::Escape("PktLag set to $HalfRttMs")
        $ExpectedLossLog = [regex]::Escape("PktLoss set to $LossPct")
        foreach ($LogPath in $LogPaths) {
            $LogName = [System.IO.Path]::GetFileName($LogPath)
            if ((Get-LogPatternCount -Path $LogPath -Pattern $ExpectedLagLog) -lt 1) {
                $Errors.Add("$LogName is missing network emulation log: PktLag set to $HalfRttMs")
            }
            if ((Get-LogPatternCount -Path $LogPath -Pattern $ExpectedLossLog) -lt 1) {
                $Errors.Add("$LogName is missing network emulation log: PktLoss set to $LossPct")
            }
        }
    }

    if ($HostLogPaths.Count -eq 1) {
        $SettlementPendingCount = Get-LogPatternCount `
            -Path $HostLogPaths[0] `
            -Pattern 'SP_SPIKE_SETTLEMENT_PENDING\s+'
        if ($SettlementPendingCount -ne 1) {
            $Errors.Add(
                "SP_SPIKE_SETTLEMENT_PENDING count mismatch: observed=$SettlementPendingCount, expected=1"
            )
        }

        $SettlementAckCount = Get-LogPatternCount `
            -Path $HostLogPaths[0] `
            -Pattern 'SP_SPIKE_SETTLEMENT_ACK\s+session='
        if ($SettlementAckCount -ne $PartySize) {
            $Errors.Add(
                "successful settlement ACK count mismatch: observed=$SettlementAckCount, expected=$PartySize"
            )
        }

        $SettlementCommittedCount = Get-LogPatternCount `
            -Path $HostLogPaths[0] `
            -Pattern 'SP_SPIKE_SETTLEMENT_COMMITTED\s+session='
        if ($SettlementCommittedCount -ne 1) {
            $Errors.Add(
                "SP_SPIKE_SETTLEMENT_COMMITTED count mismatch: " +
                "observed=$SettlementCommittedCount, expected=1"
            )
        }
        elseif ($SettlementAckCount -eq $PartySize) {
            $HostText = Get-SharedLogText -Path $HostLogPaths[0]
            $AckMatches = [regex]::Matches(
                $HostText,
                'SP_SPIKE_SETTLEMENT_ACK\s+session='
            )
            $CommittedMatch = [regex]::Match(
                $HostText,
                'SP_SPIKE_SETTLEMENT_COMMITTED\s+session='
            )
            if ($CommittedMatch.Index -lt $AckMatches[$AckMatches.Count - 1].Index) {
                $Errors.Add('settlement committed before the final successful ACK')
            }
        }
    }

    foreach ($Record in $Records.actions) {
        $Fields = $Record.fields
        $Scenario = if ($Fields.Contains('scenario_id')) {
            [string]$Fields['scenario_id']
        }
        else {
            ''
        }
        $Context = if ([string]::IsNullOrWhiteSpace($Scenario)) {
            'action without scenario_id'
        }
        else {
            "action $Scenario"
        }
        foreach ($DuplicateKey in $Record.duplicateKeys) {
            $Errors.Add("$Context duplicate field: $DuplicateKey")
        }

        Test-RequiredMetadata -Fields $Fields -FieldName 'run_id' -ExpectedValue $SafeRunId `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'commit_sha' -ExpectedValue $CommitToken `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'seed' -ExpectedValue ([string]$RunSeed) `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'party_size' -ExpectedValue ([string]$PartySize) `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'rtt_ms' -ExpectedValue ([string]$RttMs) `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'loss_pct' -ExpectedValue ([string]$LossPct) `
            -Context $Context -Errors $Errors
        foreach ($RequiredField in @(
            'build_version', 'session_id', 'scenario_id', 'client_id',
            'request_id', 'action', 'target_id', 'server_frame', 'result',
            'state_before', 'state_after', 'client_state', 'dispatched'
        )) {
            Test-RequiredField -Fields $Fields -FieldName $RequiredField `
                -Context $Context -Errors $Errors
        }

        $NormalizedActions += [pscustomobject]@{
            scenario = $Scenario
            fields = $Fields
            sourceLog = $Record.sourceLog
            raw = $Record.raw
        }
    }

    foreach ($Record in $Records.cases) {
        $Fields = $Record.fields
        $Scenario = if ($Fields.Contains('scenario')) { [string]$Fields['scenario'] } else { '' }
        $Context = if ([string]::IsNullOrWhiteSpace($Scenario)) { 'case without scenario' } else { "case $Scenario" }

        foreach ($DuplicateKey in $Record.duplicateKeys) {
            $Errors.Add("$Context duplicate field: $DuplicateKey")
        }

        $Passed = 0
        $Skipped = 0
        if ([string]::IsNullOrWhiteSpace($Scenario)) {
            $Errors.Add('case result is missing scenario')
        }
        if (-not (Test-IntegerField -Fields $Fields -Name 'passed' -Value ([ref]$Passed)) -or
            ($Passed -ne 0 -and $Passed -ne 1)) {
            $Errors.Add("$Context has invalid or missing passed")
        }
        if (-not (Test-IntegerField -Fields $Fields -Name 'skipped' -Value ([ref]$Skipped)) -or
            ($Skipped -ne 0 -and $Skipped -ne 1)) {
            $Errors.Add("$Context has invalid or missing skipped")
        }

        Test-RequiredMetadata -Fields $Fields -FieldName 'run_id' -ExpectedValue $SafeRunId `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'commit_sha' -ExpectedValue $CommitToken `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'seed' -ExpectedValue ([string]$RunSeed) `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'party_size' -ExpectedValue ([string]$PartySize) `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'rtt_ms' -ExpectedValue ([string]$RttMs) `
            -Context $Context -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'loss_pct' -ExpectedValue ([string]$LossPct) `
            -Context $Context -Errors $Errors
        foreach ($RequiredField in @(
            'build_version', 'session_id', 'server_frame',
            'result', 'state_before', 'state_after', 'detail'
        )) {
            Test-RequiredField -Fields $Fields -FieldName $RequiredField `
                -Context $Context -Errors $Errors
        }

        $NormalizedCases += [pscustomobject]@{
            scenario = $Scenario
            passed = $Passed
            skipped = $Skipped
            result = if ($Fields.Contains('result')) { [string]$Fields['result'] } else { $null }
            detail = if ($Fields.Contains('detail')) { [string]$Fields['detail'] } else { $null }
            sourceLog = $Record.sourceLog
            fields = $Fields
            raw = $Record.raw
        }
    }

    foreach ($ExpectedScenario in $ExpectedScenarios) {
        $MatchingCases = @($NormalizedCases | Where-Object { $_.scenario -ceq $ExpectedScenario })
        if ($MatchingCases.Count -ne 1) {
            $Errors.Add(
                "scenario $ExpectedScenario must appear exactly once; observed=$($MatchingCases.Count)"
            )
            continue
        }

        $Case = $MatchingCases[0]
        $ShouldSkip = $PartySize -eq 1 -and
            $SinglePlayerSkippedScenarios -ccontains $ExpectedScenario
        if ($ShouldSkip) {
            if ($Case.skipped -ne 1) {
                $Errors.Add("scenario $ExpectedScenario must be skipped for party_size=1")
            }
        }
        else {
            $ScenarioActions = @($NormalizedActions | Where-Object {
                $_.scenario -ceq $ExpectedScenario
            })
            if ($ScenarioActions.Count -lt 1) {
                $Errors.Add("scenario $ExpectedScenario is missing SP_ADV_ACTION_RESULT evidence")
            }
            if ($Case.skipped -ne 0) {
                $Errors.Add("scenario $ExpectedScenario was unexpectedly skipped")
            }
            if ($Case.passed -ne 1) {
                $Errors.Add("scenario $ExpectedScenario failed")
            }
        }
    }

    foreach ($UnknownCase in @($NormalizedCases | Where-Object {
        $ExpectedScenarios -cnotcontains $_.scenario
    })) {
        $Errors.Add("unexpected scenario result: $($UnknownCase.scenario)")
    }

    if ($NormalizedCases.Count -ne $ExpectedScenarios.Count) {
        $Errors.Add(
            "case result count mismatch: observed=$($NormalizedCases.Count), expected=$($ExpectedScenarios.Count)"
        )
    }

    $NormalizedSuites = @()
    foreach ($Record in $Records.suites) {
        $Fields = $Record.fields
        foreach ($DuplicateKey in $Record.duplicateKeys) {
            $Errors.Add("suite duplicate field: $DuplicateKey")
        }

        $SuitePassed = 0
        $CasesPassed = 0
        $CasesFailed = 0
        $CasesSkipped = 0
        if (-not (Test-IntegerField -Fields $Fields -Name 'passed' -Value ([ref]$SuitePassed)) -or
            ($SuitePassed -ne 0 -and $SuitePassed -ne 1)) {
            $Errors.Add('suite has invalid or missing passed')
        }
        if (-not (Test-IntegerField -Fields $Fields -Name 'cases_passed' -Value ([ref]$CasesPassed))) {
            $Errors.Add('suite has invalid or missing cases_passed')
        }
        if (-not (Test-IntegerField -Fields $Fields -Name 'cases_failed' -Value ([ref]$CasesFailed))) {
            $Errors.Add('suite has invalid or missing cases_failed')
        }
        if (-not (Test-IntegerField -Fields $Fields -Name 'cases_skipped' -Value ([ref]$CasesSkipped))) {
            $Errors.Add('suite has invalid or missing cases_skipped')
        }

        Test-RequiredMetadata -Fields $Fields -FieldName 'run_id' -ExpectedValue $SafeRunId `
            -Context 'suite' -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'commit_sha' -ExpectedValue $CommitToken `
            -Context 'suite' -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'seed' -ExpectedValue ([string]$RunSeed) `
            -Context 'suite' -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'party_size' -ExpectedValue ([string]$PartySize) `
            -Context 'suite' -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'rtt_ms' -ExpectedValue ([string]$RttMs) `
            -Context 'suite' -Errors $Errors
        Test-RequiredMetadata -Fields $Fields -FieldName 'loss_pct' -ExpectedValue ([string]$LossPct) `
            -Context 'suite' -Errors $Errors
        foreach ($RequiredField in @('build_version', 'session_id', 'server_frame')) {
            Test-RequiredField -Fields $Fields -FieldName $RequiredField `
                -Context 'suite' -Errors $Errors
        }

        $NormalizedSuites += [pscustomobject]@{
            passed = $SuitePassed
            casesPassed = $CasesPassed
            casesFailed = $CasesFailed
            casesSkipped = $CasesSkipped
            sourceLog = $Record.sourceLog
            fields = $Fields
            raw = $Record.raw
        }
    }

    if ($NormalizedSuites.Count -ne 1) {
        $Errors.Add("suite result must appear exactly once; observed=$($NormalizedSuites.Count)")
    }
    else {
        $Suite = $NormalizedSuites[0]
        $ExpectedPassedCount = if ($PartySize -eq 1) { 9 } else { 11 }
        $ExpectedSkippedCount = if ($PartySize -eq 1) { 2 } else { 0 }
        if ($Suite.passed -ne 1) {
            $Errors.Add('suite reported passed=0')
        }
        if ($Suite.casesPassed -ne $ExpectedPassedCount) {
            $Errors.Add(
                "suite cases_passed mismatch: observed=$($Suite.casesPassed), expected=$ExpectedPassedCount"
            )
        }
        if ($Suite.casesFailed -ne 0) {
            $Errors.Add("suite cases_failed must be 0; observed=$($Suite.casesFailed)")
        }
        if ($Suite.casesSkipped -ne $ExpectedSkippedCount) {
            $Errors.Add(
                "suite cases_skipped mismatch: observed=$($Suite.casesSkipped), expected=$ExpectedSkippedCount"
            )
        }
        if (($Suite.casesPassed + $Suite.casesFailed + $Suite.casesSkipped) -ne
            $ExpectedScenarios.Count) {
            $Errors.Add('suite case totals do not equal the expected scenario count')
        }
    }

    $PassedCount = @($NormalizedCases | Where-Object {
        $_.skipped -eq 0 -and $_.passed -eq 1
    }).Count
    $FailedCount = @($NormalizedCases | Where-Object {
        $_.skipped -eq 0 -and $_.passed -ne 1
    }).Count
    $SkippedCount = @($NormalizedCases | Where-Object { $_.skipped -eq 1 }).Count

    return [pscustomobject]@{
        passed = $Errors.Count -eq 0
        errors = @($Errors)
        actions = @($NormalizedActions)
        cases = @($NormalizedCases)
        suites = @($NormalizedSuites)
        counts = [pscustomobject]@{
            passed = $PassedCount
            failed = $FailedCount
            skipped = $SkippedCount
            observed = $NormalizedCases.Count
            expected = $ExpectedScenarios.Count
        }
    }
}

function Write-RunArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$OutputRoot,
        [Parameter(Mandatory = $true)][pscustomobject]$Summary
    )

    $SummaryPath = Join-Path $OutputRoot 'summary.json'
    $ReportPath = Join-Path $OutputRoot 'report.md'
    $Summary | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $SummaryPath -Encoding UTF8

    $Lines = New-Object 'System.Collections.Generic.List[string]'
    $Lines.Add('# M1 적대적 멀티플레이 테스트 보고서')
    $Lines.Add('')
    $Lines.Add("- Run ID: ``$($Summary.runId)``")
    $Lines.Add("- Commit: ``$($Summary.commitSha)``")
    $Lines.Add("- Seed: ``$($Summary.seed)``")
    $Lines.Add("- Executable: ``$($Summary.executable)``")
    $Lines.Add("- 시작: ``$($Summary.startedAtUtc)``")
    $Lines.Add("- 종료: ``$($Summary.finishedAtUtc)``")
    $Lines.Add("- 결과: **$($Summary.status)**")
    $Lines.Add("- 세션: $($Summary.sessionsPassed)/$($Summary.sessionsTotal) 통과")
    $Lines.Add('')
    $Lines.Add('## 행렬 결과')
    $Lines.Add('')
    $Lines.Add('| Party | RTT | Loss | Network | Port | Attempts | Cases P/F/S | Result |')
    $Lines.Add('|---:|---:|---:|:---:|---:|---:|:---:|:---:|')
    foreach ($Session in $Summary.sessions) {
        $NetworkLabel = if ($Session.partySize -eq 1) {
            'N/A'
        }
        else {
            "$($Session.rttMs)ms/$($Session.lossPct)%"
        }
        $Counts = $Session.assertions.counts
        $Lines.Add(
            "| $($Session.partySize) | $($Session.rttMs) ms | $($Session.lossPct)% | " +
            "$NetworkLabel | $($Session.port) | $($Session.attempts.Count) | " +
            "$($Counts.passed)/$($Counts.failed)/$($Counts.skipped) | $($Session.status) |"
        )
    }

    $FailedSessions = @($Summary.sessions | Where-Object { $_.status -ne 'Passed' })
    if ($FailedSessions.Count -gt 0) {
        $Lines.Add('')
        $Lines.Add('## 실패 상세')
        foreach ($Session in $FailedSessions) {
            $Lines.Add('')
            $Lines.Add("### ``$($Session.sessionKey)``")
            $Lines.Add('')
            foreach ($Attempt in $Session.attempts) {
                $Lines.Add(
                    "- attempt $($Attempt.attempt), port $($Attempt.port): " +
                    "$($Attempt.completionReason)"
                )
            }
            foreach ($AssertionError in $Session.assertions.errors) {
                $Lines.Add("- $AssertionError")
            }
        }
    }

    $Lines.Add('')
    $Lines.Add('## 로그')
    $Lines.Add('')
    foreach ($Session in $Summary.sessions) {
        $Lines.Add("- ``$($Session.sessionKey)``: ``$($Session.logDirectory)``")
    }

    $Lines | Set-Content -LiteralPath $ReportPath -Encoding UTF8
    return [pscustomobject]@{
        summaryPath = $SummaryPath
        reportPath = $ReportPath
    }
}

$GameExecutable = Resolve-GameExecutable -ExplicitExecutable $Executable -Root $PackageRoot
if ([string]::IsNullOrWhiteSpace($RunId)) {
    $RunId = 'adv-{0}-{1}' -f ([datetime]::UtcNow.ToString('yyyyMMdd-HHmmss')), $PID
}
$SafeRunId = ConvertTo-SafeToken -Value $RunId -Name 'RunId'
$SafeCommitSha = Resolve-CommitToken -ExplicitCommitSha $CommitSha -Root $ProjectRoot

$OutputRoot = Join-Path $ProjectRoot (Join-Path 'Saved\Adversarial' $SafeRunId)
if (Test-Path -LiteralPath $OutputRoot) {
    throw "동일한 RunId 결과 디렉터리가 이미 존재합니다: $OutputRoot"
}
[void](New-Item -ItemType Directory -Path $OutputRoot -Force)

$RunStartedAt = [datetime]::UtcNow
$SessionResults = @()
$SessionOrdinal = 0

Write-Host (
    "M1 적대적 멀티플레이 행렬 시작: " +
    "run_id=$SafeRunId commit_sha=$SafeCommitSha executable=$GameExecutable"
)

foreach ($PartySize in 1..4) {
    foreach ($RttMs in $RttValues) {
        foreach ($LossPct in $LossValues) {
            $SessionOrdinal++
            $SessionKey = 'p{0}-rtt{1}-loss{2}' -f $PartySize, $RttMs, $LossPct
            $SessionDirectory = Join-Path $OutputRoot $SessionKey
            [void](New-Item -ItemType Directory -Path $SessionDirectory -Force)
            $Attempts = @()
            $FinalAttempt = $null

            Write-Host "[$SessionOrdinal/24] $SessionKey 실행"
            foreach ($AttemptNumber in 1..2) {
                $Port = $BasePort + (($SessionOrdinal - 1) * 2) + ($AttemptNumber - 1)
                $Attempt = Invoke-SessionAttempt `
                    -GameExecutable $GameExecutable `
                    -SafeRunId $SafeRunId `
                    -CommitToken $SafeCommitSha `
                    -RunSeed $Seed `
                    -SessionOrdinal $SessionOrdinal `
                    -SessionKey $SessionKey `
                    -SessionDirectory $SessionDirectory `
                    -PartySize $PartySize `
                    -RttMs $RttMs `
                    -LossPct $LossPct `
                    -AttemptNumber $AttemptNumber `
                    -Port $Port `
                    -AttemptTimeoutSeconds $SessionTimeoutSeconds `
                    -HostBootTimeoutSeconds $BootTimeoutSeconds `
                    -RosterTimeoutSeconds $JoinTimeoutSeconds
                $Attempts += $Attempt
                $FinalAttempt = $Attempt

                if (-not $Attempt.infrastructureFailure -or $AttemptNumber -eq 2) {
                    break
                }

                Write-Warning (
                    "$SessionKey attempt $AttemptNumber 인프라 실패($($Attempt.infrastructureStage)); " +
                    '새 포트와 슬롯으로 한 번 재시도합니다.'
                )
            }

            $FinalLogPaths = @(
                Get-ChildItem -LiteralPath $FinalAttempt.attemptDirectory -Filter '*.log' -File `
                    -ErrorAction SilentlyContinue |
                    Sort-Object FullName |
                    Select-Object -ExpandProperty FullName
            )
            $Assertions = Test-SessionAssertions `
                -LogPaths $FinalLogPaths `
                -SafeRunId $SafeRunId `
                -CommitToken $SafeCommitSha `
                -RunSeed $Seed `
                -PartySize $PartySize `
                -RttMs $RttMs `
                -LossPct $LossPct

            $SessionPassed = -not $FinalAttempt.infrastructureFailure -and $Assertions.passed
            $SessionStatus = if ($SessionPassed) {
                'Passed'
            }
            elseif ($FinalAttempt.infrastructureFailure) {
                'InfrastructureFailed'
            }
            else {
                'AssertionFailed'
            }

            $SessionResults += [pscustomobject]@{
                sessionKey = $SessionKey
                partySize = $PartySize
                rttMs = $RttMs
                oneWayLagMs = [int]($RttMs / 2)
                lossPct = $LossPct
                networkApplied = $PartySize -gt 1
                port = $FinalAttempt.port
                status = $SessionStatus
                attempts = @($Attempts)
                assertions = $Assertions
                logDirectory = $SessionDirectory
            }

            Write-Host "[$SessionOrdinal/24] $SessionKey 결과: $SessionStatus"
        }
    }
}

$SessionsPassed = @($SessionResults | Where-Object { $_.status -eq 'Passed' }).Count
$RunFinishedAt = [datetime]::UtcNow
$Summary = [pscustomobject]@{
    schemaVersion = 1
    runId = $SafeRunId
    commitSha = $SafeCommitSha
    seed = $Seed
    executable = $GameExecutable
    packageRoot = $PackageRoot
    startedAtUtc = $RunStartedAt.ToString('o')
    finishedAtUtc = $RunFinishedAt.ToString('o')
    durationSeconds = [math]::Round(($RunFinishedAt - $RunStartedAt).TotalSeconds, 3)
    status = if ($SessionsPassed -eq 24) { 'Passed' } else { 'Failed' }
    sessionsTotal = 24
    sessionsPassed = $SessionsPassed
    sessionsFailed = 24 - $SessionsPassed
    sessions = @($SessionResults)
}

$Artifacts = Write-RunArtifacts -OutputRoot $OutputRoot -Summary $Summary
Write-Host "요약 JSON: $($Artifacts.summaryPath)"
Write-Host "PR 보고서: $($Artifacts.reportPath)"

if ($Summary.status -ne 'Passed') {
    exit 1
}
exit 0
