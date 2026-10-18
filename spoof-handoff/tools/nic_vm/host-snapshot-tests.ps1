[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$testSchema = 'nic-host-snapshot-tests.v1'
$productionPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'host-address-roundtrip.ps1'))
$mainRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
$reportRoot = Join-Path $mainRoot 'spoof-handoff\reports\nic-snapshot-fix-20261018'
[void][IO.Directory]::CreateDirectory($reportRoot)
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $reportRoot ('fixtures.' + $runId)
[void][IO.Directory]::CreateDirectory($testRoot)
$reportPath = Join-Path $reportRoot ('host-snapshot-tests.' + $runId + '.json')
$results = New-Object 'System.Collections.Generic.List[object]'
$failureDetails = New-Object 'System.Collections.Generic.List[object]'
$sourceHashBefore = (Get-FileHash -LiteralPath $productionPath -Algorithm SHA256).Hash.ToUpperInvariant()
$testHashBefore = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToUpperInvariant()

function Add-TestResult {
    param([string]$Name, [string]$Status, [string]$Detail)
    $script:results.Add([ordered]@{ name = $Name; status = $Status; detail = $Detail })
}

function Invoke-TestCase {
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][scriptblock]$Body)
    try {
        $detail = & $Body
        Add-TestResult $Name 'PASS' ([string]$detail)
    }
    catch {
        $record = [ordered]@{
            name = $Name
            status = 'FAIL'
            exceptionType = $_.Exception.GetType().FullName
            exception = $_.Exception.ToString()
            fullyQualifiedErrorId = $_.FullyQualifiedErrorId
        }
        $script:failureDetails.Add($record)
        Add-TestResult $Name 'FAIL' ([string]$_.Exception.Message)
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Throws {
    param([Parameter(Mandatory = $true)][scriptblock]$Body, [Parameter(Mandatory = $true)][string]$MessagePattern)
    try { & $Body }
    catch {
        if ($_.Exception.Message -match $MessagePattern) { return $_.Exception.Message }
        throw ('Expected error matching ' + $MessagePattern + '; actual=' + $_.Exception.Message)
    }
    throw ('Expected an error matching ' + $MessagePattern + ' but the operation succeeded.')
}

function New-ValidBcdSnapshot {
    param([string]$ExportPath = 'fixture-export.bcd')
    $success = { [ordered]@{ exitCode = 0; timedOut = $false; stdout = 'Windows Boot Manager' + "`n"; stderr = '' } }
    return [ordered]@{
        current = & $success
        all = & $success
        firmware = & $success
        exportedAll = & $success
        export = [ordered]@{ exitCode = 0; timedOut = $false; path = $ExportPath; sha256 = ('A' * 64); bytes = 8192 }
    }
}

function New-EspFixture {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [bool]$IsSystem = $true,
        [string[]]$ExtraFiles = @(),
        [string[]]$OmitFiles = @()
    )
    $root = Join-Path $script:testRoot $Name
    $boot = Join-Path $root 'EFI\Microsoft\Boot'
    [void][IO.Directory]::CreateDirectory($boot)
    $relativeFiles = @(
        'EFI\Microsoft\Boot\BCD',
        'EFI\Microsoft\Boot\BCD.LOG',
        'EFI\Microsoft\Boot\BCD.LOG1',
        'EFI\Microsoft\Boot\BCD.LOG2',
        'EFI\Microsoft\Boot\BOOTSTAT.DAT',
        'EFI\Microsoft\Boot\bootmgfw.efi'
    ) + $ExtraFiles
    foreach ($relative in $relativeFiles) {
        if ($OmitFiles -contains $relative.Replace('\', '/')) { continue }
        $full = Join-Path $root $relative
        $parent = Split-Path -Parent $full
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) { [void][IO.Directory]::CreateDirectory($parent) }
        [IO.File]::WriteAllBytes($full, [Text.Encoding]::UTF8.GetBytes('fixture:' + $relative))
    }
    $partition = [pscustomobject]@{
        GptType = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
        AccessPaths = @($root)
        IsSystem = $IsSystem
        IsReadOnly = $false
        DiskNumber = 7
        PartitionNumber = 2
        Guid = '{11111111-2222-3333-4444-555555555555}'
        Offset = [long]1048576
        Size = [long]67108864
    }
    return [ordered]@{ root = $root; partition = $partition; bcdPath = (Join-Path $boot 'BCD') }
}

function Get-HostProperty {
    param([AllowNull()][object]$Object, [Parameter(Mandatory = $true)][string]$Name)
    if ($null -eq $Object) { return $null }
    if ($Object -is [Collections.IDictionary]) {
        foreach ($key in $Object.Keys) { if ([string]::Equals([string]$key, $Name, [StringComparison]::OrdinalIgnoreCase)) { return $Object[$key] } }
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Set-MockPartitions {
    param([Parameter(Mandatory = $true)][object[]]$Partitions)
    $script:MockPartitions = $Partitions
}

function Get-Partition {
    [CmdletBinding()]
    param()
    return @($script:MockPartitions)
}

function Get-HostSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Write-HostJsonAtomic {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][object]$Value)
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { [void][IO.Directory]::CreateDirectory($parent) }
    $temp = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    $content = ConvertTo-Json -InputObject $Value -Depth 24
    [IO.File]::WriteAllText($temp, $content, [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $Path -PathType Leaf) { [IO.File]::Replace($temp, $Path, $null) }
    else { [IO.File]::Move($temp, $Path) }
}

function Write-HostJournal {
    param([Parameter(Mandatory = $true)][object]$Journal)
    $Journal.updatedUtc = [DateTime]::UtcNow.ToString('o')
    Write-HostJsonAtomic ([string]$Journal.journalPath) $Journal
}

function Get-HostProcessCapture {
    param([Parameter(Mandatory = $true)][string]$Program, [Parameter(Mandatory = $true)][string[]]$Arguments)
    $mode = $script:MockBcdMode
    if ($Arguments -contains '/export') {
        $outPath = $Arguments[1]
        if ($mode -ceq 'export-fails') { return [ordered]@{ exitCode = 5; timedOut = $false; killError = $null; stdout = ''; stderr = 'synthetic export denied' } }
        [IO.File]::WriteAllBytes($outPath, [Text.Encoding]::UTF8.GetBytes('offline BCD export'))
        return [ordered]@{ exitCode = 0; timedOut = $false; killError = $null; stdout = 'The export operation completed successfully.'; stderr = '' }
    }
    if ($Arguments -contains '/store') {
        if ($mode -ceq 'export-enumeration-fails') { return [ordered]@{ exitCode = 1; timedOut = $false; killError = $null; stdout = ''; stderr = 'synthetic offline enumeration failed' } }
        return [ordered]@{ exitCode = 0; timedOut = $false; killError = $null; stdout = 'Windows Boot Manager' + "`n"; stderr = '' }
    }
    $enumeration = if ($Arguments -contains '{current}') { 'current' } elseif ($Arguments -contains 'all') { 'all' } elseif ($Arguments -contains 'firmware') { 'firmware' } else { 'unknown' }
    if ($mode -ceq ($enumeration + '-fails')) { return [ordered]@{ exitCode = 1; timedOut = $false; killError = $null; stdout = ''; stderr = ('synthetic ' + $enumeration + ' enumeration failed') } }
    $stdout = if ($mode -ceq ($enumeration + '-empty')) { '' } else { 'Windows Boot Manager' + "`n" }
    return [ordered]@{ exitCode = 0; timedOut = $false; killError = $null; stdout = $stdout; stderr = '' }
}

function Get-HostBcdSnapshot {
    param([Parameter(Mandatory = $true)][string]$EvidenceDirectory)
    [void][IO.Directory]::CreateDirectory($EvidenceDirectory)
    $bcdedit = 'synthetic-bcdedit.exe'
    $records = [ordered]@{}
    foreach ($name in @('current', 'all', 'firmware')) {
        $args = if ($name -ceq 'current') { @('/enum', '{current}', '/v') } elseif ($name -ceq 'all') { @('/enum', 'all', '/v') } else { @('/enum', 'firmware', '/v') }
        $capture = Get-HostProcessCapture $bcdedit $args
        $records[$name] = $capture
        if ($capture.timedOut -or $capture.exitCode -ne 0) { throw ('bcdedit /enum ' + $name + ' failed exit=' + $capture.exitCode + ' timedOut=' + $capture.timedOut + ' stdout=' + $capture.stdout + ' stderr=' + $capture.stderr) }
    }
    $exportPath = Join-Path $EvidenceDirectory 'boot-store-export.bcd'
    $export = Get-HostProcessCapture $bcdedit @('/export', $exportPath)
    $records.export = [ordered]@{ exitCode = $export.exitCode; stdout = $export.stdout; stderr = $export.stderr; path = $exportPath }
    if ($export.timedOut -or $export.exitCode -ne 0 -or -not (Test-Path -LiteralPath $exportPath -PathType Leaf)) { throw ('bcdedit /export failed exit=' + $export.exitCode + ' timedOut=' + $export.timedOut + ' stdout=' + $export.stdout + ' stderr=' + $export.stderr) }
    $records.export.sha256 = Get-HostSha256 $exportPath
    $records.export.bytes = [long](Get-Item -LiteralPath $exportPath).Length
    $records.exportedAll = Get-HostProcessCapture $bcdedit @('/store', $exportPath, '/enum', 'all', '/v')
    if ($records.exportedAll.timedOut -or $records.exportedAll.exitCode -ne 0) {
        Write-HostJsonAtomic (Join-Path $EvidenceDirectory 'snapshot.json') $records
        throw ('Exported BCD enumeration failed exit=' + $records.exportedAll.exitCode + ' stdout=' + $records.exportedAll.stdout + ' stderr=' + $records.exportedAll.stderr)
    }
    $records.export.sha256 = Get-HostSha256 $exportPath
    $records.export.bytes = [long](Get-Item -LiteralPath $exportPath).Length
    Assert-HostBcdSnapshotUsable $records
    Write-HostJsonAtomic (Join-Path $EvidenceDirectory 'snapshot.json') $records
    return $records
}

function Get-HostStablePlatformData {
    param([Parameter(Mandatory = $true)][object]$Snapshot)
    $esp = @((Get-HostProperty $Snapshot 'esp') | ForEach-Object {
        $partition = $_
        $files = @((Get-HostProperty $partition 'files') | ForEach-Object {
            $path = [string](Get-HostProperty $_ 'path')
            if ([string]::Equals($path, 'EFI/Microsoft/Boot/BOOTSTAT.DAT', [StringComparison]::OrdinalIgnoreCase)) { return }
            if (Test-HostManagedBcdFile $path ([bool](Get-HostProperty $partition 'isSystem'))) {
                Assert-HostBcdSnapshotUsable (Get-HostProperty $Snapshot 'bcd')
                if ((Get-HostProperty $_ 'verification') -cne 'bcdedit-system-store') { throw ('Live BCD classification is missing supported verification: ' + $path) }
                [ordered]@{ path = $path; verification = 'bcdedit-system-store' }
            }
            else {
                $hash = [string](Get-HostProperty $_ 'sha256')
                if ($hash -notmatch '\A[0-9A-Fa-f]{64}\z') { throw ('ESP file is missing its required SHA-256: ' + $path) }
                [ordered]@{ path = $path; bytes = [long](Get-HostProperty $_ 'bytes'); sha256 = $hash }
            }
        })
        [ordered]@{
            diskNumber = [int](Get-HostProperty $partition 'diskNumber')
            partitionNumber = [int](Get-HostProperty $partition 'partitionNumber')
            partitionGuid = [string](Get-HostProperty $partition 'partitionGuid')
            isSystem = [bool](Get-HostProperty $partition 'isSystem')
            offset = [long](Get-HostProperty $partition 'offset')
            size = [long](Get-HostProperty $partition 'size')
            files = $files
        }
    })
    $firmware = [ordered]@{}
    foreach ($name in @('SecureBoot', 'SetupMode', 'BootOrder', 'BootNext', 'BootCurrent')) {
        $item = Get-HostProperty (Get-HostProperty $Snapshot 'firmware') $name
        $firmware[$name] = if ((Get-HostProperty $item 'state') -ceq 'present') { [ordered]@{ state = 'present'; guid = [string](Get-HostProperty $item 'guid'); attributes = [uint32](Get-HostProperty $item 'attributes'); size = [long](Get-HostProperty $item 'size'); bytesSha256 = [string](Get-HostProperty $item 'bytesSha256'); bytesHex = [string](Get-HostProperty $item 'bytesHex') } } else { [ordered]@{ state = [string](Get-HostProperty $item 'state'); win32Error = [int](Get-HostProperty $item 'win32Error'); errorType = [string](Get-HostProperty $item 'errorType'); error = [string](Get-HostProperty $item 'error') } }
    }
    $bcd = Get-HostProperty $Snapshot 'bcd'
    $bcdStable = [ordered]@{}
    foreach ($name in @('current', 'all', 'firmware', 'exportedAll')) {
        $entry = Get-HostProperty $bcd $name
        $bcdStable[$name] = [ordered]@{ exitCode = [int](Get-HostProperty $entry 'exitCode'); stdout = [string](Get-HostProperty $entry 'stdout'); stderr = [string](Get-HostProperty $entry 'stderr') }
    }
    $export = Get-HostProperty $bcd 'export'
    $bcdStable.export = [ordered]@{ exitCode = [int](Get-HostProperty $export 'exitCode') }
    return [ordered]@{
        secureBoot = [bool](Get-HostProperty $Snapshot 'secureBoot')
        codeIntegrity = Get-HostProperty $Snapshot 'codeIntegrity'
        trustStores = Get-HostProperty $Snapshot 'trustStores'
        nicFilter = [ordered]@{ servicePresent = [bool](Get-HostProperty (Get-HostProperty $Snapshot 'nicFilter') 'servicePresent'); matchingBindings = @(Get-HostProperty (Get-HostProperty $Snapshot 'nicFilter') 'matchingBindings') }
        firmware = $firmware
        bcd = $bcdStable
        esp = $esp
        espMutableExclusions = @('BOOTSTAT.DAT is Windows-owned mutable status.', 'System ESP BCD raw bytes are not compared; supported enumerations are compared.')
    }
}

function Assert-HostPlatformUnchanged {
    param([Parameter(Mandatory = $true)][object]$Before, [Parameter(Mandatory = $true)][object]$After)
    $beforeJson = ConvertTo-Json -InputObject (Get-HostStablePlatformData $Before) -Depth 24 -Compress
    $afterJson = ConvertTo-Json -InputObject (Get-HostStablePlatformData $After) -Depth 24 -Compress
    if (-not [string]::Equals($beforeJson, $afterJson, [StringComparison]::Ordinal)) { throw ('Host platform state changed. BEFORE=' + $beforeJson + ' AFTER=' + $afterJson) }
}

function Assert-HostAdministrator { }
function Assert-HostIdentity { }
function Get-HostLockedBindings { param([string]$TsvPath) return @([ordered]@{}, [ordered]@{}, [ordered]@{}, [ordered]@{}) }
function Assert-HostVmGate { param([string]$GatePath,[string]$RepositoryRoot,[string]$TsvPath,[string]$NativeExePath) return [ordered]@{ sha256 = 'B' * 64 } }
function Invoke-HostReadback {
    param([string]$Mode,[string]$NativeExePath,[string]$TsvPath,[string]$Directory,[AllowNull()][object]$Dot11Baseline = $null)
    $script:MockReadbackModes.Add($Mode)
    return [ordered]@{ mode = $Mode; dot11ByPnp = [ordered]@{}; proof = 'synthetic read-only original capture' }
}
function Get-HostPlatformSnapshot {
    param([string]$EvidenceDirectory,[string]$CertificateThumbprint)
    $script:MockPlatformSnapshotCount++
    if ($script:MockPlatformSnapshotCount -eq 1) { throw 'synthetic live BCD snapshot failure before any adapter mutation' }
    return $null
}
function Get-HostAdapterSnapshots { $script:MockAdapterSnapshotCalls++; throw 'Adapter snapshot must not run after platform preflight failure.' }
function Set-NetAdapter { $script:MockSetCalls++; throw 'Set-NetAdapter must not run after platform preflight failure.' }
function Restore-HostAdapters { $script:MockRestoreCalls++; return @('unexpected restore') }

$functionsNeeded = @(
    'Get-HostProperty', 'Get-HostSha256', 'Write-HostJsonAtomic', 'Write-HostJournal',
    'Test-HostManagedBcdFile', 'Assert-HostBcdSnapshotUsable', 'Get-HostEspSnapshot',
    'Get-HostBcdSnapshot', 'Get-HostStablePlatformData', 'Assert-HostPlatformUnchanged', 'Invoke-HostRun'
)
$sourceTokens = $null
$sourceErrors = $null
$productionAst = [Management.Automation.Language.Parser]::ParseFile($productionPath, [ref]$sourceTokens, [ref]$sourceErrors)
if ($sourceErrors.Count -gt 0) { throw ('Production PowerShell parse failure: ' + ($sourceErrors | Out-String)) }
$functionAsts = @($productionAst.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $functionsNeeded -ccontains $node.Name }, $true))
if ($functionAsts.Count -ne $functionsNeeded.Count) { throw ('Could not extract expected production functions: expected=' + $functionsNeeded.Count + ' actual=' + $functionAsts.Count) }
. ([ScriptBlock]::Create(($functionAsts | ForEach-Object { $_.Extent.Text }) -join "`r`n"))

