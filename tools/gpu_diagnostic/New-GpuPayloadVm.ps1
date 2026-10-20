[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [string]$WorkspaceRoot,
    [string]$RunDirectory,
    [string]$CredentialStateRoot,
    [string]$VmrunPath = 'C:\Program Files\VMware\VMware Workstation\vmrun.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($WhatIfPreference) {
    [pscustomobject]@{ Action='NewPayloadVm'; Preview=$true; HostBootChanges=$false; VmChanges=$false } | ConvertTo-Json
    return
}
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sourceCommit = (@(& git -C $sourceRoot rev-parse HEAD) -join '').Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit -cnotmatch '^[0-9a-f]{40}$') { throw 'Cannot pin the clone preparation source revision.' }
$sourceBranch = (@(& git -C $sourceRoot branch --show-current) -join '').Trim()
if ($LASTEXITCODE -ne 0 -or $sourceBranch -cne 'feat/smbios-boot-serial-probe') { throw 'Clone preparation requires the reviewed feature branch.' }
$sourceChanges = @(& git -C $sourceRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $sourceChanges.Count -ne 0) { throw 'Clone preparation requires the clean reviewed feature worktree.' }
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot '..\..\..'))
}
Import-Module (Join-Path $PSScriptRoot 'GpuHostState.psm1') -Force -DisableNameChecking
$WorkspaceRoot = Assert-GpuPathWithin -Path $WorkspaceRoot -Root $WorkspaceRoot -AllowRoot -MustExist
$null = Assert-GpuPathWithin -Path $sourceRoot -Root $WorkspaceRoot -MustExist
$buildRoot = Join-Path $WorkspaceRoot '.codex-build'
$vmReceiptPath = Join-Path $WorkspaceRoot '.analysis\gpu-payload-diagnostic\vm-prepared.json'
if (Test-Path -LiteralPath $vmReceiptPath) { throw 'A prepared diagnostic VM receipt already exists; preserve it and review that exact VM before creating another.' }
$metadataPath = Join-Path $WorkspaceRoot '.analysis\host-runner\default-windows-vm.json'
$metadata = Read-GpuJson -Path $metadataPath
$sourceVmx = Assert-GpuPathWithin -Path $metadata.VmxPath -Root $buildRoot -MustExist
$reviewedVmx = Join-Path $WorkspaceRoot '.codex-build\gpu_windows_vm\run-01\standalone\hsinv-Windows-25H2.vmx'
$reviewedVmxHash = '242C46795FEDA50B24D681AD502DAE926D756E0375809CDA8456096B554EAC16'
if (-not [string]::Equals($sourceVmx,$reviewedVmx,[StringComparison]::OrdinalIgnoreCase) -or
    [string]$metadata.VmxSha256 -cne $reviewedVmxHash) { throw 'Inventory does not select the exact reviewed retained VM.' }
if ((Get-GpuFilePin -Path $sourceVmx).Sha256 -cne [string]$metadata.VmxSha256) {
    throw 'Retained VMX differs from its reviewed metadata; preserve it and stop.'
}
if ([string]$metadata.DefaultSnapshot -cne 'hsinv-default-25h2-26200.9457' -or
    [int]$metadata.WindowsBuild -ne 26200 -or [int]$metadata.Ubr -ne 9457 -or
    [int]$metadata.LogicalCpus -ne 4 -or [int]$metadata.MemoryMiB -ne 8192 -or
    $metadata.NestedVirtualization -ne $true -or $metadata.Network -cne 'disabled' -or
    $metadata.PhysicalPassthrough -cne 'none') { throw 'Retained VM metadata is not the reviewed offline nested VM.' }
