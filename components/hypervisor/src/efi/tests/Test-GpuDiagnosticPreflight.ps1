[CmdletBinding()]
param(
    [string]$ToolPath
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolPath)) {
    $ToolPath = Join-Path $PSScriptRoot '..\..\..\setup\Get-GpuDiagnosticPreflight.ps1'
}
$resolvedTool = (Resolve-Path -LiteralPath $ToolPath).Path
$temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$testDirectory = Join-Path $temporaryRoot ("gpu-diagnostic-preflight-" + [Guid]::NewGuid().ToString('N'))
$null = [System.IO.Directory]::CreateDirectory($testDirectory)
$fixturePaths = New-Object 'System.Collections.Generic.List[string]'
$caseCount = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Invoke-Fixture {
    param([string]$Name, [object]$Fixture)
    $path = Join-Path $testDirectory ($Name + '.json')
    $fixturePaths.Add([System.IO.Path]::GetFullPath($path))
    $json = ConvertTo-Json -InputObject $Fixture -Depth 10
    [System.IO.File]::WriteAllText($path, $json, (New-Object System.Text.UTF8Encoding($false)))

    $output = & powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $resolvedTool -InventoryPath $path 2>&1
    $exitCode = $LASTEXITCODE
    Assert-True ($exitCode -eq 0) "$Name returned exit code $exitCode. Output: $($output -join [Environment]::NewLine)"
    $text = ($output | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
    try { return (ConvertFrom-Json -InputObject $text -ErrorAction Stop) }
    catch { throw "$Name did not return valid JSON: $text" }
}

try {
    $intelId = 'PCI\VEN_8086&DEV_4680&SUBSYS_00008086\3&11583659&0&10'
    $nvidiaId = 'PCI\VEN_10DE&DEV_2206&SUBSYS_00000000\4&2A3A9F15&0&0008'
    $intelMonitor = 'DISPLAY\DEL40A9\5&15E4C87&0&UID4357'
    $nvidiaMonitor = 'DISPLAY\VSC2428\5&15E4C87&0&UID4352'
    $base = @{
        SchemaVersion = 1
        DisplayDevices = @(
            @{ InstanceId = $intelId; Description = 'Intel(R) UHD Graphics 770'; Status = 'OK'; Parent = 'PCI\ROOT' },
            @{ InstanceId = $nvidiaId; Description = 'NVIDIA GeForce RTX 3080'; Status = 'OK'; Parent = 'PCI\ROOT' }
        )
        MonitorDevices = @(
            @{ InstanceId = $nvidiaMonitor; Description = 'VIZIO D24h-G9'; Status = 'OK'; Parent = $nvidiaId }
        )
        Queries = @{
            Display = @{ Succeeded = $true; ExitCode = 0 }
            Monitor = @{ Succeeded = $true; ExitCode = 0 }
        }
        Exclusion = @{ Implemented = $true; CanAuthorizeActivation = $true }
        AuthoritativeReadExclusionProof = $true
        Enable = $true
        HostChangesPerformed = $true
    }

    $report = Invoke-Fixture 'dual-gpu-nvidia-monitor' $base
    Assert-True ($report.InventoryComplete -eq $true) 'Dual-GPU inventory should be complete.'
    Assert-True ($report.DiagnosticCandidate.Present -eq $true) 'Intel UHD 770 should be detected.'
    Assert-True ($report.DiagnosticCandidate.Status -eq 'OK') 'Intel adapter status should be preserved.'
    Assert-True ($report.DiagnosticCandidate.AssociatedMonitors.Count -eq 0) 'NVIDIA monitor must not be attributed to Intel.'
    Assert-True ($report.Nvidia.Present -eq $true) 'NVIDIA adapter should be detected.'
    Assert-True ($report.Nvidia.AssociatedMonitors.Count -eq 1) 'NVIDIA monitor should be associated by its parent instance ID.'
    Assert-True ($report.Nvidia.AssociatedMonitors[0].InstanceId -eq $nvidiaMonitor) 'Associated NVIDIA monitor ID should match.'
    Assert-True ($report.PhysicalOutputEvidence.ManualRoutingConfirmed -eq $false) 'PnP inventory must not claim physical connector routing proof.'
    Assert-True ($report.Exclusion.Implemented -eq $false -and $report.Exclusion.CanAuthorizeActivation -eq $false) 'Fixture input must never authorize exclusion.'
    Assert-True ($report.HostChangesPerformed -eq $false) 'The report must always say no host changes were performed.'
    Assert-True ($report.PSObject.Properties.Name -notcontains 'AuthoritativeReadExclusionProof') 'Untrusted authorization fields must not be copied into the report.'
    $caseCount++

    $reportPath = Join-Path $testDirectory 'preflight-report.json'
    $fixturePaths.Add([System.IO.Path]::GetFullPath($reportPath))
    $output = & powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $resolvedTool -InventoryPath $fixturePaths[0] -OutputPath $reportPath 2>&1
    $exitCode = $LASTEXITCODE
    Assert-True ($exitCode -eq 0) "OutputPath case returned exit code $exitCode."
    Assert-True ([System.IO.File]::Exists($reportPath)) 'OutputPath should create the requested report file.'
    $writtenReport = ConvertFrom-Json -InputObject ([System.IO.File]::ReadAllText($reportPath)) -ErrorAction Stop
    Assert-True ($writtenReport.Exclusion.CanAuthorizeActivation -eq $false) 'Written report must retain the exclusion gate.'
    $caseCount++

    $intelAttached = @{
        DisplayDevices = @($base.DisplayDevices)
        MonitorDevices = @(@{ InstanceId = $intelMonitor; Description = 'Intel Diagnostic Panel'; Status = 'OK'; Parent = $intelId })
        Queries = $base.Queries
    }
    $report = Invoke-Fixture 'intel-attached-still-gated' $intelAttached
    Assert-True ($report.DiagnosticCandidate.AssociatedMonitors.Count -eq 1) 'Intel-parented monitor should be associated with Intel.'
    Assert-True ($report.Exclusion.CanAuthorizeActivation -eq $false) 'An attached monitor is insufficient to authorize exclusion.'
    $caseCount++

    $missingIntel = @{
        DisplayDevices = @($base.DisplayDevices | Where-Object { $_.InstanceId -eq $nvidiaId })
        MonitorDevices = @($base.MonitorDevices)
        Queries = $base.Queries
    }
    $report = Invoke-Fixture 'missing-intel' $missingIntel
    Assert-True ($report.DiagnosticCandidate.Present -eq $false) 'A complete inventory without Intel should report absent.'
    Assert-True ($report.Exclusion.CanAuthorizeActivation -eq $false) 'Missing Intel must remain gated.'
    $caseCount++

    $failedInventory = @{
        DisplayDevices = @($base.DisplayDevices)
        MonitorDevices = @($base.MonitorDevices)
        Queries = @{
            Display = @{ Succeeded = $false; ExitCode = 5; Error = 'access denied' }
            Monitor = @{ Succeeded = $true; ExitCode = 0 }
        }
    }
    $report = Invoke-Fixture 'failed-display-query' $failedInventory
    Assert-True ($report.InventoryComplete -eq $false) 'Failed display enumeration must fail closed.'
    Assert-True ($null -eq $report.DiagnosticCandidate.Present) 'A failed query must not produce a false positive for Intel presence.'
    Assert-True ($report.HostInventory.Adapters.Count -eq 0) 'Records from a failed query must be discarded.'
    Assert-True ($report.Errors.Count -gt 0) 'Query failure must be captured in Errors.'
    $caseCount++

    $malformedPath = Join-Path $testDirectory 'malformed.json'
    $fixturePaths.Add([System.IO.Path]::GetFullPath($malformedPath))
    [System.IO.File]::WriteAllText($malformedPath, '{ "DisplayDevices": [ ', (New-Object System.Text.UTF8Encoding($false)))
    $output = & powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $resolvedTool -InventoryPath $malformedPath 2>&1
    $exitCode = $LASTEXITCODE
    Assert-True ($exitCode -eq 0) "Malformed fixture returned exit code $exitCode."
    $report = ConvertFrom-Json -InputObject (($output | ForEach-Object { [string]$_ }) -join [Environment]::NewLine) -ErrorAction Stop
    Assert-True ($report.InventoryComplete -eq $false) 'Malformed fixture must fail closed.'
    Assert-True ($null -eq $report.DiagnosticCandidate.Present) 'Malformed fixture must not report a positive adapter presence.'
    Assert-True ($report.Errors.Count -gt 0) 'Malformed fixture error must be captured.'
    $caseCount++

    Write-Output "PASS: $caseCount GPU diagnostic preflight cases."
}
finally {
    foreach ($path in $fixturePaths) {
        $fullPath = [System.IO.Path]::GetFullPath($path)
        $parentPath = [System.IO.Path]::GetDirectoryName($fullPath)
        if ([string]::Equals($parentPath, [System.IO.Path]::GetFullPath($testDirectory), [StringComparison]::OrdinalIgnoreCase) -and
            [System.IO.File]::Exists($fullPath)) {
            Remove-Item -LiteralPath $fullPath -Force
        }
    }
}
