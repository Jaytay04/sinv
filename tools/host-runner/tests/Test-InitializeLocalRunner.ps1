[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RunnerRoot = Split-Path -Parent $PSScriptRoot
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $RunnerRoot)
$Bootstrap = Join-Path $RunnerRoot 'Initialize-LocalRunner.ps1'
$TestRoot = Join-Path $ProjectRoot (
    'build\host-runner-bootstrap-tests\' + [Guid]::NewGuid().ToString('N'))

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Fails {
    param(
        [Parameter(Mandatory=$true)][scriptblock]$Action,
        [Parameter(Mandatory=$true)][string]$MessagePattern
    )
    $Failed = $false
    try { & $Action }
    catch {
        $Failed = $_.Exception.Message -match $MessagePattern
    }
    Assert-True $Failed "Expected failure was not observed: $MessagePattern"
}

function New-FixtureCredential {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$UserName
    )
    $Secret = ConvertTo-SecureString 'fixture-secret-never-printed' `
        -AsPlainText -Force
    $Credential = [PSCredential]::new($UserName, $Secret)
    $Credential | Export-Clixml -LiteralPath $Path -Force
    $Secret = $null
    $Credential = $null
}

try {
    New-Item -ItemType Directory -Force -Path $TestRoot | Out-Null
    $SourceRoot = Join-Path $TestRoot 'source-uefi'
    $SourceRunnerRoot = Join-Path $SourceRoot 'tools\host-runner'
    $SourceStateRoot = Join-Path $TestRoot 'source-state'
    $TargetRoot = Join-Path $TestRoot 'combined-target'
    $TargetRunnerRoot = Join-Path $TargetRoot 'tools\host-runner'
    $TargetCredentialRoot = Join-Path $TargetRoot '.analysis\host-runner\credentials'
    $FixtureTools = Join-Path $TestRoot 'tools'

    foreach ($Directory in @(
            $SourceRunnerRoot,
            (Join-Path $SourceStateRoot 'credentials'),
            (Join-Path $TargetRoot 'components\uefi-loader'),
            (Join-Path $TargetRoot 'components\hypervisor'),
            $TargetRunnerRoot,
            $FixtureTools)) {
        New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    }
    Set-Content -LiteralPath (Join-Path $SourceRoot 'CMakeLists.txt') `
        -Value '# fixture source' -Encoding ascii
    Set-Content -LiteralPath (Join-Path $TargetRoot 'CMakeLists.txt') `
        -Value '# fixture combined root' -Encoding ascii
    Set-Content -LiteralPath (
        Join-Path $TargetRoot 'components\uefi-loader\CMakeLists.txt') `
        -Value '# fixture loader' -Encoding ascii
    Set-Content -LiteralPath (
        Join-Path $TargetRoot 'components\hypervisor\CMakeLists.txt') `
        -Value '# fixture hypervisor' -Encoding ascii
    @'
function Import-UhrConfig {
    param([Parameter(Mandatory=$true)][string]$ConfigPath)
    $Config = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json
    if ($Config.SchemaVersion -ne 1 -or
        $null -eq $Config.PSObject.Properties['ProjectRoot'] -or
        $null -eq $Config.PSObject.Properties['StateRoot'] -or
        $null -eq $Config.PSObject.Properties['Profiles']) {
        throw 'Invalid legacy fixture configuration.'
    }
    return $Config
}
Export-ModuleMember -Function 'Import-UhrConfig'
'@ | Set-Content -LiteralPath (
        Join-Path $SourceRunnerRoot 'HostRunner.Core.psm1') -Encoding utf8

    $TemplatePath = Join-Path $RunnerRoot 'host-runner.config.example.json'
    $SourceDocument = Get-Content -Raw -LiteralPath $TemplatePath |
        ConvertFrom-Json
    $SourceDocument.ProjectRoot = $SourceRoot
    $SourceDocument.StateRoot = $SourceStateRoot
    $SourceDocument.PSObject.Properties.Remove('UefiProjectRoot')
    $SourceDocument.PSObject.Properties.Remove('HypervisorProjectRoot')
    $SourceDocument.Tools.VmrunPath = Join-Path $FixtureTools 'vmrun.exe'
    $SourceDocument.Tools.VsDevCmdPath = Join-Path $FixtureTools 'VsDevCmd.bat'
    $SourceDocument.SharedLease.ScriptPath = Join-Path $FixtureTools 'lease.ps1'
    $SourceDocument.AdditionalKnownVms = @()
    $SourceDocument.Profiles.Linux.Enabled = $false
    $SourceDocument.Profiles.Windows.Enabled = $false
    foreach ($ToolPath in @(
            $SourceDocument.Tools.VmrunPath,
            $SourceDocument.Tools.VsDevCmdPath,
            $SourceDocument.SharedLease.ScriptPath)) {
        Set-Content -LiteralPath $ToolPath -Value '# fixture tool' -Encoding ascii
    }
    $SourceConfigPath = Join-Path $SourceRunnerRoot 'host-runner.config.json'
    $SourceDocument | ConvertTo-Json -Depth 20 |
        Set-Content -LiteralPath $SourceConfigPath -Encoding utf8

    foreach ($Credential in @(
            @('linux-guest.xml', 'fixture-linux'),
            @('windows-clone-mailbox-key.xml', 'fixture-mailbox'),
            @('windows-vm.xml', 'fixture-vm'))) {
        New-FixtureCredential -Path (
            Join-Path (Join-Path $SourceStateRoot 'credentials') $Credential[0]) `
            -UserName $Credential[1]
    }
    New-FixtureCredential -Path (
        Join-Path (Join-Path $SourceStateRoot 'credentials') 'unreferenced.xml') `
        -UserName 'fixture-unused'

    $TargetConfigPath = Join-Path $TargetRunnerRoot 'host-runner.config.json'
    $NoImportOutput = & $Bootstrap `
        -SourceConfigPath $SourceConfigPath `
        -TargetProjectRoot $TargetRoot `
        -TargetConfigPath $TargetConfigPath
    $NoImportText = $NoImportOutput | Out-String
    Assert-True ($NoImportText -notmatch 'fixture-secret-never-printed') `
        'Bootstrap output exposed fixture credential material.'
    Assert-True (Test-Path -LiteralPath $TargetConfigPath -PathType Leaf) `
        'Bootstrap did not write the target configuration.'
    Assert-True (-not (Test-Path -LiteralPath $TargetCredentialRoot -PathType Container)) `
        'Credentials were imported without -AuthorizeCredentialImport.'

    $TargetDocument = Get-Content -Raw -LiteralPath $TargetConfigPath |
        ConvertFrom-Json
    Assert-True ([IO.Path]::GetFullPath([string]$TargetDocument.ProjectRoot) -eq
        [IO.Path]::GetFullPath($TargetRoot)) 'Target ProjectRoot was not derived.'
    Assert-True ([IO.Path]::GetFullPath([string]$TargetDocument.UefiProjectRoot) -eq
        [IO.Path]::GetFullPath((Join-Path $TargetRoot 'components\uefi-loader'))) `
        'Target UefiProjectRoot was not derived.'
    Assert-True ([IO.Path]::GetFullPath([string]$TargetDocument.HypervisorProjectRoot) -eq
        [IO.Path]::GetFullPath((Join-Path $TargetRoot 'components\hypervisor'))) `
        'Target HypervisorProjectRoot was not derived.'
    Assert-True ([IO.Path]::GetFullPath([string]$TargetDocument.StateRoot) -eq
        [IO.Path]::GetFullPath((Join-Path $TargetRoot '.analysis\host-runner'))) `
        'Target StateRoot was not derived.'
    Assert-True ([string]$TargetDocument.Profiles.Linux.GuestCredentialFile -ceq
        'linux-guest.xml') 'Linux credential reference was not preserved.'

    Assert-Fails -Action {
        & $Bootstrap -SourceConfigPath $SourceConfigPath `
            -TargetProjectRoot $TargetRoot -TargetConfigPath $TargetConfigPath
    } -MessagePattern 'already exists.*Force'

    Remove-Item -LiteralPath $TargetConfigPath -Force
    $ImportOutput = & $Bootstrap `
        -SourceConfigPath $SourceConfigPath `
        -TargetProjectRoot $TargetRoot `
        -TargetConfigPath $TargetConfigPath `
        -AuthorizeCredentialImport
    Assert-True (($ImportOutput | Out-String) -notmatch
        'fixture-secret-never-printed') `
        'Credential import output exposed fixture credential material.'
    foreach ($Name in @(
            'linux-guest.xml', 'windows-clone-mailbox-key.xml', 'windows-vm.xml')) {
        $SourceCredential = Join-Path (Join-Path $SourceStateRoot 'credentials') $Name
        $TargetCredential = Join-Path $TargetCredentialRoot $Name
        Assert-True (Test-Path -LiteralPath $TargetCredential -PathType Leaf) `
            "Referenced credential was not imported: $Name"
        $SourceHash = (Get-FileHash -LiteralPath $SourceCredential -Algorithm SHA256).Hash
        $TargetHash = (Get-FileHash -LiteralPath $TargetCredential -Algorithm SHA256).Hash
        Assert-True ($SourceHash -ceq $TargetHash) `
            "Credential bytes changed during import: $Name"
    }
    Assert-True (-not (Test-Path -LiteralPath (
        Join-Path $TargetCredentialRoot 'unreferenced.xml') -PathType Leaf)) `
        'An unreferenced credential was imported.'
    Assert-True (@(Get-ChildItem -LiteralPath $TargetRunnerRoot -Filter `
        'host-runner.config.json.tmp-*' -Force).Count -eq 0) `
        'Atomic configuration write left a temporary file.'
    Assert-True (@(Get-ChildItem -LiteralPath $TargetRunnerRoot -Filter `
        'host-runner.config.json.bak-*' -Force).Count -eq 0) `
        'Atomic configuration write left a backup file.'

    Write-Host 'PASS: local host-runner bootstrap contracts verified.'
}
finally {
    if (Test-Path -LiteralPath $TestRoot -PathType Container) {
        $Resolved = [IO.Path]::GetFullPath($TestRoot)
        $ExpectedRoot = [IO.Path]::GetFullPath(
            (Join-Path $ProjectRoot 'build\host-runner-bootstrap-tests')).TrimEnd('\') + '\'
        if ($Resolved.StartsWith(
                $ExpectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $Resolved -Recurse -Force
        }
    }
}
