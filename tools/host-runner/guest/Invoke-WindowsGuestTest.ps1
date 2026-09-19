[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RunRoot,
    [Parameter(Mandatory=$true)]
    [ValidateSet('Probe', 'Capture')]
    [string]$Action,
    [Parameter(Mandatory=$true)]
    [ValidateSet('MET', 'NOT_MET')]
    [string]$ExpectedProbeVerdict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# These values are replaced by the host when it creates a bounded guest
# package. DirectHypervisorSetup boots loader.efi through an owned BootNext
# entry and stages the EFI hypervisor as its payload. DirectHypervisorVerify
# proves the payload's CPUID contract after Windows returns, then removes the
# owned firmware and ESP state.
$DirectHypervisorMode = '__DIRECT_HYPERVISOR_MODE__'
$DirectLoaderEfiBase64 = '__DIRECT_LOADER_EFI_BASE64__'
$DirectHypervisorEfiBase64 = '__DIRECT_HYPERVISOR_EFI_BASE64__'
$DirectInstallerBase64 = '__DIRECT_INSTALLER_BASE64__'
$DirectStartupScriptBase64 = '__DIRECT_STARTUP_SCRIPT_BASE64__'
$DirectCompanionDriverBase64 = '__DIRECT_COMPANION_DRIVER_BASE64__'
$DirectCompanionServiceBase64 = '__DIRECT_COMPANION_SERVICE_BASE64__'
$DirectCompanionCertificateBase64 = '__DIRECT_COMPANION_CERTIFICATE_BASE64__'
$DirectHypervisorProcessorCount = __DIRECT_PROCESSOR_COUNT__

$RunId = Split-Path -Leaf $RunRoot
if ($RunId -cnotmatch `
        '^uefi-windows-(probe|capture)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$') {
    throw "Unsafe RunId: $RunId"
}
$ResultPath = Join-Path $RunRoot 'result.json'
$Result = [ordered]@{
    SchemaVersion = 1
    RunId = $RunId
    Status = 'FAIL'
    Message = 'Windows guest workflow failed.'
    CompletedUtc = $null
}

function Write-Result {
    $Result.CompletedUtc = [DateTime]::UtcNow.ToString('o')
    $Temporary = $ResultPath + '.tmp'
    $Result | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $Temporary -Encoding utf8
    Move-Item -LiteralPath $Temporary -Destination $ResultPath -Force
}

function Invoke-DirectProcess {
    param(
        [Parameter(Mandatory=$true)][string]$FilePath,
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [Parameter(Mandatory=$true)][string]$StdoutPath,
        [Parameter(Mandatory=$true)][string]$StderrPath
    )
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $FilePath
    $Info.Arguments = (($Arguments | ForEach-Object {
        '"' + ([string]$_).Replace('"', '\"') + '"'
    }) -join ' ')
    $Info.WorkingDirectory = $RunRoot
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) {
        throw "Failed to start $([IO.Path]::GetFileName($FilePath))."
    }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit(120000)) {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            & "$env:SystemRoot\System32\taskkill.exe" /PID $Process.Id /T /F |
                Out-Null
        }
        throw "$([IO.Path]::GetFileName($FilePath)) timed out."
    }
    $Process.WaitForExit()
    [IO.File]::WriteAllText($StdoutPath, [string]$StdoutTask.Result,
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($StderrPath, [string]$StderrTask.Result,
        [Text.UTF8Encoding]::new($false))
    $ExitCode = [int]$Process.ExitCode
    $Process.Dispose()
    return $ExitCode
}

function Invoke-WithDirectEsp {
    param([Parameter(Mandatory=$true)][scriptblock]$Operation)
    $EspDrive = $null
    foreach ($Letter in @('R','S','T','U','V','W','X','Y','Z')) {
        if (-not (Test-Path -LiteralPath ($Letter + ':\'))) {
            $EspDrive = $Letter + ':'
            break
        }
    }
    if ($null -eq $EspDrive) { throw 'No bounded ESP mount letter is free.' }
    try {
        & "$env:SystemRoot\System32\mountvol.exe" $EspDrive /S | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "mountvol /S exited with code $LASTEXITCODE."
        }
        & $Operation $EspDrive
    }
    finally {
        if ($null -ne $EspDrive) {
            & "$env:SystemRoot\System32\mountvol.exe" $EspDrive /D | Out-Null
        }
    }
}

function Write-DirectEmbeddedFile {
    param(
        [Parameter(Mandatory=$true)][string]$Base64,
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Description
    )
    if ([string]::IsNullOrWhiteSpace($Base64)) {
        throw "$Description was not embedded by the host."
    }
    try { $Bytes = [Convert]::FromBase64String($Base64) }
    catch { throw "$Description is not valid base64." }
    try {
        if ($Bytes.Length -lt 1024 -or $Bytes[0] -ne 0x4D -or
            $Bytes[1] -ne 0x5A) {
            throw "$Description is not a bounded PE image."
        }
        [IO.File]::WriteAllBytes($Path, $Bytes)
    }
    finally { [Array]::Clear($Bytes, 0, $Bytes.Length) }
}

function Write-DirectEmbeddedDataFile {
    param(
        [Parameter(Mandatory=$true)][string]$Base64,
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Description,
        [Parameter(Mandatory=$true)][int]$MinimumBytes,
        [Parameter(Mandatory=$true)][int]$MaximumBytes
    )
    if ([string]::IsNullOrWhiteSpace($Base64)) {
        throw "$Description was not embedded by the host."
    }
    try { $Bytes = [Convert]::FromBase64String($Base64) }
    catch { throw "$Description is not valid base64." }
    try {
        if ($Bytes.Length -lt $MinimumBytes -or
            $Bytes.Length -gt $MaximumBytes) {
            throw "$Description has an invalid size."
        }
        [IO.File]::WriteAllBytes($Path, $Bytes)
    }
    finally { [Array]::Clear($Bytes, 0, $Bytes.Length) }
}

function Get-DirectWindowsProcessorTopology {
    if (-not ('DirectWindowsProcessorTopology' -as [type])) {
        Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;

public static class DirectWindowsProcessorTopology
{
    [DllImport("kernel32.dll")]
    public static extern ushort GetActiveProcessorGroupCount();

    [DllImport("kernel32.dll")]
    public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
    }
    $GroupCount = [DirectWindowsProcessorTopology]::GetActiveProcessorGroupCount()
    $Groups = @(
        for ($Group = 0; $Group -lt $GroupCount; ++$Group) {
            [pscustomobject]@{
                Group = $Group
                ActiveProcessors =
                    [DirectWindowsProcessorTopology]::GetActiveProcessorCount(
                        [uint16]$Group)
            }
        }
    )
    [pscustomobject]@{
        GroupCount = $GroupCount
        Groups = $Groups
        ActiveProcessors = [int](
            ($Groups | Measure-Object ActiveProcessors -Sum).Sum)
    }
}

function Invoke-DirectHypervisorSetup {
    if ($DirectHypervisorMode -cne 'Setup') {
        throw 'Direct EFI setup was invoked outside its fixed guest mode.'
    }
    $Installer = Join-Path $RunRoot 'ib-direct-install.exe'
    Write-DirectEmbeddedFile -Base64 $DirectInstallerBase64 `
        -Path $Installer -Description 'Direct EFI installer'
    try {
        $PersistentRoot = Join-Path $env:ProgramData `
            'UEFILoaderDirectHypervisor'
        New-Item -ItemType Directory -Path $PersistentRoot -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $RunRoot 'ib-tcg-dump.exe') `
            -Destination (Join-Path $PersistentRoot 'ib-tcg-dump.exe') -Force
        Copy-Item -LiteralPath $Installer -Destination (Join-Path `
            $PersistentRoot 'ib-direct-install.exe') -Force
        $CompanionDriver = Join-Path $PersistentRoot `
            'hvr1_efi_runtime_companion.sys'
        $CompanionService = Join-Path $PersistentRoot `
            'hvr1_runtime_service.exe'
        Write-DirectEmbeddedFile -Base64 $DirectCompanionDriverBase64 `
            -Path $CompanionDriver -Description 'HVR1 WDM companion'
        Write-DirectEmbeddedFile -Base64 $DirectCompanionServiceBase64 `
            -Path $CompanionService -Description 'HVR1 runtime service'
        $RecoveryArmCode = Invoke-DirectProcess -FilePath $CompanionService `
            -Arguments @('arm-recovery-test') `
            -StdoutPath (Join-Path $RunRoot 'hvr1-recovery-arm.stdout.txt') `
            -StderrPath (Join-Path $RunRoot 'hvr1-recovery-arm.stderr.txt')
        $RecoveryArmOutput = Get-Content -Raw -LiteralPath (Join-Path `
            $RunRoot 'hvr1-recovery-arm.stdout.txt')
        if ($RecoveryArmCode -ne 0 -or
            [regex]::Matches($RecoveryArmOutput,
                '(?m)^HVR1_CRASH_RECOVERY_ARMED=PASS sequence=[1-9][0-9]*\r?$').Count -ne 1) {
            throw 'The HVR1 prior-boot recovery test was not armed.'
        }
        $CertificatePath = Join-Path $PersistentRoot 'hvr1-test.cer'
        Write-DirectEmbeddedDataFile `
            -Base64 $DirectCompanionCertificateBase64 `
            -Path $CertificatePath `
            -Description 'HVR1 disposable public certificate' `
            -MinimumBytes 512 -MaximumBytes 16KB
        $Certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
            $CertificatePath)
        if ($Certificate.HasPrivateKey -or
            $Certificate.Thumbprint -cnotmatch '^[0-9A-F]{40}$') {
            throw 'The embedded HVR1 certificate is not a bounded public certificate.'
        }
        Import-Certificate -FilePath $CertificatePath `
            -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
        Import-Certificate -FilePath $CertificatePath `
            -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null
        $Signature = Get-AuthenticodeSignature -LiteralPath $CompanionDriver
        if ($Signature.Status -ne
                [System.Management.Automation.SignatureStatus]::Valid -or
            $null -eq $Signature.SignerCertificate -or
            $Signature.SignerCertificate.Thumbprint -cne
                $Certificate.Thumbprint) {
            throw "The HVR1 companion test signature is not valid: $($Signature.Status)"
        }
        $Certificate.Thumbprint | Set-Content -LiteralPath (Join-Path `
            $PersistentRoot 'hvr1-test.thumbprint') -Encoding ascii
        Remove-Item -LiteralPath $CertificatePath -Force
        $DriverServiceName = 'HypersinvHvr1EfiRuntimeCompanion'
        & "$env:SystemRoot\System32\sc.exe" create $DriverServiceName `
            'type=' 'kernel' 'start=' 'demand' 'binPath=' $CompanionDriver |
            Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "sc create for the HVR1 companion exited with code $LASTEXITCODE."
        }
        $StartupScriptBytes = $null
        try {
            $StartupScriptBytes = [Convert]::FromBase64String(
                $DirectStartupScriptBase64)
            $StartupScriptText = [Text.Encoding]::UTF8.GetString(
                $StartupScriptBytes)
        }
        catch { throw 'The direct startup script is not valid base64 UTF-8.' }
        finally {
            if ($null -ne $StartupScriptBytes) {
                [Array]::Clear($StartupScriptBytes, 0,
                    $StartupScriptBytes.Length)
            }
        }
        if ([string]::IsNullOrWhiteSpace($StartupScriptText) -or
            $StartupScriptText.Contains('__DIRECT_')) {
            throw 'The direct startup script is empty or has unresolved placeholders.'
        }
        $StartupScript = Join-Path $PersistentRoot `
            'Invoke-WindowsDirectHypervisorStartup.ps1'
        [IO.File]::WriteAllText($StartupScript, $StartupScriptText,
            [Text.UTF8Encoding]::new($false))
        $TaskCommand = '"{0}" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{1}"' -f `
            "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe", `
            $StartupScript
        & "$env:SystemRoot\System32\schtasks.exe" /Create /TN `
            'UEFILoader-Direct-Hypervisor-Verify' /SC ONSTART /RU SYSTEM `
            /RL HIGHEST /TR $TaskCommand /F | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "schtasks /Create exited with code $LASTEXITCODE."
        }
        & "$env:SystemRoot\System32\schtasks.exe" /Query /TN `
            'UEFILoader-Direct-Hypervisor-Verify' | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw 'The direct hypervisor startup task could not be verified.'
        }
        $Topology = Get-DirectWindowsProcessorTopology
        $Topology | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath (Join-Path $RunRoot `
                'direct-windows-topology.json') -Encoding utf8
        $ManageBde = "$env:SystemRoot\System32\manage-bde.exe"
        $BitLockerText = (& $ManageBde -status C: 2>&1 | Out-String)
        $BitLockerExitCode = $LASTEXITCODE
        [IO.File]::WriteAllText((Join-Path $RunRoot `
                'direct-bitlocker-status.txt'), $BitLockerText,
            [Text.UTF8Encoding]::new($false))
        if ($BitLockerExitCode -ne 0) {
            throw "manage-bde status exited with code $BitLockerExitCode."
        }
        $BitLockerWasProtected = $BitLockerText -match
            '(?im)^\s*Protection Status:\s+Protection On\s*$'
        [IO.File]::WriteAllText((Join-Path $PersistentRoot `
                'bitlocker-was-protected.txt'),
            $(if ($BitLockerWasProtected) { '1' } else { '0' }),
            [Text.ASCIIEncoding]::new())
        if ($BitLockerWasProtected) {
            & $ManageBde -protectors -disable C: -RebootCount 2 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "manage-bde protector suspension exited with code $LASTEXITCODE."
            }
        }
        $BcdText = (& "$env:SystemRoot\System32\bcdedit.exe" /enum `
            '{current}' 2>&1 | Out-String)
        $BcdExitCode = $LASTEXITCODE
        [IO.File]::WriteAllText((Join-Path $RunRoot 'direct-bcd-current.txt'),
            $BcdText, [Text.UTF8Encoding]::new($false))
        if ($BcdExitCode -ne 0) {
            throw "bcdedit /enum current exited with code $BcdExitCode."
        }
        $BcdEdit = "$env:SystemRoot\System32\bcdedit.exe"
        $TestSigningWasEnabled = $BcdText -match `
            '(?im)^testsigning\s+Yes\s*$'
        [IO.File]::WriteAllText((Join-Path $PersistentRoot `
                'testsigning-was-enabled.txt'),
            $(if ($TestSigningWasEnabled) { '1' } else { '0' }),
            [Text.ASCIIEncoding]::new())
        $HypervisorLaunchMatches = [regex]::Matches($BcdText,
            '(?im)^hypervisorlaunchtype\s+(Auto|Off)\s*$')
        if ($HypervisorLaunchMatches.Count -gt 1) {
            throw 'The hypervisor launch BCD baseline is ambiguous.'
        }
        $HypervisorLaunchBaseline = if (
            $HypervisorLaunchMatches.Count -eq 0) {
            'ABSENT'
        }
        else {
            $HypervisorLaunchMatches[0].Groups[1].Value.ToUpperInvariant()
        }
        [IO.File]::WriteAllText((Join-Path $PersistentRoot `
                'hypervisorlaunchtype-baseline.txt'),
            $HypervisorLaunchBaseline, [Text.ASCIIEncoding]::new())
        if (-not $TestSigningWasEnabled) {
            & $BcdEdit /set '{current}' testsigning on | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "bcdedit /set testsigning on exited with code $LASTEXITCODE."
            }
        }
        & $BcdEdit /set '{current}' hypervisorlaunchtype off | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /set hypervisorlaunchtype off exited with code $LASTEXITCODE."
        }
        & $BcdEdit /debug '{current}' off | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /debug off exited with code $LASTEXITCODE."
        }
        & $BcdEdit /bootdebug '{current}' off | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /bootdebug off exited with code $LASTEXITCODE."
        }
        if ($BcdText -match '(?im)^groupsize\s+') {
            & $BcdEdit /deletevalue '{current}' groupsize | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "bcdedit /deletevalue groupsize exited with code $LASTEXITCODE."
            }
        }
        $NormalizedBcdText = (& $BcdEdit /enum '{current}' 2>&1 | Out-String)
        $NormalizedBcdExitCode = $LASTEXITCODE
        [IO.File]::WriteAllText((Join-Path $RunRoot `
                'direct-bcd-normalized.txt'), $NormalizedBcdText,
            [Text.UTF8Encoding]::new($false))
        if ($NormalizedBcdExitCode -ne 0) {
            throw "normalized bcdedit /enum current exited with code $NormalizedBcdExitCode."
        }
        if ($NormalizedBcdText -notmatch '(?im)^testsigning\s+Yes\s*$' -or
            $NormalizedBcdText -notmatch
                '(?im)^hypervisorlaunchtype\s+Off\s*$') {
            throw 'The disposable boot entry did not retain test signing and disabled Hyper-V launch before reboot.'
        }
        Invoke-WithDirectEsp {
            param($Esp)
            $Stage = $Esp + '\EFI\insecure-boot'
            if (Test-Path -LiteralPath $Stage) {
                throw 'The direct EFI stage already exists.'
            }
            New-Item -ItemType Directory -Path $Stage | Out-Null
            $Loader = Join-Path $Stage 'shimx64.efi'
            Write-DirectEmbeddedFile -Base64 $DirectLoaderEfiBase64 `
                -Path $Loader -Description 'UEFI loader'
            Copy-Item -LiteralPath $Loader `
                -Destination (Join-Path $Stage 'grubx64.efi')
            $Hypervisor = Join-Path $Stage 'payload.efi'
            Write-DirectEmbeddedFile -Base64 $DirectHypervisorEfiBase64 `
                -Path $Hypervisor -Description 'EFI hypervisor'
            $ReferenceLog = Join-Path $RunRoot 'tcglog.ib'
            if (-not (Test-Path -LiteralPath $ReferenceLog -PathType Leaf) -or
                (Get-Item -LiteralPath $ReferenceLog).Length -eq 0) {
                throw 'The direct EFI reference log is absent or empty.'
            }
            Copy-Item -LiteralPath $ReferenceLog `
                -Destination (Join-Path $Stage 'tcglog.ib')
            $BootManager = $Esp + '\EFI\Microsoft\Boot\bootmgfw.efi'
            if (-not (Test-Path -LiteralPath $BootManager -PathType Leaf)) {
                throw 'The Windows boot manager is missing from the ESP.'
            }
            $BootHash = (Get-FileHash -LiteralPath $BootManager `
                -Algorithm SHA256).Hash
            [IO.File]::WriteAllText((Join-Path $Stage `
                'direct-bootmgfw-before.sha256'), $BootHash + "`r`n",
                [Text.ASCIIEncoding]::new())
            $Lines = @('schema=1')
            foreach ($Name in @(
                    'shimx64.efi', 'grubx64.efi', 'payload.efi',
                    'tcglog.ib')) {
                $Path = Join-Path $Stage $Name
                $Item = Get-Item -LiteralPath $Path
                $Hash = (Get-FileHash -LiteralPath $Path `
                    -Algorithm SHA256).Hash
                $Lines += "$Name size=$($Item.Length) sha256=$Hash"
            }
            [IO.File]::WriteAllText((Join-Path $Stage 'manifest.sha256'),
                (($Lines -join "`r`n") + "`r`n"),
                [Text.UTF8Encoding]::new($false))
        }
        $Code = Invoke-DirectProcess -FilePath $Installer -Arguments @(
            'oneshot', '--mode', 'bootnext', '--esp-volume', 'auto') `
            -StdoutPath (Join-Path $RunRoot 'direct-bootnext-set.stdout.txt') `
            -StderrPath (Join-Path $RunRoot 'direct-bootnext-set.stderr.txt')
        if ($Code -ne 0) { throw "Direct BootNext setup exited with code $Code." }
        $BootNextOutput = Get-Content -Raw -LiteralPath (Join-Path $RunRoot `
            'direct-bootnext-set.stdout.txt')
        if ($BootNextOutput -notmatch
                'BootNext set to owned Boot[0-9A-F]{4}; BootOrder was not changed\.') {
            throw 'Direct BootNext setup did not report its ownership contract.'
        }
        [IO.File]::AppendAllText($Stdout,
            "`nDIRECT_HYPERVISOR_SETUP=PASS`n" +
            "DIRECT_HYPERVISOR_BASELINE_ACTIVE_PROCESSORS=$($Topology.ActiveProcessors)`n" +
            "DIRECT_HYPERVISOR_BASELINE_GROUPS=$(($Topology.Groups | ForEach-Object { '{0}:{1}' -f $_.Group, $_.ActiveProcessors }) -join ',')`n" +
            "DIRECT_HYPERVISOR_BASELINE_BCD_BEGIN`n$BcdText" +
            "DIRECT_HYPERVISOR_BASELINE_BCD_END`n" +
            "DIRECT_HYPERVISOR_NORMALIZED_BCD_BEGIN`n$NormalizedBcdText" +
            "DIRECT_HYPERVISOR_NORMALIZED_BCD_END`n" +
            "DIRECT_HYPERVISOR_BOOT_PROFILE=NORMALIZED`n" +
            "DIRECT_HYPERVISOR_STARTUP_PROBE=INSTALLED`n" +
            "DIRECT_HYPERVISOR_COMPANION=SIGNED_AND_REGISTERED`n" +
            "DIRECT_HYPERVISOR_CRASH_RECOVERY=ARMED`n" +
            "DIRECT_HYPERVISOR_BITLOCKER_PROTECTORS=$(if ($BitLockerWasProtected) { 'SUSPENDED_2_REBOOTS' } else { 'NOT_ACTIVE' })`n" +
            "DIRECT_HYPERVISOR_EXPECTED_PROCESSORS=$DirectHypervisorProcessorCount`n",
            [Text.UTF8Encoding]::new($false))
    }
    finally {
        Remove-Item -LiteralPath $Installer -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-DirectHypervisorVerify {
    if ($DirectHypervisorMode -cne 'Verify') {
        throw 'Direct EFI verification was invoked outside its fixed guest mode.'
    }
    $Lines = @(Get-Content -LiteralPath $Stdout)
    $CpuLines = @($Lines | Where-Object {
        $_ -cmatch '^UEFI_HYPERVISOR_WINDOWS_CPU:([0-9]+):([0-9]+):PASS$'
    })
    $AllHealthLines = @($Lines | Where-Object {
        $_ -cmatch '^UEFI_HYPERVISOR_WINDOWS_CPU:'
    })
    $Keys = @($CpuLines | ForEach-Object {
        if ($_ -cmatch '^UEFI_HYPERVISOR_WINDOWS_CPU:([0-9]+):([0-9]+):PASS$') {
            $Matches[1] + ':' + $Matches[2]
        }
    } | Sort-Object -Unique)
    $Aggregate = @($Lines | Where-Object {
        $_ -ceq 'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS'
    })
    $HealthError = $null
    if ($AllHealthLines.Count -ne $DirectHypervisorProcessorCount -or
        $CpuLines.Count -ne $DirectHypervisorProcessorCount -or
        $Keys.Count -ne $DirectHypervisorProcessorCount -or
        $Aggregate.Count -ne 1) {
        $HealthError = 'The EFI hypervisor CPUID contract did not pass on every expected Windows processor.'
    }
    $Installer = Join-Path $RunRoot 'ib-direct-install.exe'
    Write-DirectEmbeddedFile -Base64 $DirectInstallerBase64 `
        -Path $Installer -Description 'Direct EFI installer'
    try {
        $Code = Invoke-DirectProcess -FilePath $Installer -Arguments @(
            'oneshot', '--mode', 'off', '--esp-volume', 'auto') `
            -StdoutPath (Join-Path $RunRoot 'direct-bootnext-clear.stdout.txt') `
            -StderrPath (Join-Path $RunRoot 'direct-bootnext-clear.stderr.txt')
        if ($Code -ne 0) { throw "Direct BootNext cleanup exited with code $Code." }
        Invoke-WithDirectEsp {
            param($Esp)
            $Stage = $Esp + '\EFI\insecure-boot'
            $ExpectedHashPath = Join-Path $Stage `
                'direct-bootmgfw-before.sha256'
            if (-not (Test-Path -LiteralPath $ExpectedHashPath -PathType Leaf)) {
                throw 'Direct EFI boot-manager provenance is missing.'
            }
            $ExpectedHash = (Get-Content -Raw `
                -LiteralPath $ExpectedHashPath).Trim()
            $BootManager = $Esp + '\EFI\Microsoft\Boot\bootmgfw.efi'
            $ActualHash = (Get-FileHash -LiteralPath $BootManager `
                -Algorithm SHA256).Hash
            if ($ExpectedHash -cne $ActualHash) {
                throw 'The direct EFI cycle changed bootmgfw.efi.'
            }
            Remove-Item -LiteralPath $Stage -Recurse -Force
        }
        if ($null -ne $HealthError) { throw $HealthError }
        [IO.File]::AppendAllText($Stdout,
            "DIRECT_HYPERVISOR_VERIFY=PASS`n" +
            "DIRECT_HYPERVISOR_VERIFIED_PROCESSORS=$DirectHypervisorProcessorCount`n",
            [Text.UTF8Encoding]::new($false))
    }
    finally {
        Remove-Item -LiteralPath $Installer -Force -ErrorAction SilentlyContinue
    }
}

