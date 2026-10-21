[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Low')]
param([switch]$Run224018, [switch]$Run042142, [switch]$Run062106)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
if (($Run224018 -and $Run042142) -or ($Run224018 -and $Run062106) -or ($Run042142 -and $Run062106)) {
    throw 'Run224018, Run042142, and Run062106 are mutually exclusive.'
}

function Get-GpuFinalFirmwarePin {
    param([Parameter(Mandatory = $true)][string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "Expected a regular, non-reparse file: $Path"
    }
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
    return [pscustomobject]@{ Path = [IO.Path]::GetFullPath($Path); Sha256 = $hash; Bytes = [long]$item.Length }
}

function Assert-GpuFinalFirmwarePin {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Sha256,
          [Parameter(Mandatory = $true)][long]$Bytes, [Parameter(Mandatory = $true)][string]$Description)
    $actual = Get-GpuFinalFirmwarePin -Path $Path
    if ($actual.Bytes -ne $Bytes -or $actual.Sha256 -cne $Sha256) {
        throw "$Description differs from its reviewed SHA-256/length pin."
    }
    return $actual
}

function Get-GpuFinalFirmwareInspectionProfile {
    [CmdletBinding()]
    param([switch]$Run224018, [switch]$Run042142, [switch]$Run062106)

    if (($Run224018 -and $Run042142) -or ($Run224018 -and $Run062106) -or ($Run042142 -and $Run062106)) {
        throw 'Run224018, Run042142, and Run062106 are mutually exclusive.'
    }

    if ($Run224018) {
        return [pscustomobject]@{
            Name = '224018'
            RunId = 'gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1'
            SourceCommit = '69a3c3d94f33d1903778ef5ec5b0a1f015b1be29'
            FailedAction = '20261020T225933250Z-FinalVerify-cf3ab7b1e838427c964640d87f30257e'
            BackupAction = '20261020T224257466Z-Backup-8bf99023efa4478e91e8fe626493da70'
            BaselineLeaf = 'firmware-enumeration-backup-99b1a3488d3d47ba93a25d46ae052d2d.stdout.txt'
            CurrentLeaf = 'firmware-enumeration-final-bd819c45c1e94e01b00b44dafe51c163.stdout.txt'
            BaselineSha256 = '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'
            CurrentSha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
            BaselineBytes = 4336L
            CurrentBytes = 2890L
            FinalResultLeaf = $null
            FinalResultSha256 = $null
            FinalResultBytes = 0L
            DisarmedBootId = '2026-10-20T22:45:43.5000000Z'
            CurrentBootId = '2026-10-20T22:51:15.5000000Z'
            SelectorSha256 = 'A996B969394113B052010129563BF3EAE35443B0B452B0C66D0B7B69FF9D75B3'
            SelectorBytes = 533L
            JournalSha256 = 'A1513F12AFCD2932E4DE2D9A6069C7A2CED6F969BF1AC82E6942D6818F319E07'
            JournalBytes = 45568L
            ReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
            ReferenceLogBytes = 40838L
            ProbeRelativePath = '.codex-build\gb\a68d89b7b3ecb1a1\l\ib-tcg-dump.exe'
            ProbeSha256 = '76828AFED037973FBB4357AD89C97FB65D85D3BCF7811C9281F199ECB65A4583'
            ProbeBytes = 172544L
            AuditSha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
            AuditBytes = 246784L
            StateModuleSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
            StateModuleBytes = 19843L
            PlatformModuleSha256 = '4276ED335283E2AD48E5D0426B8D4857063C2B9ADBFE1DC41693A0BE64BB9B51'
            PlatformModuleBytes = 111525L
            RetirementModuleSha256 = 'DF7F446426F63E21C5596E9CD1B35ECA0172C182B3E802B6267962B3EF8B1D75'
            RetirementModuleBytes = 105056L
        }
    }

    if ($Run042142) {
        return [pscustomobject]@{
            Name = '042142'
            RunId = 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2'
            SourceCommit = 'a52aa25e918223abae295876800933df9b642e46'
            FailedAction = '20261021T044758857Z-FinalVerify-a88b0158befb4af49d9643cb14da1a1c'
            FailedActionStatus = 'FINAL_VERIFY_FAILED_GPU_UNATTESTED'
            BackupAction = '20261021T042510110Z-Backup-b0ad2425a4cd436fa583d76122c8afe1'
            BaselineLeaf = 'firmware-enumeration-backup-297ad383796447e38ac14cc7e3ba5879.stdout.txt'
            CurrentLeaf = 'firmware-enumeration-final-0b0629c3908d46e98c61bf9462991cd8.stdout.txt'
            FinalResultLeaf = 'result.json'
            BaselineSha256 = '66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007'
            CurrentSha256 = '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'
            BaselineBytes = 2890L
            CurrentBytes = 4336L
            FinalResultSha256 = '885BC6B54E13815762C85D9BF8ED60AB4C11762A3E584F143BFAAB52E6C72B69'
            FinalResultBytes = 2285L
            DisarmedBootId = '2026-10-21T04:30:43.5000000Z'
            CurrentBootId = '2026-10-21T04:46:50.5000000Z'
            SelectorSha256 = 'B4CF6844894A920912EFF85B80012ED3B0B5504D7BD01341A531316C8FC8424C'
            SelectorBytes = 533L
            JournalSha256 = '3287B5E86D018DD374748D56E16CF7BBDE0D245ECF426779EAAB80E5A2B08583'
            JournalBytes = 44133L
            ReferenceLogSha256 = '22B1F571E7572C1D1448B14504AF18435EB487B39DB9E87C9592A95432A5C447'
            ReferenceLogBytes = 40838L
            ProbeRelativePath = '.codex-build\gb\840ce906358bbac2\l\ib-tcg-dump.exe'
            ProbeSha256 = '86D15C1E8B29A7896FC8D4A9AA14ECD926DA23D8EED2871DBA65DDDA7002BD36'
            ProbeBytes = 172544L
            AuditSha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
            AuditBytes = 246784L
            StateModuleSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
            StateModuleBytes = 19843L
            PlatformModuleSha256 = 'D30A1A38BF5B9CAD07F43C237599847A8D5ECF0825D7D3552A92F599801C543F'
            PlatformModuleBytes = 111625L
            RetirementModuleSha256 = '395101C30399FD96C2404BAA049054A287375060D95B7D9773FE2147647E563D'
            RetirementModuleBytes = 183814L
        }
    }

    if ($Run062106) {
        return [pscustomobject]@{
            Name = '062106'
            RunId = 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942'
            SourceCommit = 'c44bb52dcb38179f5d026b7331b848c114ed649e'
            FailedAction = '20261021T065114533Z-FinalVerify-6c332fa152e44a55925c801705f052cd'
            FailedActionStatus = 'FINAL_VERIFY_FAILED_GPU_UNATTESTED'
            BackupAction = '20261021T062522426Z-Backup-4d82912e15464713b6f63f1ace6ad3fb'
            BaselineLeaf = 'firmware-enumeration-backup-1648399929574818b6bfb740f9e80877.stdout.txt'
            CurrentLeaf = 'firmware-enumeration-final-cb55a93e13d648ae97e1f62bebc01a8b.stdout.txt'
            FinalResultLeaf = 'result.json'
            BaselineSha256 = '7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4'
            CurrentSha256 = 'BC104DF2EBB63636540220FC2E085980F81AEA595B1EC77FE288116A245FA661'
            BaselineBytes = 4336L
            CurrentBytes = 4336L
            FinalResultSha256 = 'C426FEEF8E70136E527AAE40B233CAC9281681F7C3A5113DBA062DDD4F49CC49'
            FinalResultBytes = 2285L
            DisarmedBootId = '2026-10-21T06:36:00.5000000Z'
            CurrentBootId = '2026-10-21T06:49:32.5000000Z'
            SelectorSha256 = '4510690FAD83991612344C958E26C334F19850F71D54B6D51669F7EF94AAB484'
            SelectorBytes = 533L
            JournalSha256 = '26B4C9D657AE65DFD007073C4E5F99A1E620720774BEFDDBB398DE7E3F8E9A80'
            JournalBytes = 44133L
            ReferenceLogSha256 = 'F7C93F246F4E2E6FAA3ABB6AF4C17DDABECD201A8F035FF1211755D676D990A8'
            ReferenceLogBytes = 40838L
            ProbeRelativePath = '.codex-build\gb\b3b06badc7018942\l\ib-tcg-dump.exe'
            ProbeSha256 = 'E7FEA1D8436416B0ADC392F03A22A4B510D4CE5DF3308B49DD65D9D6E2432E1A'
            ProbeBytes = 172544L
            AuditSha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
            AuditBytes = 246784L
            StateModuleSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
            StateModuleBytes = 19843L
            PlatformModuleSha256 = '6EEC7FAB2A3DD095215B85781A3273A87B026ED8E3DF647225C40225E332336E'
            PlatformModuleBytes = 111703L
            RetirementModuleSha256 = 'D40F76D333DCAB0F3475D02CB12343F38629E0D12F12303A6FC5C85B14A931FD'
            RetirementModuleBytes = 246612L
        }
    }

    return [pscustomobject]@{
        Name = '201140'
        RunId = 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7'
        SourceCommit = '933b4c676318f26396cdf2c66de01c3ccbf1a950'
        FailedAction = '20261020T205030359Z-FinalVerify-7ab6d4cba68941c4b3b89ed7553c8534'
        BackupAction = '20261020T201533358Z-Backup-479329a8e70a4124964be17cc597ad61'
        BaselineLeaf = 'firmware-enumeration-backup-4196e13c22604293a54688532c15cff6.stdout.txt'
        CurrentLeaf = 'firmware-enumeration-final-7a5347fd0bc4452eb06340ba1e614a92.stdout.txt'
        BaselineSha256 = 'ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE'
        CurrentSha256 = '4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE'
        BaselineBytes = 4336L
        CurrentBytes = 4336L
        FinalResultLeaf = $null
        FinalResultSha256 = $null
        FinalResultBytes = 0L
        DisarmedBootId = '2026-10-20T20:19:07.5000000Z'
        CurrentBootId = '2026-10-20T20:48:51.5000000Z'
        SelectorSha256 = $null
        SelectorBytes = 0L
        JournalSha256 = $null
        JournalBytes = 0L
        ReferenceLogSha256 = '8876361480AC4D7A7E1FFD89AD830D3A601EE6323E0695C2A7FB6DE7A9F2EDF0'
        ReferenceLogBytes = 40838L
        ProbeRelativePath = '.codex-build\gb\8be3274dfe56cec7\l\ib-tcg-dump.exe'
        ProbeSha256 = 'E3E46729304B18226FBA708C1D319FDDC96A05C6BE13AA43C468EDE83C238D47'
        ProbeBytes = 172544L
        AuditSha256 = 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4'
        AuditBytes = 246784L
        StateModuleSha256 = '067655F6843E5DF811D7B9E37A73A26BF893B1A88EFE414BE1F235594CDB51EC'
        StateModuleBytes = 19843L
        PlatformModuleSha256 = '4816FC0E640D7AEB38DF301E4D53A4982D5CABAA780650FEA8BDEE5D7C416899'
        PlatformModuleBytes = 107733L
        RetirementModuleSha256 = 'CCFA78B48A18AB90AE865ADEB14CA952D927955523B9E72F0C30A762F4FDB553'
        RetirementModuleBytes = 61875L
    }
}

function Assert-GpuFinalFirmwareInspectionEntryPins {
    param([Parameter(Mandatory = $true)][object]$Profile,
          [Parameter(Mandatory = $true)][object]$SelectorPin,
          [Parameter(Mandatory = $true)][object]$JournalPin)
    if ($SelectorPin.Sha256 -cne $Profile.SelectorSha256 -or $SelectorPin.Bytes -ne $Profile.SelectorBytes) {
        throw "The fixed $($Profile.Name) selector differs from its closed-run SHA-256/length pin."
    }
    if ($JournalPin.Sha256 -cne $Profile.JournalSha256 -or $JournalPin.Bytes -ne $Profile.JournalBytes) {
        throw "The fixed $($Profile.Name) failed journal differs from its closed-run SHA-256/length pin."
    }
}

function Assert-GpuFinalFirmwareInspectionJournal {
    param([Parameter(Mandatory = $true)][object]$Selector,
          [Parameter(Mandatory = $true)][object]$Journal,
          [Parameter(Mandatory = $true)][string]$ExpectedSourceRoot,
          [Parameter(Mandatory = $true)][string]$ExpectedRunDirectory,
          [switch]$Run224018,
          [switch]$Run042142,
          [switch]$Run062106)

    $profile = Get-GpuFinalFirmwareInspectionProfile -Run224018:$Run224018 -Run042142:$Run042142 -Run062106:$Run062106
    $closedProfile = $Run224018 -or $Run042142 -or $Run062106
    if ($Selector.SchemaVersion -ne 1 -or [string]$Selector.RunId -cne $profile.RunId -or
        [IO.Path]::GetFullPath([string]$Selector.RunDirectory) -cne [IO.Path]::GetFullPath($ExpectedRunDirectory) -or
        [IO.Path]::GetFullPath([string]$Selector.SourceRoot) -cne [IO.Path]::GetFullPath($ExpectedSourceRoot)) {
        throw "The active selector does not identify the fixed $($profile.Name) failed run and reviewed feature worktree."
    }
    if ($Journal.SchemaVersion -ne 1 -or [string]$Journal.RunId -cne $profile.RunId -or
        [string]$Journal.SourceCommit -cne $profile.SourceCommit -or
        [IO.Path]::GetFullPath([string]$Journal.SourceRoot) -cne [IO.Path]::GetFullPath($ExpectedSourceRoot) -or
        [IO.Path]::GetFullPath([string]$Journal.RunDirectory) -cne [IO.Path]::GetFullPath($ExpectedRunDirectory)) {
        throw "The journal identity or source revision differs from the reviewed $($profile.Name) failed run."
    }
    if ([string]$Journal.Phase -cne 'Disarmed' -or $Journal.EverArmIntent -ne $true -or
        [string]::IsNullOrWhiteSpace([string]$Journal.DisarmedBootId) -or
        ($closedProfile -and [string]$Journal.DisarmedBootId -cne $profile.DisarmedBootId)) {
        throw 'The run is not in the reviewed disarmed state.'
    }
    if ($null -eq $Journal.LastAction -or [string]$Journal.LastAction.Action -cne 'FinalVerify' -or
        $Journal.LastAction.Succeeded -ne $false -or
        [IO.Path]::GetFileName([string]$Journal.LastAction.Directory) -cne $profile.FailedAction) {
        throw 'The latest action is not the fixed failed FinalVerify capture.'
    }
    if (($Run042142 -or $Run062106) -and ([string]$Journal.LastAction.Status -cne $profile.FailedActionStatus -or
        [IO.Path]::GetFullPath([string]$Journal.LastAction.Directory) -cne
            [IO.Path]::GetFullPath((Join-Path (Join-Path $ExpectedRunDirectory 'actions') $profile.FailedAction)))) {
        throw "The $($profile.Name) latest action status or directory differs from its fixed failed FinalVerify capture."
    }
    if ($null -eq $Journal.InProgress -or [string]$Journal.InProgress.Action -cne 'FinalVerify' -or
        [IO.Path]::GetFullPath([string]$Journal.InProgress.Directory) -cne [IO.Path]::GetFullPath([string]$Journal.LastAction.Directory)) {
        throw 'The journal does not retain the failed FinalVerify intent for this exact action directory.'
    }

    $verification = $Journal.FinalVerification
    $health = $verification.Health
    $esp = $verification.Esp
    $firmware = $verification.FirmwareEnumeration
    $expectedError = 'Firmware enumeration differs from the saved textual baseline.'
    if (($closedProfile -and [string]$verification.BootId -cne $profile.CurrentBootId) -or
        $verification.ResetBoundaryPassed -ne $true -or $verification.SecureBoot -ne $true -or
        $verification.ActivationAttempted -ne $true -or $null -eq $health -or $health.Passed -ne $true -or
        [string]$health.ExpectedResult -cne 'ABSENT' -or [string]$health.Aggregate -cne 'ABSENT' -or
        [int]$health.ExpectedCpus -ne 20 -or [int]$health.CpuCount -ne 20 -or
        $null -eq $esp -or $esp.MatchesBaseline -ne $true -or
        [string]$esp.BootManagerSha256 -cne '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511' -or
        $null -eq $firmware -or $firmware.TextEqual -ne $false -or
        [string]$firmware.BaselineSha256 -cne $profile.BaselineSha256 -or
        [string]$firmware.CurrentSha256 -cne $profile.CurrentSha256 -or
        [string]$firmware.CaptureContext -cne 'SystemEspMountedAtS' -or
        @($verification.Errors).Count -ne 1 -or [string]$verification.Errors[0] -cne $expectedError) {
        throw 'The failed FinalVerify does not match the reviewed reset, health, ESP, and single firmware-text mismatch gates.'
    }
    if ($closedProfile -and ([int]$health.CpuPassCount -ne 0 -or [string]$health.Reason -cne 'PASS')) {
        throw "The $($profile.Name) FinalVerify health report differs from the fixed absent-state result."
    }
}

function Write-GpuFinalFirmwareJson {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][object]$Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 40 -Compress -ErrorAction Stop
    $bytes = (New-Object Text.UTF8Encoding($false)).GetBytes($json + "`n")
    $stream = New-Object IO.FileStream($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    return Get-GpuFinalFirmwarePin -Path $Path
}

function Invoke-GpuFinalFirmwareObservation {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][object]$State,
          [Parameter(Mandatory = $true)][string]$ActionDirectory,
          [Parameter(Mandatory = $true)][hashtable]$Operations)

    $result = [ordered]@{
        Succeeded = $false; Status = 'READONLY_FIRMWARE_REVIEW_FAILED'; Esp = $null
        EspUnmounted = $true; CleanupSucceeded = $true; FirmwareObservations = @()
        BootSelectionCaptures = @(); Health = $null; Errors = @(); CleanupErrors = @()
        FirmwareTextStable = $false; RawBootSelectionStable = $false
    }
    $mount = $null
    try {
        $mount = & $Operations.MountEsp $ActionDirectory
        if ($null -eq $mount -or $mount.Acquired -ne $true) { throw 'The system ESP was not acquired by this observation.' }
        $result.EspUnmounted = $false
        $espCheck = & $Operations.AssertMountedEsp $mount $State.Backup
        $result.Esp = [pscustomobject]@{
            Volume=[string]$mount.Identity.Volume; DiskNumber=[int]$mount.Identity.DiskNumber
            PartitionGuid=[string]$mount.Identity.PartitionGuid; BootManagerSha256=[string]$espCheck.Sha256
        }

        $firmwareBefore = & $Operations.CaptureFirmware 'before'
        if ($null -eq $firmwareBefore -or $firmwareBefore.Succeeded -ne $true) { throw 'Mounted firmware text capture failed before the raw capture.' }
        $result.FirmwareObservations += $firmwareBefore
        $bootBefore = & $Operations.CaptureBootSelection 'before'
        if ($null -eq $bootBefore -or $null -eq $bootBefore.Baseline) { throw 'Raw boot-selection capture failed before health.' }
        $result.BootSelectionCaptures += $bootBefore

        $health = & $Operations.CaptureHealth
        if ($null -eq $health -or $health.CommandSucceeded -ne $true -or $null -eq $health.Verdict -or
            $health.Verdict.Passed -ne $true -or [int]$health.Verdict.CpuCount -ne 20 -or
            [int]$health.Verdict.ExpectedCpus -ne 20 -or [string]$health.Verdict.Aggregate -cne 'ABSENT' -or
            [string]$health.Verdict.ExpectedResult -cne 'ABSENT') {
            throw 'Fresh post-reset health did not report 20 ABSENT CPUs.'
        }
        $result.Health = $health

        $bootAfter = & $Operations.CaptureBootSelection 'after'
        if ($null -eq $bootAfter -or $null -eq $bootAfter.Baseline) { throw 'Raw boot-selection capture failed after health.' }
        & $Operations.AssertBootSelectionStable $bootBefore.Baseline $bootAfter.Baseline
        $result.BootSelectionCaptures += $bootAfter
        $result.RawBootSelectionStable = $true

        $firmwareAfter = & $Operations.CaptureFirmware 'after'
        if ($null -eq $firmwareAfter -or $firmwareAfter.Succeeded -ne $true) { throw 'Mounted firmware text capture failed after the raw capture.' }
        $result.FirmwareObservations += $firmwareAfter
        $result.FirmwareTextStable = [string]::Equals([string]$firmwareBefore.Text, [string]$firmwareAfter.Text, [StringComparison]::Ordinal)
        if (-not $result.FirmwareTextStable) { throw 'Mounted bcdedit firmware text changed during the observation window.' }
        $result.Status = 'READONLY_FIRMWARE_REVIEW_CAPTURED'
        $result.Succeeded = $true
    }
    catch { $result.Errors = @($result.Errors) + @([string]$_.Exception.Message) }
    finally {
        if ($null -ne $mount -and $mount.Acquired -eq $true) {
            try { & $Operations.DismountEsp $mount $ActionDirectory; $result.EspUnmounted = $true }
            catch {
                $result.CleanupErrors = @($result.CleanupErrors) + @([string]$_.Exception.Message)
                $result.CleanupSucceeded = $false; $result.EspUnmounted = $false; $result.Succeeded = $false
                $result.Status = 'READONLY_FIRMWARE_REVIEW_FAILED'
            }
        }
        if (-not $result.Succeeded) { $result.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
    }
    return [pscustomobject]$result
}

