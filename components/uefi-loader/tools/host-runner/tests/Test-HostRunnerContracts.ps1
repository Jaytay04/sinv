[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RunnerRoot = Split-Path -Parent $PSScriptRoot
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $RunnerRoot)
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
            'linux-divergence-manifest', 'linux-handoff-mechanics',
            'linux-production-handoff', 'linux-oneshot-cycle',
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
    Assert-True ($Config.Policy.MaxConcurrentVms -eq 2) (
        'Example config does not permit the two known VMs to coexist.')
    Assert-True ($Config.Policy.MinimumHostReserveMib -ge 512) (
        'Example config has an unsafe host-memory reserve.')
    Assert-True ($Config.Profiles.Linux.LeaseResource -ceq
        'vm-uefiloader-ubuntu64') 'Linux lease resource changed unexpectedly.'
    Assert-True ($Config.Profiles.Windows.LeaseResource -ceq
        'vm-uefiloader-windows-clone') 'Windows lease resource changed unexpectedly.'
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

    $RunnerSource = Get-Content -Raw -LiteralPath (
        Join-Path $RunnerRoot 'Invoke-UEFILoaderHostRunner.ps1')
    Assert-True ($RunnerSource -notmatch
        'Invoke-Expression|typeKeystrokesInGuest|deleteVM') (
        'Runner exposes a forbidden execution or VM-mutation surface.')
    foreach ($Token in @(
            "'RunInstallerHostGate'", "'RunInstallerOneShotHostGate'",
            "'RunAuditHostGate'",
            "'RunLinuxPositivePath'", "'RunLinuxReplayDryRun'", "'RunLinuxPeRehash'",
            "'RunLinuxDivergenceManifest'", "'RunLinuxHandoffMechanics'",
            "'RunLinuxProductionHandoff'",
            "'RunLinuxOneShotCycle'",
            "'RunWslNestedWindowsE2E'", "'RunWslBootNextE2E'",
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
            'MinimumHostReserveMib', '-Resource',
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
    foreach ($Token in @(
            'RunWslBootNextE2E', 'linux-bootnext-e2e',
            'LinuxBootNextE2E')) {
        Assert-True ($RunnerSource.Contains($Token)) (
            "WSL BootNext runner contract is missing: $Token")
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
        Join-Path $ProjectRoot `
            'tools\patches\edk2-stable202605-no-measure.patch')
    Assert-True ($PatchSource.Contains(
        'OvmfPkg/Library/AcpiPlatformLib/QemuFwCfgAcpi.c')) (
        'The no-measure patch does not suppress QEMU ACPI PCR[1] extensions.')
    Assert-True ($LinuxSource -notmatch 'eval\s|sudo\s') (
        'Linux guest runner may elevate or evaluate arbitrary text.')

    $LoaderSource = Get-Content -Raw -LiteralPath (
        Join-Path $ProjectRoot 'src\loader\efi_entry.c')
    foreach ($Token in @('__outbyte', '0xE9', 'debugcon_print')) {
        Assert-True ($LoaderSource.Contains($Token)) (
            "Headless loader diagnostics are missing: $Token")
    }
    foreach ($Token in @(
            'reference_log_load', 'divergence_build', 'replay_uefi_init',
            'replay_run', 'boot_restore_run', 'image_launch_start',
            'handoff-status.txt', 'unsupported active PCR bank')) {
        Assert-True ($LoaderSource.Contains($Token)) (
            "Production loader integration is missing: $Token")
    }
    Assert-True ($LoaderSource -notmatch
        '->ExitBootServices\s*\(') (
        'The production loader must leave ExitBootServices to bootmgfw.')
    $ImageLaunchSource = Get-Content -Raw -LiteralPath (
        Join-Path $ProjectRoot 'src\loader\image_launch.c')
    Assert-True ($ImageLaunchSource -notmatch
        '->ExitBootServices\s*\(') (
        'The loader handoff helper must leave ExitBootServices to bootmgfw.')

    $InstallerCliSource = Get-Content -Raw -LiteralPath (
        Join-Path $ProjectRoot 'src\install\main.c')
    $P6InstallerSource = Get-Content -Raw -LiteralPath (
        Join-Path $ProjectRoot 'src\install\sign.c')
    $P6InstallerSource += Get-Content -Raw -LiteralPath (
        Join-Path $ProjectRoot 'src\install\stage.c')
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
        Join-Path $ProjectRoot 'src\install\oneshot.c')
    $P7InstallerSource += Get-Content -Raw -LiteralPath (
        Join-Path $ProjectRoot 'src\install\bootnext.c')
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
            'UEFILoader-Nested-E2E', 'manage-bde.exe', 'mountvol.exe')) {
        Assert-True ($WindowsSource.Contains($Token)) (
            "Windows guest contract is missing: $Token")
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
            'Chunk', 'manifest')) {
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
