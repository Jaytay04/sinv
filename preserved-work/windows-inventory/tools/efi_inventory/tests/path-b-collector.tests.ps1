# Plain PowerShell host tests for path-b-collector.ps1. No Pester or firmware access.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$script:CollectorScript = Join-Path $PSScriptRoot '..\path-b-collector.ps1'
$script:PowerShellExe = Join-Path $PSHOME 'powershell.exe'
$script:Base = Join-Path $script:RepoRoot '.codex-build\path_b_host_tests'
$script:RunRoot = Join-Path $script:Base ('run-' + [Guid]::NewGuid().ToString('N'))
$script:LoaderBuild = Join-Path $script:RunRoot 'loader-build'
$script:InventoryBuild = Join-Path $script:RunRoot 'inventory-build'
$script:EspRoot = Join-Path $script:RunRoot 'esp'
$script:ReparseEsp = Join-Path $script:RunRoot 'reparse-esp'
$script:ReparseTarget = Join-Path $script:RunRoot 'reparse-target'
$script:Passed = 0

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Equal([string]$Expected, [string]$Actual, [string]$Message) {
    if ($Expected -cne $Actual) {
        throw ($Message + [Environment]::NewLine + 'Expected: ' + $Expected + [Environment]::NewLine + 'Actual:   ' + $Actual)
    }
}
function New-EfiFixture([string]$Path, [byte]$Marker) {
    $bytes = New-Object byte[] 512
    $pe = 0x80
    $bytes[0] = 0x4D
    $bytes[1] = 0x5A
    [Array]::Copy([BitConverter]::GetBytes([uint32]$pe),0,$bytes,0x3C,4)
    $bytes[$pe] = 0x50
    $bytes[$pe + 1] = 0x45
    $bytes[$pe + 2] = 0
    $bytes[$pe + 3] = 0
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x8664),0,$bytes,$pe + 4,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]1),0,$bytes,$pe + 6,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]0xF0),0,$bytes,$pe + 20,2)
    $optional = $pe + 24
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x20B),0,$bytes,$optional,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]10),0,$bytes,$optional + 68,2)
    $bytes[511] = $Marker
    [IO.File]::WriteAllBytes($Path,$bytes)
}
function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Invoke-PowerShell([string[]]$Arguments) {
    $quoted = @()
    foreach ($argument in $Arguments) {
        $builder = New-Object Text.StringBuilder
        $null = $builder.Append('"')
        $slashCount = 0
        foreach ($character in ([string]$argument).ToCharArray()) {
            if ($character -eq [char]92) {
                $slashCount++
                continue
            }
            if ($character -eq '"') {
                $null = $builder.Append([char]92,($slashCount * 2 + 1))
                $null = $builder.Append('"')
            } else {
                if ($slashCount -gt 0) { $null = $builder.Append([char]92,$slashCount) }
                $null = $builder.Append($character)
            }
            $slashCount = 0
        }
        if ($slashCount -gt 0) { $null = $builder.Append([char]92,($slashCount * 2)) }
        $null = $builder.Append('"')
        $quoted += $builder.ToString()
    }
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $script:PowerShellExe
    $start.Arguments = $quoted -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    $null = $process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    return @{ ExitCode = $process.ExitCode; Output = ($stdoutTask.Result + $stderrTask.Result) }
}
function Invoke-Collector([string]$Mode, [string]$TestRoot) {
    return Invoke-PowerShell @(
        '-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass',
        '-File',$script:CollectorScript,
        '-Mode',$Mode,
        '-LoaderBuildDir',$script:LoaderBuild,
        '-InventoryBuildDir',$script:InventoryBuild,
        '-TestRoot',$TestRoot
    )
}
function Invoke-RollbackCheck {
    $harnessPath = Join-Path $script:RunRoot 'rollback-check.ps1'
    $harness = @'
param(
    [string]$CollectorScript,
    [string]$LoaderBuildDir,
    [string]$InventoryBuildDir,
    [string]$EspRoot
)
$ErrorActionPreference = 'Stop'
. $CollectorScript -Mode Stage -LoaderBuildDir $LoaderBuildDir -InventoryBuildDir $InventoryBuildDir -TestRoot $EspRoot | Out-Null
function Get-Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant() }
function Write-Receipt { throw 'Injected receipt failure for the host rollback test.' }
$stage = Join-Path $EspRoot 'EFI\HypersinvInventory'
$expectedLoaderHash = Get-Hash (Join-Path $stage 'loader.efi')
$expectedInventoryHash = Get-Hash (Join-Path $stage 'inventory.efi')
$expectedManifestHash = Get-Hash (Join-Path $stage 'manifest.sha256')
$previousBackupCount = @(Get-ChildItem -LiteralPath $InventoryBuildDir -Directory -Filter 'path-b-backup.*').Count
$expectedFailure = $false
try { Stage-ToRoot $EspRoot 'TEST-ROOT' $EspRoot }
catch {
    if ($_.Exception.Message -like '*Injected receipt failure*') { $expectedFailure = $true }
    else { throw }
}
if (-not $expectedFailure) { throw 'Injected stage failure did not occur.' }
if ((Get-Hash (Join-Path $stage 'loader.efi')) -cne $ExpectedLoaderHash -or
    (Get-Hash (Join-Path $stage 'inventory.efi')) -cne $ExpectedInventoryHash -or
    (Get-Hash (Join-Path $stage 'manifest.sha256')) -cne $ExpectedManifestHash) {
    throw 'Stage rollback did not restore the exact prior files.'
}
$backups = @(Get-ChildItem -LiteralPath $InventoryBuildDir -Directory -Filter 'path-b-backup.*')
if ($backups.Count -ne ($PreviousBackupCount + 1)) { throw 'Prior stage archive was not retained after rollback.' }
$efi = Join-Path $EspRoot 'EFI'
$leftovers = @(Get-ChildItem -LiteralPath $efi -Force | Where-Object { $_.Name -like '.HypersinvInventory.*' })
if ($leftovers.Count -ne 0) { throw 'Stage rollback left a transaction directory on the ESP fixture.' }