$certificate = Join-Path $sourceRoot 'components\hypervisor\hypersinv.cer'
if ((Get-GpuFilePin -Path $certificate).Sha256 -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE') {
    throw 'Public signing certificate differs from its reviewed pin.'
}
if ([string]::IsNullOrWhiteSpace($CredentialStateRoot)) { $CredentialStateRoot = Join-Path $WorkspaceRoot '.analysis\host-runner' }
$CredentialStateRoot = Assert-GpuPathWithin -Path $CredentialStateRoot -Root (Join-Path $WorkspaceRoot '.analysis') -MustExist
if (-not [string]::Equals($CredentialStateRoot,(Join-Path $WorkspaceRoot '.analysis\host-runner'),[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Clone preparation uses only the existing protected Host Runner credential directory.'
}
if ([string]::IsNullOrWhiteSpace($RunDirectory)) {
    $RunDirectory = Join-Path $buildRoot ('gpu_payload_vm\vm-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
}
$RunDirectory = Assert-GpuPathWithin -Path $RunDirectory -Root $buildRoot
if (Test-Path -LiteralPath $RunDirectory) { throw 'Clone RunDirectory must be fresh; existing VM state is never overwritten.' }
if (-not $PSCmdlet.ShouldProcess($RunDirectory, 'Create independent disposable VM clone with test-only firmware db defaults')) { return }

# Reuse the protected credential and redacted VMware process helpers. No
# credential values are emitted or copied into this run's files.
Import-Module (Join-Path $sourceRoot 'tools\host-runner\HostRunner.Core.psm1') -Force
$Config = [pscustomobject]@{
    ProjectRoot=$WorkspaceRoot; StateRoot=$CredentialStateRoot
    Tools=[pscustomobject]@{VmrunPath=$VmrunPath}
    Policy=[pscustomobject]@{OperationTimeoutSeconds=60}
}
$Selected = [pscustomobject]@{VmxPath=$sourceVmx;VmCredentialFile='windows-vm.xml'}
$tokens=$null; $parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $sourceRoot 'tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'),[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'Existing Host Runner did not parse.' }
foreach ($name in @('Get-PlainCredential','Invoke-ConfiguredVmrun')) {
    $functions=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$true) | Where-Object {$_.Name -ceq $name})
    if ($functions.Count -ne 1) { throw "Required Host Runner function is missing: $name" }
    . ([scriptblock]::Create($functions[0].Extent.Text))
}
$running=Invoke-ConfiguredVmrun -Selected $Selected -Operation @('list') -TimeoutSeconds 30
foreach ($line in ($running.Stdout -split '\r?\n')) {
    if ([string]::Equals($line.Trim(),$sourceVmx,[StringComparison]::OrdinalIgnoreCase)) { throw 'Retained VM is running; it will not be stopped or cloned in use.' }
}
$snapshots=Invoke-ConfiguredVmrun -Selected $Selected -Operation @('listSnapshots',$sourceVmx) -TimeoutSeconds 30
if (@($snapshots.Stdout -split '\r?\n' | Where-Object {$_.Trim() -ceq [string]$metadata.DefaultSnapshot}).Count -ne 1) { throw 'Reviewed default snapshot was not found exactly once.' }