# WhatIf is intentionally handled before module imports, path reads, native
# commands, or evidence-directory creation. Function declarations above are inert.
if ($WhatIfPreference) {
    $previewProfile = Get-GpuFinalFirmwareInspectionProfile -Run224018:$Run224018 -Run042142:$Run042142 -Run062106:$Run062106
    [pscustomobject]@{
        Status = 'PREVIEW_ONLY'; Profile = $previewProfile.Name; RunId = $previewProfile.RunId
        Preview = $true; FilesystemChangesPerformed = $false
        NativeCommandsExecuted = $false; RecoveryCompleted = $false
    } | ConvertTo-Json -Depth 4
    return
}

$profile = Get-GpuFinalFirmwareInspectionProfile -Run224018:$Run224018 -Run042142:$Run042142 -Run062106:$Run062106
$toolRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $toolRoot '..\..'))
$workspaceRoot = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $sourceRoot))))
if ((Split-Path -Leaf (Split-Path -Parent $sourceRoot)) -ine 'worktrees' -or
    (Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent $sourceRoot))) -ine '.codex-build') {
    $workspaceRoot = $sourceRoot
}
$stateRoot = Join-Path $workspaceRoot '.analysis\gpu-baremetal'
$runId = $profile.RunId
$runDirectory = Join-Path (Join-Path $stateRoot 'runs') $runId
$actionDirectory = Join-Path (Join-Path $runDirectory 'actions') $profile.FailedAction
$expectedBackupAction = Join-Path (Join-Path $runDirectory 'actions') $profile.BackupAction
$expectedBaselinePath = Join-Path $expectedBackupAction $profile.BaselineLeaf
$baselinePath = $null
$currentFirmwarePath = Join-Path $actionDirectory $profile.CurrentLeaf
$currentResultPath = if ($profile.FinalResultLeaf) { Join-Path $actionDirectory $profile.FinalResultLeaf } else { $null }
$auditToolPath = Join-Path $workspaceRoot '.codex-build\gpu-stage-retirement-audit\readonly-v1\ibaudit.exe'
$referenceLogExpected = $profile.ReferenceLogSha256
$reviewRoot = Join-Path $workspaceRoot '.analysis\gpu-final-firmware-review'
$evidenceDirectory = Join-Path $reviewRoot ([guid]::NewGuid().ToString('N'))
$resultPath = Join-Path $evidenceDirectory 'result.json'
$stateModule = $null
$platformModule = $null
$retirementModule = $null
$sourceModulePaths = [ordered]@{
    State = Join-Path $toolRoot 'GpuHostState.psm1'
    Platform = Join-Path $toolRoot 'GpuHostPlatform.psm1'
    Retirement = Join-Path $toolRoot 'GpuStageRetirement.psm1'
}
$inspectorPinBefore = $null
$sourceModulePinsBefore = $null
$selectorPinBefore = $null
$journalPinBefore = $null
$lock = $null
$report = [ordered]@{
    SchemaVersion = 1; Status = 'READONLY_FIRMWARE_REVIEW_FAILED'; Succeeded = $false
    Profile = $profile.Name; RunId = $runId; RecoveryCompleted = $false; HistoricalRawBootOrderUnchangedProven = $false
    SelectorAndJournalUnchanged = $false; EspUnmounted = $true; CleanupSucceeded = $true; EspFilesChanged = $false; FirmwareWritten = $false
    EvidenceDirectory = $evidenceDirectory; Errors = @(); CleanupErrors = @(); PinsBefore = $null; PinsAfter = $null
    FirmwareObservations = @(); BootSelectionCaptures = @(); Health = $null
    Note = 'Read-only evidence capture only. This does not complete recovery or prove that historical BootOrder never changed.'
}