# Keep test evidence writes independent of the production atomic-replace helper; these mocks only write beneath the fixture root.
function Write-HostJsonAtomic {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][object]$Value)
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { [void][IO.Directory]::CreateDirectory($parent) }
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 24), [Text.UTF8Encoding]::new($false))
}
function Write-HostJournal {
    param([Parameter(Mandatory = $true)][object]$Journal)
    $Journal.updatedUtc = [DateTime]::UtcNow.ToString('o')
    Write-HostJsonAtomic ([string]$Journal.journalPath) $Journal
}

$script:MockPartitions = @()
$script:MockBcdMode = 'success'
$script:MockReadbackModes = New-Object 'System.Collections.Generic.List[string]'
$script:MockPlatformSnapshotCount = 0
$script:MockAdapterSnapshotCalls = 0
$script:MockSetCalls = 0
$script:MockRestoreCalls = 0
$script:HostRunSchema = 'nic-host-address-roundtrip.v1'
$script:ExpectedCertificateThumbprint = 'TEST-ONLY'
$script:ExpectedBaseboardManufacturer = 'Micro-Star International Co., Ltd.'
$script:ExpectedHostUuid = 'C88B4631-C88C-871B-A6D7-D8BBC1D85720'
$script:ActiveHostTemporaryDirectory = $null
$sourceEspBody = $functionAsts | Where-Object { $_.Name -ceq 'Get-HostEspSnapshot' } | ForEach-Object { $_.Extent.Text }
$helperFunctions = @('Get-HostProperty', 'Get-HostSha256', 'Test-HostManagedBcdFile', 'Assert-HostBcdSnapshotUsable', 'Get-HostEspSnapshot', 'Get-HostBcdSnapshot', 'Get-HostStablePlatformData', 'Assert-HostPlatformUnchanged', 'Invoke-HostRun')

