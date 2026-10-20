Set-StrictMode -Version 2.0

$stateModulePath = Join-Path $PSScriptRoot 'GpuHostState.psm1'
if (-not (Test-Path -LiteralPath $stateModulePath -PathType Leaf)) {
    throw "Required state helper module is missing: $stateModulePath"
}
# Reuse the state module already imported by the dispatcher. Forcing a nested
# reload removes its exported commands from the caller, including failure logging.
Import-Module -Name $stateModulePath -ErrorAction Stop

function Get-GpuObjectProperty {
    param(
        [Parameter(Mandatory = $true)][AllowNull()][object]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $false)][AllowNull()][object]$Default = $null
    )

    if ($null -eq $Object) { return $Default }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $Default }
    return $property.Value
}

function Set-GpuStateProperty {
    param(
        [Parameter(Mandatory = $true)][object]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $false)][AllowNull()][object]$Value
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        Add-Member -InputObject $Object -MemberType NoteProperty -Name $Name -Value $Value
    }
    else {
        $Object.$Name = $Value
    }
}

function New-GpuPlatformResult {
    param(
        [Parameter(Mandatory = $true)][string]$Status,
        [Parameter(Mandatory = $true)][bool]$Succeeded,
        [Parameter(Mandatory = $false)][AllowNull()][System.Collections.IDictionary]$Data
    )

    $values = [ordered]@{ Status = $Status; Succeeded = $Succeeded }
    if ($null -ne $Data) {
        foreach ($key in $Data.Keys) { $values[$key] = $Data[$key] }
    }
    return [pscustomobject]$values
}

function Get-GpuRunPath {
    param(
        [Parameter(Mandatory = $true)][object]$State,
        [Parameter(Mandatory = $true)][string]$PropertyName,
        [Parameter(Mandatory = $false)][switch]$MustExist
    )

    $runDirectory = [string](Get-GpuObjectProperty -Object $State -Name 'RunDirectory')
    $path = [string](Get-GpuObjectProperty -Object $State.Paths -Name $PropertyName)
    if ([string]::IsNullOrWhiteSpace($runDirectory) -or [string]::IsNullOrWhiteSpace($path)) {
        throw "State path '$PropertyName' or RunDirectory is empty."
    }
    if ($PropertyName -in @('HypervisorBuild', 'LoaderBuild')) {
        $workspaceRoot = [string](Get-GpuObjectProperty -Object $State -Name 'WorkspaceRoot')
        if ([string]::IsNullOrWhiteSpace($workspaceRoot)) { throw 'State.WorkspaceRoot is empty for the short build path.' }
        $runId = Get-GpuRunIdLeaf -State $State
        if ($runId.Length -lt 16) { throw 'State.RunId must contain at least 16 characters for the short build path.' }
        $shortRunId = $runId.Substring($runId.Length - 16)
        $buildLeaf = if ($PropertyName -eq 'HypervisorBuild') { 'h' } else { 'l' }
        $buildParent = Join-Path (Join-Path ([System.IO.Path]::GetFullPath($workspaceRoot)) '.codex-build\gb') $shortRunId
        $expectedPath = Join-Path $buildParent $buildLeaf
        if (-not [string]::Equals([System.IO.Path]::GetFullPath($path), [System.IO.Path]::GetFullPath($expectedPath), [StringComparison]::OrdinalIgnoreCase)) {
            throw "State path '$PropertyName' does not match its derived short build path."
        }
        $arguments = @{ Path = $expectedPath; Root = $workspaceRoot }
        if ($MustExist) { $arguments.MustExist = $true }
        return Assert-GpuPathWithin @arguments
    }
    $arguments = @{ Path = $path; Root = $runDirectory }
    if ($MustExist) { $arguments.MustExist = $true }
    return Assert-GpuPathWithin @arguments
}

function Assert-GpuActionDirectory {
    param([string]$ActionDirectory, [object]$State)

    $runDirectory = [string](Get-GpuObjectProperty -Object $State -Name 'RunDirectory')
    if ([string]::IsNullOrWhiteSpace($runDirectory)) { throw 'State.RunDirectory is empty.' }
    $path = Assert-GpuPathWithin -Path $ActionDirectory -Root $runDirectory -MustExist
    $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer) { throw "ActionDirectory is not a directory: $path" }
    return $path
}

function Assert-GpuFreshPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $parent = [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($Path))
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        throw "Destination parent does not exist: $parent"
    }
    if (Test-Path -LiteralPath $Path) { throw "Refusing to overwrite an existing path: $Path" }
}

function Get-GpuSafeLeaf {
    param([string]$Value)
    $leaf = [System.Text.RegularExpressions.Regex]::Replace([string]$Value, '[^A-Za-z0-9._-]', '_')
    if ([string]::IsNullOrWhiteSpace($leaf)) { return 'command' }
    if ($leaf.Length -gt 48) { return $leaf.Substring(0, 48) }
    return $leaf
}

function Invoke-GpuNativeCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$ActionDirectory,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $false)][int[]]$AllowedExitCodes = @(0)
    )

    $safeLabel = Get-GpuSafeLeaf -Value $Label
    $identity = [guid]::NewGuid().ToString('N')
    $prefix = "$safeLabel-$identity"
    $stdoutPath = Join-Path $ActionDirectory ($prefix + '.stdout.txt')
    $stderrPath = Join-Path $ActionDirectory ($prefix + '.stderr.txt')
    $receiptPath = Join-Path $ActionDirectory ($prefix + '.receipt.json')
    Assert-GpuFreshPath -Path $stdoutPath
    Assert-GpuFreshPath -Path $stderrPath
    Assert-GpuFreshPath -Path $receiptPath

    [System.IO.File]::WriteAllText($stdoutPath, '', (New-Object System.Text.UTF8Encoding($false)))
    [System.IO.File]::WriteAllText($stderrPath, '', (New-Object System.Text.UTF8Encoding($false)))
    $startUtc = [DateTime]::UtcNow.ToString('o')
    $exitCode = $null
    $launchError = $null
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments 1> $stdoutPath 2> $stderrPath
        $exitCode = [int]$LASTEXITCODE
    }
    catch {
        $launchError = $_.Exception.Message
    }

    $allowed = @($AllowedExitCodes | ForEach-Object { [int]$_ })
    $succeeded = ($null -ne $exitCode -and $allowed -contains [int]$exitCode -and [string]::IsNullOrEmpty($launchError))
    $receipt = [pscustomobject]@{
        SchemaVersion = 1
        Program = $Program
        Arguments = @($Arguments)
        StartedUtc = $startUtc
        CompletedUtc = [DateTime]::UtcNow.ToString('o')
        ExitCode = $exitCode
        AllowedExitCodes = @($allowed)
        Succeeded = [bool]$succeeded
        LaunchError = $launchError
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
    }
    Write-GpuJson -Path $receiptPath -Value $receipt -NoOverwrite

    return [pscustomobject]@{
        Succeeded = [bool]$succeeded
        ExitCode = $exitCode
        LaunchError = $launchError
        Program = $Program
        Arguments = @($Arguments)
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
        ReceiptPath = $receiptPath
    }
}

function Assert-GpuNativeSuccess {
    param([object]$Result, [string]$Description)

    if ($null -eq $Result -or -not [bool]$Result.Succeeded) {
        if ($null -eq $Result) { throw "$Description did not produce a command result." }
        throw "$Description failed with exit $($Result.ExitCode). Evidence: $($Result.ReceiptPath)"
    }
}

function Get-GpuApplicationPath {
    param([Parameter(Mandatory = $true)][string]$Name)

    $command = Get-Command -Name $Name -CommandType Application -ErrorAction Stop |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace([string]$command.Source)) {
        throw "Required executable was not found: $Name"
    }
    return [string]$command.Source
}

function Get-GpuCurrentMachineSnapshot {
    param([switch]$IncludeSecureBoot)

    if (-not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) {
        throw 'The platform workflow requires 64-bit Windows and a 64-bit PowerShell process.'
    }

    $machineGuid = [string](Get-ItemPropertyValue -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Cryptography' -Name MachineGuid -ErrorAction Stop)
    if ($machineGuid -notmatch '^[0-9A-Fa-f-]{36}$') { throw 'MachineGuid is missing or malformed.' }

    $operatingSystem = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
    $computerSystem = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    $cpuCount = [int]$computerSystem.NumberOfLogicalProcessors
    if ($cpuCount -le 0) { throw 'Windows reported an invalid logical processor count.' }
    if ($operatingSystem.LastBootUpTime -is [DateTime]) {
        $lastBoot = [DateTime]$operatingSystem.LastBootUpTime
    }
    else {
        $lastBoot = [DateTime]::Parse([string]$operatingSystem.LastBootUpTime, [Globalization.CultureInfo]::CurrentCulture)
    }
    $bootId = $lastBoot.ToUniversalTime().ToString('o', [Globalization.CultureInfo]::InvariantCulture)
    $version = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
    $displayVersion = [string]$version.DisplayVersion
    $build = [string]$version.CurrentBuild
    $ubr = [int]$version.UBR
    if ([string]::IsNullOrWhiteSpace($displayVersion) -or [string]::IsNullOrWhiteSpace($build)) {
        throw 'Windows version registry data is incomplete.'
    }

    $systemDrive = [string]$env:SystemDrive
    if ($systemDrive -notmatch '^[A-Za-z]:$') { throw "SystemDrive is not a local drive: $systemDrive" }
    if (-not [string]::Equals($systemDrive, 'C:', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'This host runbook requires Windows to boot from C:.'
    }
    $driveLetter = $systemDrive.Substring(0, 1)
    $partitions = @(Get-Partition -DriveLetter $driveLetter -ErrorAction Stop)
    if ($partitions.Count -ne 1) { throw "Expected one partition for $systemDrive; found $($partitions.Count)." }
    $partition = $partitions[0]
    $disk = Get-Disk -Number ([int]$partition.DiskNumber) -ErrorAction Stop
    if ([string]::IsNullOrWhiteSpace([string]$disk.UniqueId) -and [string]::IsNullOrWhiteSpace([string]$disk.SerialNumber)) {
        throw 'Windows C: disk has no stable unique ID or serial number.'
    }
    $isVirtualProperty = $disk.PSObject.Properties['IsVirtual']
    $isVirtual = if ($null -ne $isVirtualProperty) { [bool]$isVirtualProperty.Value } else { $false }
    $diskIdentity = [pscustomobject]@{
        Number = [int]$disk.Number
        UniqueId = [string]$disk.UniqueId
        SerialNumber = [string]$disk.SerialNumber
        FriendlyName = [string]$disk.FriendlyName
        BusType = [string]$disk.BusType
        IsVirtual = $isVirtual
        PartitionNumber = [int]$partition.PartitionNumber
        PartitionGuid = [string]$partition.Guid
    }

    $secureBoot = $null
    if ($IncludeSecureBoot) {
        try {
            $secureBoot = Confirm-SecureBootUEFI -ErrorAction Stop
        }
        catch {
            throw "Secure Boot could not be read: $($_.Exception.Message)"
        }
        if ($secureBoot -isnot [bool]) { throw 'Secure Boot did not return a boolean value.' }
    }

    return [pscustomobject]@{
        MachineId = $machineGuid.ToUpperInvariant()
        BootId = $bootId
        LogicalCpus = $cpuCount
        WindowsVersion = "Windows $displayVersion build $build.$ubr"
        SystemDiskNumber = [int]$disk.Number
        SystemDisk = $diskIdentity
        SystemDrive = $systemDrive.ToUpperInvariant()
        SecureBoot = $secureBoot
    }
}

function Assert-GpuSameMachine {
    param([object]$State, [object]$Current, [switch]$RequireCpuCount)

    $preparedHost = Get-GpuObjectProperty -Object $State -Name 'Host'
    if ($null -eq $preparedHost) { throw 'No prepared host identity is recorded in State.Host.' }
    $expectedMachine = [string](Get-GpuObjectProperty -Object $preparedHost -Name 'MachineId')
    if ([string]::IsNullOrWhiteSpace($expectedMachine) -or
        -not [string]::Equals($expectedMachine, [string]$Current.MachineId, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'This run belongs to a different Windows machine.'
    }
    $expectedSystemDisk = Get-GpuObjectProperty -Object $preparedHost -Name 'SystemDisk'
    $currentSystemDisk = Get-GpuObjectProperty -Object $Current -Name 'SystemDisk'
    $expectedDiskId = [string](Get-GpuObjectProperty -Object $expectedSystemDisk -Name 'UniqueId')
    $currentDiskId = [string](Get-GpuObjectProperty -Object $currentSystemDisk -Name 'UniqueId')
    if ([string]::IsNullOrWhiteSpace($expectedDiskId)) { $expectedDiskId = [string](Get-GpuObjectProperty -Object $expectedSystemDisk -Name 'SerialNumber') }
    if ([string]::IsNullOrWhiteSpace($currentDiskId)) { $currentDiskId = [string](Get-GpuObjectProperty -Object $currentSystemDisk -Name 'SerialNumber') }
    if ([int]$Current.SystemDiskNumber -ne [int](Get-GpuObjectProperty -Object $preparedHost -Name 'SystemDiskNumber' -Default -1) -or
        [string]::IsNullOrWhiteSpace($expectedDiskId) -or [string]::IsNullOrWhiteSpace($currentDiskId) -or
        -not [string]::Equals($expectedDiskId, $currentDiskId, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The Windows system disk identity changed since Prepare.'
    }
    if ($RequireCpuCount -and [int]$Current.LogicalCpus -ne [int](Get-GpuObjectProperty -Object $preparedHost -Name 'LogicalCpus' -Default -1)) {
        throw 'Logical CPU count changed since Prepare.'
    }
}

function Assert-GpuOriginalBoot {
    param([object]$State, [object]$Current)

    $preparedHost = Get-GpuObjectProperty -Object $State -Name 'Host'
    $expectedBoot = [string](Get-GpuObjectProperty -Object $preparedHost -Name 'BootId')
    if ([string]::IsNullOrWhiteSpace($expectedBoot) -or
        -not [string]::Equals($expectedBoot, [string]$Current.BootId, [StringComparison]::Ordinal)) {
        throw 'The host rebooted during the build/stage preparation sequence; run Status and complete rollback before starting a new run.'
    }
}

function Assert-GpuSecureBootOn {
    param([object]$Current)

    if ($Current.SecureBoot -isnot [bool] -or -not [bool]$Current.SecureBoot) {
        throw 'Secure Boot must be enabled for this workflow.'
    }
}

function Get-GpuPreflight {
    param(
        [object]$State,
        [string]$ActionDirectory,
        [string]$Name
    )

    $sourceRoot = [string](Get-GpuObjectProperty -Object $State -Name 'SourceRoot')
    $scriptPath = Join-Path $sourceRoot 'components\hypervisor\setup\Get-GpuDiagnosticPreflight.ps1'
    $scriptPath = Assert-GpuPathWithin -Path $scriptPath -Root $sourceRoot -MustExist
    $outputPath = Join-Path $ActionDirectory ($Name + '.json')
    Assert-GpuFreshPath -Path $outputPath
    $powershellPath = Get-GpuApplicationPath -Name 'powershell.exe'
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $scriptPath, '-OutputPath', $outputPath)
    $result = Invoke-GpuNativeCommand -Program $powershellPath -Arguments $arguments -ActionDirectory $ActionDirectory -Label ($Name + '-preflight')
    Assert-GpuNativeSuccess -Result $result -Description 'GPU diagnostic preflight'
    if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) { throw 'Preflight did not write its JSON report.' }
    $report = Read-GpuJson -Path $outputPath
    $pin = Get-GpuFilePin -Path $outputPath
    return [pscustomobject]@{ Report = $report; Pin = $pin; Command = $result; Path = $outputPath }
}

function Assert-GpuPreflightAdapters {
    param([object]$Report)

    if ($null -eq $Report -or $Report.InventoryComplete -ne $true) {
        throw 'Display/monitor inventory is incomplete.'
    }
    if ($null -eq $Report.DiagnosticCandidate -or
        [string]$Report.DiagnosticCandidate.PciId -ne 'PCI\VEN_8086&DEV_4680' -or
        $Report.DiagnosticCandidate.Present -ne $true) {
        throw 'Intel UHD 770 PCI 8086:4680 was not completely enumerated.'
    }
    if ($null -eq $Report.Nvidia -or
        [string]$Report.Nvidia.PciId -notmatch '^PCI\\VEN_10DE' -or
        $Report.Nvidia.Present -ne $true) {
        throw 'An NVIDIA display adapter was not completely enumerated.'
    }
}

function Assert-GpuAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run the host workflow from an elevated PowerShell session.'
    }
}