$state = @{ Armed = $true; RollbackCalls = 0 }
$installerFailurePreservedState = $false
try {
    Invoke-ArmWithRollback { throw 'simulated installer transaction failure' } { throw 'verification must not run' } { $state.RollbackCalls++; $state.Armed = $false }
} catch {
    if ($_.Exception.Message -like '*simulated installer transaction failure*') { $installerFailurePreservedState = $true }
    else { throw }
}
if (-not $installerFailurePreservedState -or -not $state.Armed -or $state.RollbackCalls -ne 0) {
    throw 'Failed installer transaction must preserve prior owned BootNext state and must not issue off.'
}
$state.Armed = $false
$postcheckRollbackReported = $false
try {
    Invoke-ArmWithRollback { $state.Armed = $true } { throw 'simulated post-arm verification failure' } { $state.Armed = $false }
} catch {
    if ($_.Exception.Message -like '*owned one-time boot state was cleared*') { $postcheckRollbackReported = $true }
    else { throw }
}
if (-not $postcheckRollbackReported -or $state.Armed) { throw 'Successful Arm with failed verification did not clear mocked one-time state.' }
$cleanupFailureReported = $false
try { Invoke-ArmWithRollback { } { throw 'simulated post-arm verification failure' } { throw 'simulated cleanup failure' } }
catch {
    if ($_.Exception.Message -like '*also failed*simulated cleanup failure*') { $cleanupFailureReported = $true }
    else { throw }
}
if (-not $cleanupFailureReported) { throw 'Arm rollback did not report a cleanup failure.' }
Write-Output 'PASS staged-file rollback, local archive retention, and mocked Arm cleanup'
'@
    [IO.File]::WriteAllText($harnessPath,$harness,[Text.Encoding]::UTF8)
    return Invoke-PowerShell @(
        '-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass',
        '-File',$harnessPath,
        '-CollectorScript',$script:CollectorScript,
        '-LoaderBuildDir',$script:LoaderBuild,
        '-InventoryBuildDir',$script:InventoryBuild,
        '-EspRoot',$script:EspRoot
    )
}
function Assert-Succeeded($Result, [string]$Message) {
    if ($Result.ExitCode -ne 0) {
        throw ($Message + ' (exit ' + $Result.ExitCode + '):' + [Environment]::NewLine + $Result.Output)
    }
}
function Assert-Failed($Result, [string]$Message) {
    if ($Result.ExitCode -eq 0) { throw ($Message + ' unexpectedly succeeded:' + [Environment]::NewLine + $Result.Output) }
    Write-Output ('PASS ' + $Message)
    $script:Passed++
}

New-Item -ItemType Directory -Path $script:LoaderBuild -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $script:InventoryBuild 'efi') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $script:EspRoot 'EFI\Microsoft\Boot') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $script:EspRoot 'EFI\OEM') -Force | Out-Null

$loaderSource = Join-Path $script:LoaderBuild 'inventory_loader.efi'
$collectorSource = Join-Path (Join-Path $script:InventoryBuild 'efi') 'inventory.efi'
New-EfiFixture $loaderSource 0x31
New-EfiFixture $collectorSource 0x72
$bootManager = Join-Path $script:EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi'
$unrelated = Join-Path $script:EspRoot 'EFI\OEM\keep.bin'
[IO.File]::WriteAllBytes($bootManager,[Text.Encoding]::ASCII.GetBytes('unchanged Windows Boot Manager fixture'))
[IO.File]::WriteAllBytes($unrelated,[Text.Encoding]::ASCII.GetBytes('unrelated EFI content'))
$bootHash = Get-Hash $bootManager
$unrelatedHash = Get-Hash $unrelated

