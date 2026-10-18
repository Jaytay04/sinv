[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('DisableIntelE1000', 'RestoreBackup')]
    [string]$Action,
    [string]$BackupPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$vmxPath = Join-Path $mainRoot 'spoof-handoff\artifacts\combined-vm-20261018\DeviceFilter-Win11-Combined\DeviceFilter-Win11-Combined.vmx'
$reportRoot = Join-Path $mainRoot 'spoof-handoff\reports\combined-9field-vm-20261018\vm-execution\vmx-topology'
$configPath = Join-Path $mainRoot 'tools\host-runner\host-runner.config.json'

function Get-Sha256Hex([byte[]]$Bytes) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($Bytes)).Replace('-', '') }
    finally { $algorithm.Dispose() }
}

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Assert-TargetVmOff([string]$Vmx, [string]$Vmrun) {
    $output = @(& $Vmrun -T ws list 2>&1 | ForEach-Object { [string]$_ })
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) { throw ('Unable to query VMware running state: ' + ($output -join ' | ')) }
    $normalized = [IO.Path]::GetFullPath($Vmx)
    foreach ($line in $output) {
        if ([string]$line -match '^[A-Za-z]:\\.*\.vmx$' -and
            [IO.Path]::GetFullPath([string]$line).Equals($normalized, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The authorized combined VM must be powered off before changing its VMX.'
        }
    }
}

function Get-AsciiVmxText([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -eq 0) { throw 'The authorized VMX is empty.' }
    if ($bytes | Where-Object { $_ -gt 127 } | Select-Object -First 1) {
        throw 'The authorized VMX is not plain ASCII; refusing a text rewrite that could alter unrelated bytes.'
    }
    return [pscustomobject]@{ Bytes = $bytes; Text = [Text.Encoding]::ASCII.GetString($bytes) }
}

function Assert-Topology([string]$Text, [bool]$E1000Present) {
    $expectedIntel = if ($E1000Present) { 'TRUE' } else { 'FALSE' }
    foreach ($key in @('ethernet0', 'ethernet1')) {
        $line = $key + '.present = "' + $expectedIntel + '"'
        $count = ([regex]::Matches($Text, '(?m)^' + [regex]::Escape($line) + '\r?$')).Count
        if ($count -ne 1) { throw "VMX topology invariant failed: expected exactly one '$line'; found $count." }
    }
    foreach ($key in @('ethernet2', 'ethernet3')) {
        $line = $key + '.present = "TRUE"'
        $count = ([regex]::Matches($Text, '(?m)^' + [regex]::Escape($line) + '\r?$')).Count
        if ($count -ne 1) { throw "VMX topology invariant failed: expected exactly one '$line'; found $count." }
    }
}

if (-not (Test-Path -LiteralPath $vmxPath -PathType Leaf)) { throw "Authorized VMX is missing: $vmxPath" }
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { throw "Host-runner configuration is missing: $configPath" }
$vmrunPath = [string]((Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json).Tools.VmrunPath)
if (-not (Test-Path -LiteralPath $vmrunPath -PathType Leaf)) { throw 'Configured VMware vmrun executable is missing.' }
Assert-TargetVmOff $vmxPath $vmrunPath

[void][IO.Directory]::CreateDirectory($reportRoot)
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.' + [Guid]::NewGuid().ToString('N')
$manifestPath = Join-Path $reportRoot ('vmx-topology.' + $runId + '.json')