function Assert-GpuNoReparseAncestors {
    param([Parameter(Mandatory = $true)][string]$Path)

    $cursor = [System.IO.Path]::GetFullPath($Path)
    while (-not [string]::IsNullOrWhiteSpace($cursor)) {
        if (Test-Path -LiteralPath $cursor) {
            $attributes = [System.IO.File]::GetAttributes($cursor)
            if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse point is not allowed in the recovery path: $cursor"
            }
        }
        $parent = [System.IO.Directory]::GetParent($cursor)
        if ($null -eq $parent) { break }
        $cursor = $parent.FullName
    }
}

function Get-GpuDiskPhysicalClassification {
    param([Parameter(Mandatory = $true)][object]$Disk)

    $virtualProperty = $Disk.PSObject.Properties['IsVirtual']
    if ($null -ne $virtualProperty -and [bool]$virtualProperty.Value) {
        return [pscustomobject]@{ SupportedPhysical = $false; BusType = [string](Get-GpuObjectProperty -Object $Disk -Name 'BusType' -Default 'Unknown'); Reason = 'IsVirtual is true.' }
    }

    $rawBusType = Get-GpuObjectProperty -Object $Disk -Name 'BusType'
    $busName = if ($null -eq $rawBusType) { '' } else { ([string]$rawBusType).Trim() }
    $numericBusType = 0
    $isNumeric = [int]::TryParse($busName, [ref]$numericBusType)
    if ($isNumeric) {
        # STORAGE_BUS_TYPE values: reject Unknown (0), Virtual (14),
        # File Backed Virtual (15), and Spaces (16); accept known physical buses.
        $supported = ($numericBusType -in @(1..13) -or $numericBusType -in @(17..19))
        return [pscustomobject]@{ SupportedPhysical = [bool]$supported; BusType = $busName; Reason = if ($supported) { 'Recognized physical storage bus type.' } else { 'Unknown or virtual storage bus type.' } }
    }

    $normalized = [System.Text.RegularExpressions.Regex]::Replace($busName.ToLowerInvariant(), '[^a-z0-9]', '')
    if ($normalized -eq 'unknown' -or $normalized -match 'virtual|spaces?') {
        return [pscustomobject]@{ SupportedPhysical = $false; BusType = $busName; Reason = 'Unknown or virtual storage bus type.' }
    }
    $physicalNames = @('scsi', 'atapi', 'ata', 'ieee1394', 'ssa', 'fibrechannel', 'fiberchannel', 'usb', 'raid', 'iscsi', 'sas', 'sata', 'sd', 'mmc', 'nvme', 'scm', 'ufs')
    $supportedName = $normalized -in $physicalNames
    return [pscustomobject]@{ SupportedPhysical = [bool]$supportedName; BusType = $busName; Reason = if ($supportedName) { 'Recognized physical storage bus type.' } else { 'Unrecognized storage bus type.' } }
}

function Test-GpuDiskIdentityMatch {
    param(
        [string]$ExpectedUniqueId,
        [string]$ExpectedSerialNumber,
        [string]$ActualUniqueId,
        [string]$ActualSerialNumber
    )

    if (-not [string]::IsNullOrWhiteSpace($ExpectedUniqueId) -and -not [string]::IsNullOrWhiteSpace($ActualUniqueId)) {
        return [string]::Equals($ExpectedUniqueId, $ActualUniqueId, [StringComparison]::OrdinalIgnoreCase)
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedSerialNumber) -and -not [string]::IsNullOrWhiteSpace($ActualSerialNumber)) {
        return [string]::Equals($ExpectedSerialNumber, $ActualSerialNumber, [StringComparison]::OrdinalIgnoreCase)
    }
    return $false
}

function Get-GpuDiskForDrivePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetPathRoot($fullPath)
    if ($root -notmatch '^[A-Za-z]:\\$') { throw "Recovery path must be on a local drive: $fullPath" }
    $driveLetter = $root.Substring(0, 1)
    $partitions = @(Get-Partition -DriveLetter $driveLetter -ErrorAction Stop)
    if ($partitions.Count -ne 1) { throw "Recovery drive $root does not map to one partition." }
    $disk = Get-Disk -Number ([int]$partitions[0].DiskNumber) -ErrorAction Stop
    $classification = Get-GpuDiskPhysicalClassification -Disk $disk
    $busType = [string]$classification.BusType
    if (-not [bool]$classification.SupportedPhysical) {
        throw "Recovery disk is virtual, file-backed, Spaces-backed, or of unknown type: $($disk.Number) ($busType). $($classification.Reason)"
    }
    if ([string]::IsNullOrWhiteSpace([string]$disk.UniqueId) -and [string]::IsNullOrWhiteSpace([string]$disk.SerialNumber)) {
        throw "Recovery disk has no stable unique ID or serial number: $($disk.Number)."
    }
    return [pscustomobject]@{
        Number = [int]$disk.Number
        UniqueId = [string]$disk.UniqueId
        SerialNumber = [string]$disk.SerialNumber
        FriendlyName = [string]$disk.FriendlyName
        BusType = $busType
        IsVirtual = [bool](Get-GpuObjectProperty -Object $disk -Name 'IsVirtual' -Default $false)
    }
}

function Assert-GpuRecoveryLocation {
    param([object]$State, [switch]$RequireRunDirectory)

    $recoveryBase = [string](Get-GpuObjectProperty -Object $State -Name 'RecoveryBase')
    if ([string]::IsNullOrWhiteSpace($recoveryBase) -or
        -not (Test-Path -LiteralPath $recoveryBase -PathType Container)) {
        throw "Recovery directory does not exist: '$recoveryBase'. Before a new Prepare, create a dedicated directory on the verified separate recovery disk, or supply its existing directory with -RecoveryDirectory. Close any saved failed run with Status/rollback first."
    }
    $fullBase = [System.IO.Path]::GetFullPath($recoveryBase)
    Assert-GpuNoReparseAncestors -Path $fullBase
    $recoveryDisk = Get-GpuDiskForDrivePath -Path $fullBase
    $preparedHost = Get-GpuObjectProperty -Object $State -Name 'Host'
    if ($null -ne $preparedHost) {
        $systemDiskNumber = [int](Get-GpuObjectProperty -Object $preparedHost -Name 'SystemDiskNumber' -Default -1)
        if ($systemDiskNumber -ge 0 -and $recoveryDisk.Number -eq $systemDiskNumber) {
            throw 'RecoveryBase is on the Windows system disk; use a separate physical disk.'
        }
        $identityMatches = Test-GpuDiskIdentityMatch `
            -ExpectedUniqueId ([string](Get-GpuObjectProperty -Object $preparedHost -Name 'RecoveryDiskUniqueId')) `
            -ExpectedSerialNumber ([string](Get-GpuObjectProperty -Object $preparedHost -Name 'RecoveryDiskSerialNumber')) `
            -ActualUniqueId ([string]$recoveryDisk.UniqueId) `
            -ActualSerialNumber ([string]$recoveryDisk.SerialNumber)
        if (($null -ne $preparedHost.PSObject.Properties['RecoveryDiskUniqueId'] -or $null -ne $preparedHost.PSObject.Properties['RecoveryDiskSerialNumber']) -and -not $identityMatches) {
            throw 'Recovery disk identity changed since Prepare.'
        }
    }

    $runDirectory = [string](Get-GpuObjectProperty -Object $State -Name 'RecoveryRunDirectory')
    if ($RequireRunDirectory) {
        if ([string]::IsNullOrWhiteSpace($runDirectory) -or
            -not (Test-Path -LiteralPath $runDirectory -PathType Container)) {
            throw 'The external recovery run directory is missing.'
        }
        $canonicalRun = Assert-GpuPathWithin -Path $runDirectory -Root $fullBase -MustExist
        Assert-GpuNoReparseAncestors -Path $canonicalRun
    }
    return [pscustomobject]@{ Base = $fullBase; RunDirectory = $runDirectory; Disk = $recoveryDisk }
}

function Get-GpuRunIdLeaf {
    param([object]$State)

    $runId = [string](Get-GpuObjectProperty -Object $State -Name 'RunId')
    if ($runId -notmatch '^[A-Za-z0-9_-]{1,80}$') { throw 'State.RunId is missing or unsafe for a directory name.' }
    return $runId
}

function Get-GpuBuildDirectories {
    param([object]$State)

    $hypervisorBuild = Get-GpuRunPath -State $State -PropertyName 'HypervisorBuild'
    $loaderBuild = Get-GpuRunPath -State $State -PropertyName 'LoaderBuild'
    $signedDirectory = Get-GpuRunPath -State $State -PropertyName 'SignedDirectory'
    return [pscustomobject]@{ HypervisorBuild = $hypervisorBuild; LoaderBuild = $loaderBuild; SignedDirectory = $signedDirectory }
}

function Get-GpuExpectedToolPaths {
    param([object]$State)

    $paths = Get-GpuBuildDirectories -State $State
    $sourceRoot = [string](Get-GpuObjectProperty -Object $State -Name 'SourceRoot')
    $sourceRoot = [System.IO.Path]::GetFullPath($sourceRoot)
    return [pscustomobject]@{
        Installer = Join-Path $paths.LoaderBuild 'ib-install.exe'
        Probe = Join-Path $paths.LoaderBuild 'ib-tcg-dump.exe'
        Verifier = Join-Path $paths.LoaderBuild 'install_authenticode_test.exe'
        Shim = Join-Path $sourceRoot 'components\uefi-loader\src\vendor\shimx64.efi'
        Certificate = Join-Path $sourceRoot 'components\hypervisor\hypersinv.cer'
    }
}

function Get-GpuExpectedBuildArtifactPaths {
    param([object]$State)

    $paths = Get-GpuBuildDirectories -State $State
    return [pscustomobject]@{
        EFI = Join-Path $paths.HypervisorBuild 'src\efi\uefi_hypervisor.efi'
        Loader = Join-Path $paths.LoaderBuild 'loader.efi'
    }
}

function Get-GpuExpectedSignedArtifactPaths {
    param([object]$State)

    $paths = Get-GpuBuildDirectories -State $State
    return [pscustomobject]@{
        Payload = Join-Path $paths.SignedDirectory 'payload.efi'
        Loader = Join-Path $paths.SignedDirectory 'loader.efi'
    }
}

