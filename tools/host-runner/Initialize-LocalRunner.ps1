[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [Alias('ExistingConfigPath', 'UefiLoaderConfigPath')]
    [string]$SourceConfigPath,

    [string]$TargetProjectRoot,

    [string]$TargetConfigPath,

    [switch]$AuthorizeCredentialImport,

    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-BootstrapFullPath {
    param([Parameter(Mandatory=$true)][string]$Path)

    try {
        return [IO.Path]::GetFullPath($Path)
    }
    catch {
        throw "Path is not valid: $Path"
    }
}

function Test-BootstrapPathWithin {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [Parameter(Mandatory=$true)][string]$Path
    )

    $RootFull = (Get-BootstrapFullPath $Root).TrimEnd('\') + '\'
    return (Get-BootstrapFullPath $Path).StartsWith(
        $RootFull, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-BootstrapDirectory {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description does not exist: $Path"
    }
}

function Assert-BootstrapFile {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description does not exist: $Path"
    }
}

function Get-ReferencedCredentialFiles {
    param([Parameter(Mandatory=$true)]$Config)

    $Names = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($ProfileName in @('Linux', 'Windows')) {
        $ProfileProperty = $Config.Profiles.PSObject.Properties[$ProfileName]
        if ($null -eq $ProfileProperty) { continue }

        $Profile = $ProfileProperty.Value
        foreach ($PropertyName in @(
                'GuestCredentialFile', 'VmCredentialFile',
                'MailboxCredentialFile')) {
            $Property = $Profile.PSObject.Properties[$PropertyName]
            if ($null -eq $Property) { continue }
            $Name = [string]$Property.Value
            if ([string]::IsNullOrWhiteSpace($Name)) { continue }
            if ($Name -cnotmatch '^[A-Za-z0-9_.-]{1,100}\.xml$') {
                throw "Profiles.$ProfileName.$PropertyName contains an unsafe credential filename."
            }
            [void]$Names.Add($Name)
        }
    }
    return @($Names | Sort-Object)
}

function Assert-DpapiCredentialXml {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$FileName
    )

    # Loading the XML verifies its envelope only. Import-Clixml is deliberately
    # not used: that would ask PowerShell to decrypt the DPAPI secret.
    $Settings = [Xml.XmlReaderSettings]::new()
    $Settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
    $Settings.XmlResolver = $null
    $Reader = $null
    $Document = [Xml.XmlDocument]::new()
    $Document.XmlResolver = $null
    try {
        $Reader = [Xml.XmlReader]::Create($Path, $Settings)
        $Document.Load($Reader)
    }
    catch {
        throw "Credential file is not valid XML: $FileName"
    }
    finally {
        if ($null -ne $Reader) { $Reader.Dispose() }
    }

    if ($null -eq $Document.DocumentElement -or
        $Document.DocumentElement.LocalName -cne 'Objs') {
        throw "Credential file is not a PowerShell credential export: $FileName"
    }
    $CredentialObject = $Document.SelectSingleNode(
        '//*[local-name()="Obj" and .//*[local-name()="T" and contains(text(), "PSCredential")]]')
    $Password = $Document.SelectSingleNode(
        '//*[local-name()="SS" and @N="Password"]')
    if ($null -eq $CredentialObject -or $null -eq $Password -or
        [string]::IsNullOrWhiteSpace([string]$Password.InnerText)) {
        throw "Credential file does not contain a protected PSCredential password: $FileName"
    }
}

function Write-BootstrapAtomicJson {
    param(
        [Parameter(Mandatory=$true)]$Value,
        [Parameter(Mandatory=$true)][string]$Path,
        [switch]$AllowReplace
    )

    $Parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $Parent | Out-Null
    }
    $Temporary = $Path + '.tmp-' + [Guid]::NewGuid().ToString('N')
    $Backup = $Path + '.bak-' + [Guid]::NewGuid().ToString('N')
    $Json = $Value | ConvertTo-Json -Depth 20
    $Bytes = [Text.UTF8Encoding]::new($false).GetBytes($Json + "`n")
    try {
        [IO.File]::WriteAllBytes($Temporary, $Bytes)
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            if (-not $AllowReplace) {
                throw "Target configuration already exists; use -Force to replace it: $Path"
            }
            [IO.File]::Replace($Temporary, $Path, $Backup)
        }
        else {
            [IO.File]::Move($Temporary, $Path)
        }
    }
    finally {
        [Array]::Clear($Bytes, 0, $Bytes.Length)
        if (Test-Path -LiteralPath $Temporary -PathType Leaf) {
            Remove-Item -LiteralPath $Temporary -Force
        }
        if (Test-Path -LiteralPath $Backup -PathType Leaf) {
            Remove-Item -LiteralPath $Backup -Force
        }
    }
}