Invoke-TestCase 'system-bcd-whitelist-is-exact-and-system-only' {
    $accepted = @('EFI/Microsoft/Boot/BCD', 'EFI/Microsoft/Boot/BCD.LOG', 'EFI/Microsoft/Boot/BCD.LOG1', 'EFI/Microsoft/Boot/BCD.LOG2')
    foreach ($path in $accepted) { Assert-True (Test-HostManagedBcdFile $path $true) ('System BCD path rejected: ' + $path) }
    $rejected = @('EFI/Microsoft/Boot/BCD.bak', 'EFI/Microsoft/Boot/BCD.LOG3', 'EFI/Other/BCD', 'EFI/Microsoft/Boot/bootmgfw.efi')
    foreach ($path in $rejected) { Assert-True (-not (Test-HostManagedBcdFile $path $true)) ('Unexpected system BCD classification: ' + $path) }
    Assert-True (-not (Test-HostManagedBcdFile 'EFI/Microsoft/Boot/BCD' $false)) 'A non-system partition BCD was treated as the live system hive.'
    return '4 approved paths; 5 non-approved/system-scope cases rejected.'
}

Invoke-TestCase 'supported-bcd-snapshot-requires-all-live-and-exported-enumerations' {
    $valid = New-ValidBcdSnapshot
    Assert-HostBcdSnapshotUsable $valid
    $invalid = @()
    $invalid += ,$null
    foreach ($name in @('current', 'all', 'firmware', 'exportedAll')) {
        $copy = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $valid -Depth 8)
        $copy.$name.exitCode = 1
        $invalid += ,$copy
        $copy = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $valid -Depth 8)
        $copy.$name.stdout = ''
        $invalid += ,$copy
    }
    $copy = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $valid -Depth 8); $copy.export.exitCode = 1; $invalid += ,$copy
    $copy = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $valid -Depth 8); $copy.export.sha256 = ''; $invalid += ,$copy
    $copy = ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $valid -Depth 8); $copy.export.bytes = 0; $invalid += ,$copy
    foreach ($candidate in $invalid) { Assert-Throws { Assert-HostBcdSnapshotUsable $candidate } 'BCD snapshot|hash-pinned BCD export' | Out-Null }
    return ('accepted one valid snapshot; rejected ' + $invalid.Count + ' incomplete/failed snapshot variants.')
}