function Assert-GpuNamedPin {
    param(
        [object]$PinSet,
        [string]$Name,
        [string]$ExpectedPath,
        [string]$Description
    )

    if ($null -eq $PinSet) { throw "$Description pins are missing." }
    $pin = Get-GpuObjectProperty -Object $PinSet -Name $Name
    if ($null -eq $pin) { throw "$Description pin '$Name' is missing." }
    $canonicalExpected = [System.IO.Path]::GetFullPath($ExpectedPath)
    if (-not [string]::Equals([string]$pin.Path, $canonicalExpected, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description pin '$Name' points to an unexpected file."
    }
    Assert-GpuFilePin -Pin $pin
    return $pin
}

function Assert-GpuToolPins {
    param([object]$State)

    $expected = Get-GpuExpectedToolPaths -State $State
    foreach ($name in @('Installer', 'Probe', 'Verifier', 'Shim', 'Certificate')) {
        Assert-GpuNamedPin -PinSet $State.Tools -Name $name -ExpectedPath $expected.$name -Description 'Tool' | Out-Null
    }
}

function Assert-GpuBuildPins {
    param([object]$State)

    $expected = Get-GpuExpectedBuildArtifactPaths -State $State
    foreach ($name in @('EFI', 'Loader')) {
        Assert-GpuNamedPin -PinSet $State.BuildArtifacts -Name $name -ExpectedPath $expected.$name -Description 'Build artifact' | Out-Null
    }
}

function Assert-GpuSignedPins {
    param([object]$State)

    $expected = Get-GpuExpectedSignedArtifactPaths -State $State
    foreach ($name in @('Payload', 'Loader')) {
        Assert-GpuNamedPin -PinSet $State.SignedArtifacts -Name $name -ExpectedPath $expected.$name -Description 'Signed artifact' | Out-Null
    }
}

function Get-GpuPinnedFilePath {
    param([object]$PinSet, [string]$Name)
    $pin = Get-GpuObjectProperty -Object $PinSet -Name $Name
    if ($null -eq $pin) { throw "Pinned file is missing: $Name" }
    Assert-GpuFilePin -Pin $pin
    return [string]$pin.Path
}

function Get-GpuFreshVsToolchain {
    param([string]$ActionDirectory)

    $required = @('cl.exe', 'dumpbin.exe', 'cmake.exe', 'ctest.exe', 'ninja.exe')
    $missing = @($required | Where-Object { $null -eq (Get-Command -Name $_ -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1) })
    $installPath = $null
    $devShellPath = $null
    if ($missing.Count -gt 0) {
        $vswhereCandidates = @()
        $vswhereCommand = Get-Command -Name 'vswhere.exe' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $vswhereCommand) { $vswhereCandidates += [string]$vswhereCommand.Source }
        $programFilesX86 = [string]${env:ProgramFiles(x86)}
        if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
            $vswhereCandidates += (Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe')
        }
        $vswherePath = @($vswhereCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1)
        if ($vswherePath.Count -eq 0) { throw 'Visual Studio toolchain is unavailable and vswhere.exe was not found; no SDK or toolchain will be installed.' }

        $vswhere = Invoke-GpuNativeCommand -Program $vswherePath[0] -Arguments @('-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath') -ActionDirectory $ActionDirectory -Label 'vswhere-vc-installations'
        Assert-GpuNativeSuccess -Result $vswhere -Description 'Visual Studio installation discovery'
        $installations = @(Get-Content -LiteralPath $vswhere.StdoutPath -ErrorAction Stop | ForEach-Object { ([string]$_).Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        foreach ($candidate in $installations) {
            $devCandidate = Join-Path $candidate 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
            if (Test-Path -LiteralPath $devCandidate -PathType Leaf) {
                $installPath = [System.IO.Path]::GetFullPath($candidate)
                $devShellPath = [System.IO.Path]::GetFullPath($devCandidate)
                break
            }
        }
        if ([string]::IsNullOrWhiteSpace($installPath)) { throw 'No discovered Visual Studio installation contains Microsoft.VisualStudio.DevShell.dll and the x64 C++ tools.' }

        Import-Module -Name $devShellPath -ErrorAction Stop
        $enterCommand = Get-Command -Name Enter-VsDevShell -ErrorAction Stop
        & $enterCommand -VsInstallPath $installPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    }

    $toolPaths = [ordered]@{}
    foreach ($name in $required) {
        $command = Get-Command -Name $name -CommandType Application -ErrorAction Stop | Select-Object -First 1
        if ([string]::IsNullOrWhiteSpace([string]$command.Source)) { throw "Visual Studio shell did not expose $name." }
        $toolPaths[$name] = [string]$command.Source
    }
    if (-not [string]::IsNullOrWhiteSpace($installPath)) {
        Write-GpuJson -Path (Join-Path $ActionDirectory 'toolchain-bootstrap.json') -Value ([pscustomobject]@{
            SchemaVersion = 1
            VisualStudioInstallPath = $installPath
            DevShellModulePath = $devShellPath
            ProcessLocalEnvironmentOnly = $true
            Tools = [pscustomobject]$toolPaths
        }) -NoOverwrite
    }
    return [pscustomobject]@{ InstallPath = $installPath; DevShellPath = $devShellPath; Tools = [pscustomobject]$toolPaths }
}

function Get-GpuSignToolPath {
    $command = Get-Command -Name 'signtool.exe' -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace([string]$command.Source)) { return [string]$command.Source }

    $programFilesX86 = [string]${env:ProgramFiles(x86)}
    if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
        throw 'ProgramFiles(x86) is unavailable and signtool.exe was not found on PATH.'
    }
    $kitsRoot = Join-Path $programFilesX86 'Windows Kits\10\bin'
    if (Test-Path -LiteralPath $kitsRoot -PathType Container) {
        $versions = @(Get-ChildItem -LiteralPath $kitsRoot -Directory -ErrorAction Stop | Sort-Object Name -Descending)
        foreach ($version in $versions) {
            $candidate = Join-Path $version.FullName 'x64\signtool.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [System.IO.Path]::GetFullPath($candidate) }
        }
    }
    throw 'signtool.exe was not found on PATH or in the installed Windows Kits 10 x64 bins; no SDK will be installed.'
}