if ([string]::IsNullOrWhiteSpace($TargetProjectRoot)) {
    $TargetProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}
if ([string]::IsNullOrWhiteSpace($TargetConfigPath)) {
    $TargetConfigPath = Join-Path $TargetProjectRoot `
        'tools\host-runner\host-runner.config.json'
}

$SourceConfigPath = Get-BootstrapFullPath $SourceConfigPath
$TargetProjectRoot = Get-BootstrapFullPath $TargetProjectRoot
$TargetConfigPath = Get-BootstrapFullPath $TargetConfigPath

Assert-BootstrapFile -Path $SourceConfigPath `
    -Description 'Source host-runner configuration'
$SourceConfigDirectory = Split-Path -Parent $SourceConfigPath
$SourceCorePath = Join-Path $SourceConfigDirectory 'HostRunner.Core.psm1'
Assert-BootstrapFile -Path $SourceCorePath `
    -Description 'Source HostRunner.Core.psm1'

# Validate the input with the source runner's own validator. The source
# validator intentionally accepts the legacy UEFILoader shape where
# ProjectRoot is the UEFI project and UefiProjectRoot is absent.
Import-Module -Name $SourceCorePath -Force -Prefix BootstrapSource | Out-Null
$SourceConfig = Import-BootstrapSourceUhrConfig -ConfigPath $SourceConfigPath

$SourceProjectRoot = Get-BootstrapFullPath ([string]$SourceConfig.ProjectRoot)
$SourceStateRoot = Get-BootstrapFullPath ([string]$SourceConfig.StateRoot)
$SourceUefiRoot = $SourceProjectRoot
if ($null -ne $SourceConfig.PSObject.Properties['UefiProjectRoot'] -and
    -not [string]::IsNullOrWhiteSpace([string]$SourceConfig.UefiProjectRoot)) {
    $SourceUefiRoot = Get-BootstrapFullPath ([string]$SourceConfig.UefiProjectRoot)
}
Assert-BootstrapDirectory -Path $SourceProjectRoot `
    -Description 'Source UEFILoader project root'
Assert-BootstrapDirectory -Path $SourceUefiRoot `
    -Description 'Source UEFI project root'
Assert-BootstrapFile -Path (Join-Path $SourceUefiRoot 'CMakeLists.txt') `
    -Description 'Source UEFILoader CMakeLists.txt'

Assert-BootstrapDirectory -Path $TargetProjectRoot `
    -Description 'Target combined project root'
$TargetUefiRoot = Join-Path $TargetProjectRoot 'components\uefi-loader'
$TargetHypervisorRoot = Join-Path $TargetProjectRoot 'components\hypervisor'
Assert-BootstrapDirectory -Path $TargetUefiRoot `
    -Description 'Target UEFILoader component'
Assert-BootstrapDirectory -Path $TargetHypervisorRoot `
    -Description 'Target Hypervisor component'
Assert-BootstrapFile -Path (Join-Path $TargetProjectRoot 'CMakeLists.txt') `
    -Description 'Target combined CMakeLists.txt'
Assert-BootstrapFile -Path (Join-Path $TargetUefiRoot 'CMakeLists.txt') `
    -Description 'Target UEFILoader CMakeLists.txt'
Assert-BootstrapFile -Path (Join-Path $TargetHypervisorRoot 'CMakeLists.txt') `
    -Description 'Target Hypervisor CMakeLists.txt'

if (-not (Test-BootstrapPathWithin -Root $TargetProjectRoot `
        -Path $TargetConfigPath)) {
    throw 'Target configuration must be contained under TargetProjectRoot.'
}
if ([IO.Path]::GetFileName($TargetConfigPath) -cne 'host-runner.config.json') {
    throw 'Target configuration filename must be host-runner.config.json.'
}
if ((Test-Path -LiteralPath $TargetConfigPath -PathType Leaf) -and -not $Force) {
    throw "Target configuration already exists; use -Force to replace it: $TargetConfigPath"
}

$TargetStateRoot = Join-Path $TargetProjectRoot '.analysis\host-runner'
$TargetCredentialRoot = Join-Path $TargetStateRoot 'credentials'
$SourceCredentialRoot = Join-Path $SourceStateRoot 'credentials'
$CredentialNames = @(Get-ReferencedCredentialFiles -Config $SourceConfig)
$CredentialPlans = @()
if ($AuthorizeCredentialImport) {
    Assert-BootstrapDirectory -Path $SourceCredentialRoot `
        -Description 'Source credential directory'
    foreach ($Name in $CredentialNames) {
        $SourceCredentialPath = Join-Path $SourceCredentialRoot $Name
        $TargetCredentialPath = Join-Path $TargetCredentialRoot $Name
        Assert-BootstrapFile -Path $SourceCredentialPath `
            -Description "Referenced source credential ($Name)"
        Assert-DpapiCredentialXml -Path $SourceCredentialPath -FileName $Name
        if ((Test-Path -LiteralPath $TargetCredentialPath -PathType Leaf) -and
            -not $Force) {
            throw "Target credential already exists; use -Force to replace it: $Name"
        }
        $CredentialPlans += [pscustomobject]@{
            Name = $Name
            SourcePath = $SourceCredentialPath
            TargetPath = $TargetCredentialPath
        }
    }
}

# Clone the validated source object without touching its source file, then
# replace only paths that are owned by the combined repository.
$TargetConfig = $SourceConfig | ConvertTo-Json -Depth 20 | ConvertFrom-Json
$TargetConfig.ProjectRoot = $TargetProjectRoot
$TargetConfig.StateRoot = $TargetStateRoot
if ($null -eq $TargetConfig.PSObject.Properties['UefiProjectRoot']) {
    $TargetConfig | Add-Member -NotePropertyName UefiProjectRoot `
        -NotePropertyValue $TargetUefiRoot
}
else {
    $TargetConfig.UefiProjectRoot = $TargetUefiRoot
}
if ($null -eq $TargetConfig.PSObject.Properties['HypervisorProjectRoot']) {
    $TargetConfig | Add-Member -NotePropertyName HypervisorProjectRoot `
        -NotePropertyValue $TargetHypervisorRoot
}
else {
    $TargetConfig.HypervisorProjectRoot = $TargetHypervisorRoot
}

# Re-check the complete generated document using the target component's
# validator before any credential bytes or configuration are written.
$TargetCorePath = Join-Path $PSScriptRoot 'HostRunner.Core.psm1'
Assert-BootstrapFile -Path $TargetCorePath `
    -Description 'Target HostRunner.Core.psm1'
Import-Module -Name $TargetCorePath -Force -Prefix BootstrapTarget | Out-Null
$ValidationPath = Join-Path $TargetProjectRoot `
    ('tools\host-runner\host-runner.config.validation-{0}.json' -f
        [Guid]::NewGuid().ToString('N'))
try {
    $TargetConfigJson = $TargetConfig | ConvertTo-Json -Depth 20
    [IO.File]::WriteAllText($ValidationPath, $TargetConfigJson,
        [Text.UTF8Encoding]::new($false))
    [void](Import-BootstrapTargetUhrConfig -ConfigPath $ValidationPath)
}
finally {
    if (Test-Path -LiteralPath $ValidationPath -PathType Leaf) {
        Remove-Item -LiteralPath $ValidationPath -Force
    }
}

if ($AuthorizeCredentialImport) {
    New-Item -ItemType Directory -Force -Path $TargetCredentialRoot | Out-Null
    foreach ($Plan in $CredentialPlans) {
        # Copy the encrypted XML as opaque bytes. No credential object is
        # imported, converted, logged, or otherwise decrypted here.
        $TemporaryCredential = $Plan.TargetPath + '.tmp-' +
            [Guid]::NewGuid().ToString('N')
        try {
            [IO.File]::Copy($Plan.SourcePath, $TemporaryCredential, $true)
            if (Test-Path -LiteralPath $Plan.TargetPath -PathType Leaf) {
                [IO.File]::Replace($TemporaryCredential, $Plan.TargetPath, $null)
            }
            else {
                [IO.File]::Move($TemporaryCredential, $Plan.TargetPath)
            }
        }
        finally {
            if (Test-Path -LiteralPath $TemporaryCredential -PathType Leaf) {
                Remove-Item -LiteralPath $TemporaryCredential -Force
            }
        }
    }
}

Write-BootstrapAtomicJson -Value $TargetConfig -Path $TargetConfigPath `
    -AllowReplace:$Force

Write-Output ([pscustomobject]@{
    ConfigPath = $TargetConfigPath
    ProjectRoot = $TargetProjectRoot
    UefiProjectRoot = $TargetUefiRoot
    HypervisorProjectRoot = $TargetHypervisorRoot
    StateRoot = $TargetStateRoot
    CredentialImportAuthorized = [bool]$AuthorizeCredentialImport
    CredentialFilesImported = $CredentialPlans.Count
})