Invoke-TestCase 'bcd-snapshot-builder-fails-closed-on-export-and-enumeration-errors' {
    $modes = @('current-fails', 'all-fails', 'firmware-fails', 'export-fails', 'export-enumeration-fails', 'firmware-empty')
    foreach ($mode in $modes) {
        $script:MockBcdMode = $mode
        $directory = Join-Path $script:testRoot ('bcd-builder-' + $mode)
        Assert-Throws { Get-HostBcdSnapshot $directory } 'failed|requires a successful complete enumeration|hash-pinned BCD export' | Out-Null
    }
    $script:MockBcdMode = 'success'
    $success = Get-HostBcdSnapshot (Join-Path $script:testRoot 'bcd-builder-success')
    Assert-HostBcdSnapshotUsable $success
    Assert-True (Test-Path -LiteralPath (Join-Path $script:testRoot 'bcd-builder-success\snapshot.json') -PathType Leaf) 'Successful BCD snapshot metadata was not persisted.'
    return 'six injected failure modes stopped; a successful export/enumeration produced hash and size evidence.'
}

Invoke-TestCase 'system-esp-snapshot-classifies-locked-bcd-without-reading-its-bytes' {
    $fixture = New-EspFixture 'locked-system-esp'
    Set-MockPartitions @($fixture.partition)
    $lock = [IO.File]::Open($fixture.bcdPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try {
        Assert-Throws { Get-FileHash -LiteralPath $fixture.bcdPath -Algorithm SHA256 -ErrorAction Stop } 'being used|access|denied|sharing' | Out-Null
        $snapshot = @(Get-HostEspSnapshot (Join-Path $script:testRoot 'esp-evidence') (New-ValidBcdSnapshot))
        $files = @($snapshot[0].files)
        $bcdRows = @($files | Where-Object { $_.path -match '\AEFI/Microsoft/Boot/BCD(?:\.LOG[12]?)?\z' })
        Assert-True ($bcdRows.Count -eq 4) ('Expected four transaction files to be classified through BCD evidence; actual=' + $bcdRows.Count)
        foreach ($row in $bcdRows) {
            Assert-True ($row.verification -ceq 'bcdedit-system-store' -and $null -eq $row.sha256 -and -not $row.rawBytesCaptured) ('Live BCD bytes were incorrectly claimed for ' + $row.path)
            Assert-True ($row.exportSha256 -ceq ('A' * 64) -and $row.exportPath -ceq 'fixture-export.bcd') ('Pinned export provenance is absent for ' + $row.path)
        }
        $bootStatus = @($files | Where-Object { $_.path -ceq 'EFI/Microsoft/Boot/BOOTSTAT.DAT' })[0]
        Assert-True ($bootStatus.verification -ceq 'mutable-windows-boot-status' -and $null -eq $bootStatus.sha256) 'BOOTSTAT.DAT did not retain its explicit mutable classification.'
        $ordinary = @($files | Where-Object { $_.path -ceq 'EFI/Microsoft/Boot/bootmgfw.efi' })[0]
        Assert-True ($ordinary.verification -ceq 'file-sha256' -and $ordinary.sha256 -match '\A[0-9A-Fa-f]{64}\z') 'Ordinary EFI file was not content-hash-pinned.'
    }
    finally { $lock.Dispose() }
    return 'FileShare.None blocked direct BCD hashing; system ESP inventory succeeded via exported-store evidence and left other files strict.'
}

Invoke-TestCase 'non-system-bcd-backup-and-arbitrary-locked-efi-files-remain-strict' {
    $cases = @(
        [ordered]@{ name = 'non-system-bcd'; system = $false; extra = @(); locked = 'EFI\Microsoft\Boot\BCD' },
        [ordered]@{ name = 'system-bcd-backup'; system = $true; extra = @('EFI\Microsoft\Boot\BCD.bak'); locked = 'EFI\Microsoft\Boot\BCD.bak' },
        [ordered]@{ name = 'system-arbitrary-file'; system = $true; extra = @('EFI\Vendor\locked.bin'); locked = 'EFI\Vendor\locked.bin' }
    )
    foreach ($case in $cases) {
        $fixture = New-EspFixture $case.name ([bool]$case.system) @($case.extra)
        Set-MockPartitions @($fixture.partition)
        $lockedPath = Join-Path $fixture.root $case.locked
        $lock = [IO.File]::Open($lockedPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        try { Assert-Throws { Get-HostEspSnapshot (Join-Path $script:testRoot ($case.name + '-evidence')) (New-ValidBcdSnapshot) } 'being used|access|denied|sharing' | Out-Null }
        finally { $lock.Dispose() }
    }
    $fixture = New-EspFixture 'system-bcd-without-supported-evidence'
    Set-MockPartitions @($fixture.partition)
    Assert-Throws { Get-HostEspSnapshot (Join-Path $script:testRoot 'missing-bcd-evidence') $null } 'supported BCD snapshot is required' | Out-Null
    $bad = New-ValidBcdSnapshot
    $bad.exportedAll.exitCode = 1
    Assert-Throws { Get-HostEspSnapshot (Join-Path $script:testRoot 'failed-bcd-enumeration') $bad } 'successful complete enumeration: exportedAll' | Out-Null
    return 'Non-system BCD, BCD.bak, arbitrary EFI files, and absent/failed BCD evidence all failed closed.'
}

function New-PlatformFixture {
    param([string]$BootStatusHash = $null, [long]$BootStatusBytes = 5, [string]$EfiHash = ('C' * 64), [bool]$IncludeEfi = $true, [string]$EnumerationDrift = $null, [string]$ExportHash = ('A' * 64), [long]$ExportBytes = 8192)
    $bcdEntry = { param($stdout) [ordered]@{ exitCode = 0; timedOut = $false; stdout = [string]$stdout; stderr = '' } }
    $files = @(
        [ordered]@{ path = 'EFI/Microsoft/Boot/BCD'; bytes = $ExportBytes; sha256 = $null; verification = 'bcdedit-system-store'; rawBytesCaptured = $false },
        [ordered]@{ path = 'EFI/Microsoft/Boot/BCD.LOG'; bytes = 64; sha256 = $null; verification = 'bcdedit-system-store'; rawBytesCaptured = $false },
        [ordered]@{ path = 'EFI/Microsoft/Boot/BOOTSTAT.DAT'; bytes = $BootStatusBytes; sha256 = $BootStatusHash; verification = 'mutable-windows-boot-status'; rawBytesCaptured = $false }
    )
    if ($IncludeEfi) { $files += ,([ordered]@{ path = 'EFI/Microsoft/Boot/bootmgfw.efi'; bytes = 4096; sha256 = $EfiHash; verification = 'file-sha256'; rawBytesCaptured = $true }) }
    $enum = if ($null -eq $EnumerationDrift) { 'Windows Boot Manager' + "`n" } else { 'Windows Boot Manager' + "`n" + $EnumerationDrift + "`n" }
    $firmware = [ordered]@{
        SecureBoot = [ordered]@{ state = 'present'; guid = '8be4df61-93ca-11d2-aa0d-00e098032b8c'; attributes = 6; size = 1; bytesSha256 = ('1' * 64); bytesHex = '01' }
        SetupMode = [ordered]@{ state = 'present'; guid = '8be4df61-93ca-11d2-aa0d-00e098032b8c'; attributes = 6; size = 1; bytesSha256 = ('2' * 64); bytesHex = '00' }
        BootOrder = [ordered]@{ state = 'present'; guid = '8be4df61-93ca-11d2-aa0d-00e098032b8c'; attributes = 7; size = 4; bytesSha256 = ('3' * 64); bytesHex = '00000100' }
        BootNext = [ordered]@{ state = 'absent'; win32Error = 203; errorType = ''; error = '' }
        BootCurrent = [ordered]@{ state = 'present'; guid = '8be4df61-93ca-11d2-aa0d-00e098032b8c'; attributes = 7; size = 2; bytesSha256 = ('4' * 64); bytesHex = '0100' }
    }
    return [ordered]@{
        secureBoot = $true
        codeIntegrity = [ordered]@{ testSigning = $false }
        trustStores = [ordered]@{ Root = $false; TrustedPublisher = $false }
        nicFilter = [ordered]@{ servicePresent = $false; matchingBindings = @() }
        firmware = $firmware
        bcd = [ordered]@{
            current = & $bcdEntry $enum
            all = & $bcdEntry $enum
            firmware = & $bcdEntry 'Firmware Boot Manager' + "`n"
            exportedAll = & $bcdEntry $enum
            export = [ordered]@{ exitCode = 0; path = 'fixture.bcd'; sha256 = $ExportHash; bytes = $ExportBytes }
        }
        esp = @([ordered]@{ diskNumber = 1; partitionNumber = 1; partitionGuid = 'fixture-guid'; isSystem = $true; offset = 1048576; size = 67108864; files = $files })
    }
}

Invoke-TestCase 'stable-platform-comparison-ignores-only-approved-mutable-boot-bytes' {
    $before = New-PlatformFixture 'A' 5 ('C' * 64) $true $null ('A' * 64) 8192
    $after = New-PlatformFixture 'F' 99 ('C' * 64) $true $null ('D' * 64) 9000
    Assert-HostPlatformUnchanged $before $after
    $stable = Get-HostStablePlatformData $before
    $bcdStable = @($stable.esp[0].files | Where-Object { $_.path -ceq 'EFI/Microsoft/Boot/BCD' })[0]
    Assert-True ($bcdStable.verification -ceq 'bcdedit-system-store' -and $null -eq (Get-HostProperty $bcdStable 'sha256')) 'Stable BCD projection claimed binary equality.'
    Assert-True (@($stable.esp[0].files | Where-Object { $_.path -ceq 'EFI/Microsoft/Boot/BOOTSTAT.DAT' }).Count -eq 0) 'BOOTSTAT.DAT was not excluded from byte equality.'
    return 'Different export/live BCD byte metadata and BOOTSTAT bytes passed because BCD enumerations and immutable EFI files were stable.'
}

Invoke-TestCase 'stable-platform-comparison-rejects-bcd-value-efi-content-and-file-presence-drift' {
    $before = New-PlatformFixture
    $bcdDrift = New-PlatformFixture $null 5 ('C' * 64) $true 'description changed'
    Assert-Throws { Assert-HostPlatformUnchanged $before $bcdDrift } 'Host boot/security/ESP state changed' | Out-Null
    $exportedEnumDrift = New-PlatformFixture
    $exportedEnumDrift.bcd.exportedAll.stdout = 'Windows Boot Manager' + "`n" + 'description differs in exported store'
    Assert-Throws { Assert-HostPlatformUnchanged $before $exportedEnumDrift } 'Host boot/security/ESP state changed' | Out-Null
    $efiDrift = New-PlatformFixture $null 5 ('E' * 64)
    Assert-Throws { Assert-HostPlatformUnchanged $before $efiDrift } 'Host boot/security/ESP state changed' | Out-Null
    $deleted = New-PlatformFixture $null 5 ('C' * 64) $false
    Assert-Throws { Assert-HostPlatformUnchanged $before $deleted } 'Host boot/security/ESP state changed' | Out-Null
    $classificationDrift = New-PlatformFixture
    $classificationDrift.esp[0].files[0].verification = 'file-sha256'
    Assert-Throws { Get-HostStablePlatformData $classificationDrift } 'supported verification' | Out-Null
    return 'Changed live BCD enumeration, exported-store enumeration, ordinary EFI hash, ordinary EFI path presence, and unsupported BCD provenance were rejected.'
}

Invoke-TestCase 'preflight-platform-failure-records-original-reads-and-never-mutates-adapters' {
    $workspace = Join-Path $script:testRoot 'run-workspace'
    $repoReports = Join-Path $workspace 'spoof-handoff\reports'
    [void][IO.Directory]::CreateDirectory($repoReports)
    [IO.File]::WriteAllText((Join-Path $repoReports 'client-originals.tsv'), "fixture`n")
    $nativeExe = Join-Path $workspace 'reader.exe'
    [IO.File]::WriteAllBytes($nativeExe, [Text.Encoding]::UTF8.GetBytes('synthetic test file'))
    $oldTemp = $env:TEMP
    $oldTmp = $env:TMP
    $script:MockReadbackModes.Clear()
    $script:MockPlatformSnapshotCount = 0
    $script:MockAdapterSnapshotCalls = 0
    $script:MockSetCalls = 0
    $script:MockRestoreCalls = 0
    $script:NativeExe = $nativeExe
    $script:VmGatePath = ''
    try { [void](Invoke-HostRun $workspace 'synthetic-worktree') }
    catch { $runFailure = $_.Exception.Message }
    finally {
        if ($null -eq $oldTemp) { Remove-Item Env:TEMP -ErrorAction SilentlyContinue } else { $env:TEMP = $oldTemp }
        if ($null -eq $oldTmp) { Remove-Item Env:TMP -ErrorAction SilentlyContinue } else { $env:TMP = $oldTmp }
    }
    Assert-True ($runFailure -match 'PREFLIGHT_FAILED_NO_ADAPTER_CHANGES' -and $runFailure -match 'synthetic live BCD snapshot failure') ('Host-run preflight failure was not preserved: ' + [string]$runFailure)
    Assert-True ($script:MockReadbackModes.Count -eq 2 -and @($script:MockReadbackModes | Where-Object { $_ -ceq 'Original' }).Count -eq 2) 'Original readback did not run both before and after the pre-mutation failure.'
    Assert-True ($script:MockPlatformSnapshotCount -eq 2) 'Expected a preflight failure plus post-failure read-only platform capture.'
    Assert-True ($script:MockAdapterSnapshotCalls -eq 0 -and $script:MockSetCalls -eq 0 -and $script:MockRestoreCalls -eq 0) 'Adapter snapshot/apply/restore was called after platform preflight failed.'
    $journalPath = Get-ChildItem -LiteralPath (Join-Path $repoReports 'nic-reader-gap-20261017\host-runs') -Filter journal.json -Recurse | Select-Object -First 1 -ExpandProperty FullName
    if (-not $journalPath) { throw 'Synthetic host-run journal was not persisted.' }
    $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
    Assert-True ($journal.state -ceq 'PREFLIGHT_FAILED_NO_ADAPTER_CHANGES' -and -not $journal.mutationStarted) ('Incorrect preflight state: ' + $journal.state)
    Assert-True ($journal.adapterSnapshots.Count -eq 0 -and $journal.adapterProgress.Count -eq 0) 'A preflight failure persisted adapter mutation intent.'
    Assert-True ($null -ne $journal.readbacks.preflightOriginal -and $null -ne $journal.readbacks.originalAfterRestore -and $null -eq $journal.readbacks.replacement) 'Preflight and final original readback evidence is incomplete or claims replacement.'
    Assert-True ($null -ne $journal.preflightOriginal -and $journal.failure.exception -match 'synthetic live BCD snapshot failure') 'Legacy root readback alias or original failure details were not persisted.'
    return ('No adapter actions were called; two Original reads recorded; state=' + $journal.state + '; journal=' + $journalPath)
}

$sourceHashAfter = (Get-FileHash -LiteralPath $productionPath -Algorithm SHA256).Hash.ToUpperInvariant()
$testHashAfter = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToUpperInvariant()
$failed = @($results | Where-Object { $_.status -ceq 'FAIL' }).Count
$report = [ordered]@{
    schema = $testSchema
    status = if ($failed -eq 0) { 'PASS' } else { 'FAIL' }
    createdUtc = [DateTime]::UtcNow.ToString('o')
    testRunId = $runId
    productionSource = [ordered]@{ path = $productionPath; sha256Before = $sourceHashBefore; sha256After = $sourceHashAfter }
    testSource = [ordered]@{ path = $PSCommandPath; sha256Before = $testHashBefore; sha256After = $testHashAfter }
    cases = $results.ToArray()
    passed = @($results | Where-Object { $_.status -ceq 'PASS' }).Count
    failed = $failed
    failureDetails = $failureDetails.ToArray()
    fixtureRoot = $testRoot
    hostActionsPerformed = $false
    environment = 'Function-only AST extraction and synthetic ESP/BCD/report fixtures; no real device, registry, BCD, ESP, network, VM, or administrator API calls.'
}
[IO.File]::WriteAllText($reportPath, (ConvertTo-Json -InputObject $report -Depth 20), [Text.UTF8Encoding]::new($false))
$reportHash = (Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToUpperInvariant()
$sumPath = Join-Path $reportRoot ('SHA256SUMS.' + $runId + '.txt')
[IO.File]::WriteAllText($sumPath, ($reportHash + '  ' + $reportPath + "`r`n" + $sourceHashAfter + '  ' + $productionPath + "`r`n" + $testHashAfter + '  ' + $PSCommandPath), [Text.UTF8Encoding]::new($false))
Write-Output (ConvertTo-Json -InputObject ([ordered]@{ status = $report.status; passed = $report.passed; failed = $report.failed; report = $reportPath; reportSha256 = $reportHash; sha256Manifest = $sumPath; sourceSha256 = $sourceHashAfter }) -Compress -Depth 4)
if ($failed -gt 0) { throw ('Host snapshot tests failed; see ' + $reportPath) }