function Assert-GpuThumbprintAndPrivateKey {
    param([string]$CertificatePath)

    $expectedThumbprint = '59DBD6780CFE60C020B3F4511211FEC22C397B85'
    $certificatePath = Assert-GpuPathWithin -Path $CertificatePath -Root ([string]$script:CurrentSourceRoot) -MustExist
    $publicCertificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new([System.IO.File]::ReadAllBytes($certificatePath))
    try {
        if (-not [string]::Equals([string]$publicCertificate.Thumbprint, $expectedThumbprint, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The public signer certificate does not match the pinned project thumbprint.'
        }
    }
    finally { $publicCertificate.Dispose() }

    $storePath = 'Cert:\CurrentUser\My\' + $expectedThumbprint
    $storeCertificate = Get-Item -LiteralPath $storePath -ErrorAction Stop
    if (-not [bool]$storeCertificate.HasPrivateKey) { throw 'The existing CurrentUser\My signing certificate has no accessible private key.' }
    return $expectedThumbprint
}

function Resolve-GpuEspPartition {
    param([Parameter(Mandatory = $true)][string]$Volume)

    Assert-GpuEspIdentity -Expected $Volume -Actual $Volume
    # mountvol /S can expose a DOS mount without setting MSFT_Partition.DriveLetter.
    # Resolve only the exact volume reported by mountvol /L, never a letter or the
    # first partition with the ESP type.
    $partitions = @(Get-Partition -ErrorAction Stop | Where-Object {
        $hasVolume = $false
        foreach ($accessPath in @(Get-GpuObjectProperty -Object $_ -Name 'AccessPaths')) {
            if ([string]::Equals([string]$accessPath, $Volume, [StringComparison]::OrdinalIgnoreCase)) {
                $hasVolume = $true
                break
            }
        }
        $hasVolume
    })
    if ($partitions.Count -ne 1) {
        throw "Mounted ESP volume '$Volume' must resolve to exactly one partition; found $($partitions.Count)."
    }
    $partition = $partitions[0]
    $expectedEspType = 'c12a7328-f81f-11d2-ba4b-00a0c93ec93b'
    if ([string](Get-GpuObjectProperty -Object $partition -Name 'GptType') -notmatch ('(?i)^\{?' + $expectedEspType + '\}?$')) {
        throw 'The mounted volume is not the GPT EFI System Partition type.'
    }
    $partitionGuid = ([string](Get-GpuObjectProperty -Object $partition -Name 'Guid')).Trim('{}')
    $parsedGuid = [guid]::Empty
    if ($partitionGuid -notmatch '^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$' -or
        -not [guid]::TryParse($partitionGuid, [ref]$parsedGuid) -or $parsedGuid -eq [guid]::Empty) {
        throw 'The mounted ESP partition GUID is missing or malformed.'
    }
    $diskNumber = 0
    $partitionNumber = 0
    if (-not [int]::TryParse([string](Get-GpuObjectProperty -Object $partition -Name 'DiskNumber'), [ref]$diskNumber) -or
        -not [int]::TryParse([string](Get-GpuObjectProperty -Object $partition -Name 'PartitionNumber'), [ref]$partitionNumber) -or
        $diskNumber -lt 0 -or $partitionNumber -le 0) {
        throw 'The mounted ESP disk or partition number is missing or invalid.'
    }
    return $partition
}

function Get-GpuEspMount {
    param([string]$ActionDirectory)

    if (Get-PSDrive -Name S -ErrorAction SilentlyContinue) { throw 'S: is already assigned; refusing to unmount or replace another mapping.' }
    if (Test-Path -LiteralPath 'S:\') { throw 'S: already resolves to a path; refusing to alter it.' }
    $mountvol = Get-GpuApplicationPath -Name 'mountvol.exe'
    $mountResult = Invoke-GpuNativeCommand -Program $mountvol -Arguments @('S:', '/S') -ActionDirectory $ActionDirectory -Label 'mount-system-esp'
    Assert-GpuNativeSuccess -Result $mountResult -Description 'Mounting the system ESP at S:'
    $mountedByThisAction = $true
    try {
        $volumeResult = Invoke-GpuNativeCommand -Program $mountvol -Arguments @('S:', '/L') -ActionDirectory $ActionDirectory -Label 'identify-system-esp'
        Assert-GpuNativeSuccess -Result $volumeResult -Description 'Identifying the system ESP volume GUID'
        $volume = ((Get-Content -LiteralPath $volumeResult.StdoutPath -Raw -ErrorAction Stop).Trim())
        $partition = Resolve-GpuEspPartition -Volume $volume
        $partitionGuid = ([string]$partition.Guid).Trim('{}')
        $disk = Get-Disk -Number ([int]$partition.DiskNumber) -ErrorAction Stop
        return [pscustomobject]@{
            Volume = $volume
            DiskNumber = [int]$disk.Number
            DiskUniqueId = [string]$disk.UniqueId
            PartitionNumber = [int]$partition.PartitionNumber
            PartitionGuid = $partitionGuid.ToUpperInvariant()
            GptType = [string]$partition.GptType
        }
    }
    catch {
        if ($mountedByThisAction) { Dismount-GpuEspMount -ActionDirectory $ActionDirectory }
        throw
    }
}

function Dismount-GpuEspMount {
    param([string]$ActionDirectory)

    $mountvol = Get-GpuApplicationPath -Name 'mountvol.exe'
    $result = Invoke-GpuNativeCommand -Program $mountvol -Arguments @('S:', '/D') -ActionDirectory $ActionDirectory -Label 'unmount-owned-system-esp'
    Assert-GpuNativeSuccess -Result $result -Description 'Unmounting the ESP mapping owned by this action'
}

function Assert-GpuNoPreviousStage {
    param([string]$EspRoot)

    $stageDirectory = Join-Path $EspRoot 'EFI\insecure-boot'
    if (Test-Path -LiteralPath $stageDirectory) { throw 'EFI\insecure-boot already exists; preserve and review it before another stage.' }
    $bootDirectory = Join-Path $EspRoot 'EFI\Microsoft\Boot'
    foreach ($name in @('bootmgfw.efi.bak', 'bootmgfw.efi.ibnew', 'bootmgfw.efi.ibold', 'bootmgfw.sha256', 'grubx64.efi')) {
        $candidate = Join-Path $bootDirectory $name
        if (Test-Path -LiteralPath $candidate) { throw "Existing replacement/restore residue blocks staging: $name" }
    }
    $bootManager = Join-Path $bootDirectory 'bootmgfw.efi'
    if (-not (Test-Path -LiteralPath $bootManager -PathType Leaf)) { throw 'Microsoft boot manager is missing from the system ESP.' }
    return $bootManager
}

function Export-GpuSystemBcdBackup {
    param([string]$ActionDirectory, [string]$RecoveryRunDirectory)

    $localPath = Join-Path $ActionDirectory 'system-bcd-export.bcd'
    $externalPath = Join-Path $RecoveryRunDirectory 'system-bcd-export.bcd'
    Assert-GpuFreshPath -Path $localPath
    Assert-GpuFreshPath -Path $externalPath
    Assert-GpuNoReparseAncestors -Path $ActionDirectory
    Assert-GpuNoReparseAncestors -Path $RecoveryRunDirectory
    # /export reads the active system store through Windows' BCD API. It does
    # not modify the store. Never open the live registry hive as a raw file.
    $export = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'bcdedit.exe') `
        -Arguments @('/export', $localPath) -ActionDirectory $ActionDirectory -Label 'export-system-bcd'
    Assert-GpuNativeSuccess -Result $export -Description 'System BCD logical backup export'
    $localPin = Get-GpuFilePin -Path $localPath
    if ($localPin.Bytes -le 0) { throw 'System BCD export is empty; no EFI backup may be admitted.' }
    [System.IO.File]::Copy($localPath, $externalPath, $false)
    $stream = [System.IO.File]::Open($externalPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::Read)
    try { $stream.Flush($true) } finally { $stream.Dispose() }
    $externalPin = Get-GpuFilePin -Path $externalPath
    Assert-GpuFilePin -Pin $localPin
    if ($externalPin.Bytes -ne $localPin.Bytes -or $externalPin.Sha256 -cne $localPin.Sha256) {
        throw 'External system BCD export failed hash readback.'
    }
    return [pscustomobject]@{ LocalExport=$localPin; ExternalExport=$externalPin; ReceiptPath=$export.ReceiptPath }
}

function Copy-GpuEspTreeWithManifest {
    param(
        [string]$SourceRoot,
        [string]$DestinationRoot,
        [string]$ActionDirectory,
        [AllowNull()][object]$SystemBcdExport = $null
    )

    Assert-GpuFreshPath -Path $DestinationRoot
    Assert-GpuNoReparseAncestors -Path $SourceRoot
    $sourceFull = [System.IO.Path]::GetFullPath($SourceRoot).TrimEnd('\')
    $omitted = New-Object System.Collections.Generic.List[object]
    $systemBcdCount = 0
    if ($null -ne $SystemBcdExport) {
        Assert-GpuFilePin -Pin $SystemBcdExport
        if ([long]$SystemBcdExport.Bytes -le 0) { throw 'System BCD export must be nonempty.' }
        $exportFull = [System.IO.Path]::GetFullPath([string]$SystemBcdExport.Path)
        if ($exportFull.StartsWith(($sourceFull + '\'), [StringComparison]::OrdinalIgnoreCase)) {
            throw 'System BCD export must be outside the live EFI source tree.'
        }
    }
    $directories = New-Object System.Collections.Generic.List[string]
    $files = New-Object System.Collections.Generic.List[object]
    $pending = New-Object System.Collections.Generic.Stack[string]
    $pending.Push($sourceFull)
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        $children = @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)
        foreach ($child in $children) {
            if (($child.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "ESP backup refuses a reparse point: $($child.FullName)"
            }
            $relative = $child.FullName.Substring($sourceFull.Length).TrimStart('\')
            if ([string]::IsNullOrWhiteSpace($relative)) { throw 'ESP traversal produced an empty relative path.' }
            if ($child.PSIsContainer) {
                $directories.Add($relative)
                $pending.Push($child.FullName)
            }
            else {
                # Only the system hive and its exact volatile transaction logs
                # are replaced by the verified logical export. Other files,
                # including other stores/log-like names, retain strict copying.
                if ($null -ne $SystemBcdExport -and $relative -iin @('Microsoft\Boot\BCD', 'Microsoft\Boot\BCD.LOG', 'Microsoft\Boot\BCD.LOG1', 'Microsoft\Boot\BCD.LOG2')) {
                    $isSystemBcd = ($relative -ieq 'Microsoft\Boot\BCD')
                    if ($isSystemBcd) { $systemBcdCount++ }
                    $omitted.Add([pscustomobject]@{
                        RelativePath=$relative
                        Reason=if ($isSystemBcd) { 'SystemBcdLogicalExport' } else { 'VolatileSystemBcdLog' }
                    })
                    continue
                }
                $files.Add([pscustomobject]@{ Source = $child.FullName; RelativePath = $relative; Bytes = [long]$child.Length })
            }
        }
    }

    if ($null -ne $SystemBcdExport -and $systemBcdCount -ne 1) {
        throw 'The selected system ESP must contain exactly one Microsoft\Boot\BCD before using a system-store export.'
    }

    [void](New-Item -ItemType Directory -Path $DestinationRoot -ErrorAction Stop)
    foreach ($relativeDirectory in @($directories | Sort-Object Length)) {
        $destinationDirectory = Join-Path $DestinationRoot $relativeDirectory
        Assert-GpuPathWithin -Path $destinationDirectory -Root $DestinationRoot | Out-Null
        if (-not (Test-Path -LiteralPath $destinationDirectory)) {
            [void](New-Item -ItemType Directory -Path $destinationDirectory -ErrorAction Stop)
        }
    }

    $manifestFiles = New-Object System.Collections.Generic.List[object]
    foreach ($entry in $files) {
        $sourcePinBefore = Get-GpuFilePin -Path $entry.Source
        if ($sourcePinBefore.Bytes -ne $entry.Bytes) { throw "ESP source file changed while enumerating: $($entry.RelativePath)" }
        $destinationPath = Join-Path $DestinationRoot $entry.RelativePath
        Assert-GpuPathWithin -Path $destinationPath -Root $DestinationRoot | Out-Null
        [System.IO.File]::Copy($entry.Source, $destinationPath, $false)
        $destinationPin = Get-GpuFilePin -Path $destinationPath
        $sourcePinAfter = Get-GpuFilePin -Path $entry.Source
        if ($destinationPin.Bytes -ne $sourcePinBefore.Bytes -or
            -not [string]::Equals($destinationPin.Sha256, $sourcePinBefore.Sha256, [StringComparison]::OrdinalIgnoreCase) -or
            -not [string]::Equals($sourcePinAfter.Sha256, $sourcePinBefore.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "ESP backup readback mismatch: $($entry.RelativePath)"
        }
        $manifestFiles.Add([pscustomobject]@{
            RelativePath = $entry.RelativePath.Replace('/', '\')
            Sha256 = $destinationPin.Sha256
            Bytes = [long]$destinationPin.Bytes
        })
    }

    if ($null -ne $SystemBcdExport) { Assert-GpuFilePin -Pin $SystemBcdExport }

    return [pscustomobject]@{
        Root = $DestinationRoot
        FileCount = $manifestFiles.Count
        DirectoryCount = $directories.Count
        Files = @($manifestFiles.ToArray())
        OmittedFiles = @($omitted.ToArray())
    }
}

function Assert-GpuSystemBcdBackup {
    param([object]$Backup, [object]$Manifest, [string]$RecoveryRunDirectory)

    if ([int]$Manifest.SchemaVersion -eq 1) { return }
    if ([int]$Manifest.SchemaVersion -ne 2) { throw 'Unsupported ESP backup manifest schema.' }
    $exportPin = Get-GpuObjectProperty -Object $Backup -Name 'SystemBcdExport'
    $description = Get-GpuObjectProperty -Object $Manifest -Name 'SystemBcdExport'
    if ($null -eq $exportPin -or $null -eq $description) { throw 'System BCD export evidence is missing.' }
    $expectedPath = Join-Path $RecoveryRunDirectory 'system-bcd-export.bcd'
    if (-not [string]::Equals([string]$exportPin.Path, [System.IO.Path]::GetFullPath($expectedPath), [StringComparison]::OrdinalIgnoreCase) -or
        [string]$description.Kind -cne 'BcdeditSystemStoreExport' -or [string]$description.RelativePath -cne 'system-bcd-export.bcd' -or
        [long]$exportPin.Bytes -le 0 -or [long]$description.Bytes -ne [long]$exportPin.Bytes -or
        [string]$description.Sha256 -cne [string]$exportPin.Sha256) { throw 'System BCD export identity differs from the backup manifest.' }
    Assert-GpuFilePin -Pin $exportPin
    $allowed = @('Microsoft\Boot\BCD', 'Microsoft\Boot\BCD.LOG', 'Microsoft\Boot\BCD.LOG1', 'Microsoft\Boot\BCD.LOG2')
    $seen = @{}
    foreach ($entry in @($Manifest.OmittedFiles)) {
        $relative = [string]$entry.RelativePath
        $reason = if ($relative -ieq 'Microsoft\Boot\BCD') { 'SystemBcdLogicalExport' } else { 'VolatileSystemBcdLog' }
        if ($relative -inotin $allowed -or $seen.ContainsKey($relative) -or [string]$entry.Reason -cne $reason) {
            throw 'ESP manifest contains an invalid or duplicate BCD omission.'
        }
        $seen[$relative] = $true
    }
    if (-not $seen.ContainsKey('Microsoft\Boot\BCD')) { throw 'ESP manifest does not record the system BCD logical export substitution.' }
    foreach ($entry in @($Manifest.Files)) {
        if ([string]$entry.RelativePath -iin $allowed) { throw 'ESP manifest incorrectly claims a raw copy of an exported system BCD file.' }
    }
}

function Assert-GpuRecoveryManifest {
    param([object]$State)

    $location = Assert-GpuRecoveryLocation -State $State -RequireRunDirectory
    $backup = Get-GpuObjectProperty -Object $State -Name 'Backup'
    if ($null -eq $backup) { throw 'No ESP backup is recorded.' }
    $manifestPin = Get-GpuObjectProperty -Object $backup -Name 'ExternalManifest'
    if ($null -eq $manifestPin) { throw 'External recovery manifest pin is missing.' }
    Assert-GpuFilePin -Pin $manifestPin
    $expectedManifestPath = Join-Path $location.RunDirectory 'esp-manifest.json'
    if (-not [string]::Equals([string]$manifestPin.Path, [System.IO.Path]::GetFullPath($expectedManifestPath), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'External manifest pin does not point into this run recovery directory.'
    }
    $manifest = Read-GpuJson -Path $expectedManifestPath
    Assert-GpuEspIdentity -Expected ([string]$backup.EspVolume) -Actual ([string]$manifest.EspVolume)
    if ([string]$manifest.BootManagerSha256 -ne [string]$backup.BootManagerSha256) {
        throw 'External recovery manifest has a different boot-manager baseline.'
    }
    $manifestDiskMatches = Test-GpuDiskIdentityMatch `
        -ExpectedUniqueId ([string](Get-GpuObjectProperty -Object $manifest -Name 'RecoveryDiskUniqueId')) `
        -ExpectedSerialNumber ([string](Get-GpuObjectProperty -Object $manifest -Name 'RecoveryDiskSerialNumber')) `
        -ActualUniqueId ([string]$location.Disk.UniqueId) `
        -ActualSerialNumber ([string]$location.Disk.SerialNumber)
    if ([int]$manifest.EspDiskNumber -ne [int]$backup.EspDiskNumber -or
        [int]$manifest.RecoveryDiskNumber -ne [int]$location.Disk.Number -or
        -not $manifestDiskMatches -or
        [int](Get-GpuObjectProperty -Object $backup -Name 'RecoveryDiskNumber' -Default -1) -ne [int]$location.Disk.Number -or
        -not (Test-GpuDiskIdentityMatch `
            -ExpectedUniqueId ([string](Get-GpuObjectProperty -Object $backup -Name 'RecoveryDiskUniqueId')) `
            -ExpectedSerialNumber ([string](Get-GpuObjectProperty -Object $backup -Name 'RecoveryDiskSerialNumber')) `
            -ActualUniqueId ([string]$location.Disk.UniqueId) `
            -ActualSerialNumber ([string]$location.Disk.SerialNumber))) {
        throw 'External recovery manifest disk identities differ from the journal.'
    }
    if ([int]$location.Disk.Number -eq [int]$State.Host.SystemDiskNumber) {
        throw 'Recovery files are no longer on a separate physical disk.'
    }
    Assert-GpuSystemBcdBackup -Backup $backup -Manifest $manifest -RecoveryRunDirectory $location.RunDirectory
    $tree = Join-Path $location.RunDirectory 'esp-efi-before'
    if (-not (Test-Path -LiteralPath $tree -PathType Container)) { throw 'External ESP backup tree is missing.' }
    foreach ($entry in @($manifest.Files)) {
        $relativePath = [string]$entry.RelativePath
        if ([string]::IsNullOrWhiteSpace($relativePath)) { throw 'External recovery manifest contains an empty relative path.' }
        $filePath = Assert-GpuPathWithin -Path (Join-Path $tree $relativePath) -Root $tree -MustExist
        $actual = Get-GpuFilePin -Path $filePath
        if ($actual.Bytes -ne [long]$entry.Bytes -or
            -not [string]::Equals($actual.Sha256, [string]$entry.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "External recovery file failed manifest verification: $($entry.RelativePath)"
        }
    }
    return [pscustomobject]@{ Location = $location; Manifest = $manifest; ManifestPin = $manifestPin; Tree = $tree }
}

function Assert-GpuRecordedBackupIdentity {
    param([object]$State)

    $backup = Get-GpuObjectProperty -Object $State -Name 'Backup'
    if ($null -eq $backup) { throw 'ESP backup data is missing.' }
    foreach ($name in @('EspVolume', 'BootManagerSha256', 'EspDiskNumber', 'EspPartitionGuid')) {
        if ($null -eq $backup.PSObject.Properties[$name] -or [string]::IsNullOrWhiteSpace([string]$backup.$name)) {
            throw "ESP backup field '$name' is missing."
        }
    }
    if ([string]$backup.EspVolume -notmatch '^\\\\\?\\Volume\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}\\$' -or
        [string]$backup.BootManagerSha256 -notmatch '^[0-9A-Fa-f]{64}$' -or
        [int]$backup.EspDiskNumber -lt 0) {
        throw 'ESP backup target identity is malformed.'
    }
    $partitionGuid = [guid]::Empty
    if (-not [guid]::TryParse([string]$backup.EspPartitionGuid, [ref]$partitionGuid)) {
        throw 'ESP backup partition GUID is malformed.'
    }
    foreach ($localPinName in @('LocalManifest', 'ReferenceLog', 'FirmwareEnumeration', 'LocalBcdExport', 'BcdExportReceipt')) {
        $localPin = Get-GpuObjectProperty -Object $backup -Name $localPinName
        if ($null -ne $localPin) { Assert-GpuFilePin -Pin $localPin }
    }
    return $backup
}

function Assert-GpuBackupState {
    param([object]$State)

    $backup = Assert-GpuRecordedBackupIdentity -State $State
    Assert-GpuRecoveryManifest -State $State | Out-Null
    return $backup
}

function Get-GpuMountedEspBootManagerHash {
    param([string]$EspRoot)

    $path = Join-Path $EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi'
    return (Get-GpuFilePin -Path $path)
}

function Assert-GpuMountedEspMatchesBackup {
    param([object]$MountedEsp, [object]$Backup, [string]$Description)

    Assert-GpuEspIdentity -Expected ([string]$Backup.EspVolume) -Actual ([string]$MountedEsp.Volume)
    if ([int]$MountedEsp.DiskNumber -ne [int]$Backup.EspDiskNumber -or
        -not [string]::Equals([string]$MountedEsp.PartitionGuid, [string]$Backup.EspPartitionGuid, [StringComparison]::OrdinalIgnoreCase)) {
        throw "${Description}: system ESP disk or partition GUID changed."
    }
    $bootManager = Get-GpuMountedEspBootManagerHash -EspRoot 'S:'
    if (-not [string]::Equals([string]$bootManager.Sha256, [string]$Backup.BootManagerSha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw "${Description}: Microsoft boot-manager hash differs from the saved baseline."
    }
    return $bootManager
}

function Get-GpuStageExpectedFiles {
    return @(
        [pscustomobject]@{ RelativePath = 'EFI\insecure-boot\shimx64.efi'; Name = 'shimx64.efi' }
        [pscustomobject]@{ RelativePath = 'EFI\insecure-boot\grubx64.efi'; Name = 'grubx64.efi' }
        [pscustomobject]@{ RelativePath = 'EFI\insecure-boot\tcglog.ib'; Name = 'tcglog.ib' }
        [pscustomobject]@{ RelativePath = 'EFI\insecure-boot\payload.efi'; Name = 'payload.efi' }
        [pscustomobject]@{ RelativePath = 'EFI\insecure-boot\manifest.sha256'; Name = 'manifest.sha256' }
    )
}

function Get-GpuStageReadback {
    param([string]$EspRoot)

    $files = New-Object System.Collections.Generic.List[object]
    foreach ($entry in (Get-GpuStageExpectedFiles)) {
        $path = Join-Path $EspRoot $entry.RelativePath
        $pin = Get-GpuFilePin -Path $path
        $files.Add([pscustomobject]@{
            RelativePath = $entry.RelativePath
            Name = $entry.Name
            Sha256 = $pin.Sha256
            Bytes = [long]$pin.Bytes
        })
    }
    return @($files.ToArray())
}

function Assert-GpuStageInputPins {
    param([object]$State, [object[]]$StageFiles)

    $toolPaths = Get-GpuExpectedToolPaths -State $State
    $signedPaths = Get-GpuExpectedSignedArtifactPaths -State $State
    $backup = Get-GpuObjectProperty -Object $State -Name 'Backup'
    $expectedInputs = @(
        [pscustomobject]@{
            RelativePath = 'EFI\insecure-boot\shimx64.efi'
            Pin = (Assert-GpuNamedPin -PinSet $State.Tools -Name 'Shim' -ExpectedPath $toolPaths.Shim -Description 'Tool')
        }
        [pscustomobject]@{
            RelativePath = 'EFI\insecure-boot\grubx64.efi'
            Pin = (Assert-GpuNamedPin -PinSet $State.SignedArtifacts -Name 'Loader' -ExpectedPath $signedPaths.Loader -Description 'Signed artifact')
        }
        [pscustomobject]@{
            RelativePath = 'EFI\insecure-boot\payload.efi'
            Pin = (Assert-GpuNamedPin -PinSet $State.SignedArtifacts -Name 'Payload' -ExpectedPath $signedPaths.Payload -Description 'Signed artifact')
        }
        [pscustomobject]@{
            RelativePath = 'EFI\insecure-boot\tcglog.ib'
            Pin = (Get-GpuObjectProperty -Object $backup -Name 'ReferenceLog')
        }
    )

    foreach ($expected in $expectedInputs) {
        if ($null -eq $expected.Pin) { throw "Prepared stage input pin is missing for $($expected.RelativePath)." }
        Assert-GpuFilePin -Pin $expected.Pin
        $matchingStageInputs = @($StageFiles | Where-Object { [string]::Equals([string]$_.RelativePath, [string]$expected.RelativePath, [StringComparison]::OrdinalIgnoreCase) })
        if ($matchingStageInputs.Count -ne 1 -or
            [long]$matchingStageInputs[0].Bytes -ne [long]$expected.Pin.Bytes -or
            -not [string]::Equals([string]$matchingStageInputs[0].Sha256, [string]$expected.Pin.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Staged file does not match its prepared input pin: $($expected.RelativePath)"
        }
    }
}

function Assert-GpuStageState {
    param([object]$State, [string]$EspRoot)

    $stage = Get-GpuObjectProperty -Object $State -Name 'Stage'
    $backup = Assert-GpuBackupState -State $State
    if ($null -eq $stage) { throw 'No staged-file readback is recorded.' }
    Assert-GpuEspIdentity -Expected ([string]$backup.EspVolume) -Actual ([string]$stage.EspVolume)
    $liveFiles = Get-GpuStageReadback -EspRoot $EspRoot
    $recordedFiles = @($stage.Files)
    if ($recordedFiles.Count -ne 5 -or $liveFiles.Count -ne 5) { throw 'The recorded or live stage does not contain exactly five owned files.' }
    foreach ($expected in $liveFiles) {
        $recorded = @($recordedFiles | Where-Object { [string]::Equals([string]$_.RelativePath, [string]$expected.RelativePath, [StringComparison]::OrdinalIgnoreCase) })
        if ($recorded.Count -ne 1 -or
            [long]$recorded[0].Bytes -ne [long]$expected.Bytes -or
            -not [string]::Equals([string]$recorded[0].Sha256, [string]$expected.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Staged file changed since readback: $($expected.RelativePath)"
        }
    }
    Assert-GpuStageInputPins -State $State -StageFiles $liveFiles
    return $liveFiles
}

function Invoke-GpuPrepare {
    param([object]$State, [string]$ActionDirectory, [switch]$PhysicalOutputConfirmed, [switch]$RecoveryReady)

    if (-not $PhysicalOutputConfirmed) { throw 'PhysicalOutputConfirmed must be supplied after manually verifying an Intel-connected diagnostic display.' }
    if (-not $RecoveryReady) { throw 'RecoveryReady must be supplied after manually confirming external recovery media and the offline BitLocker recovery key.' }
    Assert-GpuAdministrator
    $current = Get-GpuCurrentMachineSnapshot -IncludeSecureBoot
    Assert-GpuSecureBootOn -Current $current

    $preflight = Get-GpuPreflight -State $State -ActionDirectory $ActionDirectory -Name 'gpu-preflight-before'
    Assert-GpuPreflightAdapters -Report $preflight.Report
    $manageBde = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'manage-bde.exe') -Arguments @('-status', $current.SystemDrive) -ActionDirectory $ActionDirectory -Label 'manage-bde-status'
    Assert-GpuNativeSuccess -Result $manageBde -Description 'BitLocker status query'
    $firmware = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'bcdedit.exe') -Arguments @('/enum', 'firmware', '/v') -ActionDirectory $ActionDirectory -Label 'firmware-enumeration-before'
    Assert-GpuNativeSuccess -Result $firmware -Description 'Firmware boot-entry enumeration'

    $recoveryLocation = Assert-GpuRecoveryLocation -State $State
    if ($recoveryLocation.Disk.Number -eq $current.SystemDiskNumber) {
        throw 'RecoveryBase is on the Windows system disk.'
    }
    $runId = Get-GpuRunIdLeaf -State $State
    $externalDirectory = [string](Get-GpuObjectProperty -Object $State -Name 'RecoveryRunDirectory')
    $expectedExternalDirectory = Join-Path $recoveryLocation.Base $runId
    if ([string]::IsNullOrWhiteSpace($externalDirectory) -or
        -not [string]::Equals([System.IO.Path]::GetFullPath($externalDirectory), [System.IO.Path]::GetFullPath($expectedExternalDirectory), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'State.RecoveryRunDirectory must equal RecoveryBase\RunId before Prepare.'
    }
    $externalDirectory = Assert-GpuPathWithin -Path $externalDirectory -Root $recoveryLocation.Base
    Assert-GpuFreshPath -Path $externalDirectory
    [void](New-Item -ItemType Directory -Path $externalDirectory -ErrorAction Stop)
    $current | Add-Member -MemberType NoteProperty -Name RecoveryDiskNumber -Value ([int]$recoveryLocation.Disk.Number)
    $current | Add-Member -MemberType NoteProperty -Name RecoveryDiskUniqueId -Value ([string]$recoveryLocation.Disk.UniqueId)
    $current | Add-Member -MemberType NoteProperty -Name RecoveryDiskSerialNumber -Value ([string]$recoveryLocation.Disk.SerialNumber)
    $current | Add-Member -MemberType NoteProperty -Name RecoveryBase -Value $recoveryLocation.Base
    $current | Add-Member -MemberType NoteProperty -Name Preflight -Value $preflight.Pin
    $current | Add-Member -MemberType NoteProperty -Name ManageBdeStdout -Value $manageBde.StdoutPath
    $current | Add-Member -MemberType NoteProperty -Name FirmwareEnumeration -Value $firmware.StdoutPath
    Set-GpuStateProperty -Object $State -Name 'Host' -Value $current
    return New-GpuPlatformResult -Status 'PREPARED_READONLY_GATE_PASS' -Succeeded $true -Data ([ordered]@{
        Host = $current
        RecoveryRunDirectory = $externalDirectory
        RecoveryDisk = $recoveryLocation.Disk
        PreflightPath = $preflight.Path
        PreflightPin = $preflight.Pin
        ManageBdeReceipt = $manageBde.ReceiptPath
        FirmwareEnumerationReceipt = $firmware.ReceiptPath
        GateMeaning = 'Read-only host gate and operator prerequisites passed; this is not GPU ownership proof.'
    })
}

function Invoke-GpuBuild {
    param([object]$State, [string]$ActionDirectory)

    Assert-GpuAdministrator
    $current = Get-GpuCurrentMachineSnapshot -IncludeSecureBoot
    Assert-GpuSameMachine -State $State -Current $current -RequireCpuCount
    Assert-GpuOriginalBoot -State $State -Current $current
    Assert-GpuSecureBootOn -Current $current
    $toolchain = Get-GpuFreshVsToolchain -ActionDirectory $ActionDirectory
    $paths = Get-GpuBuildDirectories -State $State
    # Get-GpuBuildDirectories enforces the derived workspace-local build roots
    # and the signed directory's separate per-run boundary.
    if (Test-Path -LiteralPath $paths.HypervisorBuild) { throw 'Hypervisor build directory already exists; refusing to reuse generated state.' }
    if (Test-Path -LiteralPath $paths.LoaderBuild) { throw 'Loader build directory already exists; refusing to reuse generated state.' }
    if (Test-Path -LiteralPath $paths.SignedDirectory) { throw 'Signed directory already exists; signing requires a fresh destination.' }
    $sourceRoot = [string](Get-GpuObjectProperty -Object $State -Name 'SourceRoot')
    $hypervisorSource = Assert-GpuPathWithin -Path (Join-Path $sourceRoot 'components\hypervisor') -Root $sourceRoot -MustExist
    $loaderSource = Assert-GpuPathWithin -Path (Join-Path $sourceRoot 'components\uefi-loader') -Root $sourceRoot -MustExist
    $cmake = [string]$toolchain.Tools.'cmake.exe'
    $ctest = [string]$toolchain.Tools.'ctest.exe'

    $configureHypervisor = Invoke-GpuNativeCommand -Program $cmake -Arguments @(
        '-S', $hypervisorSource, '-B', $paths.HypervisorBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OBJECT_PATH_MAX=220',
        '-DHYPERSINV_BUILD_CORE=OFF', '-DHYPERSINV_BUILD_DRIVER=OFF',
        '-DHYPERSINV_BUILD_LOADER=OFF', '-DHYPERSINV_BUILD_UEFI=ON',
        '-DHYPERSINV_GPU_VM_SELFTEST=OFF', '-DHYPERSINV_GPU_DIAGNOSTIC_NATIVE=ON',
        '-DHYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED=ON'
    ) -ActionDirectory $ActionDirectory -Label 'cmake-configure-native-hypervisor'
    Assert-GpuNativeSuccess -Result $configureHypervisor -Description 'Native hypervisor CMake configure'
    $buildHypervisor = Invoke-GpuNativeCommand -Program $cmake -Arguments @('--build', $paths.HypervisorBuild, '--parallel', '4') -ActionDirectory $ActionDirectory -Label 'cmake-build-native-hypervisor'
    Assert-GpuNativeSuccess -Result $buildHypervisor -Description 'Native hypervisor build'
    $hypervisorCtestLog = Join-Path $ActionDirectory 'hypervisor-ctest.log'
    Assert-GpuFreshPath -Path $hypervisorCtestLog
    $testHypervisor = Invoke-GpuNativeCommand -Program $ctest -Arguments @('--test-dir', $paths.HypervisorBuild, '--output-on-failure', '--output-log', $hypervisorCtestLog) -ActionDirectory $ActionDirectory -Label 'ctest-native-hypervisor'
    Assert-GpuNativeSuccess -Result $testHypervisor -Description 'Native hypervisor CTest suite'

    $audit = Assert-GpuPathWithin -Path (Join-Path $sourceRoot 'components\hypervisor\src\efi\tests\Test-EfiRuntimeScannerImage.ps1') -Root $sourceRoot -MustExist
    $efiPath = Join-Path $paths.HypervisorBuild 'src\efi\uefi_hypervisor.efi'
    $auditUnsigned = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'powershell.exe') -Arguments @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $audit,
        '-ImagePath', $efiPath, '-DumpbinPath', [string]$toolchain.Tools.'dumpbin.exe', '-MaximumPages', '16'
    ) -ActionDirectory $ActionDirectory -Label 'hvscan-native-unsigned'
    Assert-GpuNativeSuccess -Result $auditUnsigned -Description 'Unsigned native .hvscan audit'
    if (-not ((Get-Content -LiteralPath $auditUnsigned.StdoutPath -Raw) -match 'PASS: \.hvscan is self-contained')) {
        throw 'Unsigned native EFI image did not report the required .hvscan self-containment PASS.'
    }

    $configureLoader = Invoke-GpuNativeCommand -Program $cmake -Arguments @(
        '-S', $loaderSource, '-B', $paths.LoaderBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OBJECT_PATH_MAX=220', '-DIB_STATIC_RUNTIME=ON',
        '-DIB_QEMU_DEBUG_PORTS=OFF', '-DIB_PAYLOAD_REPLAY_INDEPENDENT=ON'
    ) -ActionDirectory $ActionDirectory -Label 'cmake-configure-loader'
    Assert-GpuNativeSuccess -Result $configureLoader -Description 'Loader CMake configure'
    $buildLoader = Invoke-GpuNativeCommand -Program $cmake -Arguments @('--build', $paths.LoaderBuild, '--parallel', '4') -ActionDirectory $ActionDirectory -Label 'cmake-build-loader'
    Assert-GpuNativeSuccess -Result $buildLoader -Description 'Loader build'
    $loaderCtestLog = Join-Path $ActionDirectory 'loader-ctest.log'
    Assert-GpuFreshPath -Path $loaderCtestLog
    $testLoader = Invoke-GpuNativeCommand -Program $ctest -Arguments @('--test-dir', $paths.LoaderBuild, '--output-on-failure', '--output-log', $loaderCtestLog) -ActionDirectory $ActionDirectory -Label 'ctest-loader'
    Assert-GpuNativeSuccess -Result $testLoader -Description 'Loader CTest suite'

    $expectedTools = Get-GpuExpectedToolPaths -State $State
    $toolPins = [ordered]@{}
    foreach ($name in @('Installer', 'Probe', 'Verifier', 'Shim', 'Certificate')) {
        $toolPins[$name] = Get-GpuFilePin -Path $expectedTools.$name
    }
    $expectedArtifacts = Get-GpuExpectedBuildArtifactPaths -State $State
    $artifactPins = [ordered]@{
        EFI = Get-GpuFilePin -Path $expectedArtifacts.EFI
        Loader = Get-GpuFilePin -Path $expectedArtifacts.Loader
    }
    Set-GpuStateProperty -Object $State -Name 'Tools' -Value ([pscustomobject]$toolPins)
    Set-GpuStateProperty -Object $State -Name 'BuildArtifacts' -Value ([pscustomobject]$artifactPins)
    return New-GpuPlatformResult -Status 'BUILD_AND_TESTS_PASS' -Succeeded $true -Data ([ordered]@{
        Tools = [pscustomobject]$toolPins
        BuildArtifacts = [pscustomobject]$artifactPins
        HypervisorCtestLog = $hypervisorCtestLog
        LoaderCtestLog = $loaderCtestLog
        HvscanUnsigned = $auditUnsigned.StdoutPath
        Toolchain = $toolchain
    })
}

function Invoke-GpuSign {
    param([object]$State, [string]$ActionDirectory)

    Assert-GpuAdministrator
    $current = Get-GpuCurrentMachineSnapshot -IncludeSecureBoot
    Assert-GpuSameMachine -State $State -Current $current -RequireCpuCount
    Assert-GpuOriginalBoot -State $State -Current $current
    Assert-GpuSecureBootOn -Current $current
    Assert-GpuToolPins -State $State
    Assert-GpuBuildPins -State $State
    $toolchain = Get-GpuFreshVsToolchain -ActionDirectory $ActionDirectory
    $paths = Get-GpuBuildDirectories -State $State
    [void](New-Item -ItemType Directory -Path $paths.SignedDirectory -ErrorAction Stop)
    $expected = Get-GpuExpectedSignedArtifactPaths -State $State
    $sourceArtifacts = Get-GpuExpectedBuildArtifactPaths -State $State
    foreach ($path in @($expected.Loader, $expected.Payload)) { Assert-GpuFreshPath -Path $path }
    [System.IO.File]::Copy($sourceArtifacts.Loader, $expected.Loader, $false)
    [System.IO.File]::Copy($sourceArtifacts.EFI, $expected.Payload, $false)

    $tools = $State.Tools
    $certificatePath = Get-GpuPinnedFilePath -PinSet $tools -Name 'Certificate'
    $thumbprint = Assert-GpuThumbprintAndPrivateKey -CertificatePath $certificatePath
    $signTool = Get-GpuSignToolPath
    $verifierPath = Get-GpuPinnedFilePath -PinSet $tools -Name 'Verifier'
    $signedImages = @($expected.Loader, $expected.Payload)
    foreach ($image in $signedImages) {
        $signResult = Invoke-GpuNativeCommand -Program $signTool -Arguments @('sign', '/s', 'My', '/sha1', $thumbprint, '/fd', 'SHA256', $image) -ActionDirectory $ActionDirectory -Label ('signtool-' + [IO.Path]::GetFileName($image))
        Assert-GpuNativeSuccess -Result $signResult -Description "Signing $([IO.Path]::GetFileName($image))"
        $verifyResult = Invoke-GpuNativeCommand -Program $verifierPath -Arguments @($image, $certificatePath) -ActionDirectory $ActionDirectory -Label ('verify-signature-' + [IO.Path]::GetFileName($image))
        Assert-GpuNativeSuccess -Result $verifyResult -Description "Authenticode signer verification for $([IO.Path]::GetFileName($image))"
    }

    $sourceRoot = [string](Get-GpuObjectProperty -Object $State -Name 'SourceRoot')
    $audit = Assert-GpuPathWithin -Path (Join-Path $sourceRoot 'components\hypervisor\src\efi\tests\Test-EfiRuntimeScannerImage.ps1') -Root $sourceRoot -MustExist
    $dumpbin = [string]$toolchain.Tools.'dumpbin.exe'
    $signedAudit = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'powershell.exe') -Arguments @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $audit,
        '-ImagePath', $expected.Payload, '-DumpbinPath', $dumpbin, '-MaximumPages', '16'
    ) -ActionDirectory $ActionDirectory -Label 'hvscan-signed-payload'
    Assert-GpuNativeSuccess -Result $signedAudit -Description 'Signed native .hvscan audit'
    if (-not ((Get-Content -LiteralPath $signedAudit.StdoutPath -Raw) -match 'PASS: \.hvscan is self-contained')) {
        throw 'Signed payload did not report the required .hvscan self-containment PASS.'
    }

    $shimPath = Get-GpuPinnedFilePath -PinSet $tools -Name 'Shim'
    $shimPin = Get-GpuFilePin -Path $shimPath
    if (-not [string]::Equals($shimPin.Sha256, '6FE6E1BCBE6CF6BAEC8E056D40361CA1AA715CC04DDCC2855351DE060B84350B', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Vendored shim does not match the reviewed SHA-256.'
    }
    $signedPins = [pscustomobject]@{
        Loader = Get-GpuFilePin -Path $expected.Loader
        Payload = Get-GpuFilePin -Path $expected.Payload
    }
    Set-GpuStateProperty -Object $State -Name 'SignedArtifacts' -Value $signedPins
    return New-GpuPlatformResult -Status 'SIGNATURE_AND_AUDIT_PASS' -Succeeded $true -Data ([ordered]@{
        SignedArtifacts = $signedPins
        SignerThumbprint = $thumbprint
        SignToolPath = $signTool
        Toolchain = $toolchain
        HvscanSigned = $signedAudit.StdoutPath
    })
}

function Invoke-GpuBackup {
    param([object]$State, [string]$ActionDirectory)

    Assert-GpuAdministrator
    $current = Get-GpuCurrentMachineSnapshot -IncludeSecureBoot
    Assert-GpuSameMachine -State $State -Current $current -RequireCpuCount
    Assert-GpuOriginalBoot -State $State -Current $current
    Assert-GpuSecureBootOn -Current $current
    Assert-GpuToolPins -State $State
    Assert-GpuBuildPins -State $State
    Assert-GpuSignedPins -State $State
    $location = Assert-GpuRecoveryLocation -State $State -RequireRunDirectory
    if ($location.Disk.Number -eq [int]$State.Host.SystemDiskNumber) { throw 'Recovery disk is no longer separate from the Windows system disk.' }

    $probe = Get-GpuPinnedFilePath -PinSet $State.Tools -Name 'Probe'
    $probeResult = Invoke-GpuNativeCommand -Program $probe -Arguments @('probe') -ActionDirectory $ActionDirectory -Label 'tpm-precondition-probe' -AllowedExitCodes @(1)
    Assert-GpuNativeSuccess -Result $probeResult -Description 'Expected nonempty-PCR TPM precondition probe'
    $probeText = (Get-Content -LiteralPath $probeResult.StdoutPath -Raw) + "`n" + (Get-Content -LiteralPath $probeResult.StderrPath -Raw)
    if ($probeText -notmatch 'PCR\[0-7\] at reset values: NO') { throw 'TPM probe did not report a readable nonempty PCR[0-7] bank.' }

    $referenceLogPath = Join-Path $ActionDirectory 'tcglog.ib'
    $fullLogPath = Join-Path $ActionDirectory 'tcglog-full.bin'
    Assert-GpuFreshPath -Path $referenceLogPath
    Assert-GpuFreshPath -Path $fullLogPath
    $capture = Invoke-GpuNativeCommand -Program $probe -Arguments @('capture', '--out', $referenceLogPath, '--full', $fullLogPath) -ActionDirectory $ActionDirectory -Label 'capture-tcg2-reference-log'
    Assert-GpuNativeSuccess -Result $capture -Description 'TCG2 measured-boot log capture'
    $referenceLogPin = Get-GpuFilePin -Path $referenceLogPath

    $ownedMount = $false
    try {
        $esp = Get-GpuEspMount -ActionDirectory $ActionDirectory
        $ownedMount = $true
        Assert-GpuEspIdentity -Expected ([string]$esp.Volume) -Actual ([string]$esp.Volume)
        if ([int]$esp.DiskNumber -ne [int]$State.Host.SystemDiskNumber) {
            throw 'Mounted system ESP is not on the current Windows system disk.'
        }
        $bootManagerPath = Assert-GpuNoPreviousStage -EspRoot 'S:'
        $bootManagerPin = Get-GpuFilePin -Path $bootManagerPath
        $recoveryLocation = Assert-GpuRecoveryLocation -State $State -RequireRunDirectory
        $bcdBackup = Export-GpuSystemBcdBackup -ActionDirectory $ActionDirectory -RecoveryRunDirectory $recoveryLocation.RunDirectory
        $externalTree = Join-Path $recoveryLocation.RunDirectory 'esp-efi-before'
        $treeCopy = Copy-GpuEspTreeWithManifest -SourceRoot 'S:\EFI' -DestinationRoot $externalTree -ActionDirectory $ActionDirectory -SystemBcdExport $bcdBackup.ExternalExport
        $firmware = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'bcdedit.exe') -Arguments @('/enum', 'firmware', '/v') -ActionDirectory $ActionDirectory -Label 'firmware-enumeration-backup'
        Assert-GpuNativeSuccess -Result $firmware -Description 'Firmware boot-entry baseline capture'
        $firmwarePin = Get-GpuFilePin -Path $firmware.StdoutPath
        $manifest = [pscustomobject]@{
            SchemaVersion = 2
            RunId = [string]$State.RunId
            CreatedUtc = [DateTime]::UtcNow.ToString('o')
            EspVolume = [string]$esp.Volume
            EspDiskNumber = [int]$esp.DiskNumber
            EspPartitionNumber = [int]$esp.PartitionNumber
            EspPartitionGuid = [string]$esp.PartitionGuid
            BootManagerSha256 = [string]$bootManagerPin.Sha256
            RecoveryDiskNumber = [int]$recoveryLocation.Disk.Number
            RecoveryDiskUniqueId = [string]$recoveryLocation.Disk.UniqueId
            RecoveryDiskSerialNumber = [string]$recoveryLocation.Disk.SerialNumber
            EfiTree = 'esp-efi-before'
            FileCount = [int]$treeCopy.FileCount
            DirectoryCount = [int]$treeCopy.DirectoryCount
            Files = @($treeCopy.Files)
            OmittedFiles = @($treeCopy.OmittedFiles)
            SystemBcdExport = [pscustomobject]@{
                Kind='BcdeditSystemStoreExport'; RelativePath='system-bcd-export.bcd'
                Sha256=$bcdBackup.ExternalExport.Sha256; Bytes=[long]$bcdBackup.ExternalExport.Bytes
            }
        }
        $localManifestPath = Join-Path $ActionDirectory 'esp-manifest.json'
        $externalManifestPath = Join-Path $recoveryLocation.RunDirectory 'esp-manifest.json'
        Write-GpuJson -Path $localManifestPath -Value $manifest -NoOverwrite
        Write-GpuJson -Path $externalManifestPath -Value $manifest -NoOverwrite
        $localManifestPin = Get-GpuFilePin -Path $localManifestPath
        $externalManifestPin = Get-GpuFilePin -Path $externalManifestPath
        if (-not [string]::Equals($localManifestPin.Sha256, $externalManifestPin.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Local and external recovery manifest readback hashes differ.'
        }
        $backup = [pscustomobject]@{
            EspVolume = [string]$esp.Volume
            EspDiskNumber = [int]$esp.DiskNumber
            EspPartitionNumber = [int]$esp.PartitionNumber
            EspPartitionGuid = [string]$esp.PartitionGuid
            BootManagerSha256 = [string]$bootManagerPin.Sha256
            ReferenceLog = $referenceLogPin
            FullReferenceLog = Get-GpuFilePin -Path $fullLogPath
            ExternalManifest = $externalManifestPin
            LocalManifest = $localManifestPin
            FirmwareEnumeration = $firmwarePin
            SystemBcdExport = $bcdBackup.ExternalExport
            LocalBcdExport = $bcdBackup.LocalExport
            BcdExportReceipt = Get-GpuFilePin -Path $bcdBackup.ReceiptPath
            RecoveryDiskNumber = [int]$recoveryLocation.Disk.Number
            RecoveryDiskUniqueId = [string]$recoveryLocation.Disk.UniqueId
            RecoveryDiskSerialNumber = [string]$recoveryLocation.Disk.SerialNumber
            BackupTree = $externalTree
            Files = @($treeCopy.Files)
        }
        Set-GpuStateProperty -Object $State -Name 'Backup' -Value $backup
        return New-GpuPlatformResult -Status 'ESP_BACKUP_VERIFIED' -Succeeded $true -Data ([ordered]@{
            Backup = $backup
            LocalManifestPath = $localManifestPath
            ExternalManifestPath = $externalManifestPath
            ExternalTree = $externalTree
        })
    }
    finally {
        if ($ownedMount) { Dismount-GpuEspMount -ActionDirectory $ActionDirectory }
    }
}

function Invoke-GpuStage {
    param([object]$State, [string]$ActionDirectory)

    Assert-GpuAdministrator
    $current = Get-GpuCurrentMachineSnapshot -IncludeSecureBoot
    Assert-GpuSameMachine -State $State -Current $current -RequireCpuCount
    Assert-GpuOriginalBoot -State $State -Current $current
    Assert-GpuSecureBootOn -Current $current
    Assert-GpuToolPins -State $State
    Assert-GpuBuildPins -State $State
    Assert-GpuSignedPins -State $State
    $backup = Assert-GpuBackupState -State $State
    $sourceRoot = [string](Get-GpuObjectProperty -Object $State -Name 'SourceRoot')
    $certificate = Get-GpuPinnedFilePath -PinSet $State.Tools -Name 'Certificate'
    $shim = Get-GpuPinnedFilePath -PinSet $State.Tools -Name 'Shim'
    $loader = Get-GpuPinnedFilePath -PinSet $State.SignedArtifacts -Name 'Loader'
    $payload = Get-GpuPinnedFilePath -PinSet $State.SignedArtifacts -Name 'Payload'
    $installer = Get-GpuPinnedFilePath -PinSet $State.Tools -Name 'Installer'
    $referenceLog = Get-GpuPinnedFilePath -PinSet $backup -Name 'ReferenceLog'

    $ownedMount = $false
    try {
        $esp = Get-GpuEspMount -ActionDirectory $ActionDirectory
        $ownedMount = $true
        Assert-GpuMountedEspMatchesBackup -MountedEsp $esp -Backup $backup -Description 'Before Stage' | Out-Null
        Assert-GpuNoPreviousStage -EspRoot 'S:' | Out-Null
        $stage = Invoke-GpuNativeCommand -Program $installer -Arguments @(
            'stage', '--signer-cert', $certificate, '--shim', $shim, '--loader', $loader,
            '--log', $referenceLog, '--payload', $payload, '--esp-volume', [string]$backup.EspVolume
        ) -ActionDirectory $ActionDirectory -Label 'installer-stage'
        Assert-GpuNativeSuccess -Result $stage -Description 'Installer ESP stage'
        $stageOutput = (Get-Content -LiteralPath $stage.StdoutPath -Raw) + (Get-Content -LiteralPath $stage.StderrPath -Raw)
        if ($stageOutput -notmatch 'Staged EFI\\insecure-boot with verified read-back manifest') {
            throw 'Installer did not report its read-back manifest verification.'
        }
        if ($stageOutput -notmatch 'No firmware variables or Microsoft boot-manager files were changed') {
            throw 'Installer did not confirm Stage left firmware variables and boot-manager files unchanged.'
        }
        $bootManager = Get-GpuMountedEspBootManagerHash -EspRoot 'S:'
        if (-not [string]::Equals($bootManager.Sha256, [string]$backup.BootManagerSha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Boot manager changed during staging.'
        }
        $stageFiles = Get-GpuStageReadback -EspRoot 'S:'
        Assert-GpuStageInputPins -State $State -StageFiles $stageFiles
        $record = [pscustomobject]@{
            EspVolume = [string]$esp.Volume
            EspDiskNumber = [int]$esp.DiskNumber
            EspPartitionGuid = [string]$esp.PartitionGuid
            BootManagerSha256 = [string]$bootManager.Sha256
            Files = @($stageFiles)
            InstallerReceipt = $stage.ReceiptPath
        }
        Set-GpuStateProperty -Object $State -Name 'Stage' -Value $record
        return New-GpuPlatformResult -Status 'STAGE_READBACK_PASS' -Succeeded $true -Data ([ordered]@{
            Stage = $record
            StageReceipt = $stage.ReceiptPath
        })
    }
    finally {
        if ($ownedMount) { Dismount-GpuEspMount -ActionDirectory $ActionDirectory }
    }
}

function Invoke-GpuArm {
    param([object]$State, [string]$ActionDirectory)

    Assert-GpuAdministrator
    $current = Get-GpuCurrentMachineSnapshot -IncludeSecureBoot
    Assert-GpuSameMachine -State $State -Current $current -RequireCpuCount
    Assert-GpuOriginalBoot -State $State -Current $current
    Assert-GpuSecureBootOn -Current $current
    Assert-GpuToolPins -State $State
    Assert-GpuBuildPins -State $State
    Assert-GpuSignedPins -State $State
    $backup = Assert-GpuBackupState -State $State
    $installer = Get-GpuPinnedFilePath -PinSet $State.Tools -Name 'Installer'

    $ownedMount = $false
    try {
        $esp = Get-GpuEspMount -ActionDirectory $ActionDirectory
        $ownedMount = $true
        Assert-GpuMountedEspMatchesBackup -MountedEsp $esp -Backup $backup -Description 'Before Arm' | Out-Null
        $liveStage = Assert-GpuStageState -State $State -EspRoot 'S:'
        if (-not [string]::Equals([string]$State.Stage.BootManagerSha256, [string]$backup.BootManagerSha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Staged boot-manager baseline differs from the ESP backup.'
        }
        Set-GpuStateProperty -Object $State -Name 'ArmBootId' -Value ([string]$current.BootId)
        $arm = Invoke-GpuNativeCommand -Program $installer -Arguments @(
            'oneshot', '--mode', 'bootnext', '--profile', 'legacy', '--esp-volume', [string]$backup.EspVolume
        ) -ActionDirectory $ActionDirectory -Label 'installer-arm-bootnext'
        Assert-GpuNativeSuccess -Result $arm -Description 'Installer BootNext arm'
        $armText = (Get-Content -LiteralPath $arm.StdoutPath -Raw) + (Get-Content -LiteralPath $arm.StderrPath -Raw)
        if ($armText -notmatch 'BootNext set to owned Boot[0-9A-Fa-f]{4}; BootOrder was not changed') {
            throw 'Installer did not confirm its owned one-shot BootNext entry.'
        }
    }
    finally {
        if ($ownedMount) { Dismount-GpuEspMount -ActionDirectory $ActionDirectory }
    }

    $firmware = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'bcdedit.exe') -Arguments @('/enum', 'firmware', '/v') -ActionDirectory $ActionDirectory -Label 'firmware-enumeration-armed'
    Assert-GpuNativeSuccess -Result $firmware -Description 'Post-arm firmware enumeration'
    return New-GpuPlatformResult -Status 'ARMED_MANUAL_REBOOT_REQUIRED' -Succeeded $true -Data ([ordered]@{
        ArmBootId = [string]$current.BootId
        InstallerReceipt = $arm.ReceiptPath
        FirmwareEnumerationReceipt = $firmware.ReceiptPath
        VerifiedStageFiles = @($liveStage)
        RebootPerformed = $false
    })
}

function Copy-GpuStageEvidence {
    param([string]$EspRoot, [string]$ActionDirectory)

    $stageRoot = Join-Path $EspRoot 'EFI\insecure-boot'
    $results = New-Object System.Collections.Generic.List[object]
    foreach ($name in @('shimx64.efi', 'grubx64.efi', 'tcglog.ib', 'payload.efi', 'manifest.sha256', 'handoff-status.txt', 'payload-diagnostic.txt')) {
        $source = Join-Path $stageRoot $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            $results.Add([pscustomobject]@{ Name = $name; Present = $false; Path = $null; Pin = $null })
            continue
        }
        $destination = Join-Path $ActionDirectory ('esp-' + $name)
        Assert-GpuFreshPath -Path $destination
        [System.IO.File]::Copy($source, $destination, $false)
        $pin = Get-GpuFilePin -Path $destination
        $sourcePin = Get-GpuFilePin -Path $source
        if ($pin.Bytes -ne $sourcePin.Bytes -or -not [string]::Equals($pin.Sha256, $sourcePin.Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            throw "ESP evidence copy did not read back identically: $name"
        }
        $results.Add([pscustomobject]@{ Name = $name; Present = $true; Path = $destination; Pin = $pin })
    }
    return @($results.ToArray())
}

function Invoke-GpuCollect {
    param([object]$State, [string]$ActionDirectory)

    Assert-GpuAdministrator
    $errors = New-Object System.Collections.Generic.List[string]
    $current = $null
    $hostIdentityValid = $false
    try {
        $current = Get-GpuCurrentMachineSnapshot
        Assert-GpuSameMachine -State $State -Current $current -RequireCpuCount
        $armBootId = [string](Get-GpuObjectProperty -Object $State -Name 'ArmBootId')
        if ([string]::IsNullOrWhiteSpace($armBootId)) { throw 'No Arm boot identity is recorded.' }
        if ([string]::Equals([string]$current.BootId, $armBootId, [StringComparison]::Ordinal)) {
            throw 'The host has not crossed the manual reboot boundary since Arm.'
        }
        $hostIdentityValid = $true
    }
    catch { $errors.Add("Host identity/boot check: $($_.Exception.Message)") }

    $health = $null
    $probeReceipt = $null
    try {
        Assert-GpuToolPins -State $State
        $probe = Get-GpuPinnedFilePath -PinSet $State.Tools -Name 'Probe'
        $probeReceipt = Invoke-GpuNativeCommand -Program $probe -Arguments @('health') -ActionDirectory $ActionDirectory -Label 'post-boot-health'
        if (-not $probeReceipt.Succeeded) { $errors.Add("Health command failed with exit $($probeReceipt.ExitCode).") }
        $healthLines = @((Get-Content -LiteralPath $probeReceipt.StdoutPath -ErrorAction Stop) + (Get-Content -LiteralPath $probeReceipt.StderrPath -ErrorAction Stop))
        $expectedCpus = [int](Get-GpuObjectProperty -Object $State.Host -Name 'LogicalCpus' -Default 0)
        $health = Get-GpuHealthVerdict -Lines $healthLines -ExpectedCpus $expectedCpus -ExpectedResult PASS
        if (-not [bool]$health.Passed) { $errors.Add("Windows health observation failed: $($health.Reason)") }
    }
    catch { $errors.Add("Windows health collection: $($_.Exception.Message)") }

    $preflight = $null
    try {
        $preflight = Get-GpuPreflight -State $State -ActionDirectory $ActionDirectory -Name 'gpu-preflight-after'
    }
    catch { $errors.Add("Post-boot GPU preflight: $($_.Exception.Message)") }

    $secureBoot = $null
    try { $secureBoot = Confirm-SecureBootUEFI -ErrorAction Stop }
    catch { $errors.Add("Secure Boot query: $($_.Exception.Message)") }
    if ($secureBoot -isnot [bool] -or -not [bool]$secureBoot) { $errors.Add('Secure Boot was not observed enabled after boot.') }

    $firmware = $null
    try {
        $firmware = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'bcdedit.exe') -Arguments @('/enum', 'firmware', '/v') -ActionDirectory $ActionDirectory -Label 'firmware-enumeration-after-boot'
        if (-not $firmware.Succeeded) { $errors.Add("Firmware enumeration failed with exit $($firmware.ExitCode).") }
    }
    catch { $errors.Add("Firmware enumeration: $($_.Exception.Message)") }

    $espObservation = $null
    if (-not $hostIdentityValid) {
        $errors.Add('Current ESP was not mounted because host identity or reboot-boundary validation failed.')
    }
    else {
        $ownedMount = $false
        try {
            $esp = Get-GpuEspMount -ActionDirectory $ActionDirectory
            $ownedMount = $true
            $backup = Get-GpuObjectProperty -Object $State -Name 'Backup'
            $matchesBackup = $false
            if ($null -ne $backup) {
                try {
                    Assert-GpuEspIdentity -Expected ([string]$backup.EspVolume) -Actual ([string]$esp.Volume)
                    if ([int]$backup.EspDiskNumber -ne [int]$esp.DiskNumber -or
                        -not [string]::Equals([string]$backup.EspPartitionGuid, [string]$esp.PartitionGuid, [StringComparison]::OrdinalIgnoreCase)) {
                        throw 'Current ESP disk/partition identity differs from the backup.'
                    }
                    $matchesBackup = $true
                }
                catch { $errors.Add("Current system ESP identity: $($_.Exception.Message)") }
            }
            else { $errors.Add('ESP backup identity is missing; current ESP target files were not read.') }

            $bootHash = $null
            $stageEvidence = $null
            if ($matchesBackup) {
                try {
                    $bootHash = Get-GpuMountedEspBootManagerHash -EspRoot 'S:'
                    if (-not [string]::Equals($bootHash.Sha256, [string]$backup.BootManagerSha256, [StringComparison]::OrdinalIgnoreCase)) {
                        $errors.Add('Current Microsoft boot-manager hash differs from the saved baseline.')
                    }
                }
                catch { $errors.Add("Current boot-manager hash: $($_.Exception.Message)") }

                try { $stageEvidence = Copy-GpuStageEvidence -EspRoot 'S:' -ActionDirectory $ActionDirectory }
                catch { $errors.Add("Staged ESP evidence: $($_.Exception.Message)") }
            }
            $espObservation = [pscustomobject]@{
                Volume = [string]$esp.Volume
                DiskNumber = [int]$esp.DiskNumber
                PartitionNumber = [int]$esp.PartitionNumber
                PartitionGuid = [string]$esp.PartitionGuid
                MatchesBackup = [bool]$matchesBackup
                BootManagerSha256 = if ($null -ne $bootHash) { [string]$bootHash.Sha256 } else { $null }
                StageFiles = $stageEvidence
            }
        }
        catch { $errors.Add("Current ESP collection: $($_.Exception.Message)") }
        finally {
            if ($ownedMount) {
                try { Dismount-GpuEspMount -ActionDirectory $ActionDirectory }
                catch { $errors.Add("Unmount owned ESP: $($_.Exception.Message)") }
            }
        }
    }

    $collection = [pscustomobject]@{
        BootId = if ($null -ne $current) { [string]$current.BootId } else { $null }
        LogicalCpus = if ($null -ne $current) { [int]$current.LogicalCpus } else { $null }
        Health = $health
        HealthStdout = if ($null -ne $probeReceipt) { [string]$probeReceipt.StdoutPath } else { $null }
        HealthStderr = if ($null -ne $probeReceipt) { [string]$probeReceipt.StderrPath } else { $null }
        Preflight = if ($null -ne $preflight) { $preflight.Pin } else { $null }
        SecureBoot = $secureBoot
        Esp = $espObservation
        FirmwareEnumeration = if ($null -ne $firmware) { Get-GpuFilePin -Path $firmware.StdoutPath } else { $null }
        GpuAttestation = 'UNATTESTED'
        Errors = @($errors.ToArray())
    }
    Set-GpuStateProperty -Object $State -Name 'Collection' -Value $collection
    $passed = ($errors.Count -eq 0 -and $null -ne $health -and [bool]$health.Passed)
    $status = 'COLLECTED_HEALTH_FAIL_GPU_UNATTESTED'
    if ($passed) { $status = 'COLLECTED_HEALTH_ONLY_GPU_UNATTESTED' }
    return New-GpuPlatformResult -Status $status -Succeeded ([bool]$passed) -Data ([ordered]@{
        Collection = $collection
        HealthPassed = if ($null -ne $health) { [bool]$health.Passed } else { $false }
        GpuAttestation = 'UNATTESTED'
    })
}

function Get-GpuDisarmBoundaryBootId {
    param([object]$State, [string]$CurrentBootId)

    $previous = [string](Get-GpuObjectProperty -Object $State -Name 'DisarmedBootId')
    $phase = [string](Get-GpuObjectProperty -Object $State -Name 'Phase')
    # Recovery retries cannot re-arm a Disarmed run. Keep its first successful
    # cleanup boundary so a failed verification does not require another reset.
    if ($phase -ceq 'Disarmed' -and -not [string]::IsNullOrWhiteSpace($previous)) { return $previous }
    return $CurrentBootId
}

function Invoke-GpuDisarm {
    param([object]$State, [string]$ActionDirectory)

    Assert-GpuAdministrator
    $current = Get-GpuCurrentMachineSnapshot
    $preparedHost = Get-GpuObjectProperty -Object $State -Name 'Host'
    if ($null -ne $preparedHost) { Assert-GpuSameMachine -State $State -Current $current }
    $backup = Get-GpuObjectProperty -Object $State -Name 'Backup'
    if ($null -eq $backup) {
        $status = 'DISARMED_NO_ACTIVATION'
        if ([bool](Get-GpuObjectProperty -Object $State -Name 'EverArmIntent' -Default $false)) { $status = 'DISARMED_NO_RECORDED_FIRMWARE_TARGET' }
        $disarmedBootId = Get-GpuDisarmBoundaryBootId -State $State -CurrentBootId ([string]$current.BootId)
        Set-GpuStateProperty -Object $State -Name 'DisarmedBootId' -Value $disarmedBootId
        return New-GpuPlatformResult -Status $status -Succeeded ([bool]($status -eq 'DISARMED_NO_ACTIVATION')) -Data ([ordered]@{
            DisarmedBootId = $disarmedBootId
            InstallerInvoked = $false
            Detail = 'No recorded ESP target exists; no firmware entry was guessed or changed.'
        })
    }

    Assert-GpuRecordedBackupIdentity -State $State | Out-Null
    $expectedInstaller = (Get-GpuExpectedToolPaths -State $State).Installer
    $installerPin = Assert-GpuNamedPin -PinSet $State.Tools -Name 'Installer' -ExpectedPath $expectedInstaller -Description 'Installer'
    $installer = [string]$installerPin.Path
    $ownedMount = $false
    try {
        $esp = Get-GpuEspMount -ActionDirectory $ActionDirectory
        $ownedMount = $true
        Assert-GpuEspIdentity -Expected ([string]$backup.EspVolume) -Actual ([string]$esp.Volume)
        if ([int]$esp.DiskNumber -ne [int]$backup.EspDiskNumber -or
            -not [string]::Equals([string]$esp.PartitionGuid, [string]$backup.EspPartitionGuid, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Current system ESP does not match the recorded firmware target; refusing to invoke installer cleanup.'
        }
        $off = Invoke-GpuNativeCommand -Program $installer -Arguments @(
            'oneshot', '--mode', 'off', '--profile', 'legacy', '--esp-volume', [string]$backup.EspVolume
        ) -ActionDirectory $ActionDirectory -Label 'installer-disarm-bootnext'
        Assert-GpuNativeSuccess -Result $off -Description 'Installer owned BootNext cleanup'
        $offText = (Get-Content -LiteralPath $off.StdoutPath -Raw) + (Get-Content -LiteralPath $off.StderrPath -Raw)
        if ($offText -notmatch 'Owned BootNext/Boot#### state cleared; BootOrder was not changed') {
            throw 'Installer did not confirm owned BootNext cleanup.'
        }
    }
    finally {
        if ($ownedMount) { Dismount-GpuEspMount -ActionDirectory $ActionDirectory }
    }
    $disarmedBootId = Get-GpuDisarmBoundaryBootId -State $State -CurrentBootId ([string]$current.BootId)
    Set-GpuStateProperty -Object $State -Name 'DisarmedBootId' -Value $disarmedBootId
    $firmware = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'bcdedit.exe') -Arguments @('/enum', 'firmware', '/v') -ActionDirectory $ActionDirectory -Label 'firmware-enumeration-disarmed'
    Assert-GpuNativeSuccess -Result $firmware -Description 'Post-disarm firmware enumeration'
    $everArm = [bool](Get-GpuObjectProperty -Object $State -Name 'EverArmIntent' -Default $false)
    $resetBoundaryPassed = ($everArm -and -not [string]::Equals($disarmedBootId, [string]$current.BootId, [StringComparison]::Ordinal))
    $status = 'DISARMED_NO_ACTIVATION'
    if ($resetBoundaryPassed) { $status = 'DISARMED_RESET_OBSERVED' }
    elseif ($everArm) { $status = 'DISARMED_RESET_REQUIRED' }
    return New-GpuPlatformResult -Status $status -Succeeded $true -Data ([ordered]@{
        DisarmedBootId = $disarmedBootId
        CurrentBootId = [string]$current.BootId
        ResetBoundaryPassed = [bool]$resetBoundaryPassed
        InstallerReceipt = $off.ReceiptPath
        FirmwareEnumerationReceipt = $firmware.ReceiptPath
        RuntimeHandback = $false
    })
}

function Invoke-GpuFinalFirmwareReview {
    param([object]$State, [object]$CurrentHost, [object]$CurrentFirmwarePin,
          [string]$ActionDirectory)

    $modulePath = Join-Path $PSScriptRoot 'GpuFinalFirmwareReview.psm1'
    $null = Assert-GpuPathWithin -Path $modulePath -Root ([string]$State.SourceRoot) -MustExist
    $reviewModule = Import-Module $modulePath -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
    return & $reviewModule {
        param($s,$hostSnapshot,$firmwarePin,$directory)
        Invoke-GpuReviewedFinalFirmware -State $s -CurrentHost $hostSnapshot -CurrentFirmwarePin $firmwarePin -ActionDirectory $directory
    } $State $CurrentHost $CurrentFirmwarePin $ActionDirectory
}

function Invoke-GpuFinalVerify {
    param([object]$State, [string]$ActionDirectory, [switch]$ReviewedFirmwareDifference)

    Assert-GpuAdministrator
    $errors = New-Object System.Collections.Generic.List[string]
    $firmwareReviewAccepted = $false
    if ($ReviewedFirmwareDifference -and
        [string](Get-GpuObjectProperty -Object $State -Name 'RunId') -cne 'gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7') {
        $errors.Add('The reviewed firmware difference applies only to the fixed 201140 run.')
    }
    $current = $null
    $preparedHost = Get-GpuObjectProperty -Object $State -Name 'Host'
    $hostIdentityValid = $false
    try {
        $current = Get-GpuCurrentMachineSnapshot -IncludeSecureBoot
        if ($null -ne $preparedHost) { Assert-GpuSameMachine -State $State -Current $current -RequireCpuCount }
        Assert-GpuSecureBootOn -Current $current
        $hostIdentityValid = $true
    }
    catch { $errors.Add("Host/Secure Boot identity: $($_.Exception.Message)") }

    $everArm = [bool](Get-GpuObjectProperty -Object $State -Name 'EverArmIntent' -Default $false)
    $resetBoundaryPassed = $false
    if ($everArm) {
        $disarmedBootId = [string](Get-GpuObjectProperty -Object $State -Name 'DisarmedBootId')
        if ([string]::IsNullOrWhiteSpace($disarmedBootId) -or $null -eq $current -or
            [string]::Equals($disarmedBootId, [string]$current.BootId, [StringComparison]::Ordinal)) {
            $errors.Add('A full platform reset after disarm has not been observed.')
        }
        else { $resetBoundaryPassed = $true }
    }

    $probePin = Get-GpuObjectProperty -Object (Get-GpuObjectProperty -Object $State -Name 'Tools') -Name 'Probe'
    $probeVerdict = $null
    $probeAvailable = $false
    $expectedProbePath = $null
    try { $expectedProbePath = (Get-GpuExpectedToolPaths -State $State).Probe }
    catch { if ($everArm -or $null -ne $probePin) { $errors.Add("Expected probe path: $($_.Exception.Message)") } }
    if ($null -ne $probePin -and -not [string]::IsNullOrWhiteSpace([string]$expectedProbePath)) {
        try {
            $probePin = Assert-GpuNamedPin -PinSet $State.Tools -Name 'Probe' -ExpectedPath $expectedProbePath -Description 'Probe'
            $probeAvailable = $true
            $probePath = [string]$probePin.Path
            $probe = Invoke-GpuNativeCommand -Program $probePath -Arguments @('health') -ActionDirectory $ActionDirectory -Label 'post-disarm-absent-health' -AllowedExitCodes @(0)
            $lines = @((Get-Content -LiteralPath $probe.StdoutPath -ErrorAction Stop) + (Get-Content -LiteralPath $probe.StderrPath -ErrorAction Stop))
            $currentCpuCount = [int](Get-GpuObjectProperty -Object $current -Name 'LogicalCpus' -Default 0)
            $expectedCpus = [int](Get-GpuObjectProperty -Object $preparedHost -Name 'LogicalCpus' -Default $currentCpuCount)
            $probeVerdict = Get-GpuHealthVerdict -Lines $lines -ExpectedCpus $expectedCpus -ExpectedResult ABSENT
            if (-not $probe.Succeeded -or -not [bool]$probeVerdict.Passed) { $errors.Add("ABSENT health check failed: $($probeVerdict.Reason)") }
        }
        catch { $errors.Add("Post-reset health query: $($_.Exception.Message)") }
    }
    elseif ($everArm) { $errors.Add('Post-reset probe is unavailable after Arm.') }

    $preflight = $null
    try {
        $preflight = Get-GpuPreflight -State $State -ActionDirectory $ActionDirectory -Name 'gpu-preflight-final'
        Assert-GpuPreflightAdapters -Report $preflight.Report
    }
    catch { $errors.Add("Final adapter enumeration: $($_.Exception.Message)") }

    $espCheck = $null
    $firmwareComparison = $null
    $backup = Get-GpuObjectProperty -Object $State -Name 'Backup'
    if ($null -ne $backup) {
        $ownedMount = $false
        try {
            if (-not $hostIdentityValid) { throw 'ESP was not mounted because host identity or Secure Boot validation failed.' }
            Assert-GpuBackupState -State $State | Out-Null
            $esp = Get-GpuEspMount -ActionDirectory $ActionDirectory
            $ownedMount = $true
            $bootManager = Assert-GpuMountedEspMatchesBackup -MountedEsp $esp -Backup $backup -Description 'Final verification'
            $espCheck = [pscustomobject]@{
                Volume = [string]$esp.Volume
                DiskNumber = [int]$esp.DiskNumber
                PartitionGuid = [string]$esp.PartitionGuid
                BootManagerSha256 = [string]$bootManager.Sha256
                MatchesBaseline = $true
            }
            # Backup records bcdedit output with the system ESP mounted at S:.
            # Capture in the same state: an unmounted ESP is instead printed as
            # \Device\HarddiskVolumeN, making a byte comparison fail spuriously.
            # Keep the complete text comparison, including every boot entry.
            try {
                $firmwareNow = Invoke-GpuNativeCommand -Program (Get-GpuApplicationPath 'bcdedit.exe') -Arguments @('/enum', 'firmware', '/v') -ActionDirectory $ActionDirectory -Label 'firmware-enumeration-final'
                if (-not $firmwareNow.Succeeded) { throw "bcdedit failed with exit $($firmwareNow.ExitCode)." }
                $currentPin = Get-GpuFilePin -Path $firmwareNow.StdoutPath
                $baselinePin = Get-GpuObjectProperty -Object $backup -Name 'FirmwareEnumeration'
                $baselineText = $null
                if ($null -ne $baselinePin) {
                    Assert-GpuFilePin -Pin $baselinePin
                    $baselineText = [System.IO.File]::ReadAllText([string]$baselinePin.Path)
                }
                $currentText = [System.IO.File]::ReadAllText([string]$firmwareNow.StdoutPath)
                $firmwareComparison = [pscustomobject]@{
                    BaselineSha256 = if ($null -ne $baselinePin) { [string]$baselinePin.Sha256 } else { $null }
                    CurrentSha256 = [string]$currentPin.Sha256
                    TextEqual = if ($null -ne $baselineText) { [string]::Equals($baselineText, $currentText, [StringComparison]::Ordinal) } else { $null }
                    CaptureContext = 'SystemEspMountedAtS'
                    Meaning = 'Textual bcdedit enumeration comparison with the system ESP mounted at S: for both captures; not a raw firmware-variable attestation.'
                }
                if ($null -ne $baselineText -and -not [bool]$firmwareComparison.TextEqual) {
                    if ($ReviewedFirmwareDifference) {
                        $firmwareComparison | Add-Member -NotePropertyName ReviewedDifferenceAccepted -NotePropertyValue $false
                        if ($errors.Count -eq 0 -and $everArm -and $resetBoundaryPassed -and
                            [string](Get-GpuObjectProperty -Object $State -Name 'Phase') -ceq 'Disarmed') {
                            $review = Invoke-GpuFinalFirmwareReview -State $State -CurrentHost $current -CurrentFirmwarePin $currentPin -ActionDirectory $ActionDirectory
                            if ($null -eq $review -or $review.Accepted -isnot [bool] -or $review.Accepted -ne $true) {
                                throw 'The fixed firmware-difference review did not accept the fresh observations.'
                            }
                            $firmwareReviewAccepted = $true
                            $firmwareComparison.ReviewedDifferenceAccepted = $true
                            $firmwareComparison | Add-Member -NotePropertyName ReviewedDifference -NotePropertyValue $review
                        }
                    }
                    if (-not $firmwareReviewAccepted) {
                        $errors.Add('Firmware enumeration differs from the saved textual baseline.')
                    }
                }
            }
            catch { $errors.Add("Firmware enumeration comparison: $($_.Exception.Message)") }
        }
        catch { $errors.Add("Final ESP/boot-manager verification: $($_.Exception.Message)") }
        finally {
            if ($ownedMount) {
                try { Dismount-GpuEspMount -ActionDirectory $ActionDirectory }
                catch { $errors.Add("Final ESP unmount: $($_.Exception.Message)") }
            }
        }

    }
    elseif ($everArm) { $errors.Add('Recorded ESP backup identity is unavailable after Arm.') }

    $aborted = ($everArm -and (-not $probeAvailable -or $null -eq $backup))
    $succeeded = (-not $aborted -and $errors.Count -eq 0 -and $null -ne $preflight -and
        (-not $everArm -or ($null -ne $probeVerdict -and [bool]$probeVerdict.Passed -and $null -ne $espCheck)))
    $status = 'FINAL_VERIFY_FAILED_GPU_UNATTESTED'
    if ($aborted) { $status = 'ABORTED_RECOVERED_NO_GPU_PROOF'; $succeeded = $false }
    elseif ($succeeded -and -not $everArm -and $null -eq $backup) { $status = 'VERIFIED_ABORTED_NO_ACTIVATION' }
    elseif ($succeeded -and $everArm -and $firmwareReviewAccepted) {
        $status = 'FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED'
    }
    elseif ($succeeded -and $everArm) { $status = 'FINAL_RESET_VERIFIED_GPU_UNATTESTED' }
    elseif ($succeeded) { $status = 'FINAL_STATE_VERIFIED_GPU_UNATTESTED' }
    $verification = [pscustomobject]@{
        BootId = if ($null -ne $current) { [string]$current.BootId } else { $null }
        SecureBoot = if ($null -ne $current) { $current.SecureBoot } else { $null }
        ResetBoundaryPassed = [bool]$resetBoundaryPassed
        ActivationAttempted = [bool]$everArm
        Health = $probeVerdict
        ProbeAvailable = [bool]$probeAvailable
        Preflight = if ($null -ne $preflight) { $preflight.Pin } else { $null }
        Esp = $espCheck
        FirmwareEnumeration = $firmwareComparison
        ActiveGpuProof = $false
        GpuAttestation = 'UNATTESTED'
        Errors = @($errors.ToArray())
    }
    Set-GpuStateProperty -Object $State -Name 'FinalVerification' -Value $verification
    return New-GpuPlatformResult -Status $status -Succeeded ([bool]$succeeded) -Data ([ordered]@{
        FinalVerification = $verification
        GpuAttestation = 'UNATTESTED'
        ActiveGpuProof = $false
    })
}

function Invoke-GpuHostPlatformAction {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][ValidateSet('Prepare', 'Build', 'Sign', 'Backup', 'Stage', 'Arm', 'Collect', 'Disarm', 'FinalVerify')][string]$Action,
        [Parameter(Mandatory = $true)][psobject]$State,
        [Parameter(Mandatory = $true)][string]$ActionDirectory,
        [Parameter(Mandatory = $false)][switch]$PhysicalOutputConfirmed,
        [Parameter(Mandatory = $false)][switch]$RecoveryReady,
        [Parameter(Mandatory = $false)][switch]$ReviewedFirmwareDifference
    )

    if ($ReviewedFirmwareDifference -and $Action -ne 'FinalVerify') {
        throw 'ReviewedFirmwareDifference is supported only with FinalVerify.'
    }
    $validatedActionDirectory = Assert-GpuActionDirectory -ActionDirectory $ActionDirectory -State $State
    $script:CurrentSourceRoot = [string](Get-GpuObjectProperty -Object $State -Name 'SourceRoot')
    if ([string]::IsNullOrWhiteSpace($script:CurrentSourceRoot)) { throw 'State.SourceRoot is empty.' }
    $script:CurrentSourceRoot = [System.IO.Path]::GetFullPath($script:CurrentSourceRoot)

    switch ($Action) {
        'Prepare' { return Invoke-GpuPrepare -State $State -ActionDirectory $validatedActionDirectory -PhysicalOutputConfirmed:$PhysicalOutputConfirmed -RecoveryReady:$RecoveryReady }
        'Build' { return Invoke-GpuBuild -State $State -ActionDirectory $validatedActionDirectory }
        'Sign' { return Invoke-GpuSign -State $State -ActionDirectory $validatedActionDirectory }
        'Backup' { return Invoke-GpuBackup -State $State -ActionDirectory $validatedActionDirectory }
        'Stage' { return Invoke-GpuStage -State $State -ActionDirectory $validatedActionDirectory }
        'Arm' { return Invoke-GpuArm -State $State -ActionDirectory $validatedActionDirectory }
        'Collect' { return Invoke-GpuCollect -State $State -ActionDirectory $validatedActionDirectory }
        'Disarm' { return Invoke-GpuDisarm -State $State -ActionDirectory $validatedActionDirectory }
        'FinalVerify' { return Invoke-GpuFinalVerify -State $State -ActionDirectory $validatedActionDirectory -ReviewedFirmwareDifference:$ReviewedFirmwareDifference }
    }
}

Export-ModuleMember -Function 'Invoke-GpuHostPlatformAction'