[void](New-Item -ItemType Directory -Path $RunDirectory)
$cloneRoot = Join-Path $RunDirectory 'clone'
[void](New-Item -ItemType Directory -Path $cloneRoot)
$cloneVmx = Join-Path $cloneRoot 'hsinv-payload-signed.vmx'
$intent=[ordered]@{
    SchemaVersion=1;Status='CLONE_IN_PROGRESS';SourceCommit=$sourceCommit;SourceVmx=$sourceVmx;SourceVmxSha256=[string]$metadata.VmxSha256
    Snapshot=[string]$metadata.DefaultSnapshot;VmxPath=$cloneVmx;RetainedVmMoved=$false;HostBootChanges=$false
    TrustMechanism='VM firmware db append, not MOK enrollment';TrustValidated=$false
}
Write-GpuJson -Path (Join-Path $RunDirectory 'clone-intent.json') -Value $intent -NoOverwrite
try {
    $cloneResult=Invoke-ConfiguredVmrun -Selected $Selected -Operation @('clone',$sourceVmx,$cloneVmx,'full',('-snapshot=' + [string]$metadata.DefaultSnapshot),'-cloneName=hsinv payload signed diagnostic') -TimeoutSeconds 600
    [IO.File]::WriteAllText((Join-Path $RunDirectory 'clone.stdout.txt'),[string]$cloneResult.Stdout)
    [IO.File]::WriteAllText((Join-Path $RunDirectory 'clone.stderr.txt'),[string]$cloneResult.Stderr)
    if ((Get-GpuFilePin -Path $sourceVmx).Sha256 -cne [string]$metadata.VmxSha256) { throw 'Retained source VMX changed while cloning.' }
    $null=Assert-GpuPathWithin -Path $cloneVmx -Root $cloneRoot -MustExist
    $lines=[Collections.Generic.List[string]]::new([IO.File]::ReadAllLines($cloneVmx))
    if (@($lines | Where-Object {$_ -match '^\s*uefi\.secureBoot\.(?:dbDefault|allowAuthBypass)'}).Count -ne 0) { throw 'Clone already has custom Secure Boot policy; refusing to replace it.' }
    $publicCopy=Join-Path $cloneRoot 'hypersinv-public.cer'
    [IO.File]::Copy($certificate,$publicCopy,$false)
    if ((Get-GpuFilePin -Path $publicCopy).Sha256 -cne (Get-GpuFilePin -Path $certificate).Sha256) { throw 'Public certificate clone readback failed.' }
    $settings=[ordered]@{
        'nvram'='gpu-payload-test.nvram'
        'uefi.secureBoot.enabled'='TRUE'
        'uefi.secureBoot.dbDefault.file0'='hypersinv-public.cer'
        'uefi.secureBoot.dbDefault.append'='TRUE'
        'ethernet0.present'='FALSE'
    }
    if (Test-Path -LiteralPath (Join-Path $cloneRoot $settings.nvram)) { throw 'New test NVRAM path already exists.' }
    foreach($key in $settings.Keys) {
        $pattern='^\s*' + [regex]::Escape($key) + '\s*='
        $matches=@(for($i=0;$i -lt $lines.Count;$i++){if($lines[$i] -match $pattern){$i}})
        if($matches.Count -gt 1){throw "Repeated VM setting: $key"}
        $replacement=$key + ' = "' + $settings[$key] + '"'
        if($matches.Count -eq 1){$lines[$matches[0]]=$replacement}else{$lines.Add($replacement)}
    }
    [IO.File]::WriteAllLines($cloneVmx,$lines,[Text.UTF8Encoding]::new($false))
    $intent.Status='CLONE_PREPARED_TRUST_UNVERIFIED'
    $intent.VmxSha256=(Get-GpuFilePin -Path $cloneVmx).Sha256
    $intent.PublicCertificateSha256=(Get-GpuFilePin -Path $publicCopy).Sha256
    $intent.WorkstationSupport='Unverified until VMware log and guest Secure Boot/db readback confirm this ESXi-documented option.'
    Write-GpuJson -Path (Join-Path $RunDirectory 'result.json') -Value $intent -NoOverwrite
    $receiptParent = Split-Path -Parent $vmReceiptPath
    if (-not (Test-Path -LiteralPath $receiptParent -PathType Container)) { [void](New-Item -ItemType Directory -Path $receiptParent) }
    Write-GpuJson -Path $vmReceiptPath -Value $intent -NoOverwrite
    [pscustomobject]$intent | ConvertTo-Json -Depth 5
} catch {
    $failureMessage = $_.Exception.Message
    $sourceUnchanged = $false
    try { $sourceUnchanged = ((Get-GpuFilePin -Path $sourceVmx).Sha256 -ceq $reviewedVmxHash) } catch { }
    Write-GpuJson -Path (Join-Path $RunDirectory 'failure.json') -Value @{Failure=$failureMessage;SourceVmxUnchanged=$sourceUnchanged;PartialVmPreserved=$true;HostBootChanges=$false} -NoOverwrite
    if (-not $sourceUnchanged) { throw ($failureMessage + ' Retained source VMX could not be verified unchanged; preserve all state.') }
    throw $failureMessage
}