if (-not $PSCmdlet.ShouldProcess($runId, 'Capture bounded post-recovery firmware evidence')) {
    [pscustomobject]@{
        SchemaVersion = 1; Status = 'CAPTURE_DECLINED'; Succeeded = $false; RunId = $runId
        RecoveryCompleted = $false; FilesystemChangesPerformed = $false; NativeCommandsExecuted = $false
    } | ConvertTo-Json -Depth 4
    return
}

try {
    $inspectorPinBefore = Get-GpuFinalFirmwarePin -Path $PSCommandPath
    $sourceModulePinsBefore = [ordered]@{
        State = Assert-GpuFinalFirmwarePin -Path $sourceModulePaths.State -Sha256 $profile.StateModuleSha256 -Bytes $profile.StateModuleBytes -Description 'Pinned GPU host state module'
        Platform = Assert-GpuFinalFirmwarePin -Path $sourceModulePaths.Platform -Sha256 $profile.PlatformModuleSha256 -Bytes $profile.PlatformModuleBytes -Description 'Pinned GPU host platform module'
        Retirement = Assert-GpuFinalFirmwarePin -Path $sourceModulePaths.Retirement -Sha256 $profile.RetirementModuleSha256 -Bytes $profile.RetirementModuleBytes -Description 'Pinned GPU stage-retirement module'
    }
    $report.PinsBefore = [pscustomobject]@{ InspectorScript=$inspectorPinBefore; Modules=[pscustomobject]$sourceModulePinsBefore }
    $stateModule = Import-Module $sourceModulePaths.State -Force -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $platformModule = Import-Module $sourceModulePaths.Platform -Force -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $retirementModule = Import-Module $sourceModulePaths.Retirement -Force -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r } $reviewRoot $workspaceRoot
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } $sourceRoot $workspaceRoot
    [void][IO.Directory]::CreateDirectory($evidenceDirectory)
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } $evidenceDirectory $workspaceRoot

    if (-not [IO.Directory]::Exists($stateRoot)) { throw 'The existing GPU host state directory is missing.' }
    $lockPath = Join-Path $stateRoot 'host-step.lock'
    $null = & $stateModule { param($p,$r) Assert-GpuPathWithin -Path $p -Root $r -MustExist } $lockPath $stateRoot
    if ([IO.Directory]::Exists($lockPath)) { throw 'The shared host-step lock path is a directory.' }
    try { $lock = [IO.File]::Open($lockPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
    catch { throw 'Another GPU host action is running, or the existing shared host-step lock is unavailable.' }

    $selectorPath = Join-Path $stateRoot 'host-prepared.json'
    $journalPath = Join-Path $runDirectory 'journal.json'
    $selectorPinBefore = Get-GpuFinalFirmwarePin $selectorPath
    $journalPinBefore = Get-GpuFinalFirmwarePin $journalPath
    if ($Run224018 -or $Run042142 -or $Run062106) {
        Assert-GpuFinalFirmwareInspectionEntryPins -Profile $profile -SelectorPin $selectorPinBefore -JournalPin $journalPinBefore
    }
    $report.PinsBefore = [pscustomobject]@{
        InspectorScript=$inspectorPinBefore; Modules=[pscustomobject]$sourceModulePinsBefore
        Selector=$selectorPinBefore; Journal=$journalPinBefore
    }
    $selector = Get-Content -LiteralPath $selectorPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    $journal = Get-Content -LiteralPath $journalPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    Assert-GpuFinalFirmwareInspectionJournal -Selector $selector -Journal $journal -ExpectedSourceRoot $sourceRoot -ExpectedRunDirectory $runDirectory -Run224018:$Run224018 -Run042142:$Run042142 -Run062106:$Run062106

    $baselinePath = [IO.Path]::GetFullPath([string]$journal.Backup.FirmwareEnumeration.Path)
    if ($baselinePath -cne [IO.Path]::GetFullPath($expectedBaselinePath)) { throw 'The journal baseline path differs from the fixed current-run Backup capture.' }
    $baselinePinBefore = Assert-GpuFinalFirmwarePin -Path $baselinePath -Sha256 $profile.BaselineSha256 -Bytes $profile.BaselineBytes -Description 'Current-run firmware baseline capture'
    $currentPinBefore = Assert-GpuFinalFirmwarePin -Path $currentFirmwarePath -Sha256 $profile.CurrentSha256 -Bytes $profile.CurrentBytes -Description 'Failed FinalVerify firmware capture'
    $finalResultPinBefore = $null
    if ($Run042142 -or $Run062106) {
        $finalResultPinBefore = Assert-GpuFinalFirmwarePin -Path $currentResultPath -Sha256 $profile.FinalResultSha256 -Bytes $profile.FinalResultBytes -Description 'Failed FinalVerify result'
    }
    $auditToolPinBefore = Assert-GpuFinalFirmwarePin -Path $auditToolPath -Sha256 $profile.AuditSha256 -Bytes $profile.AuditBytes -Description 'Pinned ibaudit executable'
    $referenceLog = $journal.Backup.ReferenceLog
    $referenceLogPinBefore = Assert-GpuFinalFirmwarePin -Path ([string]$referenceLog.Path) -Sha256 $referenceLogExpected -Bytes $profile.ReferenceLogBytes -Description 'Current run TPM reference log'
    if ([string]$referenceLog.Sha256 -cne $referenceLogExpected -or [long]$referenceLog.Bytes -ne $profile.ReferenceLogBytes) { throw 'The journal reference-log pin differs from the reviewed pin.' }
    $expectedToolPaths = & $platformModule { param($s) Get-GpuExpectedToolPaths -State $s } $journal
    $probePath = [IO.Path]::GetFullPath([string]$expectedToolPaths.Probe)
    $expectedProbePath = [IO.Path]::GetFullPath((Join-Path $workspaceRoot $profile.ProbeRelativePath))
    if ($probePath -cne $expectedProbePath) { throw 'The probe path differs from the fixed reviewed build directory.' }
    if ([IO.Path]::GetFullPath([string]$journal.Tools.Probe.Path) -cne $probePath) { throw 'The probe path differs from the current run build directory.' }
    if ([string]$journal.Tools.Probe.Sha256 -cne $profile.ProbeSha256 -or [long]$journal.Tools.Probe.Bytes -ne $profile.ProbeBytes) {
        throw 'The current run probe journal pin differs from the reviewed build artifact.'
    }
    $probePinBefore = Assert-GpuFinalFirmwarePin -Path $probePath -Sha256 $profile.ProbeSha256 -Bytes $profile.ProbeBytes -Description 'Current run health probe'
    $report.PinsBefore = [pscustomobject]@{
        Selector=$selectorPinBefore; Journal=$journalPinBefore; FirmwareBaseline=$baselinePinBefore; FirmwareCurrent=$currentPinBefore
        Ibaudit=$auditToolPinBefore; ReferenceLog=$referenceLogPinBefore; Probe=$probePinBefore
        FinalResult=$finalResultPinBefore; InspectorScript=$inspectorPinBefore; Modules=[pscustomobject]$sourceModulePinsBefore
    }

    & $platformModule { Assert-GpuAdministrator }
    $current = & $platformModule { Get-GpuCurrentMachineSnapshot -IncludeSecureBoot }
    & $platformModule { param($s,$c) Assert-GpuSameMachine -State $s -Current $c -RequireCpuCount } $journal $current
    & $platformModule { param($c) Assert-GpuSecureBootOn -Current $c } $current
    if ([int]$current.LogicalCpus -ne 20) { throw 'Current host logical CPU count differs from the reviewed 20 CPUs.' }
    if ([string]::Equals([string]$current.BootId, [string]$journal.DisarmedBootId, [StringComparison]::Ordinal)) {
        throw 'A full host reset after disarm has not been observed.'
    }
    if (($Run224018 -or $Run042142 -or $Run062106) -and [string]$current.BootId -cne $profile.CurrentBootId) {
        throw "Current host BootId differs from the fixed $($profile.Name) post-reset observation."
    }
    $report.CurrentHost = [pscustomobject]@{ BootId=[string]$current.BootId; LogicalCpus=[int]$current.LogicalCpus; SecureBoot=[bool]$current.SecureBoot }

    $observationsDirectory = Join-Path $evidenceDirectory 'observations'
    [void][IO.Directory]::CreateDirectory($observationsDirectory)
    $nativeOperations = & $retirementModule {
        param($root,$auditPath,$auditHash,$auditBytes,$logHash,$logBytes)
        New-GpuStageRetirementPlatformOperations -ToolRoot $root -AuditToolPath $auditPath -ExpectedAuditToolSha256 $auditHash -ExpectedAuditToolBytes $auditBytes -ExpectedReferenceLogSha256 $logHash -ExpectedReferenceLogBytes $logBytes
    } $toolRoot $auditToolPath 'C16517939C887A7991DF4A0CDF05604AEC030141D9DA1FC45FDE8E989EC599A4' 246784 $referenceLogExpected 40838
    $captureOperations = @{}
    $captureOperations.MountEsp = $nativeOperations.MountEsp
    $captureOperations.AssertMountedEsp = $nativeOperations.AssertMountedEsp
    $captureOperations.DismountEsp = { param($m,$d) & $nativeOperations.DismountEsp $m }.GetNewClosure()
    $captureOperations.CaptureFirmware = {
        param([string]$Checkpoint)
        $capture = & $nativeOperations.CaptureFirmware $observationsDirectory
        if ($null -eq $capture -or [long]$capture.Pin.Bytes -ne $profile.CurrentBytes -or
            [string]$capture.Pin.Sha256 -cne $profile.CurrentSha256) {
            throw "Mounted bcdedit /enum firmware /v differs from the reviewed current capture at $Checkpoint."
        }
        return [pscustomobject]@{ Checkpoint=$Checkpoint; Succeeded=$true; Text=[string]$capture.Text; Pin=$capture.Pin; ReceiptPath=$capture.ReceiptPath }
    }.GetNewClosure()
    $captureOperations.CaptureBootSelection = { param([string]$Checkpoint) & $nativeOperations.CaptureBootSelection $journal $observationsDirectory $Checkpoint }.GetNewClosure()
    $captureOperations.CaptureHealth = {
        $capture = & $nativeOperations.CaptureHealth $journal $observationsDirectory
        return [pscustomobject]@{ CommandSucceeded=[bool]$capture.CommandSucceeded; Verdict=$capture.Verdict; ReceiptPath=$capture.ReceiptPath }
    }.GetNewClosure()
    $captureOperations.AssertBootSelectionStable = { param($before,$after) & $retirementModule { param($b,$a) Assert-GpuStageRetirementBootSelectionStable -Before $b -Current $a -Checkpoint 'during firmware inspection' } $before $after }.GetNewClosure()
    $observation = Invoke-GpuFinalFirmwareObservation -State $journal -ActionDirectory $observationsDirectory -Operations $captureOperations
    $report.Esp = $observation.Esp
    $report.EspUnmounted = $observation.EspUnmounted
    $report.CleanupSucceeded = $observation.CleanupSucceeded
    $report.CleanupErrors = @($observation.CleanupErrors)
    $report.FirmwareObservations = @($observation.FirmwareObservations)
    $report.BootSelectionCaptures = @($observation.BootSelectionCaptures)
    $report.Health = $observation.Health
    $report.FirmwareTextStable = $observation.FirmwareTextStable
    $report.RawBootSelectionStable = $observation.RawBootSelectionStable
    if (-not $observation.Succeeded) {
        $captureErrors = @(@($observation.Errors) + @($observation.CleanupErrors))
        throw ($captureErrors -join ' ')
    }
    $report.Status = 'READONLY_FIRMWARE_REVIEW_CAPTURED'
    $report.Succeeded = $true
}
catch {
    $report.Errors = @($report.Errors) + @([string]$_.Exception.Message)
}
finally {
    if ($null -ne $lock) {
        try {
            $selectorAfter = Get-GpuFinalFirmwarePin -Path (Join-Path $stateRoot 'host-prepared.json')
            $journalAfter = Get-GpuFinalFirmwarePin -Path (Join-Path $runDirectory 'journal.json')
            $pinsAfter = [ordered]@{ Selector=$selectorAfter; Journal=$journalAfter }
            $report.SelectorAndJournalUnchanged = ($null -ne $selectorPinBefore -and $null -ne $journalPinBefore -and
                $selectorAfter.Sha256 -ceq $selectorPinBefore.Sha256 -and $selectorAfter.Bytes -eq $selectorPinBefore.Bytes -and
                $journalAfter.Sha256 -ceq $journalPinBefore.Sha256 -and $journalAfter.Bytes -eq $journalPinBefore.Bytes)
            if (-not $report.SelectorAndJournalUnchanged) { throw 'Selector or journal changed during the read-only evidence capture.' }
            if ($report.PinsBefore) {
                $pinsAfter.FirmwareBaseline = Assert-GpuFinalFirmwarePin -Path $baselinePath -Sha256 $report.PinsBefore.FirmwareBaseline.Sha256 -Bytes $report.PinsBefore.FirmwareBaseline.Bytes -Description 'Current-run firmware baseline after observation'
                $pinsAfter.FirmwareCurrent = Assert-GpuFinalFirmwarePin -Path $currentFirmwarePath -Sha256 $report.PinsBefore.FirmwareCurrent.Sha256 -Bytes $report.PinsBefore.FirmwareCurrent.Bytes -Description 'Failed FinalVerify firmware capture after observation'
                $pinsAfter.Ibaudit = Assert-GpuFinalFirmwarePin -Path $auditToolPath -Sha256 $report.PinsBefore.Ibaudit.Sha256 -Bytes $report.PinsBefore.Ibaudit.Bytes -Description 'Pinned ibaudit after observation'
                $pinsAfter.ReferenceLog = Assert-GpuFinalFirmwarePin -Path ([string]$report.PinsBefore.ReferenceLog.Path) -Sha256 $report.PinsBefore.ReferenceLog.Sha256 -Bytes $report.PinsBefore.ReferenceLog.Bytes -Description 'Reference log after observation'
                $pinsAfter.Probe = Assert-GpuFinalFirmwarePin -Path ([string]$report.PinsBefore.Probe.Path) -Sha256 $report.PinsBefore.Probe.Sha256 -Bytes $report.PinsBefore.Probe.Bytes -Description 'Health probe after observation'
                if ($null -ne $report.PinsBefore.FinalResult) {
                    $pinsAfter.FinalResult = Assert-GpuFinalFirmwarePin -Path $currentResultPath -Sha256 $report.PinsBefore.FinalResult.Sha256 -Bytes $report.PinsBefore.FinalResult.Bytes -Description 'Failed FinalVerify result after observation'
                }
            }
            $pinsAfter.InspectorScript = Assert-GpuFinalFirmwarePin -Path $inspectorPinBefore.Path -Sha256 $inspectorPinBefore.Sha256 -Bytes $inspectorPinBefore.Bytes -Description 'Inspector script after observation'
            $pinsAfter.Modules = [pscustomobject]@{
                State = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.State.Path -Sha256 $sourceModulePinsBefore.State.Sha256 -Bytes $sourceModulePinsBefore.State.Bytes -Description 'GPU host state module after observation'
                Platform = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.Platform.Path -Sha256 $sourceModulePinsBefore.Platform.Sha256 -Bytes $sourceModulePinsBefore.Platform.Bytes -Description 'GPU host platform module after observation'
                Retirement = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.Retirement.Path -Sha256 $sourceModulePinsBefore.Retirement.Sha256 -Bytes $sourceModulePinsBefore.Retirement.Bytes -Description 'GPU stage-retirement module after observation'
            }
            $report.PinsAfter = [pscustomobject]$pinsAfter
        }
        catch { $report.Errors = @($report.Errors) + @([string]$_.Exception.Message); $report.Succeeded = $false; $report.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
        finally { $lock.Dispose(); $lock = $null }
    }
    elseif ($null -ne $inspectorPinBefore -and $null -ne $sourceModulePinsBefore) {
        try {
            $report.PinsAfter = [pscustomobject]@{
                InspectorScript = Assert-GpuFinalFirmwarePin -Path $inspectorPinBefore.Path -Sha256 $inspectorPinBefore.Sha256 -Bytes $inspectorPinBefore.Bytes -Description 'Inspector script after preflight'
                Modules = [pscustomobject]@{
                    State = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.State.Path -Sha256 $sourceModulePinsBefore.State.Sha256 -Bytes $sourceModulePinsBefore.State.Bytes -Description 'GPU host state module after preflight'
                    Platform = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.Platform.Path -Sha256 $sourceModulePinsBefore.Platform.Sha256 -Bytes $sourceModulePinsBefore.Platform.Bytes -Description 'GPU host platform module after preflight'
                    Retirement = Assert-GpuFinalFirmwarePin -Path $sourceModulePinsBefore.Retirement.Path -Sha256 $sourceModulePinsBefore.Retirement.Sha256 -Bytes $sourceModulePinsBefore.Retirement.Bytes -Description 'GPU stage-retirement module after preflight'
                }
            }
        }
        catch { $report.Errors = @($report.Errors) + @([string]$_.Exception.Message); $report.Succeeded = $false; $report.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
    }
    if (-not $report.Succeeded) { $report.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
    try { $report.ResultPin = Write-GpuFinalFirmwareJson -Path $resultPath -Value ([pscustomobject]$report) }
    catch { $report.Errors = @($report.Errors) + @("Could not write result.json: $($_.Exception.Message)"); $report.Succeeded = $false; $report.Status = 'READONLY_FIRMWARE_REVIEW_FAILED' }
}

$report | ConvertTo-Json -Depth 40
if (-not $report.Succeeded) { exit 1 }