function Get-KernelSymbolMetadata {
    $KernelPath = Join-Path $env:SystemRoot 'System32\ntoskrnl.exe'
    $Bytes = [IO.File]::ReadAllBytes($KernelPath)
    try {
        function Assert-ByteRange([int64]$Offset, [int64]$Length,
            [string]$Description) {
            if ($Offset -lt 0 -or $Length -lt 0 -or
                $Offset + $Length -gt $Bytes.LongLength) {
                throw "The kernel PE $Description is outside the image."
            }
        }
        function Read-UInt16([int64]$Offset) {
            Assert-ByteRange $Offset 2 'field'
            return [BitConverter]::ToUInt16($Bytes, [int]$Offset)
        }
        function Read-UInt32([int64]$Offset) {
            Assert-ByteRange $Offset 4 'field'
            return [BitConverter]::ToUInt32($Bytes, [int]$Offset)
        }
        function Convert-RvaToFileOffset([uint32]$Rva, [int]$SectionTable,
            [int]$SectionCount) {
            for ($SectionIndex = 0; $SectionIndex -lt $SectionCount;
                    $SectionIndex++) {
                $Section = $SectionTable + ($SectionIndex * 40)
                Assert-ByteRange $Section 40 'section header'
                $VirtualSize = Read-UInt32 ($Section + 8)
                $VirtualAddress = Read-UInt32 ($Section + 12)
                $RawSize = Read-UInt32 ($Section + 16)
                $RawPointer = Read-UInt32 ($Section + 20)
                $MappedSize = [Math]::Max([uint64]$VirtualSize,
                    [uint64]$RawSize)
                if ([uint64]$Rva -ge [uint64]$VirtualAddress -and
                    [uint64]$Rva -lt [uint64]$VirtualAddress + $MappedSize) {
                    $Delta = [uint64]$Rva - [uint64]$VirtualAddress
                    if ($Delta -ge [uint64]$RawSize) {
                        throw 'The kernel PE debug directory is not file-backed.'
                    }
                    return [int64]([uint64]$RawPointer + $Delta)
                }
            }
            throw 'The kernel PE debug directory RVA is not mapped by a section.'
        }

        Assert-ByteRange 0 64 'DOS header'
        if ($Bytes[0] -ne 0x4D -or $Bytes[1] -ne 0x5A) {
            throw 'The Windows kernel does not have an MZ header.'
        }
        $PeOffset = [int](Read-UInt32 0x3C)
        Assert-ByteRange $PeOffset 24 'NT header'
        if ($Bytes[$PeOffset] -ne 0x50 -or $Bytes[$PeOffset + 1] -ne 0x45 -or
            $Bytes[$PeOffset + 2] -ne 0 -or $Bytes[$PeOffset + 3] -ne 0) {
            throw 'The Windows kernel does not have a PE signature.'
        }
        $SectionCount = [int](Read-UInt16 ($PeOffset + 6))
        $OptionalSize = [int](Read-UInt16 ($PeOffset + 20))
        $Optional = $PeOffset + 24
        Assert-ByteRange $Optional $OptionalSize 'optional header'
        $Magic = Read-UInt16 $Optional
        $DataDirectory = if ($Magic -eq 0x20B) {
            $Optional + 112
        }
        elseif ($Magic -eq 0x10B) {
            $Optional + 96
        }
        else {
            throw ('Unsupported Windows kernel PE optional-header magic: 0x{0:X}' -f $Magic)
        }
        Assert-ByteRange ($DataDirectory + (6 * 8)) 8 'debug data directory'
        $DebugRva = Read-UInt32 ($DataDirectory + (6 * 8))
        $DebugSize = Read-UInt32 ($DataDirectory + (6 * 8) + 4)
        if ($DebugRva -eq 0 -or $DebugSize -lt 28 -or ($DebugSize % 28) -ne 0) {
            throw 'The Windows kernel PE debug directory is absent or malformed.'
        }
        $SectionTable = $Optional + $OptionalSize
        $DebugOffset = Convert-RvaToFileOffset $DebugRva $SectionTable $SectionCount
        Assert-ByteRange $DebugOffset $DebugSize 'debug directory'
        $CodeViewOffset = $null
        $CodeViewSize = 0
        for ($Index = 0; $Index -lt ($DebugSize / 28); $Index++) {
            $Entry = $DebugOffset + ($Index * 28)
            if ((Read-UInt32 ($Entry + 12)) -eq 2) {
                $CodeViewSize = [int](Read-UInt32 ($Entry + 16))
                $CodeViewOffset = [int64](Read-UInt32 ($Entry + 24))
                break
            }
        }
        if ($null -eq $CodeViewOffset -or $CodeViewSize -lt 25) {
            throw 'The Windows kernel PE has no usable CodeView record.'
        }
        Assert-ByteRange $CodeViewOffset $CodeViewSize 'CodeView record'
        if ($Bytes[$CodeViewOffset] -ne 0x52 -or
            $Bytes[$CodeViewOffset + 1] -ne 0x53 -or
            $Bytes[$CodeViewOffset + 2] -ne 0x44 -or
            $Bytes[$CodeViewOffset + 3] -ne 0x53) {
            throw 'The Windows kernel CodeView record is not RSDS.'
        }
        $GuidBytes = [byte[]]::new(16)
        [Array]::Copy($Bytes, $CodeViewOffset + 4, $GuidBytes, 0, 16)
        $Guid = [Guid]::new($GuidBytes)
        $Age = Read-UInt32 ($CodeViewOffset + 20)
        $NameLength = $CodeViewSize - 24
        $PdbName = [Text.Encoding]::ASCII.GetString(
            $Bytes, [int]($CodeViewOffset + 24), $NameLength).TrimEnd([char]0)
        if ([string]::IsNullOrWhiteSpace($PdbName) -or
            [IO.Path]::GetFileName($PdbName) -cne $PdbName -or
            $PdbName -cnotmatch '^[A-Za-z0-9_.-]+\.pdb$') {
            throw 'The Windows kernel CodeView PDB name is unsafe.'
        }
        $Version = [Diagnostics.FileVersionInfo]::GetVersionInfo($KernelPath)
        return [ordered]@{
            SchemaVersion = 1
            KernelFileName = 'ntoskrnl.exe'
            FileVersion = [string]$Version.FileVersion
            ProductVersion = [string]$Version.ProductVersion
            PdbGuid = $Guid.ToString('D').ToUpperInvariant()
            PdbAge = [uint32]$Age
            PdbName = $PdbName
            SymbolKey = $Guid.ToString('N').ToUpperInvariant() +
                $Age.ToString('X')
            Sha256 = (Get-FileHash -LiteralPath $KernelPath `
                -Algorithm SHA256).Hash
        }
    }
    finally {
        [Array]::Clear($Bytes, 0, $Bytes.Length)
    }
}

try {
    $NestedPrep = $Action -eq 'Capture' -and
        $ExpectedProbeVerdict -eq 'MET'
    $Expected = @{}
    foreach ($Line in Get-Content -LiteralPath (Join-Path $RunRoot 'SHA256SUMS.txt')) {
        if ($Line -cnotmatch '^([0-9A-F]{64})  ([A-Za-z0-9_.-]+)$') {
            throw "Malformed checksum line: $Line"
        }
        $Expected[$Matches[2]] = $Matches[1]
    }
    $RequiredFiles = @('ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1')
    foreach ($Name in $RequiredFiles) {
        $Path = Join-Path $RunRoot $Name
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Staged file is missing: $Name"
        }
        $Actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        if ($Expected[$Name] -cne $Actual) {
            throw "Checksum mismatch: $Name"
        }
    }

    $Executable = Join-Path $RunRoot 'ib-tcg-dump.exe'
    $Stdout = Join-Path $RunRoot 'stdout.txt'
    $Stderr = Join-Path $RunRoot 'stderr.txt'
    if ($Action -eq 'Probe') {
        $ArgumentText = 'probe'
    }
    else {
        $ArgumentText = 'capture --out "{0}" --full "{1}"' -f
            (Join-Path $RunRoot 'tcglog.ib'),
            (Join-Path $RunRoot 'tcglog-full.bin')
    }
    $StartInfo = [Diagnostics.ProcessStartInfo]::new()
    $StartInfo.FileName = $Executable
    $StartInfo.Arguments = $ArgumentText
    $StartInfo.WorkingDirectory = $RunRoot
    $StartInfo.UseShellExecute = $false
    $StartInfo.CreateNoWindow = $true
    $StartInfo.RedirectStandardOutput = $true
    $StartInfo.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $StartInfo
    if (-not $Process.Start()) { throw 'ib-tcg-dump did not start.' }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit(300000)) {
        $Result.Message = 'ib-tcg-dump exceeded its five-minute deadline; it was not terminated.'
        Write-Result
        exit 124
    }
    $Process.WaitForExit()
    $ExitCode = [int]$Process.ExitCode
    $Output = [string]$StdoutTask.Result
    $ErrorOutput = [string]$StderrTask.Result
    [IO.File]::WriteAllText($Stdout, $Output, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($Stderr, $ErrorOutput,
        [Text.UTF8Encoding]::new($false))
    $KernelMetadata = Get-KernelSymbolMetadata
    $KernelMetadata | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $RunRoot 'kernel-metadata.json') `
            -Encoding utf8
    [IO.File]::AppendAllText($Stdout,
        "`nKERNEL_FILE_VERSION=$($KernelMetadata.FileVersion)`n" +
        "KERNEL_PDB_NAME=$($KernelMetadata.PdbName)`n" +
        "KERNEL_SYMBOL_KEY=$($KernelMetadata.SymbolKey)`n" +
        "KERNEL_SHA256=$($KernelMetadata.Sha256)`n",
        [Text.UTF8Encoding]::new($false))
    if ($Action -eq 'Probe') {
        $ExpectedText = if ($ExpectedProbeVerdict -eq 'MET') {
            'verdict: empty-bank precondition MET'
        }
        else {
            'verdict: empty-bank precondition NOT MET'
        }
        if ($Output -notmatch [regex]::Escape($ExpectedText)) {
            throw "Probe output did not contain the configured verdict: $ExpectedText"
        }
        $ExpectedExitCode = if ($ExpectedProbeVerdict -eq 'MET') { 0 } else { 1 }
        if ($ExitCode -ne $ExpectedExitCode) {
            throw "Probe exit code $ExitCode did not match $ExpectedExitCode."
        }
        if ($DirectHypervisorMode -ceq 'Verify') {
            Invoke-DirectHypervisorVerify
        }
    }
    else {
        if ($ExitCode -ne 0) {
            throw "Capture exited with code $ExitCode."
        }
        foreach ($Name in @('tcglog.ib', 'tcglog-full.bin')) {
            $Path = Join-Path $RunRoot $Name
            if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
                (Get-Item -LiteralPath $Path).Length -eq 0) {
                throw "Capture artifact is absent or empty: $Name"
            }
        }
        if ($DirectHypervisorMode -ceq 'Setup') {
            Invoke-DirectHypervisorSetup
        }
    }

    if ($NestedPrep) {
        $BitLockerOutput = Join-Path $RunRoot 'bitlocker-status.txt'
        $ManageBde = Join-Path $env:SystemRoot 'System32\manage-bde.exe'
        $BitLockerText = (& $ManageBde -status C: 2>&1 | Out-String)
        $BitLockerText |
            Set-Content -LiteralPath $BitLockerOutput -Encoding utf8
        if ($LASTEXITCODE -ne 0) {
            throw "manage-bde status exited with code $LASTEXITCODE."
        }

        $BcdEdit = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
        & $BcdEdit /debug '{current}' off | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /debug off exited with code $LASTEXITCODE."
        }
        & $BcdEdit /bootdebug '{current}' off | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /bootdebug off exited with code $LASTEXITCODE."
        }

        $EspDrive = $null
        foreach ($Letter in @('R','S','T','U','V','W','X','Y','Z')) {
            if (-not (Test-Path -LiteralPath ($Letter + ':\'))) {
                $EspDrive = $Letter + ':'
                break
            }
        }
        if ($null -eq $EspDrive) { throw 'No bounded ESP mount letter is free.' }
        try {
            & "$env:SystemRoot\System32\mountvol.exe" $EspDrive /S | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "mountvol /S exited with code $LASTEXITCODE."
            }
            $BootManager = $EspDrive + '\EFI\Microsoft\Boot\bootmgfw.efi'
            if (-not (Test-Path -LiteralPath $BootManager -PathType Leaf)) {
                throw 'The Windows boot manager was not found on the mounted ESP.'
            }
            $BootManagerHash = (Get-FileHash -LiteralPath $BootManager `
                -Algorithm SHA256).Hash
            $BootManagerHash |
                Set-Content -LiteralPath (Join-Path $RunRoot 'bootmgfw.sha256') `
                    -Encoding ascii
        }
        finally {
            if ($null -ne $EspDrive) {
                & "$env:SystemRoot\System32\mountvol.exe" $EspDrive /D |
                    Out-Null
            }
        }

        $PersistentRoot = Join-Path $env:ProgramData 'UEFILoaderNestedE2E'
        New-Item -ItemType Directory -Path $PersistentRoot -Force | Out-Null
        $StartupScript = Join-Path $PersistentRoot 'Invoke-NestedE2ECapture.ps1'
        $ServiceBinary = Join-Path $PersistentRoot 'ib-nested-e2e-service.exe'
        $StartupScriptText = [Text.Encoding]::UTF8.GetString(
            [Convert]::FromBase64String('__NESTED_SCRIPT_BASE64__'))
        [IO.File]::WriteAllText($StartupScript, $StartupScriptText,
            [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllBytes($ServiceBinary,
            [Convert]::FromBase64String('__NESTED_SERVICE_BASE64__'))
        if ((Get-Item -LiteralPath $ServiceBinary).Length -eq 0) {
            throw 'The embedded nested E2E service artifact is empty.'
        }
        $ServiceHash = (Get-FileHash -LiteralPath $ServiceBinary `
            -Algorithm SHA256).Hash
        $TaskCommand = '"{0}" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{1}"' -f `
            "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe", `
            $StartupScript
        & "$env:SystemRoot\System32\schtasks.exe" /Create /TN `
            'UEFILoader-Nested-E2E' /SC ONSTART /RU SYSTEM /RL HIGHEST `
            /TR $TaskCommand /F | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "schtasks /Create exited with code $LASTEXITCODE."
        }
        & "$env:SystemRoot\System32\schtasks.exe" /Query /TN `
            'UEFILoader-Nested-E2E' | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw 'The nested E2E startup task could not be verified.'
        }
        [IO.File]::AppendAllText($Stdout,
            "`nNESTED_PREP_BOOTMGFW_SHA256=$BootManagerHash`n" +
            "NESTED_PREP_TASK=INSTALLED`n" +
            "NESTED_PREP_SERVICE=INSTALLED`n" +
            "NESTED_PREP_SERVICE_SHA256=$ServiceHash`n" +
            "NESTED_PREP_KERNEL_DEBUG=DISABLED`n" +
            "NESTED_PREP_BITLOCKER_BEGIN`n$BitLockerText" +
            "NESTED_PREP_BITLOCKER_END`n",
            [Text.UTF8Encoding]::new($false))
    }

    $Result.Status = 'PASS'
    $Result.Message = if ($Action -eq 'Probe') {
        if ($DirectHypervisorMode -ceq 'Verify') {
            "Windows verified the EFI hypervisor on $DirectHypervisorProcessorCount processors."
        } else {
            "Windows TPM/TBS probe matched $ExpectedProbeVerdict."
        }
    }
    elseif ($DirectHypervisorMode -ceq 'Setup') {
        'Windows installed the owned loader plus EFI hypervisor BootNext entry.'
    }
    elseif ($NestedPrep) {
        'Windows reference log captured and one-shot nested E2E startup task installed.'
    }
    else {
        'Windows TBS capture produced both bounded and full logs.'
    }
    Write-Result
    exit 0
}
catch {
    $Result.Message = $_.Exception.Message
    Write-Result
    exit 1
}