try {
    $result = Invoke-Collector 'Stage' $script:EspRoot
    Assert-Succeeded $result 'Initial staging'
    $stage = Join-Path $script:EspRoot 'EFI\HypersinvInventory'
    $names = (@(Get-ChildItem -LiteralPath $stage -Force | Select-Object -ExpandProperty Name | Sort-Object) -join ',')
    Assert-Equal 'inventory.efi,loader.efi,manifest.sha256' $names 'Stage must contain only the three owned files.'
    Assert-Equal (Get-Hash $loaderSource) (Get-Hash (Join-Path $stage 'loader.efi')) 'Staged loader hash'
    Assert-Equal (Get-Hash $collectorSource) (Get-Hash (Join-Path $stage 'inventory.efi')) 'Staged collector hash'
    $nl = [string][char]13 + [char]10
    $expectedManifest = 'schema=1' + $nl +
        ('loader.efi size={0} sha256={1}' -f (Get-Item $loaderSource).Length,(Get-Hash $loaderSource)) + $nl +
        ('inventory.efi size={0} sha256={1}' -f (Get-Item $collectorSource).Length,(Get-Hash $collectorSource)) + $nl
    Assert-Equal $expectedManifest ([IO.File]::ReadAllText((Join-Path $stage 'manifest.sha256'),[Text.Encoding]::ASCII)) 'Manifest format'
    Assert-Equal $bootHash (Get-Hash $bootManager) 'Windows Boot Manager must remain unchanged after staging.'
    Assert-Equal $unrelatedHash (Get-Hash $unrelated) 'Unrelated EFI content must remain unchanged after staging.'
    Write-Output 'PASS exact staging, manifest, and unchanged EFI files'
    $script:Passed++

    $result = Invoke-Collector 'Status' $script:EspRoot
    Assert-Succeeded $result 'Status verification'
    Assert-True ($result.Output -match 'does not indicate whether BootNext is armed') 'Status must not claim firmware is armed.'
    Write-Output 'PASS Status verifies files without claiming BootNext state'
    $script:Passed++

    $oldStageLoaderHash = Get-Hash (Join-Path $stage 'loader.efi')
    New-EfiFixture $loaderSource 0x32
    $result = Invoke-Collector 'Stage' $script:EspRoot
    Assert-Succeeded $result 'Repeat staging of owned content'
    Assert-Equal (Get-Hash $loaderSource) (Get-Hash (Join-Path $stage 'loader.efi')) 'Repeat stage must install new source bytes.'
    $backups = @(Get-ChildItem -LiteralPath $script:InventoryBuild -Directory -Filter 'path-b-backup.*')
    Assert-True ($backups.Count -eq 1) 'Repeat stage must preserve exactly one local copy of the prior stage.'
    Assert-Equal $oldStageLoaderHash (Get-Hash (Join-Path $backups[0].FullName 'loader.efi')) 'Local backup must retain the prior loader bytes.'
    Assert-True ((Test-Path -LiteralPath (Join-Path $backups[0].FullName 'inventory.efi')) -and
        (Test-Path -LiteralPath (Join-Path $backups[0].FullName 'manifest.sha256'))) 'Local backup must preserve all three prior stage files.'
    $leftovers = @(Get-ChildItem -LiteralPath (Join-Path $script:EspRoot 'EFI') -Force |
        Where-Object { $_.Name -like '.HypersinvInventory.*' })
    Assert-True ($leftovers.Count -eq 0) 'Repeat stage must clean its owned ESP transaction directory.'
    Assert-Equal $bootHash (Get-Hash $bootManager) 'Boot manager hash after repeat staging'
    Assert-Equal $unrelatedHash (Get-Hash $unrelated) 'Unrelated file hash after repeat staging'
    Write-Output 'PASS repeat staging preserves prior bytes locally and changes only the owned stage'
    $script:Passed++

    New-EfiFixture $loaderSource 0x33
    $result = Invoke-RollbackCheck
    Assert-Succeeded $result 'Transactional stage rollback and mocked Arm rollback'
    Assert-Equal (Get-Hash $loaderSource) (Get-Hash (Join-Path $stage 'loader.efi')) 'Failed stage transaction must restore the prior loader.'
    Write-Output $result.Output.Trim()
    $script:Passed++

    $stagedLoader = Join-Path $stage 'loader.efi'
    $originalLoader = [IO.File]::ReadAllBytes($stagedLoader)
    $tampered = [IO.File]::ReadAllBytes($stagedLoader)
    $tampered[511] = $tampered[511] -bxor 0xFF
    [IO.File]::WriteAllBytes($stagedLoader,$tampered)
    $result = Invoke-Collector 'Status' $script:EspRoot
    Assert-Failed $result 'Tampered stage status detection'
    $result = Invoke-Collector 'Stage' $script:EspRoot
    Assert-Failed $result 'Tampered existing stage replacement refusal'
    [IO.File]::WriteAllBytes($stagedLoader,$originalLoader)

    $unknown = Join-Path $stage 'user-owned.bin'
    [IO.File]::WriteAllText($unknown,'preserve me')
    $beforeLoader = Get-Hash $stagedLoader
    $result = Invoke-Collector 'Stage' $script:EspRoot
    Assert-Failed $result 'Unknown stage content refusal'
    Assert-True (Test-Path -LiteralPath $unknown) 'Unknown content must be preserved.'
    Assert-Equal $beforeLoader (Get-Hash $stagedLoader) 'Refusal must preserve prior owned content.'
    Remove-Item -LiteralPath $unknown -Force

    $report = Join-Path $script:EspRoot 'HypersinvEfiInventory.txt'
    [IO.File]::WriteAllText($report,'existing report must remain')
    $result = Invoke-Collector 'Status' $script:EspRoot
    Assert-Succeeded $result 'Status with an existing collector report'
    Assert-True ($result.Output -match 'STAGED AND VERIFIED') 'Status must verify staged files while preserving an existing report.'
    $result = Invoke-Collector 'Stage' $script:EspRoot
    Assert-Failed $result 'Existing report refusal'
    Assert-Equal 'existing report must remain' ([IO.File]::ReadAllText($report)) 'Existing root report must be preserved.'
    Remove-Item -LiteralPath $report -Force

    $bootManagerBytes = [IO.File]::ReadAllBytes($bootManager)
    [IO.File]::WriteAllText($bootManager,'changed Windows Boot Manager fixture')
    $result = Invoke-Collector 'Status' $script:EspRoot
    Assert-Failed $result 'Changed boot manager receipt detection'
    [IO.File]::WriteAllBytes($bootManager,$bootManagerBytes)
    $result = Invoke-Collector 'Status' $script:EspRoot
    Assert-Succeeded $result 'Status after restoring the boot manager fixture'

    $collectorBytes = [IO.File]::ReadAllBytes($collectorSource)
    [IO.File]::WriteAllBytes($collectorSource,(New-Object byte[] 512))
    $result = Invoke-Collector 'Stage' $script:EspRoot
    Assert-Failed $result 'Invalid/non-PE source refusal'
    Assert-Equal $beforeLoader (Get-Hash $stagedLoader) 'Invalid build artifact must not replace prior staged files.'
    [IO.File]::WriteAllBytes($collectorSource,$collectorBytes)

    $result = Invoke-Collector 'Stage' ([IO.Path]::GetPathRoot($script:RepoRoot))
    Assert-Failed $result 'Drive-root TestRoot refusal'

    New-Item -ItemType Directory -Path $script:ReparseTarget -Force | Out-Null
    $targetBoot = Join-Path $script:ReparseTarget 'Microsoft\Boot\bootmgfw.efi'
    New-Item -ItemType Directory -Path (Split-Path -Parent $targetBoot) -Force | Out-Null
    [IO.File]::WriteAllBytes($targetBoot,[Text.Encoding]::ASCII.GetBytes('junction target fixture'))
    New-Item -ItemType Directory -Path $script:ReparseEsp -Force | Out-Null
    New-Item -ItemType Junction -Path (Join-Path $script:ReparseEsp 'EFI') -Target $script:ReparseTarget | Out-Null
    $result = Invoke-Collector 'Stage' $script:ReparseEsp
    Assert-Failed $result 'Reparse ancestor refusal'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $script:ReparseTarget 'HypersinvInventory'))) 'Reparse target must not be modified.'
    [IO.Directory]::Delete((Join-Path $script:ReparseEsp 'EFI'),$false)

    Assert-Equal $bootHash (Get-Hash $bootManager) 'Final Windows Boot Manager hash'
    Assert-Equal $unrelatedHash (Get-Hash $unrelated) 'Final unrelated EFI hash'
    Write-Output ('PASS all ' + $script:Passed + ' Path B host staging checks')
}
finally {
    if (Test-Path -LiteralPath (Join-Path $script:ReparseEsp 'EFI')) {
        $link = Get-Item -LiteralPath (Join-Path $script:ReparseEsp 'EFI') -Force
        if (($link.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            [IO.Directory]::Delete($link.FullName,$false)
        }
    }
    $baseFull = [IO.Path]::GetFullPath($script:Base).TrimEnd([char]92) + [IO.Path]::DirectorySeparatorChar
    $runFull = [IO.Path]::GetFullPath($script:RunRoot)
    if ($runFull.StartsWith($baseFull,[StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $runFull) -like 'run-*') {
        Remove-Item -LiteralPath $runFull -Recurse -Force
    }
}