if ($Action -ceq 'DisableIntelE1000') {
    $source = Get-AsciiVmxText $vmxPath
    $originalBytes = [byte[]]$source.Bytes
    $originalHash = Get-Sha256Hex $originalBytes
    Assert-Topology $source.Text $true
    $backupPath = Join-Path $reportRoot ('vmx-original.' + $runId + '.vmx')
    [IO.File]::WriteAllBytes($backupPath, $originalBytes)
    if ((Get-FileSha256 $backupPath) -cne $originalHash) { throw 'VMX backup failed byte-for-byte verification.' }

    try {
        $updatedText = $source.Text.Replace('ethernet0.present = "TRUE"', 'ethernet0.present = "FALSE"').Replace('ethernet1.present = "TRUE"', 'ethernet1.present = "FALSE"')
        Assert-Topology $updatedText $false
        $updatedBytes = [Text.Encoding]::ASCII.GetBytes($updatedText)
        [IO.File]::WriteAllBytes($vmxPath, $updatedBytes)
        $after = Get-AsciiVmxText $vmxPath
        Assert-Topology $after.Text $false
        $afterHash = Get-Sha256Hex $after.Bytes
        $evidence = [ordered]@{
            schema = 'combined-vm-vmx-topology.v1'
            action = $Action
            vmxPath = $vmxPath
            vmxOriginalPath = $backupPath
            vmxOriginalSha256 = $originalHash
            vmxAfterSha256 = $afterHash
            changedLines = @(
                [ordered]@{ before = 'ethernet0.present = "TRUE"'; after = 'ethernet0.present = "FALSE"' },
                [ordered]@{ before = 'ethernet1.present = "TRUE"'; after = 'ethernet1.present = "FALSE"' }
            )
            unchangedRequiredLines = @('ethernet2.present = "TRUE"', 'ethernet3.present = "TRUE"')
            backupVerifiedByteIdentical = $true
            recordedUtc = [DateTime]::UtcNow.ToString('o')
        }
        $evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8
        Write-Output "VMX TOPOLOGY UPDATED: $manifestPath"
        Write-Output "ORIGINAL VMX BACKUP: $backupPath"
        Write-Output "ORIGINAL SHA256: $originalHash"
        Write-Output "UPDATED SHA256: $afterHash"
    }
    catch {
        [IO.File]::WriteAllBytes($vmxPath, $originalBytes)
        if ((Get-FileSha256 $vmxPath) -cne $originalHash) {
            throw ('VMX topology update failed and automatic byte restore did not verify; backup=' + $backupPath + '; original error=' + $_.Exception.Message)
        }
        throw
    }
    return
}

if ([string]::IsNullOrWhiteSpace($BackupPath)) { throw 'RestoreBackup requires the exact vmx-original backup path.' }
$backupFull = [IO.Path]::GetFullPath($BackupPath)
$reportPrefix = [IO.Path]::GetFullPath($reportRoot).TrimEnd('\') + '\'
if (-not $backupFull.StartsWith($reportPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($backupFull) -notmatch '^vmx-original\.[0-9]{8}T[0-9]{6}Z\.[0-9a-f]{32}\.vmx$') {
    throw 'BackupPath must identify a VMX backup created by this tool under its report directory.'
}
if (-not (Test-Path -LiteralPath $backupFull -PathType Leaf)) { throw "VMX backup is missing: $backupFull" }
$backupBytes = [IO.File]::ReadAllBytes($backupFull)
$backupHash = Get-Sha256Hex $backupBytes
$current = Get-AsciiVmxText $vmxPath
$currentHash = Get-Sha256Hex $current.Bytes
Assert-Topology $current.Text $false
[IO.File]::WriteAllBytes($vmxPath, $backupBytes)
$restored = Get-AsciiVmxText $vmxPath
Assert-Topology $restored.Text $true
$restoredHash = Get-Sha256Hex $restored.Bytes
if ($restoredHash -cne $backupHash) { throw 'Restored VMX bytes do not match the recorded original backup.' }
$evidence = [ordered]@{
    schema = 'combined-vm-vmx-topology-restore.v1'
    action = $Action
    vmxPath = $vmxPath
    vmxBackupPath = $backupFull
    preRestoreVmxSha256 = $currentHash
    backupSha256 = $backupHash
    restoredVmxSha256 = $restoredHash
    byteIdenticalToBackup = $true
    recordedUtc = [DateTime]::UtcNow.ToString('o')
}
$evidence | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "VMX TOPOLOGY RESTORED BYTE-FOR-BYTE: $manifestPath"
Write-Output "RESTORED VMX SHA256: $restoredHash"
