[CmdletBinding()]
param(
    [string]$InventoryPath,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

function New-QueryResult {
    param(
        [bool]$Succeeded,
        [int]$ExitCode,
        [string]$Command,
        [string]$ErrorMessage
    )

    [pscustomobject]@{
        Succeeded = $Succeeded
        ExitCode = $ExitCode
        Command = $Command
        Error = $ErrorMessage
    }
}

function Get-LiveDeviceSet {
    param([ValidateSet('Display', 'Monitor')][string]$Class)

    $commandText = "pnputil.exe /enum-devices /class $Class /connected /relations"
    try {
        $pnputil = Get-Command pnputil.exe -ErrorAction Stop
        # PnPUtil output is localized; use stable PnP property keys for structured fields.
        $null = & $pnputil.Source /enum-devices /class $Class /connected /relations 2>&1
        $nativeExitCode = $LASTEXITCODE
        if ($nativeExitCode -ne 0) {
            return [pscustomobject]@{
                Query = New-QueryResult $false $nativeExitCode $commandText "pnputil.exe returned exit code $nativeExitCode."
                Devices = @()
            }
        }

        $devices = @(Get-PnpDevice -Class $Class -PresentOnly -ErrorAction Stop)
        $normalized = @()
        foreach ($device in $devices) {
            $instanceId = [string]$device.InstanceId
            if ([string]::IsNullOrWhiteSpace($instanceId)) {
                throw "Get-PnpDevice returned a $Class device without an instance ID."
            }

            $parent = $null
            $parentProperty = @(Get-PnpDeviceProperty -InstanceId $instanceId -KeyName 'DEVPKEY_Device_Parent' -ErrorAction Stop)
            if ($parentProperty.Count -gt 0 -and $null -ne $parentProperty[0].Data) {
                $parent = [string]$parentProperty[0].Data
            }

            $description = [string]$device.FriendlyName
            $status = [string]$device.Status
            $normalized += [pscustomobject]@{
                InstanceId = $instanceId
                Description = $description
                Status = $status
                Parent = $parent
            }
        }

        return [pscustomobject]@{
            Query = New-QueryResult $true $nativeExitCode $commandText $null
            Devices = @($normalized)
        }
    }
    catch {
        return [pscustomobject]@{
            Query = New-QueryResult $false -1 $commandText $_.Exception.Message
            Devices = @()
        }
    }
}

function Convert-FixtureDeviceSet {
    param(
        [object]$Fixture,
        [string]$Class,
        [string]$PropertyName
    )

    $query = $Fixture.Queries.$Class
    if ($null -eq $query -or $query.Succeeded -isnot [bool]) {
        return [pscustomobject]@{
            Query = New-QueryResult $false -1 "fixture.$Class" 'Fixture is missing a boolean query result.'
            Devices = @()
        }
    }

    $exitCode = -1
    if ($query.ExitCode -is [int]) { $exitCode = $query.ExitCode }
    if (-not $query.Succeeded -or $exitCode -ne 0) {
        $message = [string]$query.Error
        if ([string]::IsNullOrWhiteSpace($message)) { $message = 'Fixture query failed.' }
        return [pscustomobject]@{
            Query = New-QueryResult $false $exitCode "fixture.$Class" $message
            Devices = @()
        }
    }

    $devices = @()
    foreach ($device in @($Fixture.$PropertyName)) {
        $instanceId = [string]$device.InstanceId
        if ([string]::IsNullOrWhiteSpace($instanceId)) {
            return [pscustomobject]@{
                Query = New-QueryResult $false -1 "fixture.$Class" 'Fixture device is missing InstanceId.'
                Devices = @()
            }
        }

        $devices += [pscustomobject]@{
            InstanceId = $instanceId
            Description = [string]$device.Description
            Status = [string]$device.Status
            Parent = [string]$device.Parent
        }
    }

    [pscustomobject]@{
        Query = New-QueryResult $true $exitCode "fixture.$Class" $null
        Devices = @($devices)
    }
}

function Get-AdapterSummary {
    param(
        [string]$Name,
        [string]$PciId,
        [string]$Pattern,
        [bool]$InventoryComplete,
        [object[]]$Adapters,
        [object[]]$Monitors
    )

    $matches = @($Adapters | Where-Object { $_.InstanceId -match $Pattern })
    if (-not $InventoryComplete) {
        $present = $null
        $status = $null
        $associated = @()
    }
    else {
        $present = $matches.Count -gt 0
        if ($matches.Count -eq 0) { $status = 'NotPresent' }
        else { $status = (@($matches | ForEach-Object { $_.Status }) -join '; ') }
        $ids = @($matches | ForEach-Object { $_.InstanceId })
        $associated = @($Monitors | Where-Object {
            $null -ne $_.Parent -and $ids -contains $_.Parent
        } | ForEach-Object {
            [pscustomobject]@{ InstanceId = $_.InstanceId; Description = $_.Description; Status = $_.Status }
        })
    }

    [pscustomobject]@{
        Name = $Name
        PciId = $PciId
        Present = $present
        Status = $status
        AssociatedMonitors = @($associated)
    }
}

function New-PreflightReport {
    param(
        [string]$Source,
        [object]$DisplaySet,
        [object]$MonitorSet,
        [string[]]$SourceErrors
    )

    $complete = $DisplaySet.Query.Succeeded -and $MonitorSet.Query.Succeeded
    $adapters = @($DisplaySet.Devices)
    $monitors = @($MonitorSet.Devices)
    $monitorReports = @()
    foreach ($monitor in $monitors) {
        $parents = @($adapters | Where-Object {
            $null -ne $monitor.Parent -and $_.InstanceId -eq $monitor.Parent
        } | ForEach-Object { $_.InstanceId })
        $monitorReports += [pscustomobject]@{
            InstanceId = $monitor.InstanceId
            Description = $monitor.Description
            Status = $monitor.Status
            Parent = $monitor.Parent
            AssociatedAdapterInstanceIds = @($parents)
        }
    }

    $intel = Get-AdapterSummary 'Intel UHD Graphics 770' 'PCI\VEN_8086&DEV_4680' '^PCI\\VEN_8086&DEV_4680(?:&|\\|$)' $complete $adapters $monitors
    $nvidia = Get-AdapterSummary 'NVIDIA display adapter' 'PCI\VEN_10DE' '^PCI\\VEN_10DE&DEV_[0-9A-F]{4}(?:&|\\|$)' $complete $adapters $monitors
    $errors = @($SourceErrors)
    foreach ($query in @($DisplaySet.Query, $MonitorSet.Query)) {
        if (-not $query.Succeeded -and -not [string]::IsNullOrWhiteSpace($query.Error)) {
            $errors += "$($query.Command): $($query.Error)"
        }
    }

    [pscustomobject]@{
        SchemaVersion = 2
        InventorySource = $Source
        InventoryComplete = $complete
        HostInventory = [pscustomobject]@{
            DisplayQuery = $DisplaySet.Query
            MonitorQuery = $MonitorSet.Query
            Adapters = @($adapters)
            Monitors = @($monitorReports)
        }
        DiagnosticCandidate = $intel
        Nvidia = $nvidia
        PhysicalOutputEvidence = [pscustomobject]@{
            ConnectedMonitorCount = if ($MonitorSet.Query.Succeeded) { $monitorReports.Count } else { $null }
            Monitors = @($monitorReports)
            ManualRoutingConfirmed = $false
            Limitation = 'A connected monitor entry does not prove which physical adapter connector carries the signal.'
        }
        Exclusion = [pscustomobject]@{
            Mechanism = 'boot-vpci-ept-vtd'
            Implemented = $null
            NativeBackendAvailableInSource = $true
            RequiredBuildOption = 'HYPERSINV_GPU_DIAGNOSTIC_NATIVE=ON'
            RequiredPhysicalOutputDeclaration = 'HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED=ON after physical verification'
            BootEnforcementStatus = 'NotAttested'
            CanAuthorizeActivation = $false
            Reason = 'PnP inventory cannot attest the running EFI build, installed PCI/EPT filters, GPU VT-d domain, or WC mapping. Native activation requires a dedicated diagnostic boot; a disabled or absent PnP device is insufficient.'
            FirmwareSmmTrust = 'Required platform assumption; firmware writes can violate ownership without detection.'
        }
        HostChangesPerformed = $false
        Errors = @($errors)
    }
}

$sourceErrors = @()
if (-not [string]::IsNullOrWhiteSpace($InventoryPath)) {
    $inventorySource = 'Fixture'
    try {
        $fixtureText = [System.IO.File]::ReadAllText([System.IO.Path]::GetFullPath($InventoryPath))
        $fixture = ConvertFrom-Json -InputObject $fixtureText -ErrorAction Stop
        $displaySet = Convert-FixtureDeviceSet $fixture 'Display' 'DisplayDevices'
        $monitorSet = Convert-FixtureDeviceSet $fixture 'Monitor' 'MonitorDevices'
    }
    catch {
        $message = "Fixture could not be read: $($_.Exception.Message)"
        $sourceErrors += $message
        $displaySet = [pscustomobject]@{ Query = (New-QueryResult $false -1 'fixture.Display' $message); Devices = @() }
        $monitorSet = [pscustomobject]@{ Query = (New-QueryResult $false -1 'fixture.Monitor' $message); Devices = @() }
    }
}
else {
    $inventorySource = 'Live'
    $displaySet = Get-LiveDeviceSet 'Display'
    $monitorSet = Get-LiveDeviceSet 'Monitor'
}

$report = New-PreflightReport $inventorySource $displaySet $monitorSet $sourceErrors
$json = ConvertTo-Json -InputObject $report -Depth 12
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    try {
        $outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
        $scriptFullPath = [System.IO.Path]::GetFullPath($MyInvocation.MyCommand.Path)
        if ([string]::Equals($outputFullPath, $scriptFullPath, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'OutputPath cannot replace the preflight script.'
        }
        if (-not [string]::IsNullOrWhiteSpace($InventoryPath)) {
            $inputFullPath = [System.IO.Path]::GetFullPath($InventoryPath)
            if ([string]::Equals($outputFullPath, $inputFullPath, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'OutputPath cannot replace the inventory input.'
            }
        }
        $parentPath = [System.IO.Path]::GetDirectoryName($outputFullPath)
        if (-not [System.IO.Directory]::Exists($parentPath)) {
            $null = [System.IO.Directory]::CreateDirectory($parentPath)
        }
        [System.IO.File]::WriteAllText($outputFullPath, $json + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))
    }
    catch {
        $sourceErrors += "OutputPath could not be written: $($_.Exception.Message)"
        $report.Errors = @($report.Errors) + @($sourceErrors[-1])
        $json = ConvertTo-Json -InputObject $report -Depth 12
    }
}

Write-Output $json
