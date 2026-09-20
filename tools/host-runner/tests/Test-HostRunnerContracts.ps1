[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RunnerRoot = Split-Path -Parent $PSScriptRoot
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $RunnerRoot)
$UefiProjectRoot = Join-Path $ProjectRoot 'components\uefi-loader'
$TestRoot = Join-Path $ProjectRoot (
    'build\host-runner-tests\' + [Guid]::NewGuid().ToString('N'))

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

try {
    New-Item -ItemType Directory -Force -Path $TestRoot | Out-Null
    Import-Module (Join-Path $RunnerRoot 'HostRunner.Core.psm1') -Force

    $PowerShellFiles = @(Get-ChildItem -LiteralPath $RunnerRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.ps1', '.psm1') })
    foreach ($File in $PowerShellFiles) {
        $Tokens = $null
        $Errors = $null
        [Management.Automation.Language.Parser]::ParseFile(
            $File.FullName, [ref]$Tokens, [ref]$Errors) | Out-Null
        Assert-True ($Errors.Count -eq 0) "PowerShell parse failed: $($File.Name)"
    }

    foreach ($Workflow in @(
        'linux-positive', 'linux-replay-dry-run', 'linux-pe-rehash',
            'linux-hypervisor-smoke', 'linux-hypervisor-post-ebs-smoke',
            'linux-hypervisor-post-ebs-interrupt-smoke',
            'linux-divergence-manifest', 'linux-handoff-mechanics',
            'linux-production-handoff', 'linux-oneshot-cycle',
            'linux-nested-disk-probe', 'linux-nested-windows-e2e',
            'linux-nested-windows-hypervisor-e2e', 'linux-bootnext-e2e',
            'windows-probe', 'windows-capture', 'windows-nested-prep')) {
        $RunId = New-UhrRunId $Workflow
        Assert-True (Test-UhrRunId $RunId) "Generated unsafe RunId: $RunId"
    }
    Assert-True (-not (Test-UhrRunId '../escape')) 'Traversal RunId was accepted.'

    $JsonPath = Join-Path $TestRoot 'state.json'
    Write-UhrAtomicJson -Path $JsonPath -Value ([ordered]@{
        SchemaVersion = 1
        Status = 'PASS'
    })
    $Json = Get-Content -Raw -LiteralPath $JsonPath | ConvertFrom-Json
    Assert-True ($Json.Status -ceq 'PASS') 'Atomic JSON round-trip failed.'
    Write-UhrAtomicJson -Path $JsonPath -Value ([ordered]@{
        SchemaVersion = 1
        Status = 'REPLACED'
    })
    $Json = Get-Content -Raw -LiteralPath $JsonPath | ConvertFrom-Json
    Assert-True ($Json.Status -ceq 'REPLACED') 'Atomic JSON replacement failed.'
    Assert-True (-not (Test-Path -LiteralPath ($JsonPath + '.tmp'))) (
        'Atomic JSON left a fixed temporary file.')

    Assert-True (Test-UhrPathWithin -Root $TestRoot -Path (
        Join-Path $TestRoot 'child\file')) 'Child path was rejected.'
    Assert-True (-not (Test-UhrPathWithin -Root $TestRoot -Path (
        Join-Path (Split-Path -Parent $TestRoot) 'escape'))) (
        'Escaping path was accepted.')

    $ProcessParameters = @{
        FilePath = $env:ComSpec
        ArgumentList = @('/d', '/c', 'echo runner-process-pass')
        WorkingDirectory = $ProjectRoot
        TimeoutSeconds = 30
    }
    $Process = Invoke-UhrProcess @ProcessParameters
    Assert-True ($Process.ExitCode -eq 0 -and
        $Process.Stdout -match 'runner-process-pass') (
        'Bounded process execution failed.')

    $Config = Import-UhrConfig -ConfigPath (
        Join-Path $RunnerRoot 'host-runner.config.example.json')
    Assert-True ($Config.SchemaVersion -eq 1) 'Example config did not validate.'
    Assert-True ([IO.Path]::GetFullPath([string]$Config.ProjectRoot) -eq
        [IO.Path]::GetFullPath($ProjectRoot)) (
        'Example config ProjectRoot is not the combined repository root.')
    Assert-True ([IO.Path]::GetFullPath([string]$Config.UefiProjectRoot) -eq
        [IO.Path]::GetFullPath($UefiProjectRoot)) (
        'Example config UefiProjectRoot does not point to components/uefi-loader.')
    Assert-True ([IO.Path]::GetFullPath([string]$Config.HypervisorProjectRoot) -eq
        [IO.Path]::GetFullPath((Join-Path $ProjectRoot 'components\hypervisor'))) (
        'Example config HypervisorProjectRoot does not point to components/hypervisor.')
    Assert-True ($Config.Policy.MaxConcurrentVms -eq 2) (
        'Example config does not permit the two known VMs to coexist.')
    Assert-True ($Config.Policy.MinimumHostReserveMib -ge 512) (
        'Example config has an unsafe host-memory reserve.')
    Assert-True ([uint64]$Config.Policy.MinimumHostFreeDiskMib -ge 20480) (
        'Example config must preserve at least 20 GiB of host disk space.')
    Assert-True ($Config.Profiles.Linux.LeaseResource -ceq
        'vm-uefiloader-ubuntu64') 'Linux lease resource changed unexpectedly.'
    Assert-True ($Config.Profiles.Windows.LeaseResource -ceq
        'vm-uefiloader-windows-exactbuild-vtpm') 'Windows lease resource changed unexpectedly.'
    Assert-True ([uint64]$Config.Profiles.Windows.ExpectedWindowsBuild -eq
        [uint64]26200) 'Example Windows build expectation changed unexpectedly.'
    Assert-True ([uint64]$Config.Profiles.Windows.ExpectedWindowsUbr -eq
        [uint64]9445) 'Example Windows UBR expectation changed unexpectedly.'
    Assert-True (@($Config.AdditionalKnownVms | Where-Object {
                [string]$_.LeaseResource -ceq 'vm-hypervisor-v3h-gate4'
            }).Count -eq 1) (
        'The original Hypervisor VM must remain in the known-VM concurrency set.')
    Assert-True ($Config.Profiles.Windows.GuestTransport -ceq
        'VmwareGuestInfo') 'Example Windows transport is not Guest Variables.'
    Assert-True ([string]::IsNullOrWhiteSpace(
        [string]$Config.Profiles.Windows.GuestCredentialFile)) (
        'Guest Variables transport unexpectedly requires a Windows guest credential.')
    Assert-True ($null -ne $Config.Profiles.Windows.PSObject.Properties[
        'MailboxCredentialFile'] -and
        -not [string]::IsNullOrWhiteSpace(
            [string]$Config.Profiles.Windows.MailboxCredentialFile)) (
        'Guest Variables transport is missing its mailbox HMAC credential.')
    Assert-True ($null -eq $Config.Profiles.Windows.PSObject.Properties[
        'MailboxHostRoot'] -or
        [string]::IsNullOrWhiteSpace(
            [string]$Config.Profiles.Windows.MailboxHostRoot)) (
        'Guest Variables transport must not depend on a shared-folder mailbox.')

    $BootstrapSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'Initialize-LocalRunner.ps1')
    foreach ($Token in @(
            'AuthorizeCredentialImport',
            'Target configuration already exists; use -Force',
            'Target configuration must be contained under TargetProjectRoot',
            'Assert-DpapiCredentialXml',
            "'components\uefi-loader'", "'components\hypervisor'",
            "'.analysis\host-runner'",
            '[IO.File]::Replace', '[IO.File]::Move')) {
        Assert-True ($BootstrapSource.Contains($Token)) (
            "Local-runner bootstrap contract is missing: $Token")
    }
    Assert-True ($BootstrapSource -notmatch
        '(?m)^\s*(Import-Clixml|ConvertTo-SecureString)\b|\.GetNetworkCredential\s*\(') (
        'Local-runner bootstrap must not decrypt or expose credentials.')

    $InvalidConfig = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'host-runner.config.example.json') |
        ConvertFrom-Json
    $InvalidConfig.Profiles.Windows.GuestTransport = 'VmrunGuest'
    $InvalidConfig.Profiles.Windows.GuestCredentialFile = ''
    $InvalidConfigPath = Join-Path $TestRoot 'invalid-vmrun-config.json'
    [IO.File]::WriteAllText($InvalidConfigPath,
        ($InvalidConfig | ConvertTo-Json -Depth 12),
        [Text.UTF8Encoding]::new($false))
    $Rejected = $false
    try { [void](Import-UhrConfig -ConfigPath $InvalidConfigPath) }
    catch { $Rejected = $_.Exception.Message -match 'GuestCredentialFile' }
    Assert-True $Rejected 'VmrunGuest accepted an empty guest credential.'

    $LegacyConfig = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'host-runner.config.example.json') |
        ConvertFrom-Json
    $LegacyConfig.Profiles.Windows.PSObject.Properties.Remove(
        'ExpectedWindowsBuild')
    $LegacyConfig.Profiles.Windows.PSObject.Properties.Remove(
        'ExpectedWindowsUbr')
    $LegacyConfigPath = Join-Path $TestRoot 'legacy-config.json'
    [IO.File]::WriteAllText($LegacyConfigPath,
        ($LegacyConfig | ConvertTo-Json -Depth 12),
        [Text.UTF8Encoding]::new($false))
    $LegacyAccepted = $true
    try { [void](Import-UhrConfig -ConfigPath $LegacyConfigPath) }
    catch { $LegacyAccepted = $false }
    Assert-True $LegacyAccepted (
        'Windows profiles without optional build expectations must remain valid.')

    foreach ($Property in @('ExpectedWindowsBuild', 'ExpectedWindowsUbr')) {
        $InvalidVersionConfig = Get-Content -Raw -LiteralPath (
            Join-Path $RunnerRoot 'host-runner.config.example.json') |
            ConvertFrom-Json
        $InvalidVersionConfig.Profiles.Windows.$Property = 'not-a-number'
        $InvalidVersionPath = Join-Path $TestRoot (
            "invalid-$Property.json")
        [IO.File]::WriteAllText($InvalidVersionPath,
            ($InvalidVersionConfig | ConvertTo-Json -Depth 12),
            [Text.UTF8Encoding]::new($false))
        $Rejected = $false
        try { [void](Import-UhrConfig -ConfigPath $InvalidVersionPath) }
        catch { $Rejected = $_.Exception.Message -match [regex]::Escape($Property) }
        Assert-True $Rejected (
            "Invalid $Property value was accepted.")
    }

    $InvalidLayout = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'host-runner.config.example.json') |
        ConvertFrom-Json
    $InvalidLayout.PSObject.Properties.Remove('UefiProjectRoot')
    $InvalidLayoutPath = Join-Path $TestRoot 'invalid-layout-config.json'
    [IO.File]::WriteAllText($InvalidLayoutPath,
        ($InvalidLayout | ConvertTo-Json -Depth 12),
        [Text.UTF8Encoding]::new($false))
    $Rejected = $false
    try { [void](Import-UhrConfig -ConfigPath $InvalidLayoutPath) }
    catch { $Rejected = $_.Exception.Message -match 'UefiProjectRoot' }
    Assert-True $Rejected 'A combined config without UefiProjectRoot was accepted.'

    $RunnerSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'Invoke-UEFILoaderHostRunner.ps1')
    Assert-True ($RunnerSource.Contains('StoreMailboxCredential') -and
        $RunnerSource.Contains('must decode to exactly 32 bytes')) (
        'Mailbox credential refresh action is missing or unbounded.')
    $ExactBuildSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\Invoke-WindowsExactBuildUpdate.ps1')
    foreach ($Token in @(
            'KB5124008', '$TargetBuild = [uint64]26200',
            '$TargetUbr = [uint64]9445',
            'guestinfo.uefiloader.exact.status',
            "[ValidateSet('Initial', 'Worker')]", 'Microsoft.Update.Session',
            "IsInstalled=0 and Type='Software'", 'KBArticleIDs',
            'schtasks.exe', 'New-ScheduledTaskTrigger -Once',
            'New-ScheduledTaskTrigger -AtStartup',
            "New-ScheduledTaskPrincipal -UserId 'SYSTEM'",
            'Register-ScheduledTask', 'StartWhenAvailable',
             'Start-ScheduledTask -TaskName $TaskName',
             'Get-SystemDriveFreeGiB', 'Format-HResult',
             'GetUpdateResult(0)',
             '-Mode Worker', 'RebootRequired', 'shutdown.exe', '--cmd',
             "-State 'QUEUED'", 'exact-build-worker-status.json',
             "-State 'REPAIRING'", 'dism.exe', 'sfc.exe',
             '/Cleanup-Image /CheckHealth /NoRestart', '/scannow',
             'Get-ServicingFailureContext', 'SERVICING_CONTEXT:',
             'Reset-ExactWindowsUpdateCache',
             'Remove-ExactWindowsUpdateCacheBackup',
             '$CacheResetFloorUbr', '$Current.Ubr -ge $CacheResetFloorUbr',
             "@('bits', 'wuauserv', 'cryptsvc')",
             'SoftwareDistribution', 'System32\catroot2',
             'CANCELLED',
            'Test-ExactCancellation', 'Stop-ExactUpdateIfCancelled',
            'Remove-ExactTask', 'supersedes the exact target',
            'Install-ExactTask')) {
        Assert-True ($ExactBuildSource.Contains($Token)) (
            "Exact Windows build servicing contract is missing: $Token")
    }
    Assert-True ($ExactBuildSource -notmatch
        'UsoClient|Start-Process|Invoke-Expression|runProgramInGuest|UpdateSearcher.*Search\("IsInstalled=0"') (
        'Exact Windows build servicing exposes an arbitrary update or execution surface.')
    foreach ($Token in @(
            'RunWindowsExactBuildUpdate', 'Invoke-WindowsExactBuildUpdate',
            'ResumeWindowsExactBuildUpdate',
            'Invoke-WindowsExactBuildResume',
            'Wait-WindowsExactBuildStatus', 'Assert-HostCapacity',
            'Assert-HostDiskCapacity',
            'probe|capture|nested-prep|exact-build-update',
            "New-UhrRunId 'windows-capture'",
             'SnapshotPreserved', 'Publish-WindowsExactCancellation',
             'Clear-VmwareGuestInfoVariables',
             'QUEUED = 0; STARTING = 1',
             "`$Status.State -in @('QUEUED', 'STARTING')",
             'Resume-WindowsExactBuildUpdateVm', '-InitialOnly',
             '-SeedStatus $InitialStatusResult.Final',
             'exact-build-status-live.json', 'ResumedFromQuarantine',
             'resume-failure-recovery.json',
             'Legacy exact-build quarantine does not match the expired lease owner.',
             'INSTALLING/REBOOTING -> STARTING',
             'Exact Windows build VM stop failed while the VM remained running',
             'Enable-WindowsUpdateNetwork', 'Restore-WindowsUpdateNetwork',
            'ethernet0.connectionType = "nat"',
            'The saved Windows update VMX does not prove the adapter was disabled.',
            "@('present', 'connectionType', 'startConnected'",
            'Acquire-SharedLease', 'Release-SharedLease')) {
        Assert-True ($RunnerSource.Contains($Token)) (
            "Exact Windows host servicing contract is missing: $Token")
    }
    $ExactWorkflowStart = $RunnerSource.IndexOf(
        'function Invoke-WindowsExactBuildUpdate',
        [StringComparison]::Ordinal)
    $ExactWorkflowEnd = $RunnerSource.IndexOf(
        'function Invoke-LiveWorkflow', $ExactWorkflowStart,
        [StringComparison]::Ordinal)
    Assert-True ($ExactWorkflowStart -ge 0 -and
        $ExactWorkflowEnd -gt $ExactWorkflowStart) (
        'Exact Windows host servicing workflow is not isolated.')
    $ExactWorkflowSource = $RunnerSource.Substring(
        $ExactWorkflowStart, $ExactWorkflowEnd - $ExactWorkflowStart)
    Assert-True ($ExactWorkflowSource -notmatch
        'Stop-ConfiguredVmAfterPass|revertToSnapshot.*SnapshotPreserved') (
        'Exact Windows successful servicing must preserve the clean snapshot and retain the update delta.')
    $DirectStartupSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot `
            'guest\Invoke-WindowsDirectHypervisorStartup.ps1')
    foreach ($Token in @(
            '__DIRECT_VERIFY_RUN_ID__',
            '__DIRECT_SCANNER_ONLY__',
            'UEFI2HSINV-DIRECT:', 'UEFI2HSINV-DIAG:',
            "[IO.Ports.SerialPort]::new('COM2'",
            "Write-DirectDiagnostic 'STARTUP_ENTERED'",
            "Write-DirectDiagnostic 'PROBE_START'",
            "Write-DirectDiagnostic 'PROBE_COMPLETE'",
            "Write-DirectDiagnostic 'PROBE_CLASSIFIED_ABSENT'",
            "Write-DirectDiagnostic 'PROBE_CLASSIFIED_PASS'",
            "Write-DirectDiagnostic 'DRIVER_START_BEGIN'",
            "Write-DirectDiagnostic 'RUNTIME_SCAN_START'",
            'UEFI_HYPERVISOR_WINDOWS_CPU:',
            'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS',
            'CurrentVersionPath', 'CurrentBuild', 'UBR',
            'WINDOWS_BUILD=', 'WINDOWS_UBR=',
            'DIRECT_HYPERVISOR_VERIFIED_PROCESSORS=',
             'ResponseHmacSha256', 'HMACSHA256',
             'selftest-shutdown',
             "@('scan', '??', '0', '0', 'first')",
             'HVR1_RUNTIME_SCAN=PASS',
             'HVR1_RUNTIME_SCAN_ONLY=PASS',
             'HVR1_CRASH_RECOVERY_PRIOR_BOOT=PASS',
             'HVR1_POWER_REQUEST_HELD=PASS',
             'HVR1_AUTH_INVALID_TAG_REJECTED=PASS',
             'HVR1_AUTH_VALID_SHUTDOWN=PASS',
             'HVR1_AUTH_REPLAY_REJECTED=PASS',
             'HVR1_AUTH_SELFTEST=PASS cpus={0}/{0}',
             'HVR1_POWER_REQUEST_RELEASED_AFTER_OFFLINE=PASS',
             'HVR1_POST_DETACH_CPUID_ABSENT=PASS',
             '$PostDetachAbsentCpuLines.Count -ne $ExpectedProcessors',
             '$PostDetachAbsentAggregate.Count -ne 1',
             'HVR1_BITLOCKER_PROTECTORS_RESTORED=PASS',
            'HVR1_COMPANION_UNLOADED=PASS',
             'HVR1_CODE_INTEGRITY_DATA=',
            'Publish-DirectStageTwoReady',
            'BOOTNEXT_READY',
            'UEFI_HYPERVISOR_WINDOWS_CPUID:ABSENT',
             'hypervisorlaunchtype-baseline.txt',
             'hypervisorlaunchtype auto',
             '/deletevalue',
             'ib-direct-install.exe', "'oneshot', '--mode', 'off'",
            'direct-bootmgfw-before.sha256',
            'Remove-Item -LiteralPath $Stage -Recurse -Force')) {
        Assert-True ($DirectStartupSource.Contains($Token)) (
            "Direct Windows startup-probe contract is missing: $Token")
    }
    foreach ($Token in @(
            'Wait-WindowsDirectHypervisorResult',
            'RunWindowsDirectScannerE2E',
            'Invoke-WindowsDirectHypervisorE2E -ScannerOnly',
            'Get-WindowsDirectBuildMetadata',
            'ExpectedWindowsBuild', 'ExpectedWindowsUbr',
            'Windows build mismatch', 'Windows UBR mismatch',
            'Start-WindowsDirectResultCapture',
            'Update-WindowsDirectCapture',
            "'uefiloader_direct_result'",
            "'uefiloader_clone_debug'",
            "'direct-debug-serial.log'",
            'serial1.present = "TRUE"',
            'serial1.fileName = "\\.\pipe\uefiloader_direct_result"',
            'serial1.pipe.endPoint = "server"',
            'serial1.tryNoRxLoss = "FALSE"',
             '-DirectVerifyRunId $VerifyRunId',
             'Invoke-Hvr1RuntimeCompanionBuild',
             'cmake -S . -B build\hvr1-direct -A x64',
             '$script:VmwareGuestInfoMaxPackageBytes = 4MB',
             '$script:VmwareGuestInfoMaxRequestChunks = 256',
             'topology restoration does not match its saved baseline',
             'temporary direct EFI result pipe remained after recovery',
             '-IncludeRuntimeCompanion',
             '-CompanionDriverPath ([string]$Build.CompanionDriverPath)',
             '-CompanionServicePath ([string]$Build.CompanionServicePath)',
             '-MinimumCompletedUtc $ResetRequestedUtc',
             "'reset', [string]`$Selected.VmxPath, 'soft'",
             'authoritative completion',
             'Wait-WindowsDirectStageTwoReady',
             'Wait-WindowsDirectStageTwoOrResult',
             "Kind = 'DIRECT_RESULT'",
             'RemoteDisplay\.vnc\.ip',
             'Capture-LocalVncScreen.ps1',
             'direct-failure-vnc-screen.png',
             'if ($Result.TimedOut -and -not $AllowFailure)',
             'Direct EFI setup did not report the BitLocker protection state exactly once.',
             'direct stage-two marker authentication failed',
             'PASS\r?$')) {
        Assert-True ($RunnerSource.Contains($Token)) (
            "Direct Windows host-result contract is missing: $Token")
    }
    Assert-True ($RunnerSource -notmatch
        'Invoke-Expression|typeKeystrokesInGuest|deleteVM|-G "Visual Studio') (
        'Runner exposes a forbidden execution or VM-mutation surface.')
    foreach ($Token in @(
            "'RunInstallerHostGate'", "'RunInstallerOneShotHostGate'",
            "'RunAuditHostGate'",
            "'RunLinuxPositivePath'", "'RunLinuxReplayDryRun'", "'RunLinuxPeRehash'",
            "'RunLinuxHypervisorSmoke'", 'LinuxHypervisorSmoke',
            "'RunLinuxHypervisorPostEbsSmoke'",
            'LinuxHypervisorPostEbsSmoke',
            "'RunLinuxHypervisorPostEbsInterruptSmoke'",
            'LinuxHypervisorPostEbsInterruptSmoke',
            "'RunLinuxDivergenceManifest'", "'RunLinuxHandoffMechanics'",
            "'RunLinuxProductionHandoff'",
            "'RunLinuxOneShotCycle'",
            "'RunWslNestedWindowsE2E'",
            "'RunWslNestedWindowsHypervisorE2E'",
            "'RunWslBootNextE2E'",
            "'RunLinuxNestedWindowsHypervisorE2E'",
            "'RunWindowsProbe'",
            "'RunWindowsCapture'", "'RunWindowsNestedPrep'",
            "'CreateWindowsSnapshot'",
            '-AuthorizeLiveVmRun',
            '-AuthorizeVmStop', '-AuthorizeSnapshotRevert',
            '-AuthorizeSnapshotCreate',
            'Acquire-SharedLease', 'QUARANTINED-',
            'Acquire-StartupLease', 'host-vm-startup',
            'Assert-ConcurrentVmSetSafe', 'Assert-HostCapacity',
            'LeaseResource', 'MaxConcurrentVms',
            'MinimumHostReserveMib', 'MinimumHostFreeDiskMib',
            '[IO.DriveInfo]', '-Resource',
            'TakeOverExpiredLease',
            'Assert-ProfileCredentialsReady',
            'VmwareGuestInfo', 'Publish-WindowsGuestInfoRequest',
            'Wait-WindowsGuestInfoResult', 'Clear-VmwareGuestInfoVariables',
            'ConvertTo-VmwareGuestInfoChunks',
            "'^(running|installed)$'",
            'refuse to attach to or stop a VM not started by this run')) {
        Assert-True ($RunnerSource.Contains($Token)) (
            "Runner contract token is missing: $Token")
    }
    Assert-True ($RunnerSource -notmatch
        '\[string\]\$Executable|\[string\[\]\]\$Arguments') (
        'Public parameters permit an arbitrary executable or argument list.')

    $LinuxSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\Invoke-LinuxPositivePath.sh')
    foreach ($Token in @(
            'edk2-stable202605', 'b03a21a',
            'socket_root="/tmp/uhr-${run_id##*-}"',
            'control_socket="${data_socket}.ctrl"',
            '/dev/kvm', 'missing_tools', 'uuid-dev',
            'rm -f -- Conf/BuildEnv.sh',
            'rm -rf -- "$edk2_root/Build/OvmfX64"',
            'set +u', '. edksetup.sh BaseTools', 'set -u',
            '-t GCC', 'RELEASE_GCC/FV/OVMF_CODE.fd',
            'isa-debugcon.iobase=0xe9',
            'PCR banks empty - replay may proceed',
            'callback sequence: HRH',
            'PCR extensions performed: 00000000',
            'pcrcheck.efi',
            'after-pcr-check.txt')) {
        Assert-True ($LinuxSource.Contains($Token)) (
            "Linux positive-path contract is missing: $Token")
    }
    foreach ($Token in @(
            'uefi_hypervisor.efi', 'uefi_hypervisor_probe.efi',
            'payload_source', 'HypervisorSmoke', 'uefi_hypervisor',
            'uefi_hypervisor_probe', 'HypervisorEfiPath',
            'cmake -S . -B build\uefi-hypervisor',
            'uefi hypervisor: processor resources: BSP ONLY',
            'uefi hypervisor: processor state: BSP ONLY',
            'uefi hypervisor: VMX launch: PASS',
            'uefi hypervisor: BSP virtualization: PASS',
            'UEFI_HYPERVISOR_PROBE:PASS',
            'loader production handoff: PASS', 'hypervisor-order.txt')) {
        Assert-True ($RunnerSource.Contains($Token) -or
            $LinuxSource.Contains($Token)) (
            "Linux hypervisor smoke contract is missing: $Token")
    }
    foreach ($Token in @(
            'uefi_hypervisor_post_ebs_probe.efi',
            'HypervisorPostEbsSmoke', 'linux-hypervisor-post-ebs-smoke',
            'isa-debug-exit,iobase=0x501,iosize=0x04',
            'UEFI_HYPERVISOR_NATIVE_DETACH:PASS',
            'UEFI_HYPERVISOR_POST_EBS_PROBE:PASS',
            'hypervisor-post-ebs-order.txt',
            'hypervisor-post-ebs-interrupt-order.txt',
            'qemu-exit-code.txt',
            '$qemu_rc -ne 33')) {
        Assert-True ($RunnerSource.Contains($Token) -or
            $LinuxSource.Contains($Token)) (
            "Linux post-EBS hypervisor contract is missing: $Token")
    }
    foreach ($Token in @(
            'uefi_hypervisor_post_ebs_interrupt_probe.efi',
            'HypervisorPostEbsInterruptSmoke',
            'linux-hypervisor-post-ebs-interrupt-smoke',
            'HypervisorPostEbsInterruptProbeEfiPath',
            'isa-debug-exit,iobase=0x501,iosize=0x04',
            'UEFI_HYPERVISOR_GUEST_INTERRUPT_VECTOR:06',
            'UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:PASS',
            'UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:FAIL',
            'hypervisor-post-ebs-interrupt-order.txt',
            'qemu-exit-code.txt', '$qemu_rc -ne 37',
            '(0x12 << 1) | 1 = 37',
            '^[FIVX]:[0-9A-Fa-f]{8}$')) {
        Assert-True ($RunnerSource.Contains($Token) -or
            $LinuxSource.Contains($Token)) (
            "Linux post-EBS guest-interrupt contract is missing: $Token")
    }
    $InterruptBranchStart = $LinuxSource.LastIndexOf(
        'elif [[ "$mode" == "HypervisorPostEbsInterruptSmoke" ]]',
        [StringComparison]::Ordinal)
    $InterruptBranchEnd = $LinuxSource.IndexOf(
        'elif [[ "$mode" == "OneShotCycle" ]]',
        $InterruptBranchStart + 1, [StringComparison]::Ordinal)
    Assert-True ($InterruptBranchStart -ge 0 -and
        $InterruptBranchEnd -gt $InterruptBranchStart) (
        'Guest-interrupt guard validation branch is missing or malformed.')
    $InterruptBranch = $LinuxSource.Substring(
        $InterruptBranchStart, $InterruptBranchEnd - $InterruptBranchStart)
    Assert-True ($InterruptBranch.Contains(
        'UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:PASS')) (
        'Guest-interrupt guard branch does not enforce its exact PASS exit contract.')
    Assert-True ($LinuxSource.Contains('$qemu_rc -ne 37')) (
        'Guest-interrupt guard does not enforce QEMU exit status 37.')
    Assert-True ($InterruptBranch -notmatch
        'boot manager launch: PASS|loader production handoff: PASS') (
        'Guest-interrupt guard must stop after its exact guard marker.')
    $InterruptPayloadMarker = $LinuxSource.IndexOf(
        '"payload launch: PASS",', [StringComparison]::Ordinal)
    $InterruptGuardMarker = $LinuxSource.IndexOf(
        '"UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:PASS",',
        [StringComparison]::Ordinal)
    Assert-True ($InterruptPayloadMarker -ge 0 -and
        $InterruptGuardMarker -gt $InterruptPayloadMarker) (
        'Guest-interrupt guard must be ordered after payload launch.')
    $HypervisorAsmSource = Get-Content -Raw -LiteralPath (
        Join-Path $ProjectRoot 'components\hypervisor\src\efi\efi_hypervisor.asm')
    $HypervisorCSource = Get-Content -Raw -LiteralPath (
        Join-Path $ProjectRoot 'components\hypervisor\src\efi\efi_hypervisor.c')
    foreach ($Token in @(
            'EXIT_REASON_XSETBV',
            'VMCS_GUEST_CR4',
            'ResidentCpuidCr4Ready:',
            'CPUID_LEAF1_GUEST_ECX_MASK',
            'ResidentCpuidStore:',
            'ResidentXsetbv:',
            'xsetbv')) {
        Assert-True ($HypervisorAsmSource.Contains($Token)) (
            "EFI resident XSETBV route is missing: $Token")
    }
    Assert-True ($HypervisorAsmSource -notmatch
        'ResidentCpuidCheckHypervisorClass:|CPUID_HYPERVISOR_CLASS_MASK') (
        'EFI hypervisor must preserve the containing VM hypervisor interface.')
    foreach ($Token in @(
            'EXIT_REASON_IO_INSTRUCTION',
            'ResidentIoInstruction:',
            'VMWARE_BACKDOOR_IN_EAX_DX',
            'VMWARE_BACKDOOR_PORT',
            'in eax, dx')) {
        Assert-True ($HypervisorAsmSource.Contains($Token)) (
            "EFI VMware compatibility I/O route is missing: $Token")
    }
    foreach ($Register in @('RAX', 'RBX', 'RCX', 'RDX', 'RSI', 'RDI')) {
        Assert-True ($HypervisorAsmSource.Contains(
            "CONTEXT_GUEST_$Register")) (
            "EFI VMware compatibility I/O ABI omits $Register.")
    }
    foreach ($Token in @(
            'HV_EFI_VM_CPU_USE_IO_BITMAPS',
            'HV_EFI_VMCS_CTRL_IO_BITMAP_A',
            'HV_EFI_VMCS_CTRL_IO_BITMAP_B',
            'HV_EFI_IO_BITMAP_A_PAGE',
            'HV_EFI_IO_BITMAP_B_PAGE')) {
        Assert-True ($HypervisorCSource.Contains($Token)) (
            "EFI selective I/O bitmap setup is missing: $Token")
    }
    $InitStart = $HypervisorAsmSource.IndexOf(
        'ResidentInitSignal:', [StringComparison]::Ordinal)
    $SipiStart = $HypervisorAsmSource.IndexOf(
        'ResidentSipi:', [StringComparison]::Ordinal)
    Assert-True ($InitStart -ge 0 -and $SipiStart -gt $InitStart) (
        'EFI resident INIT/SIPI handlers are missing or misordered.')
    $InitRoute = $HypervisorAsmSource.Substring(
        $InitStart, $SipiStart - $InitStart)
    Assert-True ($InitRoute.Contains(
            'RESIDENT_VMWRITE_REG VMCS_GUEST_ACTIVITY, rax') -and
        $InitRoute.Contains('jmp ResidentRestoreAndResume') -and
        -not $InitRoute.Contains('out dx, al') -and
        $InitRoute.Contains('ResidentSipiReset:')) (
        'EFI INIT must enter wait-for-SIPI without debug I/O and defer reset until SIPI.')
    Assert-True ($HypervisorAsmSource -notmatch
        '(?m)^\s*mov\s+ecx,\s*HV_EFI_IA32_PERF_GLOBAL_CTRL\s*$') (
        'EFI payload unconditionally reads optional IA32_PERF_GLOBAL_CTRL.')
    Assert-True ($HypervisorCSource -notmatch
        'HvEfiWriteField\(HV_EFI_VMCS_(GUEST|HOST)_PERF_GLOBAL_CTRL') (
        'EFI payload writes PERF_GLOBAL_CTRL without enabling its VMX controls.')
    Assert-True ($HypervisorCSource.Contains(
        '((uint32_t)(descriptor[6] & 0xf0u) << 8)')) (
        'EFI segment flags are not encoded in VMCS access-right bits 12-15.')
    $PayloadMarker = $LinuxSource.IndexOf(
        '"payload launch: PASS",', [StringComparison]::Ordinal)
    $ProbeMarker = $LinuxSource.IndexOf(
        '"UEFI_HYPERVISOR_PROBE:PASS",', [StringComparison]::Ordinal)
    Assert-True ($PayloadMarker -ge 0 -and $ProbeMarker -gt $PayloadMarker) (
        'Hypervisor smoke order must report payload success before boot-manager probe.')
    foreach ($Token in @(
            'DivergenceManifest', 'divergence.ib',
            'after-raw-extend.txt', 'manifest-parse.txt',
            'post-extend SHA256 PCR0 nonzero: YES')) {
        Assert-True ($RunnerSource.Contains($Token) -or
            $LinuxSource.Contains($Token)) (
            "Linux divergence contract is missing: $Token")
    }
    foreach ($Token in @(
            'HandoffMechanics', 'tcglog.ib', 'payload.efi',
            'bootmgfw.efi.bak', 'bootmgfw.efi.ibnew',
            'bootmgfw.efi.ibold', 'restore-verify.txt',
            'handoff-order.txt', 'ExitBootServices calls: 00000000')) {
        Assert-True ($RunnerSource.Contains($Token) -or
            $LinuxSource.Contains($Token)) (
            "Linux handoff contract is missing: $Token")
    }
    foreach ($Token in @(
            'ProductionHandoff', 'linux-production-handoff',
            'handoff-status.txt', 'loader production handoff: PASS')) {
        Assert-True ($RunnerSource.Contains($Token) -or
            $LinuxSource.Contains($Token)) (
            "Linux production-handoff contract is missing: $Token")
    }
    foreach ($Token in @(
            'OneShotCycle', 'linux-oneshot-cycle', 'shimx64.efi',
            'EFI\\Microsoft\\Boot\\bootmgfw.efi', 'grubx64.efi',
            'ERROR: PCR banks not empty - aborting replay',
            'loader fail-closed handoff: PASS')) {
        Assert-True ($RunnerSource.Contains($Token) -or
            $LinuxSource.Contains($Token)) (
            "Linux one-shot-cycle contract is missing: $Token")
    }
    $NestedWindowsSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\Invoke-LinuxNestedWindowsE2E.sh')
    $NestedStartupSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\Invoke-NestedE2ECapture.ps1')
    $NestedServiceSource = Get-Content -Raw -LiteralPath (
        Join-Path $UefiProjectRoot 'src\runner_service\main.c')
    $NestedCacheValidationSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\Validate-LinuxNestedCache.sh')
    $LinuxCleanupSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\Cleanup-LinuxGuestRun.sh')
    foreach ($Token in @(
            'RunWslBootNextE2E', 'linux-bootnext-e2e',
            'LinuxBootNextE2E')) {
        Assert-True ($RunnerSource.Contains($Token)) (
            "WSL BootNext runner contract is missing: $Token")
    }
    foreach ($Token in @(
            'prepared-qcow2-v1', 'conditioned-v4',
            'BASE_CHECK=PASS', 'CONDITION_CHECK=PASS',
            'LEGACY_CONDITION_CACHE=ABSENT', 'AVAILABLE_BYTES=')) {
        Assert-True ($NestedCacheValidationSource.Contains($Token)) (
            "Linux nested-cache validation contract is missing: $Token")
    }
    foreach ($Token in @(
            'RunLinuxNestedWindowsHypervisorE2E',
            'RunWslNestedWindowsHypervisorE2E',
            'linux-nested-windows-hypervisor-e2e',
            'LinuxNestedWindowsHypervisorE2E',
            'WslNestedWindowsHypervisorE2E',
            'Invoke-WslNestedWindowsE2E -Hypervisor',
            'NestedWindowsHypervisorE2E',
            'always builds fresh loader, hypervisor, and static Windows tools',
            'uhr-uefi-paths.cmake',
            'HYPERSINV_UEFI_SOURCE_DIR',
             'HYPERSINV_UEFI_SUPPORT_DIR',
             'HypervisorEfiPath',
             '/usr/include/uuid/uuid.h',
             'libuuid development package is missing',
             'condition_timeout=1200',
             'boot_image="$runtime/boot.img"',
             '-device ide-hd,drive=boot,bus=ide.0,bootindex=1',
             '-device ide-hd,drive=evidence,bus=ide.1,bootindex=3',
             'if not exist fs2:\EFI\Microsoft\Boot\bootmgfw.efi then',
             'conditioning-fat-directory.txt',
             'windows_smp=2',
             'windows_smp=20',
             'windows_smp_topology="cpus=20,sockets=1,cores=10,threads=2"',
             'per-CPU health validation',
             'nested_memory_mb=3072',
             '-m "$nested_memory_mb"',
            '-smp "$windows_smp_topology"',
            'archive_condition_attempt',
            'prepare_condition_retry',
            'condition_attempt=2',
             'conditioning-post-result-qemu-exit.txt',
             'qemu-img check -q',
             'prepared-qcow2-v1',
             'conditioned-v4',
             'for legacy_condition_version in conditioned-v1 conditioned-v2 conditioned-v3',
             'legacy_condition_cache="$cache_root/$legacy_condition_version"',
             'stale_run_name',
             'stale_base_name',
             'stale_condition_name',
             'qemu-img convert -p -c -f vmdk -O qcow2',
             'minimum_bytes=$((40 * 1024 * 1024 * 1024))',
             'windows-base-cache-info.json',
             'windows-base-cache-convert.log',
             'windows-base-cache-hit.txt',
             'windows-base-cache-published.txt',
             'conditioning-cache-hit.txt',
            'conditioning-cache-published.txt',
            'condition_result_is_pass',
            'Windows conditioning exited without a result',
            'watch_hypervisor_fatal',
            'qemu-fatal-marker.txt',
            'sample_qemu_registers',
    'qemu-register-snapshots.txt',
    'info lapic',
    'info irq',
            'x /64bx 0x',
            'x /16gx 0x',
            'screendump',
            'qemu-screen.ppm',
            'commands.append(f"screendump {screen_path}")',
            'rip_value >= 0xffff800000000000',
            'windows_timeout=900',
            'range(1, 61)',
            'KERNEL_BASE=',
            'BUGCHECK_DATA_PDB=',
            'BUGCHECK_DATA_ADDRESS=',
            'BUGCHECK_CODE=',
            'BUGCHECK_PARAMETER4=',
            'diagnostic_commands',
            '72C69E726C648BC18257AF38FA78A2F21',
            'postboot_evidence_files=(',
             'capture_partial_postboot_evidence',
             'postboot-fat-directory.txt',
             'postboot-progress.txt',
             'prepare_replay_attempt',
             'archive_replay_attempt first-attempt',
             'hypervisor_launch_markers_complete',
             "grep -Fq '0xc000000d'",
             'windows-replay-retry.txt',
             'replay_retry_reason="BOOT_MANAGER_0xC000000D"',
             'replay_retry_reason="CLEAN_MARKER_COMPLETE_PRE_EVIDENCE_SHUTDOWN"',
             "printf 'ATTEMPTS=2\nFIRST_RESULT=%s\n'",
             'first-attempt-qemu-serial.log',
             'swtpm-retry.log',
             'missing_postboot_evidence=()',
             'Nested Windows powered off without publishing required evidence:',
             'mdir -i "$fat_volume" ::',
             'reference log events: (?!00000000)[0-9A-Fa-f]{8}',
             'hypervisor topology, AP preflight, or launch markers were missing or out of order',
             'UEFILoader-Nested-E2E-Replay',
            'Install-ReplayStartupTask',
            'Install-AuditTask',
            'UEFILoader-Nested-E2E-Audit',
            '/SC MINUTE /MO 1 /ST $StartTime',
            'HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Run',
             'UEFILoaderNestedE2EAudit',
             'New-ItemProperty -LiteralPath $AuditLauncherRegistryPath',
             'sc.exe" create $AuditServiceName',
             'start= auto obj= LocalSystem binPath= $ServiceCommand',
             '$AuditServiceBinary',
             'ib-nested-e2e-service.exe',
            '/Run /TN "',
            'CONDITION_REPLAY_TASK_ARMED',
            'CONDITION_REPLAY_TASK_TRIGGERED',
            'CONDITION_HIBERNATION_DISABLED',
            'CONDITION_AUDIT_TASK_ARMED',
            'CONDITION_AUDIT_LOGON_LAUNCHER_ARMED',
            'CONDITION_AUDIT_SERVICE_ARMED',
            'CONDITION_REPLAY_TASK_DISABLED',
            'Get-CimInstance -ClassName Win32_Volume',
            '$script:MountedEvidenceRoot = $MountPoint',
            '$MountedEvidenceRoot /D',
            'powercfg.exe', '/hibernate off',
            'conditioning-reboot.flag',
            '/r /t 5 /f',
            '/p /f /d p:0:0',
            '/Disable',
            'STABLE_CPUS=',
            'qemu-stall-marker.txt',
             'nested-windows-hypervisor-order.txt')) {
        Assert-True ($RunnerSource.Contains($Token) -or
            $NestedWindowsSource.Contains($Token) -or
            $NestedStartupSource.Contains($Token)) (
            "Linux VMware Windows-hypervisor contract is missing: $Token")
    }
    foreach ($Token in @(
            'StartServiceCtrlDispatcherW', 'RegisterServiceCtrlHandlerExW',
            'CreateProcessW', 'IB_CAPTURE_TIMEOUT_MS',
            'UEFILoaderNestedE2EAudit', 'Invoke-NestedE2ECapture.ps1',
            '--self-test')) {
        Assert-True ($NestedServiceSource.Contains($Token)) (
            "Nested Windows service contract is missing: $Token")
    }
    foreach ($Token in @(
            'build\windows-static\ib-nested-e2e-service.exe',
            'NESTED_PREP_SERVICE=INSTALLED',
            'NESTED_PREP_SERVICE_SHA256=')) {
        Assert-True ($RunnerSource.Contains($Token) -or
            (Get-Content -Raw -LiteralPath (Join-Path $RunnerRoot `
                'guest\Invoke-WindowsGuestTest.ps1')).Contains($Token)) (
            "Nested Windows service staging contract is missing: $Token")
    }
    foreach ($Token in @(
            'Remove-SupersededPreparedDisks',
            'RemovedSupersededPreparedDisks',
            'Prepared-disk retention root may not be a reparse point.',
            'Remove-LinuxGuestRunRoot',
            'Refuse unsafe Linux guest run-root cleanup target.',
            'Cleanup-LinuxGuestRun.sh',
            'linux-guest-cleanup-warning.txt')) {
        Assert-True ($RunnerSource.Contains($Token)) (
            "Host-runner retention contract is missing: $Token")
    }
    foreach ($Token in @(
            'target="${1:-}"', 'script_root=',
            'uefiloader-host-runner/runs/', 'rm -rf -- "$target"')) {
        Assert-True ($LinuxCleanupSource.Contains($Token)) (
            "Linux guest cleanup helper contract is missing: $Token")
    }
    Assert-True ($RunnerSource.Contains(
        "@('createDirectoryInGuest', [string]`$Selected.VmxPath,")) (
        'Linux guest stage creation must use VMware dedicated directory creation.')
    Assert-True ($RunnerSource.Contains(
        'Linux guest stage creation failed:')) (
        'Linux guest stage failures must report bounded filesystem diagnostics.')
    foreach ($Token in @("'/usr/bin/test', '-O'", "'/usr/bin/test', '-L'",
            'linux-stage-create-warning.txt', 'tmp-write', 'runs-write',
            'deleteFileInGuest')) {
        Assert-True ($RunnerSource.Contains($Token)) (
            "Linux guest stage fallback is missing: $Token")
    }
    $HypervisorNestedStart = $RunnerSource.IndexOf(
        'elseif ($Workflow -eq ''LinuxNestedWindowsHypervisorE2E'')',
        [StringComparison]::Ordinal)
    $HypervisorNestedEnd = $RunnerSource.IndexOf(
        "else {", $HypervisorNestedStart + 1,
        [StringComparison]::Ordinal)
    Assert-True ($HypervisorNestedStart -ge 0 -and
        $HypervisorNestedEnd -gt $HypervisorNestedStart) (
        'Linux VMware Windows-hypervisor staging branch is missing.')
    Assert-True ($RunnerSource -match
        'elseif \(\$Workflow -in @\(''LinuxNestedWindowsE2E'',\s*''LinuxNestedWindowsHypervisorE2E'',\s*''LinuxBootNextE2E''\)\)') (
        'Linux VMware Windows-hypervisor staging branch is unreachable from the nested-Windows workflow gate.')
    $NestedShareAdd = $RunnerSource.IndexOf(
        "'addSharedFolder'", [StringComparison]::Ordinal)
    $NestedShareRefresh = if ($NestedShareAdd -ge 0) {
        $RunnerSource.LastIndexOf(
            "'removeSharedFolder'", $NestedShareAdd,
            [StringComparison]::Ordinal)
    } else { -1 }
    Assert-True ($NestedShareRefresh -ge 0) (
        'Nested Windows staging must remove a stale runner share before adding it.')
    $HypervisorNestedBranch = $RunnerSource.Substring(
        $HypervisorNestedStart, $HypervisorNestedEnd - $HypervisorNestedStart)
    foreach ($Token in @(
            'build\windows\loader.efi',
            'AbsoluteSource = $HypervisorEfiPath; Name = ''payload.efi''',
            'build\windows-static\ib-tcg-dump.exe',
            'build\windows-static\ibaudit.exe',
            'nested-SHA256SUMS.txt',
            'UEFILoader-Nested-E2E.marker')) {
        Assert-True ($HypervisorNestedBranch.Contains($Token)) (
            "Linux VMware Windows-hypervisor staging is missing: $Token")
    }
    $StubNestedStart = $RunnerSource.IndexOf(
        "@{ Source = 'build\windows\handoff_payload_stub.efi'; Name = 'payload.efi' }",
        [StringComparison]::Ordinal)
    Assert-True ($StubNestedStart -ge 0) (
        'Existing nested Windows stub baseline was removed.')
    Assert-True ($HypervisorNestedBranch -notmatch 'handoff_payload_stub\.efi') (
        'Linux VMware Windows-hypervisor path still stages the stub payload.')
    foreach ($Token in @(
             'Hypervisor', 'nested-windows-hypervisor-e2e',
             'uefi hypervisor: processor topology: PASS',
             'uefi hypervisor: AP VMX preflight: PASS',
             'uefi hypervisor: processor resources: PASS',
             'uefi hypervisor: processor state: PASS',
             'uefi hypervisor: AP virtualization: PASS',
             'uefi hypervisor: VMX launch: PASS',
            'uefi hypervisor: BSP virtualization: PASS',
            'payload launch: PASS',
            'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS',
            'UEFI_HYPERVISOR_WINDOWS_CPUID:',
            'UEFI_HYPERVISOR_WINDOWS_CPU:',
            'len(cpu_ids) != expected_cpu_count',
            'terminate_qemu_timeout_tree',
            'terminate_timeout_tree',
            'for cpu in range(expected_cpu_count)',
            'activated AP count: 0x{expected_ap_count:08X}',
            'uefi hypervisor: AP virtualization: FAIL STOP',
            'uefi hypervisor: FATAL INTERRUPT',
            '^[FIVX]:[0-9A-Fa-f]{8}$')) {
        Assert-True ($NestedWindowsSource.Contains($Token)) (
            "Linux nested Windows-hypervisor guest contract is missing: $Token")
    }
    foreach ($Token in @(
            'UEFILoader-Nested-BootNext-Setup.marker',
            'bootnext-setup-result.json', 'bootnext-result.json',
            'BootNext set to owned Boot', 'BootOrder was not changed.',
            'bootnext-firmware-after-cleanup.txt')) {
        Assert-True ($NestedWindowsSource.Contains($Token) -or
            $NestedStartupSource.Contains($Token)) (
            "WSL BootNext live contract is missing: $Token")
    }
    foreach ($Token in @(
            'PeRehash', 'bootmgfw.efi',
            'HashLogExtendEvent flags: PE_COFF_IMAGE',
            'PE pre-relocation rehash gate: PASS')) {
        Assert-True ($LinuxSource.Contains($Token)) (
            "Linux PE-rehash contract is missing: $Token")
    }
    Assert-True ($RunnerSource.Contains(
        "'C:\Windows\Boot\EFI\bootmgfw.efi'")) (
        'PE-rehash source is not fixed to the host Windows boot manager.')
    $PatchSource = Get-Content -Raw -LiteralPath (
        Join-Path $UefiProjectRoot `
            'tools\patches\edk2-stable202605-no-measure.patch')
    Assert-True ($PatchSource.Contains(
        'OvmfPkg/Library/AcpiPlatformLib/QemuFwCfgAcpi.c')) (
        'The no-measure patch does not suppress QEMU ACPI PCR[1] extensions.')
    Assert-True ($LinuxSource -notmatch 'eval\s|sudo\s') (
        'Linux guest runner may elevate or evaluate arbitrary text.')

    $LoaderSource = Get-Content -Raw -LiteralPath (
        Join-Path $UefiProjectRoot 'src\loader\efi_entry.c')
    foreach ($Token in @('__outbyte', '0xE9', 'debugcon_print')) {
        Assert-True ($LoaderSource.Contains($Token)) (
            "Headless loader diagnostics are missing: $Token")
    }
    foreach ($Token in @(
            'reference_log_load', 'divergence_build', 'replay_uefi_init',
            'replay_run', 'boot_restore_run', 'image_launch_start',
            'image_launch_start_resident', 'IB_PAYLOAD_REPLAY_INDEPENDENT',
            'handoff-status.txt', 'unsupported active PCR bank')) {
        Assert-True ($LoaderSource.Contains($Token)) (
            "Production loader integration is missing: $Token")
    }
    Assert-True ($LoaderSource -notmatch
        '->ExitBootServices\s*\(') (
        'The production loader must leave ExitBootServices to bootmgfw.')
    $ImageLaunchSource = Get-Content -Raw -LiteralPath (
        Join-Path $UefiProjectRoot 'src\loader\image_launch.c')
    Assert-True ($ImageLaunchSource -notmatch
        '->ExitBootServices\s*\(') (
        'The loader handoff helper must leave ExitBootServices to bootmgfw.')

    $InstallerCliSource = Get-Content -Raw -LiteralPath (
        Join-Path $UefiProjectRoot 'src\install\main.c')
    $P6InstallerSource = Get-Content -Raw -LiteralPath (
        Join-Path $UefiProjectRoot 'src\install\sign.c')
    $P6InstallerSource += Get-Content -Raw -LiteralPath (
        Join-Path $UefiProjectRoot 'src\install\stage.c')
    foreach ($Token in @(
            'signtool', 'password-env', 'MokManager', 'shimx64.efi',
            'grubx64.efi', 'manifest.sha256', 'IOCTL_DISK_GET_PARTITION_INFO_EX',
            'PARTITION_STYLE_GPT')) {
        Assert-True (($InstallerCliSource + $P6InstallerSource).Contains($Token)) (
            "P6 installer contract token is missing: $Token")
    }
    Assert-True ($P6InstallerSource -notmatch
        'SetFirmwareEnvironmentVariable|BootNext|bootmgfw\.efi') (
        'P6 must not mutate firmware variables or the Windows boot manager.')
    $InstallerGateSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'tests\Invoke-InstallerHostGate.ps1')
    foreach ($Token in @(
            'CertificateRequest', 'CreateSelfSigned',
            'X509ContentType]::Pfx', 'reject-unsigned', 'reject-mok-write')) {
        Assert-True ($InstallerGateSource.Contains($Token)) (
            "Installer host-gate contract token is missing: $Token")
    }
    Assert-True ($InstallerGateSource -notmatch
        'Cert:\\CurrentUser|New-SelfSignedCertificate|Export-PfxCertificate') (
        'Installer host gate must not depend on a writable user certificate store.')
    $P7InstallerSource = Get-Content -Raw -LiteralPath (
        Join-Path $UefiProjectRoot 'src\install\oneshot.c')
    $P7InstallerSource += Get-Content -Raw -LiteralPath (
        Join-Path $UefiProjectRoot 'src\install\bootnext.c')
    foreach ($Token in @(
            'bootmgfw.efi.bak', 'bootmgfw.efi.ibnew',
            'bootmgfw.efi.ibold', 'bootmgfw.sha256',
            'SNAPSHOT REQUIRED', 'SetFirmwareEnvironmentVariableExW',
            'SeSystemEnvironmentPrivilege', 'BootNext', 'BootOrder')) {
        Assert-True (($InstallerCliSource + $P7InstallerSource).Contains($Token)) (
            "P7 installer contract token is missing: $Token")
    }
    Assert-True ($P7InstallerSource -notmatch
        'write_variable\(L"BootOrder"') (
        'P7 must not write BootOrder.')
    $OneShotGateSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'tests\Invoke-InstallerOneShotHostGate.ps1')
    foreach ($Token in @(
            'RunId', 'SNAPSHOT REQUIRED', 'bootmgfw.efi.bak',
            'verify-before-restore', 'verify-after-restore',
            'Refusing firmware-variable writes')) {
        Assert-True ($OneShotGateSource.Contains($Token)) (
            "P7 host-gate contract token is missing: $Token")
    }

    $WindowsSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\Invoke-WindowsGuestTest.ps1')
    foreach ($Token in @(
            "[ValidateSet('Probe', 'Capture')]", 'Get-FileHash',
            'verdict: empty-bank precondition NOT MET', 'tcglog-full.bin',
            'kernel-metadata.json', 'PdbGuid', 'SymbolKey',
            'ImageTimestamp', 'ImageSize',
            'KERNEL_SYMBOL_KEY=',
            'UEFILoader-Nested-E2E', 'manage-bde.exe', 'mountvol.exe',
             '__DIRECT_LOADER_EFI_DEFLATE_BASE64__',
             '__DIRECT_HYPERVISOR_EFI_DEFLATE_BASE64__',
             '__DIRECT_INSTALLER_DEFLATE_BASE64__',
             '__DIRECT_COMPANION_DRIVER_DEFLATE_BASE64__',
             '__DIRECT_COMPANION_SERVICE_DEFLATE_BASE64__',
             'Write-DirectEmbeddedCompressedFile',
             '__DIRECT_COMPANION_CERTIFICATE_BASE64__',
             'Write-DirectEmbeddedDataFile',
             'arm-recovery-test',
             'HVR1_CRASH_RECOVERY_ARMED=PASS',
             'SUSPENDED_2_REBOOTS',
             '-RebootCount 2',
             'hypervisorlaunchtype-baseline.txt',
             'hypervisorlaunchtype off',
             "'Cert:\LocalMachine\TrustedPublisher'",
             "'type=' 'kernel' 'start=' 'demand'",
             "Join-Path `$Stage 'shimx64.efi'",
            "Join-Path `$Stage 'payload.efi'",
            "Join-Path `$Stage 'tcglog.ib'")) {
        Assert-True ($WindowsSource.Contains($Token)) (
            "Windows guest contract is missing: $Token")
    }
    foreach ($Token in @(
            'IB_PAYLOAD_REPLAY_INDEPENDENT=$PayloadPolicy',
            'LoaderEfiPath = $LoaderBuild.LoaderPath',
             '-LoaderEfiPath ([string]$Build.LoaderEfiPath)',
             '$DirectLoaderPlaceholder',
             '$DirectCompanionDriverPlaceholder',
             '$DirectCompanionServicePlaceholder',
             'hvr1_overlay_adapter_tests',
             'Compress-UhrArtifactBytes',
             '$DirectCompanionCertificatePlaceholder',
             '$DirectProcessProfilePlaceholder',
             'Get-UhrSignToolPath',
             'New-SelfSignedCertificate',
             "'sign', '/v', '/fd', 'SHA256', '/ph'",
             'SignTool page-hash signing failed')) {
        Assert-True ($RunnerSource.Contains($Token)) (
            "Direct Windows combined staging contract is missing: $Token")
    }
    $DirectStartupSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\Invoke-WindowsDirectHypervisorStartup.ps1')
    foreach ($Token in @(
            'KernelProfileImage', 'KernelProfilePdb',
            'HYPERSINV_REQUIRE_KERNEL_PROFILE ON',
            'HVR1_PROCESS_LOCATE=PASS',
            "@('analysis-start', `$ShadowPid)",
            "@('analysis-configure', 'mz', '4D 5A', '2')",
            "@('analysis-scan', 'first', '0x10000000')",
            "'analysis-publish'",
            "'Local\HypersinvOverlayGateV2'",
            'HVR1_OVERLAY_PUBLISHER=PASS',
            "@('analysis-stop')",
            'HVR1_DYNAMIC_ANALYSIS=PASS',
            '$DirectResultTimeoutSeconds',
            '(?=UEFI2HSINV-DIRECT:',
            "@('shadow-bind', `$ShadowPid)",
            "@('shadow-status')",
            "@('shadow-unbind')",
            'HVR1_SHADOW_EPT=PASS',
            '__DIRECT_PROCESS_PROFILE_ENABLED__')) {
        Assert-True (($RunnerSource + $WindowsSource +
                $DirectStartupSource).Contains($Token)) (
            "Pinned process-enumeration gate token is missing: $Token")
    }
    Assert-True ($WindowsSource -notmatch
        'Invoke-Expression|Start-Process\s+-FilePath\s+\$[A-Za-z]+Path') (
        'Windows guest runner exposes an arbitrary execution surface.')

    $GuestInfoSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\Invoke-UEFILoaderGuestInfo.ps1')
    foreach ($Token in @(
            'Assert-ExactRequest', 'Expand-VerifiedPackage',
            'The package must contain exactly the allowlisted root files',
            "'Probe', 'Capture'", 'QUARANTINED.json',
            'Invoke-WindowsGuestTest.ps1', 'RequestHmacSha256',
            'ResponseHmacSha256', 'taskkill.exe',
            'process tree was terminated')) {
        Assert-True ($GuestInfoSource.Contains($Token)) (
            "Windows mailbox contract is missing: $Token")
    }
    Assert-True ($GuestInfoSource -notmatch
        'Invoke-Expression|runProgramInGuest|CopyFileFromHostToGuest|\-gu|\-gp') (
        'Passwordless mailbox contains a forbidden guest-auth/execution surface.')
    $GuestInfoHostSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'Invoke-UEFILoaderHostRunner.ps1')
    foreach ($Token in @(
            'Test-VmwareGuestInfo', 'writeVariable', 'readVariable',
            'RequestHmacSha256', 'ResponseHmacSha256',
            'Chunk', 'manifest', 'kernel-metadata.json')) {
        Assert-True ($GuestInfoHostSource.Contains($Token)) (
            "Guest Variables transport contract is missing: $Token")
    }
    $GuestInfoLines = @($GuestInfoHostSource -split "`r?`n" | Where-Object {
        $_ -match '(?i)GuestInfo|guestinfo|writeVariable|readVariable'
    })
    Assert-True (($GuestInfoLines -join "`n") -notmatch
        '(?i)\-gu\b|\-gp\b|runProgramInGuest|Invoke-Expression|Start-Process') (
        'Guest Variables host path contains a guest-auth or arbitrary execution surface.')
    foreach ($Token in @(
            "'uefiloader'", 'request\.manifest', 'response\.manifest',
            'request\.chunk', 'response\.chunk',
            'PackageSize', 'PackageChunkSize', 'PackageChunkCount',
            'EvidenceSha256', 'EvidenceSize', 'EvidenceChunkSize',
            'EvidenceChunkCount', 'evidence.zip')) {
        Assert-True (($GuestInfoSource + $GuestInfoHostSource) -match $Token) (
            "Guest Variables shared contract token is missing: $Token")
    }
    Assert-True (($GuestInfoSource + $GuestInfoHostSource) -notmatch
        '(?i)UHR1|guestinfo\.UHR1|PackageLength|EvidenceCount|\.req\.c|\.res\.e\.') (
        'Guest Variables contract retains an obsolete namespace or schema field.')
    Assert-True ($GuestInfoHostSource -match
        '(?is)ManifestBytes.{0,250}ToBase64String') (
        'Host Guest Variables manifests must be base64-encoded JSON.')
    Assert-True ($GuestInfoSource.Contains('ConvertFrom-Base64Exact')) (
        'Guest Guest Variables manifests must be decoded with bounded base64 validation.')
    foreach ($Token in @(
            'WriteDeadline', 'PickupDeadline', 'Deadline',
            'PollMilliseconds', 'WaitForExit', 'taskkill.exe')) {
        Assert-True (($GuestInfoSource + $GuestInfoHostSource).Contains($Token)) (
            "Guest Variables bounded-wait token is missing: $Token")
    }
    $InstallerPath = Join-Path $RunnerRoot 'guest\Install-UEFILoaderGuestInfo.ps1'
    if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
        $InstallerPath = Join-Path $RunnerRoot 'guest\Install-UEFILoaderGuestMailbox.ps1'
    }
    $MailboxInstaller = Get-Content -Raw -LiteralPath $InstallerPath
    $MailboxService = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'guest\UEFILoaderGuestMailboxService.cs')
    foreach ($Token in @(
            'UEFILoaderGuestMailboxService', 'New-Service',
            'StartupType Automatic', "DependsOn 'VMTools'",
            'LocalSystem', 'System.ServiceProcess.dll',
            'failureflag', 'restart/5000', 'ServiceBase',
            'ProcessStartInfo', 'CreateNoWindow = true')) {
        Assert-True (($MailboxInstaller + $MailboxService).Contains($Token)) (
            "Windows mailbox service contract is missing: $Token")
    }
    Assert-True (($MailboxInstaller + $MailboxService) -notmatch
        '(?i)AutoAdminLogon|DefaultPassword|New-ScheduledTaskTrigger') (
        'Windows mailbox service depends on autologon or a scheduled trigger.')
    Assert-True ($MailboxInstaller -notmatch
        '(?i)Get-Credential|Read-Host|ConvertTo-SecureString') (
        'Windows mailbox installer prompts for or stores a non-empty password.')

    $GitIgnore = Get-Content -Raw -LiteralPath (
        Join-Path $ProjectRoot '.gitignore')
    Assert-True ($GitIgnore.Contains('/.analysis/')) (
        'Runner evidence/credential state is not ignored.')
    Assert-True ($GitIgnore.Contains(
        '/tools/host-runner/host-runner.config.json')) (
        'Machine-local runner configuration is not ignored.')

    Write-Host 'PASS: UEFILoader host-runner contracts verified.'
}
finally {
    if (Test-Path -LiteralPath $TestRoot -PathType Container) {
        $Resolved = [IO.Path]::GetFullPath($TestRoot)
        $ExpectedRoot = [IO.Path]::GetFullPath(
            (Join-Path $ProjectRoot 'build\host-runner-tests')).TrimEnd('\') + '\'
        if ($Resolved.StartsWith(
                $ExpectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $Resolved -Recurse -Force
        }
    }
}
