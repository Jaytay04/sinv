[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RunRoot,
    [string]$Action='Probe',
    [string]$ExpectedProbeVerdict='NOT_MET'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$script:Config = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('__LIFECYCLE_CONFIG_BASE64__')) | ConvertFrom-Json -ErrorAction Stop
$script:LifecycleAction = [string]$script:Config.action
$script:RunRootFull = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\')
$script:StateRoot = [IO.Path]::GetFullPath([string]$script:Config.stateRoot).TrimEnd('\')
$script:JournalPath = Join-Path $script:StateRoot 'journal.json'
$script:RecoveryDirectory = Join-Path $script:StateRoot 'recovery'
$script:EvidenceRoot = Join-Path $script:StateRoot 'evidence'
$script:CommandRecords = [Collections.Generic.List[object]]::new()
$script:Diagnostics = [Collections.Generic.List[string]]::new()
$script:Failure = $null
$script:Journal = $null
$script:Assets = @{}
$script:StorageModel = $null
$script:StorageRead = $null
$script:PathBScript = $null
$script:ESPRelativeMutable = @('EFI\Microsoft\Boot\BOOTSTAT.DAT')
$script:ESPRelativeExternallyManagedPattern = '(?i)^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[12]?)?$'

function Write-JsonAtomic {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)]$Value)
    $parent = Split-Path -Parent $Path
    [void][IO.Directory]::CreateDirectory($parent)
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temporary, (($Value | ConvertTo-Json -Depth 50) + "`r`n"), [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    } else {
        Move-Item -LiteralPath $temporary -Destination $Path
    }
}

function Write-RunFile {
    param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)
    if ($Name -notmatch '\A[A-Za-z0-9_.-]{1,80}\z') { throw 'Invalid run evidence file name.' }
    [IO.File]::WriteAllText((Join-Path $script:RunRootFull $Name), $Text, [Text.UTF8Encoding]::new($false))
}

function New-RunnerResultEnvelope {
    param(
        [Parameter(Mandatory = $true)][string]$RunRoot,
        [Parameter(Mandatory = $true)][ValidateSet('PASS', 'FAIL')][string]$Status,
        [Parameter(Mandatory = $true)][string]$Message
    )
    $runId = Split-Path -Leaf ([IO.Path]::GetFullPath($RunRoot).TrimEnd('\'))
    if ([string]::IsNullOrWhiteSpace($runId)) { throw 'Mailbox result RunId cannot be empty.' }
    return [ordered]@{ SchemaVersion = 1; RunId = $runId; Status = $Status; Message = $Message }
}

function New-LifecycleMetadata {
    param(
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $true)][string]$LifecycleId,
        [Parameter(Mandatory = $true)][string]$OutcomeStatus,
        [Parameter(Mandatory = $true)]$Result,
        $Journal,
        $RawEvidence
    )
    return [ordered]@{
        schema = 'combined-vm-lifecycle-metadata.v1'
        action = $Action
        lifecycleId = $LifecycleId
        outcomeStatus = $OutcomeStatus
        result = $Result
        journal = $Journal
        rawEvidence = $RawEvidence
        completedUtc = [DateTime]::UtcNow.ToString('o')
    }
}

function Assert-GuestGateArguments {
    param([string]$Action, [string]$ExpectedProbeVerdict)
    if ($Action -cne 'Probe' -or $ExpectedProbeVerdict -cne 'NOT_MET') {
        throw 'Lifecycle entrypoint requires the fixed guest-gate arguments -Action Probe -ExpectedProbeVerdict NOT_MET.'
    }
}

function Test-TestSigningStateMatches {
    param($Actual, $Expected)
    if ($null -eq $Actual -or $null -eq $Expected) { return $false }
    $actualExists = [bool]$Actual.exists
    $expectedExists = [bool]$Expected.exists
    if ($actualExists -ne $expectedExists) { return $false }
    if (-not $expectedExists) { return $true }
    return ([string]$Actual.value).ToLowerInvariant() -ceq ([string]$Expected.value).ToLowerInvariant()
}

function Test-TestSigningStateOff {
    param($State)
    if ($null -eq $State) { return $false }
    if (-not [bool]$State.exists) { return $true }
    return ([string]$State.value).ToLowerInvariant() -ceq 'no'
}

function Get-TestSigningRestoreArguments {
    param($Baseline)
    if ($null -eq $Baseline) { throw 'The captured testsigning baseline is missing.' }
    if (-not [bool]$Baseline.exists) { return @('/deletevalue', '{current}', 'testsigning') }
    switch (([string]$Baseline.value).ToLowerInvariant()) {
        'no' { return @('/set', '{current}', 'testsigning', 'off') }
        'yes' { return @('/set', '{current}', 'testsigning', 'on') }
        default { throw 'The captured testsigning baseline value is invalid.' }
    }
}

function Assert-PathUnder {
    param([string]$Root, [string]$Path, [string]$Label)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $pathFull = [IO.Path]::GetFullPath($Path)
    if (-not $pathFull.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) { throw "$Label is outside its permitted root." }
    return $pathFull
}

function Assert-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Lifecycle actions require an elevated guest PowerShell process.'
    }
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [int[]]$AcceptedExitCodes = @(0)
    )
    $priorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = @(& $Program @Arguments 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = [int]$LASTEXITCODE
    }
    finally { $ErrorActionPreference = $priorPreference }
    $record = [ordered]@{ program = $Program; arguments = @($Arguments); exitCode = $exitCode; output = @($lines) }
    $script:CommandRecords.Add($record)
    if ($AcceptedExitCodes -notcontains $exitCode) {
        throw ('Command failed: ' + $Program + ' ' + ($Arguments -join ' ') + '; exit=' + $exitCode + '; output=' + ($lines -join ' | '))
    }
    return $record
}

function Invoke-PowerShellAction {
    param([Parameter(Mandatory = $true)][scriptblock]$Operation)
    $output = @(& $Operation 2>&1 | ForEach-Object { [string]$_ })
    foreach ($line in $output) { $script:Diagnostics.Add($line) }
    return $output
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}

function Get-CurrentBootIdentity {
    $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
    if ($null -eq $os -or $null -eq $os.LastBootUpTime) { throw 'Windows boot identity could not be read.' }
    return [ordered]@{ lastBootUpTimeUtc = $os.LastBootUpTime.ToUniversalTime().ToString('o') }
}

function Assert-BootedAfterAction {
    param([Parameter(Mandatory = $true)][string]$ActionName)
    $action = Assert-JournalActionPassed $ActionName
    $actionStarted = [DateTime]::Parse([string]$action.startedUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    $bootIdentity = Get-CurrentBootIdentity
    $lastBoot = [DateTime]::Parse([string]$bootIdentity.lastBootUpTimeUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    if ($lastBoot -le $actionStarted) {
        throw "A fresh Windows boot after $ActionName is required; actionStartedUtc=$($actionStarted.ToString('o')) actualLastBootUtc=$($lastBoot.ToString('o'))."
    }
    return $bootIdentity
}

function Test-IsMutableEspPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return $script:ESPRelativeMutable -contains $Path
}

function Test-IsExternallyManagedEspPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return $Path -match $script:ESPRelativeExternallyManagedPattern
}

function Test-IsExcludedEspPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Test-IsMutableEspPath $Path) -or (Test-IsExternallyManagedEspPath $Path)
}

function New-EmbeddedLifecycleEvidence {
    $archivePath = Join-Path $script:StateRoot ('lifecycle-evidence.' + (Split-Path -Leaf $script:RunRootFull) + '.zip')
    $roots = @(
        [pscustomobject]@{ label = 'evidence'; path = $script:EvidenceRoot },
        [pscustomobject]@{ label = 'recovery'; path = $script:RecoveryDirectory },
        [pscustomobject]@{ label = 'path-b/inventory/captures'; path = (Join-Path $script:StateRoot 'path-b\inventory\captures') }
    )
    $files = [Collections.Generic.List[object]]::new()
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root.path -PathType Container)) { continue }
        foreach ($file in @(Get-ChildItem -LiteralPath $root.path -File -Recurse -Force -ErrorAction Stop | Sort-Object FullName)) {
            if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Lifecycle evidence includes a reparse point: $($file.FullName)" }
            $relative = $file.FullName.Substring(([IO.Path]::GetFullPath($root.path).TrimEnd('\') + '\').Length).Replace('\', '/')
            $files.Add([ordered]@{ sourcePath = $file.FullName; entryName = $root.label + '/' + $relative; bytes = [long]$file.Length; sha256 = Get-Sha256 $file.FullName })
        }
    }
    $stageReceipt = Join-Path $script:StateRoot 'path-b\inventory\path-b-stage.json'
    if (Test-Path -LiteralPath $stageReceipt -PathType Leaf) {
        $files.Add([ordered]@{ sourcePath = $stageReceipt; entryName = 'path-b/inventory/path-b-stage.json'; bytes = [long](Get-Item -LiteralPath $stageReceipt).Length; sha256 = Get-Sha256 $stageReceipt })
    }
    if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force -ErrorAction Stop }
    $archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in $files) {
            $entry = $archive.CreateEntry([string]$file.entryName, [IO.Compression.CompressionLevel]::Optimal)
            $input = [IO.File]::OpenRead([string]$file.sourcePath)
            $output = $entry.Open()
            try { $input.CopyTo($output) }
            finally { $output.Dispose(); $input.Dispose() }
        }
    }
    finally { $archive.Dispose() }
    try {
        $archiveBytes = [IO.File]::ReadAllBytes($archivePath)
        if ($archiveBytes.Length -gt (4.5MB)) { throw ('Embedded lifecycle evidence archive exceeds 4.5 MiB: ' + $archiveBytes.Length) }
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $archiveHash = ([BitConverter]::ToString($sha.ComputeHash($archiveBytes))).Replace('-', '') }
        finally { $sha.Dispose() }
        return [ordered]@{ encoding = 'base64-zip'; byteLength = $archiveBytes.Length; sha256 = $archiveHash; files = @($files.ToArray() | ForEach-Object { [ordered]@{ entryName = $_.entryName; bytes = $_.bytes; sha256 = $_.sha256 } }); contentBase64 = [Convert]::ToBase64String($archiveBytes) }
    }
    finally {
        if (Test-Path -LiteralPath $archivePath -PathType Leaf) { Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue }
    }
}

function Initialize-GuestAssets {
    [void][IO.Directory]::CreateDirectory($script:StateRoot)
    foreach ($asset in $script:Config.assets) {
        $name = [string]$asset.name
        $hash = ([string]$asset.sha256).ToUpperInvariant()
        $unc = [string]$asset.uncPath
        if ($name -notmatch '\A[A-Za-z0-9_.-]+\z' -or $hash -notmatch '\A[A-F0-9]{64}\z' -or
            $unc -cnotmatch ('\A' + [regex]::Escape([string]$script:Config.shareRoot) + '\\[0-9a-f]{64}\\' + [regex]::Escape($name) + '\z')) {
            throw "Asset pin is malformed: $name"
        }
        if (-not (Test-Path -LiteralPath $unc -PathType Leaf)) { throw "Read-only VM asset is unavailable: $unc" }
        $sourceHash = Get-Sha256 $unc
        if ($sourceHash -cne $hash) { throw "Shared VM asset hash mismatch: $name expected=$hash actual=$sourceHash" }
        $targetDirectory = Join-Path (Join-Path $script:StateRoot 'assets') $hash.ToLowerInvariant()
        [void][IO.Directory]::CreateDirectory($targetDirectory)
        $target = Join-Path $targetDirectory $name
        if (-not (Test-Path -LiteralPath $target -PathType Leaf) -or (Get-Sha256 $target) -cne $hash) {
            Copy-Item -LiteralPath $unc -Destination $target -Force -ErrorAction Stop
        }
        $targetHash = Get-Sha256 $target
        if ($targetHash -cne $hash) { throw "Guest cache hash mismatch: $name expected=$hash actual=$targetHash" }
        $script:Assets[$name] = $target
    }
    $script:StorageRead = [string]$script:Assets['storage-read.ps1']
    $script:StorageModel = [string]$script:Assets['storage-model.psm1']
    $script:PathBScript = [string]$script:Assets['path-b-collector.ps1']
}

function Initialize-PathBLibrary {
    $loaderBuild = Join-Path $script:StateRoot 'path-b\loader'
    $inventoryBuild = Join-Path $script:StateRoot 'path-b\inventory'
    [void][IO.Directory]::CreateDirectory((Join-Path $loaderBuild 'efi'))
    [void][IO.Directory]::CreateDirectory((Join-Path $inventoryBuild 'efi'))
    Copy-Item -LiteralPath ([string]$script:Assets['inventory_loader.efi']) -Destination (Join-Path $loaderBuild 'inventory_loader.efi') -Force
    Copy-Item -LiteralPath ([string]$script:Assets['ib-install.exe']) -Destination (Join-Path $loaderBuild 'ib-install.exe') -Force
    Copy-Item -LiteralPath ([string]$script:Assets['efi_inventory.exe']) -Destination (Join-Path $inventoryBuild 'efi_inventory.exe') -Force
    Copy-Item -LiteralPath ([string]$script:Assets['smbios-inventory.efi']) -Destination (Join-Path $inventoryBuild 'efi\inventory.efi') -Force
    foreach ($copy in @(
        [pscustomobject]@{ path = (Join-Path $loaderBuild 'inventory_loader.efi'); name = 'inventory_loader.efi' },
        [pscustomobject]@{ path = (Join-Path $loaderBuild 'ib-install.exe'); name = 'ib-install.exe' },
        [pscustomobject]@{ path = (Join-Path $inventoryBuild 'efi_inventory.exe'); name = 'efi_inventory.exe' },
        [pscustomobject]@{ path = (Join-Path $inventoryBuild 'efi\inventory.efi'); name = 'smbios-inventory.efi' }
    )) {
        $expected = @($script:Config.assets | Where-Object { [string]$_.name -ceq $copy.name })
        if ($expected.Count -ne 1 -or (Get-Sha256 $copy.path) -cne ([string]$expected[0].sha256).ToUpperInvariant()) {
            throw "Path-B local build input is not pinned: $($copy.name)"
        }
    }
}

function Initialize-LifecycleDependencyScope {
    foreach ($requiredCommand in @(
        'Get-StorageHostTestSigningElement',
        'Mount-Esp', 'Dismount-Esp', 'Stage-ToRoot', 'Invoke-Arm', 'Invoke-Collect',
        'Remove-OwnedStage', 'Read-Receipt', 'Get-MountedStageStatus'
    )) {
        if (-not (Get-Command -Name $requiredCommand -ErrorAction SilentlyContinue)) {
            throw "Required lifecycle dependency was not imported into the action scope: $requiredCommand"
        }
    }
    if ($null -eq ('StorageHostReadNative' -as [type])) {
        throw 'StorageHostReadNative type was not compiled into the action scope.'
    }
}

function Get-IdentitySnapshot {
    $computer = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    $product = Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop
    if ($null -eq $computer -or $null -eq $product) { throw 'VM identity CIM query returned no record.' }
    $uuid = [Guid]::Empty
    if (-not [Guid]::TryParse([string]$product.UUID, [ref]$uuid)) { throw 'VM SMBIOS UUID is invalid.' }
    $diskRows = @(Get-CimInstance -ClassName Win32_DiskDrive -ErrorAction Stop | Where-Object { [string]$_.DeviceID -ceq '\\.\PHYSICALDRIVE0' })
    if ($diskRows.Count -ne 1) { throw "Expected exactly one VM PHYSICALDRIVE0 record; actual=$($diskRows.Count)." }
    $disk = Get-Disk -Number 0 -ErrorAction Stop
    $hardwareProperty = Get-PnpDeviceProperty -InstanceId ([string]$diskRows[0].PNPDeviceID) -KeyName 'DEVPKEY_Device_HardwareIds' -ErrorAction Stop
    $hardware = @($hardwareProperty.Data | ForEach-Object { [string]$_ })
    $version = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
    return [ordered]@{
        manufacturer = [string]$computer.Manufacturer
        model = [string]$computer.Model
        uuid = $uuid.ToString('D').ToUpperInvariant()
        diskNumber = [int]$disk.Number
        diskPnp = [string]$diskRows[0].PNPDeviceID
        diskIsBoot = [bool]$disk.IsBoot
        diskIsSystem = [bool]$disk.IsSystem
        diskHardwareIds = @($hardware)
        diskWmiSerial = ([string]$diskRows[0].SerialNumber).Trim()
        windowsBuild = [int]$version.CurrentBuildNumber
        windowsUbr = [int]$version.UBR
    }
}

function Assert-ExpectedIdentity {
    param([switch]$AllowPatchedSmbios)
    $identity = Get-IdentitySnapshot
    $original = $script:Config.vmIdentity.original
    $patched = $script:Config.vmIdentity.patched
    $usePatched = $AllowPatchedSmbios.IsPresent
    $expectedManufacturer = if ($usePatched) { [string]$patched.manufacturer } else { [string]$original.manufacturer }
    $expectedModel = if ($usePatched) { [string]$patched.model } else { [string]$original.model }
    $expectedUuid = if ($usePatched) { [string]$patched.uuid } else { [string]$original.uuid }
    if ([string]$identity.manufacturer -cne $expectedManufacturer -or [string]$identity.model -cne $expectedModel -or
        [string]$identity.uuid -cne $expectedUuid) {
        throw ('VM identity guard failed: expected=' + $expectedManufacturer + '/' + $expectedModel + '/' + $expectedUuid +
            ' actual=' + $identity.manufacturer + '/' + $identity.model + '/' + $identity.uuid)
    }
    if ([string]$identity.diskPnp -cne [string]$script:Config.disk.pnpDeviceId -or
        $identity.diskNumber -ne [int]$script:Config.disk.number -or $identity.diskIsBoot -ne $true -or $identity.diskIsSystem -ne $true -or
        [int]$identity.windowsBuild -ne [int]$script:Config.disk.windowsBuild -or [int]$identity.windowsUbr -ne [int]$script:Config.disk.windowsUbr) {
        throw 'VM disk identity, boot/system role, or guest Windows build differs from the authenticated fixture.'
    }
    $expectedHardware = @($script:Config.disk.hardwareIds | ForEach-Object { [string]$_ } | Sort-Object -CaseSensitive)
    $actualHardware = @($identity.diskHardwareIds | Sort-Object -CaseSensitive)
    if (($expectedHardware -join "`n") -cne ($actualHardware -join "`n")) { throw 'VM disk hardware ID set differs from the authenticated fixture.' }
    return $identity
}

function Get-BcdSnapshot {
    $bcdPath = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
    $current = Invoke-NativeCommand $bcdPath @('/enum', '{current}', '/v')
    $firmware = Invoke-NativeCommand $bcdPath @('/enum', 'firmware', '/v')
    $all = Invoke-NativeCommand $bcdPath @('/enum', 'all', '/v')
    $testSigning = Get-StorageHostTestSigningElement -BcdLines @($current.output)
    return [ordered]@{
        currentExitCode = [int]$current.exitCode
        currentLines = @($current.output)
        testSigning = $testSigning
        firmwareExitCode = [int]$firmware.exitCode
        firmwareLines = @($firmware.output)
        firmwareSha256 = ([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes(($firmware.output -join "`r`n")))).Replace('-', ''))
        allExitCode = [int]$all.exitCode
        allLines = @($all.output)
    }
}

function Get-TrustMembership {
    param([Parameter(Mandatory = $true)][string]$Thumbprint)
    $membership = [ordered]@{}
    foreach ($name in @('Root', 'TrustedPublisher')) {
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($name, 'LocalMachine')
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
        try { $membership[$name] = @($store.Certificates | Where-Object { $_.Thumbprint -ceq $Thumbprint }).Count -gt 0 }
        finally { $store.Close() }
    }
    return $membership
}

function Get-FirmwareVariable {
    param([Parameter(Mandatory = $true)][string]$Name)
    return [CombinedVmFirmware]::Read($Name, '{8be4df61-93ca-11d2-aa0d-00e098032b8c}')
}

function Format-FirmwareReadStatus {
    param([Parameter(Mandatory = $true)]$Variable)
    return ('win32Error=' + [string]$Variable.win32Error + ' win32ErrorText=' + [string]$Variable.win32ErrorText + ' privilegeError=' + [string]$Variable.privilegeError + ' privilegeRestoreError=' + [string]$Variable.privilegeRestoreError + ' bytesHex=' + [string]$Variable.bytesHex)
}

function Test-SecureBootOffSnapshot {
    param($Snapshot)
    if ($null -eq $Snapshot -or $null -eq $Snapshot.variable) { return $false }
    $variable = $Snapshot.variable
    return ($Snapshot.enabled -eq $false -and $variable.supported -eq $true -and [int]$variable.win32Error -eq 0 -and [int]$variable.size -eq 1 -and [string]$variable.bytesHex -ceq '00')
}

function Format-SecureBootStatus {
    param([Parameter(Mandatory = $true)]$Snapshot)
    $variable = $Snapshot.variable
    $propertyNames = @($variable.PSObject.Properties | ForEach-Object { [string]$_.Name })
    return ('enabled=' + [string]$Snapshot.enabled + ' variableProperties=' + ($propertyNames -join ',') + ' supported=' + [string]$variable.supported + ' size=' + [string]$variable.size + ' bytesHex=' + [string]$variable.bytesHex + ' win32Error=' + [string]$variable.win32Error + ' win32ErrorText=' + [string]$variable.win32ErrorText)
}

function Get-EspTreeSnapshot {
    $mount = Mount-Esp
    try {
        $files = [Collections.Generic.List[object]]::new()
        foreach ($file in @(Get-ChildItem -LiteralPath $mount.EspRoot -File -Recurse -Force -ErrorAction Stop | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($mount.EspRoot.Length).TrimStart('\').Replace('/', '\')
            $sha = $null
            $note = $null
            if ($relative -ieq 'EFI\Microsoft\Boot\BCD') { $note = 'Windows-managed BCD; semantic state is captured through bcdedit output and export evidence, not a raw file hash.' }
            elseif (Test-IsExternallyManagedEspPath $relative) { $note = 'Windows-managed BCD transaction log; raw hash is excluded from pristine comparison.' }
            elseif (Test-IsMutableEspPath $relative) { $note = 'Windows/firmware mutable boot bookkeeping; raw hash is excluded from pristine comparison.' }
            else { $sha = Get-Sha256 $file.FullName }
            $files.Add([ordered]@{ path = $relative; bytes = [long]$file.Length; sha256 = $sha; note = $note })
        }
        return [ordered]@{ volumeGuid = [string]$mount.VolumeGuid; bootManagerSha256 = Get-Sha256 (Join-Path $mount.EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi'); files = @($files.ToArray()) }
    }
    finally { Dismount-Esp $mount }
}

function Get-SecureBootSnapshot {
    $secureBoot = $null
    try { $secureBoot = [bool](Confirm-SecureBootUEFI -ErrorAction Stop) }
    catch { throw ('Unable to query guest Secure Boot: ' + $_.Exception.Message) }
    $variable = [StorageHostReadNative]::ReadSecureBootVariable()
    if ($variable.supported -ne $true -or [int]$variable.win32Error -ne 0 -or [int]$variable.size -ne 1 -or [string]$variable.bytesHex -notmatch '\A[0-9A-Fa-f]{2}\z') {
        throw ('SecureBoot UEFI variable read returned an invalid byte record: supported=' + [string]$variable.supported + ' size=' + [string]$variable.size + ' bytesHex=' + [string]$variable.bytesHex + ' win32Error=' + [string]$variable.win32Error + ' win32ErrorText=' + [string]$variable.win32ErrorText)
    }
    $propertyNames = @($variable.PSObject.Properties | ForEach-Object { [string]$_.Name })
    return [ordered]@{ enabled = $secureBoot; variable = $variable; variablePropertyNames = $propertyNames; variableValuePropertyPresent = ($propertyNames -ccontains 'value') }
}

function Get-SmbiosReadback {
    param([Parameter(Mandatory = $true)][string]$OutputPath)
    $OutputPath = Assert-PathUnder -Root $script:StateRoot -Path $OutputPath -Label 'SMBIOS capture output'
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $OutputPath))
    $exe = [string]$script:Assets['smbios_windows_readback.exe']
    $result = Invoke-NativeCommand $exe @('--capture', '--output', $OutputPath)
    if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) { throw 'SMBIOS reader completed without creating its output file.' }
    $capture = Get-Content -LiteralPath $OutputPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($capture.schema -cne 'smbios-windows-capture.v1' -or $capture.result -cne 'PASS') { throw 'SMBIOS readback schema/result is invalid.' }
    foreach ($field in @('system_serial', 'system_vendor', 'system_product', 'board_serial', 'board_vendor', 'board_product', 'system_uuid')) {
        $value = $capture.fields.PSObject.Properties[$field]
        if ($null -eq $value -or $value.Value.nativeMatchesWmi -ne $true -or
            [string]::IsNullOrWhiteSpace([string]$value.Value.observedNative) -or
            [string]$value.Value.observedNative -cne [string]$value.Value.observedWmi) {
            throw "SMBIOS native/WMI readback mismatch or missing value: $field"
        }
    }
    return $capture
}

function Assert-SmbiosMode {
    param([Parameter(Mandatory = $true)]$Capture, [ValidateSet('Original', 'Replacement')][string]$Mode)
    foreach ($field in @('system_serial', 'system_vendor', 'system_product', 'board_serial', 'board_vendor', 'board_product', 'system_uuid')) {
        $expected = if ($Mode -ceq 'Replacement') { [string]$script:Config.smbios.replacements.$field } else { [string]$script:Config.smbios.originals.$field }
        $value = $Capture.fields.$field
        if ([string]$value.observedNative -cne $expected) { throw "SMBIOS native $field expected=$expected actual=$($value.observedNative)" }
        if ([string]$value.observedWmi -cne $expected) { throw "SMBIOS WMI $field expected=$expected actual=$($value.observedWmi)" }
    }
}

function Get-DiskReadback {
    $identity = Get-IdentitySnapshot
    if ([string]$identity.diskPnp -cne [string]$script:Config.disk.pnpDeviceId) { throw 'Disk PnP identity changed during the lifecycle.' }
    $native = [StorageHostReadNative]::ReadStorageSerial('\\.\PHYSICALDRIVE0')
    $wmiRows = @(Get-CimInstance -ClassName Win32_DiskDrive -ErrorAction Stop | Where-Object { [string]$_.DeviceID -ceq '\\.\PHYSICALDRIVE0' })
    if ($wmiRows.Count -ne 1) { throw "Expected one PHYSICALDRIVE0 WMI row; actual=$($wmiRows.Count)." }
    $wmi = ([string]$wmiRows[0].SerialNumber).Trim()
    $stackRecord = Invoke-NativeCommand (Join-Path $env:SystemRoot 'System32\pnputil.exe') @('/enum-devices', '/instanceid', [string]$script:Config.disk.pnpDeviceId, '/stack')
    $stack = @(Get-StorageHostStackLines -PnpUtilOutput @($stackRecord.output))
    $filterInStack = Test-StorageHostStackContainsFilter -PnpUtilOutput @($stackRecord.output)
    $services = @(Get-CimInstance -ClassName Win32_SystemDriver -Filter "Name='client_storage_filter'" -ErrorAction Stop)
    $servicePath = 'HKLM:\SYSTEM\CurrentControlSet\Services\client_storage_filter'
    $serviceRegistry = if (Test-Path -LiteralPath $servicePath) { Get-ItemProperty -LiteralPath $servicePath -ErrorAction Stop } else { $null }
    $fileIoPath = Join-Path $script:StateRoot ('file-io.' + [Guid]::NewGuid().ToString('N') + '.bin')
    $bytes = New-Object byte[] (1MB)
    [Random]::new(61417).NextBytes($bytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $expectedHash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
        $stream = [IO.FileStream]::new($fileIoPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None, 4096, [IO.FileOptions]::WriteThrough)
        try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        $actualHash = Get-Sha256 $fileIoPath
    }
    finally { $sha.Dispose(); if (Test-Path -LiteralPath $fileIoPath) { Remove-Item -LiteralPath $fileIoPath -Force } }
    return [ordered]@{
        nativeSerial = $native
        wmiSerial = $wmi
        nativeMatchesWmi = ($native -ceq $wmi)
        filterInStack = [bool]$filterInStack
        stack = @($stack)
        services = @($services | Select-Object Name, State, StartMode, PathName)
        registry = if ($null -ne $serviceRegistry) { [ordered]@{ start = $serviceRegistry.Start; type = $serviceRegistry.Type; imagePath = $serviceRegistry.ImagePath } } else { $null }
        fileIo = [ordered]@{ bytes = $bytes.Length; expectedSha256 = $expectedHash; actualSha256 = $actualHash; match = ($expectedHash -ceq $actualHash) }
    }
}

function Assert-DiskMode {
    param([Parameter(Mandatory = $true)]$Readback, [ValidateSet('Original', 'Replacement')][string]$Mode, [ValidateSet('Present', 'Absent', 'Any')][string]$FilterState = 'Any')
    $expected = if ($Mode -ceq 'Replacement') { [string]$script:Config.disk.replacement } else { [string]$script:Config.disk.originalNative }
    if ([string]$Readback.nativeSerial -cne $expected) { throw "Storage IOCTL serial expected=$expected actual=$($Readback.nativeSerial)" }
    if ([string]$Readback.wmiSerial -cne $expected) { throw "Storage WMI serial expected=$expected actual=$($Readback.wmiSerial)" }
    if ($Readback.nativeMatchesWmi -ne $true) { throw 'Storage native IOCTL and WMI values differ.' }
    if ($Readback.fileIo.match -ne $true) { throw ('Disk file I/O probe hash mismatch: expected=' + $Readback.fileIo.expectedSha256 + ' actual=' + $Readback.fileIo.actualSha256) }
    $isRunning = @($Readback.services | Where-Object { [string]$_.State -ceq 'Running' -and [string]$_.StartMode -ceq 'Boot' }).Count -gt 0
    if ($FilterState -ceq 'Present') {
        if (-not $isRunning -or -not $Readback.filterInStack -or $null -eq $Readback.registry -or [int]$Readback.registry.start -ne 0) {
            $registryStart = if ($null -ne $Readback.registry) { [string]$Readback.registry.start } else { '<missing>' }
            throw ('Storage filter expected running/boot-start/in-stack; actual running=' + $isRunning + ' inStack=' + [bool]$Readback.filterInStack + ' registryStart=' + $registryStart)
        }
    }
    if ($FilterState -ceq 'Absent' -and ($Readback.filterInStack -or $isRunning -or @($Readback.services).Count -gt 0 -or $null -ne $Readback.registry)) {
        throw ('Storage filter expected absent; actual inStack=' + [bool]$Readback.filterInStack + ' services=' + @($Readback.services).Count + ' registry=' + ($null -ne $Readback.registry))
    }
}

function Get-BaselineJournal {
    if ($null -ne $script:Journal) { $journal = $script:Journal }
    else {
        if (-not (Test-Path -LiteralPath $script:JournalPath -PathType Leaf)) { throw 'Lifecycle journal is missing; run CaptureOriginal first.' }
        $journal = Get-Content -LiteralPath $script:JournalPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    }
    if ($journal.schema -cne 'combined-vm-lifecycle-journal.v1' -or
        [string]$journal.lifecycleId -cne [string]$script:Config.lifecycleId -or
        [string]$journal.vmUuid -cne [string]$script:Config.vmIdentity.original.uuid -or
        [string]$journal.fixturePins.storageFixtureManifestSha256 -cne [string]$script:Config.fixturePins.storageFixtureManifestSha256 -or
        [string]$journal.fixturePins.smbiosFixtureManifestSha256 -cne [string]$script:Config.fixturePins.smbiosFixtureManifestSha256) {
        throw 'Lifecycle journal does not match this run ID, VM UUID, and fixture pin set.'
    }
    return $journal
}

function Assert-JournalActionPassed {
    param([Parameter(Mandatory = $true)][string]$Name)
    $journal = Get-BaselineJournal
    $matches = @($journal.actions | Where-Object { [string]$_.name -ceq $Name -and [string]$_.status -ceq 'PASS' })
    if ($matches.Count -ne 1) { throw "Lifecycle action '$Name' must have exactly one PASS record before this step." }
    return $matches[0]
}

function Get-CleanupActionName {
    $journal = Get-BaselineJournal
    $records = @($journal.actions | Where-Object {
        [string]$_.name -in @('CleanupSmbiosInventory', 'VerifyCorrectedCollectorCleanup')
    })
    if ($records.Count -ne 1 -or [string]$records[0].status -cne 'PASS') {
        throw 'Exactly one completed PASS record for the legacy cleanup or corrected-collector cleanup bridge is required.'
    }
    return [string]$records[0].name
}

function Assert-CorrectedCollectorCaptureFacts {
    param([Parameter(Mandatory = $true)]$Capture)

    $cleanupProperty = $Capture.PSObject.Properties['cleanup']
    if ($null -eq $cleanupProperty -or $null -eq $cleanupProperty.Value) {
        throw 'Authenticated CaptureActive metadata is missing its cleanup record.'
    }
    $cleanup = $cleanupProperty.Value
    foreach ($field in @('attempted', 'result', 'pathBStageRemoved', 'trackedEspBaselineRestored', 'errors')) {
        if ($null -eq $cleanup.PSObject.Properties[$field]) {
            throw "Authenticated CaptureActive cleanup record is missing '$field'."
        }
    }

    if ([string]$Capture.schema -cne 'combined-vm-fixture-correction-action.v1' -or
        [string]$Capture.internalAction -cne 'CaptureActive' -or
        [string]$Capture.mode -cne 'corrected-collector' -or
        [string]$Capture.status -cne 'PASS' -or
        [string]$Capture.pathBAction -cne 'Collect' -or
        $cleanup.attempted -ne $true -or
        [string]$cleanup.result -cne 'OWNED_ENTRIES_STAGE_AND_TRACKED_ESP_RESTORED' -or
        $cleanup.pathBStageRemoved -ne $true -or
        $cleanup.trackedEspBaselineRestored -ne $true -or
        @($cleanup.errors).Count -ne 0) {
        throw 'Authenticated CaptureActive metadata does not show a completed corrected-collector capture and cleanup.'
    }

    $expectedUuid = [string]$script:Config.vmIdentity.patched.uuid
    if ([string]$Capture.vm.uuid -cne $expectedUuid) { throw 'CaptureActive VM UUID differs from this lifecycle.' }
    if ([string]$Capture.journal.state -cne 'CAPTURED_AND_CLEANED' -or
        [string]$Capture.journal.sha256 -cnotmatch '\A[A-Fa-f0-9]{64}\z' -or
        [string]::IsNullOrWhiteSpace([string]$Capture.journal.path)) {
        throw 'CaptureActive metadata is missing the terminal fixture-journal identity or hash.'
    }

    $captureBoot = [string]$Capture.bootIdentity.lastBootUpTimeUtc
    $captureBootParsed = [DateTime]::MinValue
    if (-not [DateTime]::TryParse($captureBoot, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind, [ref]$captureBootParsed)) {
        throw 'CaptureActive Windows boot identity is missing or malformed.'
    }
    $bootMaterial = [Text.Encoding]::UTF8.GetBytes([string]$script:Config.vmIdentity.original.uuid + '|' + $captureBootParsed.ToUniversalTime().ToString('o'))
    $bootSha = [Security.Cryptography.SHA256]::Create()
    try { $expectedBootIdHex = ([BitConverter]::ToString($bootSha.ComputeHash($bootMaterial))).Replace('-', '') }
    finally { $bootSha.Dispose(); [Array]::Clear($bootMaterial, 0, $bootMaterial.Length) }
    if ([string]$Capture.bootIdentity.bootId -cne $expectedBootIdHex) { throw 'CaptureActive boot token does not bind to its VM UUID and Windows LastBootUpTime.' }

    if ($Capture.firmware.secureBoot -ne $false -or
        $null -eq $Capture.firmware.secureBootVariable -or
        [int]$Capture.firmware.secureBootVariable.win32Error -ne 0 -or
        [int]$Capture.firmware.secureBootVariable.size -ne 1 -or
        [string]$Capture.firmware.secureBootVariable.bytesHex -cne '00' -or
        $null -eq $Capture.firmware.bootOrderBefore -or $null -eq $Capture.firmware.bootOrderAfter -or
        [string]$Capture.firmware.bootOrderBefore.bytesBase64 -cne [string]$Capture.firmware.bootOrderAfter.bytesBase64 -or
        $Capture.firmware.bootNextAfter.supported -ne $false -or [int]$Capture.firmware.bootNextAfter.win32Error -ne 203) {
        throw 'CaptureActive firmware evidence does not show Secure Boot OFF, unchanged BootOrder, and absent BootNext.'
    }

    if ($null -eq $Capture.efi -or
        [string]$Capture.efi.phase -cne 'ready_to_chainload' -or
        [string]$Capture.efi.status -cne '0x0000000000000000' -or
        [string]$Capture.efi.statusSource -cne 'prelude.report.child_load_status' -or
        [string]$Capture.efi.statusPhase -cne 'LoadImage(loader.efi)' -or
        $Capture.efi.startStatusObserved -ne $false) {
        throw 'CaptureActive EFI evidence is missing its observed loader LoadImage status or overstates StartImage.'
    }

    $patch = $Capture.collectorReport
    if ($null -eq $patch -or [string]$patch.status -cne 'PASS' -or
        [string]$patch.result -cne 'applied' -or [int]$patch.mask -ne 127 -or
        [string]$patch.buildId -cne [string]$script:Config.fixturePins.smbiosBuildIdentity -or
        [string]$patch.efiStatus -cne '0x0000000000000000' -or
        [string]$patch.efiStatusSource -cne 'prelude.report.child_load_status' -or
        [string]$patch.efiStatusPhase -cne 'LoadImage(loader.efi)' -or
        $patch.startStatusObserved -ne $false) {
        throw 'CaptureActive collector report is not the exact pinned seven-field PASS build.'
    }
    foreach ($field in @('system_serial','system_vendor','system_product','board_serial','board_vendor','board_product','system_uuid')) {
        $binding = $patch.values.PSObject.Properties['SMBIOS:' + $field]
        if ($null -eq $binding -or [string]$binding.Value -cne [string]$script:Config.smbios.replacements.$field) {
            throw "CaptureActive collector replacement mismatch for $field."
        }
        $read = $Capture.windowsReadback.fields.PSObject.Properties[$field]
        if ($null -eq $read -or [string]$read.Value.native -cne [string]$script:Config.smbios.replacements.$field -or
            [string]$read.Value.wmi -cne [string]$script:Config.smbios.replacements.$field -or
            $read.Value.nativeMatchesWmi -ne $true) {
            throw "CaptureActive fresh native/WMI readback mismatch for $field."
        }
    }
    if ([string]$Capture.windowsReadback.expectedMode -cne 'replacement') { throw 'CaptureActive readback did not declare replacement mode.' }
    return $true
}

function Assert-CorrectedFixtureJournal {
    param(
        [Parameter(Mandatory = $true)]$FixtureJournal,
        [Parameter(Mandatory = $true)]$Capture,
        [Parameter(Mandatory = $true)][string]$ExpectedLifecycleId,
        [Parameter(Mandatory = $true)][string]$ExpectedUuid
    )
    if ([string]$FixtureJournal.schema -cne 'combined-vm-fixture-correction-journal.v2' -or
        [string]$FixtureJournal.lifecycleId -cne $ExpectedLifecycleId -or
        [string]$FixtureJournal.vmUuid -cne $ExpectedUuid -or
        [string]$FixtureJournal.mode -cne 'corrected-collector' -or
        [string]$FixtureJournal.state -cne 'CAPTURED_AND_CLEANED' -or
        $null -ne $FixtureJournal.pendingMutation -or
        [string]$FixtureJournal.pathBReceiptSha256 -cnotmatch '\A[A-Fa-f0-9]{64}\z' -or
        @($FixtureJournal.completedMutations | Where-Object { [string]$_ -ceq 'capture_pathb_collect' }).Count -ne 1 -or
        @($FixtureJournal.completedMutations | Where-Object { [string]$_ -ceq 'capture_remove_custom_boot_entry' }).Count -ne 1) {
        throw 'Corrected-collector fixture journal does not show one exact completed collection and owned-entry cleanup.'
    }
    if ([string]$FixtureJournal.bootManagerSha256 -cne [string]$Capture.esp.afterCapture.bootManagerSha256 -or
        [string]$FixtureJournal.bootOrderBefore -cne [string]$Capture.firmware.bootOrderBefore.bytesBase64 -or
        [string]$FixtureJournal.bootOrderAttributes -cne [string]$Capture.firmware.bootOrderBefore.attributes) {
        throw 'Corrected-collector journal does not match its authenticated CaptureActive receipt.'
    }
    $captureBoot = [DateTime]::Parse([string]$Capture.bootIdentity.lastBootUpTimeUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    $started = [DateTime]::Parse([string]$FixtureJournal.captureStartedUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    $completed = [DateTime]::Parse([string]$FixtureJournal.cleanupCompletedUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    if ($captureBoot -gt $started -or $completed -lt $started -or
        [string]$FixtureJournal.modeFlag -cne 'corrected-collector') {
        throw 'Corrected-collector fixture journal timestamps or one-shot mode do not match the capture.'
    }
    return $true
}

function Save-Journal {
    Write-JsonAtomic -Path $script:JournalPath -Value $script:Journal
}

function Start-JournalAction {
    param([Parameter(Mandatory = $true)]$Journal)
    $script:Journal = $Journal
    $actions = [Collections.Generic.List[object]]::new()
    foreach ($action in @($script:Journal.actions)) { $actions.Add($action) }
    if (@($actions | Where-Object { [string]$_.name -ceq $script:LifecycleAction }).Count -gt 0) {
        throw "Lifecycle action '$script:LifecycleAction' has already been attempted; do not repeat it in this state."
    }
    $actions.Add([ordered]@{ name = $script:LifecycleAction; state = 'started'; startedUtc = [DateTime]::UtcNow.ToString('o'); bootIdentityBefore = Get-CurrentBootIdentity; completedUtc = $null; status = 'IN_PROGRESS'; details = $null; error = $null })
    $script:Journal.actions = @($actions.ToArray())
    Save-Journal
}

function Complete-JournalAction {
    param([string]$Status, $Details)
    $actions = @($script:Journal.actions)
    if ($actions.Count -eq 0) { throw 'Lifecycle journal has no started action to complete.' }
    $last = $actions[$actions.Count - 1]
    $last.state = 'completed'
    $last.status = $Status
    $last.completedUtc = [DateTime]::UtcNow.ToString('o')
    $last.details = $Details
    $script:Journal.actions = $actions
    Save-Journal
}

function Get-DriverOwnership {
    param([ValidateSet('transparent', 'enabled')][string]$Mode)
    $path = Join-Path $script:StateRoot ('driver-' + $Mode + '.json')
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "No owned $Mode package record exists." }
    $record = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -ErrorAction Stop
    if ($record.schema -cne 'combined-vm-driver-ownership.v1' -or $record.lifecycleId -cne [string]$script:Config.lifecycleId -or
        $record.vmUuid -cne [string]$script:Config.vmIdentity.original.uuid -or $record.mode -cne $Mode -or
        [string]$record.publishedName -cnotmatch '^oem[0-9]+\.inf$') { throw "Owned $Mode package record is invalid." }
    return $record
}

function Install-DriverPackage {
    param([ValidateSet('transparent', 'enabled')][string]$Mode)
    if ([int]$script:Config.packages.$Mode.startType -ne 0) { throw "The $Mode fixture is not boot-start (StartType=0)." }
    $existing = @(Get-CimInstance -ClassName Win32_SystemDriver -Filter "Name='client_storage_filter'" -ErrorAction Stop)
    if ($existing.Count -gt 0 -or (Test-Path -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\client_storage_filter')) {
        throw 'A storage filter service already exists; refusing to overlay or replace it.'
    }
    $profile = $script:Config.packages.$Mode
    $packageDirectory = Join-Path (Join-Path $script:StateRoot 'packages') $Mode
    [void][IO.Directory]::CreateDirectory($packageDirectory)
    foreach ($file in $profile.files) {
        $name = [string]$file.name
        if ($name -notin @('client_storage_filter.inf', 'client_storage_filter.sys', 'client_storage_filter.cat')) { throw "Unexpected $Mode package member: $name" }
        $source = [string]$file.uncPath
        $asset = @($script:Config.assets | Where-Object { [string]$_.name -ceq [string]$file.sourceName })
        if ($asset.Count -ne 1 -or [string]$asset[0].sha256 -cne [string]$file.sha256) { throw "Package asset pin is inconsistent: $name" }
        $cached = [string]$script:Assets[[string]$asset[0].name]
        if ((Get-Sha256 $cached) -cne ([string]$file.sha256).ToUpperInvariant()) { throw "Cached package hash mismatch: $name" }
        Copy-Item -LiteralPath $cached -Destination (Join-Path $packageDirectory $name) -Force
        if ((Get-Sha256 (Join-Path $packageDirectory $name)) -cne ([string]$file.sha256).ToUpperInvariant()) { throw "Guest package copy hash mismatch: $name" }
    }
    $inf = Join-Path $packageDirectory 'client_storage_filter.inf'
    $command = Invoke-NativeCommand (Join-Path $env:SystemRoot 'System32\pnputil.exe') @('/add-driver', $inf, '/install') @(0, 3010)
    $published = @($command.output | ForEach-Object { if ([string]$_ -match '(?i)Published Name\s*:\s*(oem[0-9]+\.inf)') { $Matches[1].ToLowerInvariant() } } | Select-Object -Unique)
    if ($published.Count -ne 1) { throw ('Cannot uniquely identify the newly installed ' + $Mode + ' package; output=' + ($command.output -join ' | ')) }
    $ownership = [ordered]@{
        schema = 'combined-vm-driver-ownership.v1'; lifecycleId = [string]$script:Config.lifecycleId; vmUuid = [string]$script:Config.vmIdentity.original.uuid
        mode = $Mode; publishedName = [string]$published[0]; packageDirectory = $packageDirectory; infSha256 = Get-Sha256 $inf
        sysSha256 = Get-Sha256 (Join-Path $packageDirectory 'client_storage_filter.sys'); catSha256 = Get-Sha256 (Join-Path $packageDirectory 'client_storage_filter.cat')
        commandExitCode = [int]$command.exitCode; recordedUtc = [DateTime]::UtcNow.ToString('o')
    }
    Write-JsonAtomic (Join-Path $script:StateRoot ('driver-' + $Mode + '.json')) $ownership
    return $ownership
}

function Remove-DriverPackage {
    param([ValidateSet('transparent', 'enabled')][string]$Mode)
    $ownership = Get-DriverOwnership $Mode
    $record = Invoke-NativeCommand (Join-Path $env:SystemRoot 'System32\pnputil.exe') @('/delete-driver', [string]$ownership.publishedName, '/uninstall') @(0, 3010)
    return [ordered]@{ mode = $Mode; publishedName = [string]$ownership.publishedName; exitCode = [int]$record.exitCode; output = @($record.output); rebootRequired = ($record.exitCode -eq 3010) }
}

function Get-CurrentBootSnapshot {
    return [ordered]@{ bcd = Get-BcdSnapshot; secureBoot = Get-SecureBootSnapshot; trust = Get-TrustMembership ([string]$script:Config.signer.thumbprint); codeIntegrity = [StorageHostReadNative]::ReadCodeIntegrityOptions() }
}

function Capture-Original {
    if (Test-Path -LiteralPath $script:JournalPath) { throw "Run ID already has a lifecycle journal: $script:JournalPath" }
    $identity = Assert-ExpectedIdentity
    $boot = Get-CurrentBootSnapshot
    if (-not (Test-SecureBootOffSnapshot $boot.secureBoot)) {
        throw ('VM lifecycle requires Secure Boot OFF and SecureBoot bytes 00 before any changes; ' + (Format-SecureBootStatus $boot.secureBoot))
    }
    if (-not (Test-TestSigningStateOff $boot.bcd.testSigning)) {
        throw ('VM lifecycle requires testsigning absent/default-off or explicitly No; actual exists=' + [string]$boot.bcd.testSigning.exists + ' value=' + [string]$boot.bcd.testSigning.value)
    }
    if ($boot.codeIntegrity.success -ne $true -or ([uint32]$boot.codeIntegrity.options -band 2) -ne 0) {
        throw ('VM lifecycle requires the effective test-signing Code Integrity bit OFF before changes; success=' + [string]$boot.codeIntegrity.success + ' options=' + [string]$boot.codeIntegrity.options)
    }
    $bootNext = Get-FirmwareVariable 'BootNext'
    if ([int]$bootNext.win32Error -ne 203) { throw ('BootNext must be absent before lifecycle capture; ' + (Format-FirmwareReadStatus $bootNext)) }
    $bootOrder = Get-FirmwareVariable 'BootOrder'
    if ([int]$bootOrder.win32Error -ne 0 -or [string]::IsNullOrWhiteSpace([string]$bootOrder.bytesBase64)) { throw 'BootOrder could not be read; refusing to start lifecycle.' }
    $systemRoot = Join-Path $script:StateRoot 'recovery'
    [void][IO.Directory]::CreateDirectory($systemRoot)
    $bcdExport = Join-Path $systemRoot 'bcd-original.bak'
    $exportRecord = Invoke-NativeCommand (Join-Path $env:SystemRoot 'System32\bcdedit.exe') @('/export', $bcdExport)
    if (-not (Test-Path -LiteralPath $bcdExport -PathType Leaf)) { throw 'bcdedit /export did not create the BCD recovery export.' }
    $esp = Get-EspTreeSnapshot
    $stage = @($esp.files | Where-Object { [string]$_.path -ieq 'EFI\HypersinvInventory\loader.efi' -or [string]$_.path -ieq 'EFI\HypersinvInventory\inventory.efi' -or [string]$_.path -ieq 'EFI\HypersinvInventory\manifest.sha256' })
    if ($stage.Count -gt 0) { throw 'EFI\HypersinvInventory already contains files; refusing to replace an existing stage.' }
    [void][IO.Directory]::CreateDirectory($script:EvidenceRoot)
    $smbiosPath = Join-Path $script:EvidenceRoot 'smbios-original.json'
    $smbios = Get-SmbiosReadback $smbiosPath
    Assert-SmbiosMode $smbios 'Original'
    $disk = Get-DiskReadback
    Assert-DiskMode $disk 'Original' 'Absent'
    if ([string]$disk.nativeSerial -cne [string]$script:Config.disk.originalNative -or [string]$disk.wmiSerial -cne [string]$script:Config.disk.originalWmi) {
        throw 'Fresh guest disk originals do not match the authenticated VM inventory.'
    }
    $script:Journal = [ordered]@{
        schema = 'combined-vm-lifecycle-journal.v1'; lifecycleId = [string]$script:Config.lifecycleId; vmUuid = [string]$script:Config.vmIdentity.original.uuid
        createdUtc = [DateTime]::UtcNow.ToString('o'); status = 'CAPTURED'; initialBootIdentity = Get-CurrentBootIdentity; fixturePins = $script:Config.fixturePins; replacementMap = $script:Config.replacementMap
        initialIdentity = $identity; secureBoot = $boot.secureBoot; initialTestSigning = $boot.bcd.testSigning; initialTrust = $boot.trust
        initialCodeIntegrity = $boot.codeIntegrity; bootOrder = $bootOrder; bootNext = $bootNext
        currentBcdLines = @($boot.bcd.currentLines); firmwareEnumerationSha256 = [string]$boot.bcd.firmwareSha256; firmwareEnumerationLines = @($boot.bcd.firmwareLines)
        bcdExportPath = $bcdExport; bcdExportSha256 = Get-Sha256 $bcdExport; bcdExportCommand = $exportRecord
        espSnapshot = $esp; originalSmbiosCapturePath = $smbiosPath; originalSmbiosCaptureSha256 = Get-Sha256 $smbiosPath
        originalDiskReadback = $disk; actions = @([ordered]@{ name = 'CaptureOriginal'; state = 'completed'; status = 'PASS'; startedUtc = $null; completedUtc = [DateTime]::UtcNow.ToString('o'); details = [ordered]@{ smbiosCaptureSha256 = Get-Sha256 $smbiosPath; bcdExportSha256 = Get-Sha256 $bcdExport }; error = $null })
        trustAddedByRun = $null; testSigningEnableCommand = $null; testSigningEnableUtc = $null; testSigningEnableTrustMembership = $null
        transparentPackage = $null; replacementPackage = $null; efiStageReceiptPath = $null; efiStageReceiptSha256 = $null; efiStageReceipt = $null
        bootNextAfterArm = $null; efiSmbiosCapturePath = $null; efiSmbiosCaptureSha256 = $null; efiCollectOutput = $null; bootNextAfterCollect = $null
        testSigningRestoreCommand = $null; trustRestore = $null; policyRestoreUtc = $null; policyRestoreBootIdentityBefore = $null; finalSnapshot = $null; lastError = $null
        recovery = [ordered]@{ vmSnapshotRestoreRecommended = $true; bcdExportIsEvidenceOnly = $true }
    }
    Save-Journal
    return [ordered]@{ identity = $identity; secureBoot = $boot.secureBoot; testSigning = $boot.bcd.testSigning; trust = $boot.trust; bootOrder = $bootOrder; espFileCount = @($esp.files).Count; bcdExport = $bcdExport; smbiosOriginal = $smbios; diskOriginal = $disk }
}

function Enable-TestMode {
    $journal = Get-BaselineJournal
    [void](Assert-JournalActionPassed 'CaptureOriginal')
    $identity = Assert-ExpectedIdentity
    $before = Get-CurrentBootSnapshot
    if (-not (Test-SecureBootOffSnapshot $before.secureBoot) -or [string]$before.secureBoot.variable.bytesBase64 -cne [string]$journal.secureBoot.variable.bytesBase64) { throw ('Secure Boot state differs from the captured VM baseline; ' + (Format-SecureBootStatus $before.secureBoot)) }
    if (-not (Test-TestSigningStateMatches $before.bcd.testSigning $journal.initialTestSigning) -or -not (Test-TestSigningStateOff $before.bcd.testSigning)) { throw 'testsigning differs from its captured OFF baseline.' }
    $cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes([string]$script:Assets['hypersinv.cer']))
    if ($cert.Thumbprint -cne [string]$script:Config.signer.thumbprint) { throw 'Guest public certificate thumbprint differs from the fixture pin.' }
    $membership = [ordered]@{}
    foreach ($storeName in @('Root', 'TrustedPublisher')) {
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($storeName, 'LocalMachine')
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        try {
            $wasPresent = @($store.Certificates | Where-Object { $_.Thumbprint -ceq $cert.Thumbprint }).Count -gt 0
            if (-not $wasPresent) { $store.Add($cert) }
            $isPresent = @($store.Certificates | Where-Object { $_.Thumbprint -ceq $cert.Thumbprint }).Count -gt 0
            if (-not $isPresent) { throw "Existing public certificate import failed in $storeName." }
            $membership[$storeName] = [ordered]@{ presentBefore = $wasPresent; presentAfter = $isPresent }
        }
        finally { $store.Close() }
    }
    $set = Invoke-NativeCommand (Join-Path $env:SystemRoot 'System32\bcdedit.exe') @('/set', '{current}', 'testsigning', 'on')
    $journal.trustAddedByRun = [ordered]@{ Root = (-not [bool]$journal.initialTrust.Root); TrustedPublisher = (-not [bool]$journal.initialTrust.TrustedPublisher) }
    $journal.testSigningEnableCommand = $set
    $journal.testSigningEnableUtc = [DateTime]::UtcNow.ToString('o')
    $journal.testSigningEnableTrustMembership = $membership
    $script:Journal = $journal
    return [ordered]@{ identity = $identity; trust = $membership; bcdedit = $set; rebootRequired = $true }
}

function Invoke-DriverPhase {
    param([ValidateSet('InstallTransparent','VerifyTransparent','RemoveTransparent','VerifyTransparentRemoved','InstallReplacement','VerifyReplacement','RemoveReplacement','VerifyReplacementRemoved')][string]$Phase)
    $journal = Get-BaselineJournal
    $requiredPrevious = @{
        InstallTransparent = 'EnableTestMode'
        VerifyTransparent = 'InstallTransparent'
        RemoveTransparent = 'VerifyTransparent'
        VerifyTransparentRemoved = 'RemoveTransparent'
        InstallReplacement = 'VerifyTransparentRemoved'
        VerifyReplacement = 'InstallReplacement'
        VerifyReplacementRemoved = 'RemoveReplacement'
    }[$Phase]
    if ($Phase -ceq 'RemoveReplacement') { $requiredPrevious = Get-CleanupActionName }
    [void](Assert-JournalActionPassed $requiredPrevious)
    $rebootAfter = @{
        VerifyTransparent = 'InstallTransparent'
        VerifyTransparentRemoved = 'RemoveTransparent'
        VerifyReplacement = 'InstallReplacement'
        VerifyReplacementRemoved = 'RemoveReplacement'
    }
    if ($rebootAfter.ContainsKey($Phase)) { $null = Assert-BootedAfterAction $rebootAfter[$Phase] }
    $identity = if ($Phase -ceq 'RemoveReplacement') {
        Assert-ExpectedIdentity -AllowPatchedSmbios
    }
    else { Assert-ExpectedIdentity }
    $secureBoot = Get-SecureBootSnapshot
    if (-not (Test-SecureBootOffSnapshot $secureBoot)) { throw ('Guest Secure Boot must remain OFF for the disposable VM filter phase; ' + (Format-SecureBootStatus $secureBoot)) }
    $mode = if ($Phase -match 'Transparent') { 'transparent' } else { 'enabled' }
    $isInstall = $Phase -like 'Install*'
    $isRemove = $Phase -like 'Remove*'
    $isVerify = $Phase -like 'Verify*'
    if ($isInstall) {
        $null = Assert-BootedAfterAction 'EnableTestMode'
        $boot = Get-BcdSnapshot
        if ($boot.testSigning.exists -ne $true -or [string]$boot.testSigning.value -cne 'yes') { throw 'Guest test-signing BCD value is not on after the policy reboot.' }
        $ci = [StorageHostReadNative]::ReadCodeIntegrityOptions()
        if ($ci.success -ne $true -or ([uint32]$ci.options -band 2) -ne 2) { throw 'Effective kernel Code Integrity options do not show test-signing enabled.' }
        $ownership = Install-DriverPackage $mode
        return [ordered]@{ identity = $identity; mode = $mode; package = $ownership; rebootRequired = $true }
    }
    if ($isRemove) {
        if ($Phase -ceq 'RemoveReplacement') {
            $activeReadback = Get-DiskReadback
            Assert-DiskMode $activeReadback 'Replacement' 'Present'
        }
        $result = Remove-DriverPackage $mode
        return [ordered]@{ identity = $identity; removal = $result; rebootRequired = $true }
    }
    $expectedMode = if ($Phase -ceq 'VerifyReplacement') { 'Replacement' } else { 'Original' }
    $filterState = if ($Phase -in @('VerifyTransparent','VerifyReplacement')) { 'Present' } else { 'Absent' }
    $readback = Get-DiskReadback
    Assert-DiskMode $readback $expectedMode $filterState
    if ($Phase -ceq 'VerifyReplacementRemoved') {
        [void](Get-CleanupActionName)
        [void](Assert-JournalActionPassed 'RemoveReplacement')
        $smbiosPath = Join-Path $script:EvidenceRoot 'smbios-after-storage-removal.json'
        $smbios = Get-SmbiosReadback $smbiosPath
        Assert-SmbiosMode $smbios 'Original'
    }
    return [ordered]@{ identity = $identity; expectedMode = $expectedMode; filterState = $filterState; readback = $readback }
}

function Invoke-VerifyCorrectedCollectorCleanup {
    [void](Assert-JournalActionPassed 'VerifyReplacement')
    $fixture = $script:Config.fixtureCorrection
    if ($null -eq $fixture -or [string]$fixture.schema -cne 'combined-vm-corrected-cleanup-input.v1' -or
        [string]$fixture.lifecycleId -cne 'smbios2-ep-correction-f12-r2-release-20261018' -or
        [string]$fixture.vmUuid -cne [string]$script:Config.vmIdentity.original.uuid -or
        [string]$fixture.captureMetadataSha256 -cnotmatch '\A[A-Fa-f0-9]{64}\z' -or
        [string]$fixture.mailbox.runId -cnotmatch '\Auefi-windows-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\z' -or
        [string]$fixture.mailbox.responseHmacSha256 -cnotmatch '\A[A-F0-9]{64}\z' -or
        [string]$fixture.mailbox.archiveSha256 -cnotmatch '\A[A-F0-9]{64}\z') {
        throw 'The authenticated CaptureActive receipt is not pinned to the required fixture lifecycle.'
    }
    $capture = $fixture.capture
    [void](Assert-CorrectedCollectorCaptureFacts $capture)

    $fixtureRoot = Join-Path (Join-Path $env:ProgramData 'CombinedVmFixture') ($fixture.vmUuid.Replace('-', '').ToLowerInvariant() + '\' + $fixture.lifecycleId)
    $fixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
    $fixtureJournalPath = Join-Path $fixtureRoot 'corrected-collector.journal.json'
    $expectedJournalPath = [IO.Path]::GetFullPath([string]$capture.journal.path)
    if (-not $expectedJournalPath.Equals($fixtureJournalPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'CaptureActive fixture journal path is outside the exact owned fixture state root.'
    }
    $null = Assert-PathUnder -Root $fixtureRoot -Path $fixtureJournalPath -Label 'fixture journal'
    if (-not (Test-Path -LiteralPath $fixtureJournalPath -PathType Leaf)) { throw 'The exact corrected-collector journal is missing.' }
    $fixtureJournalHash = Get-Sha256 $fixtureJournalPath
    if ($fixtureJournalHash -cne ([string]$capture.journal.sha256).ToUpperInvariant()) {
        throw ('Fixture journal differs from the authenticated CaptureActive receipt: expected=' + [string]$capture.journal.sha256 + ' actual=' + $fixtureJournalHash)
    }
    $fixtureJournal = Get-Content -LiteralPath $fixtureJournalPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    [void](Assert-CorrectedFixtureJournal -FixtureJournal $fixtureJournal -Capture $capture -ExpectedLifecycleId ([string]$fixture.lifecycleId) -ExpectedUuid ([string]$fixture.vmUuid))

    $expectedReceiptPath = Join-Path $fixtureRoot 'pathb\inventory\path-b-stage.json'
    if (-not ([IO.Path]::GetFullPath([string]$fixtureJournal.pathBReceiptPath)).Equals([IO.Path]::GetFullPath($expectedReceiptPath), [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $expectedReceiptPath -PathType Leaf)) {
        throw 'The Path-B stage receipt is missing or outside the exact helper state root.'
    }
    $receiptHash = Get-Sha256 $expectedReceiptPath
    if ($receiptHash -cne ([string]$fixtureJournal.pathBReceiptSha256).ToUpperInvariant()) { throw 'The Path-B receipt hash differs from its terminal journal.' }

    $currentBoot = Get-CurrentBootIdentity
    if ([string]$currentBoot.lastBootUpTimeUtc -cne [string]$capture.bootIdentity.lastBootUpTimeUtc) {
        throw ('Windows boot changed after CaptureActive: captured=' + [string]$capture.bootIdentity.lastBootUpTimeUtc + ' actual=' + [string]$currentBoot.lastBootUpTimeUtc)
    }
    $identity = Assert-ExpectedIdentity -AllowPatchedSmbios
    $disk = Get-DiskReadback
    Assert-DiskMode $disk 'Replacement' 'Present'
    $smbiosPath = Join-Path $script:EvidenceRoot 'smbios-after-corrected-collector-cleanup.json'
    $smbios = Get-SmbiosReadback $smbiosPath
    Assert-SmbiosMode $smbios 'Replacement'
    $secureBoot = Get-SecureBootSnapshot
    if (-not (Test-SecureBootOffSnapshot $secureBoot)) { throw ('Secure Boot must remain OFF for the VM replacement phase; ' + (Format-SecureBootStatus $secureBoot)) }
    $bcd = Get-BcdSnapshot
    if ($bcd.testSigning.exists -ne $true -or [string]$bcd.testSigning.value -cne 'yes') { throw 'testsigning is not on during corrected-collector cleanup verification.' }

    $bootOrder = Get-FirmwareVariable 'BootOrder'
    if ([int]$bootOrder.win32Error -ne 0 -or [string]$bootOrder.bytesBase64 -cne [string]$fixtureJournal.bootOrderBefore -or
        [uint32]$bootOrder.attributes -ne [uint32]$fixtureJournal.bootOrderAttributes -or
        [string]$bootOrder.bytesBase64 -cne [string]$script:Journal.bootOrder.bytesBase64) {
        throw ('BootOrder is not byte-identical to the fixture and lifecycle baselines: fixture=' + [string]$fixtureJournal.bootOrderBefore + ' actual=' + [string]$bootOrder.bytesBase64)
    }
    $bootNext = Get-FirmwareVariable 'BootNext'
    if ([bool]$bootNext.supported -ne [bool]$script:Journal.bootNext.supported -or
        [int]$bootNext.win32Error -ne [int]$script:Journal.bootNext.win32Error -or
        [string]$bootNext.bytesBase64 -cne [string]$script:Journal.bootNext.bytesBase64) {
        throw ('BootNext differs from lifecycle baseline: expectedError=' + [string]$script:Journal.bootNext.win32Error + ' actualError=' + [string]$bootNext.win32Error)
    }
    foreach ($name in @([string]$fixtureJournal.customBootName, [string]$fixtureJournal.standardBootName)) {
        if ([string]::IsNullOrWhiteSpace($name)) { throw 'Fixture journal is missing a recorded owned Boot#### name.' }
        $entry = Get-FirmwareVariable $name
        if ($entry.supported -ne $false -or [int]$entry.win32Error -ne 203) { throw "Owned firmware entry $name remains after CaptureActive cleanup." }
    }
    $esp = Get-EspTreeSnapshot
    if ([string]$esp.bootManagerSha256 -cne [string]$script:Journal.espSnapshot.bootManagerSha256) { throw 'Windows Boot Manager hash differs from the original lifecycle snapshot.' }
    $espDelta = @(Compare-EspSnapshot $script:Journal.espSnapshot $esp)
    if ($espDelta.Count -gt 0) { throw ('ESP differs from the original lifecycle snapshot after corrected-collector cleanup: ' + ($espDelta -join '; ')) }
    $leftoverStage = @($esp.files | Where-Object { [string]$_.path -match '(?i)^EFI\\HypersinvInventory(?:\\|$)' })
    if ($leftoverStage.Count -gt 0) { throw ('Owned Path-B stage remains on the ESP after CaptureActive: ' + (($leftoverStage | ForEach-Object { [string]$_.path }) -join ', ')) }

    return [ordered]@{
        identity = $identity
        bootIdentity = $currentBoot
        fixtureJournalPath = $fixtureJournalPath
        fixtureJournalSha256 = $fixtureJournalHash
        pathBReceiptPath = $expectedReceiptPath
        pathBReceiptSha256 = $receiptHash
        captureMetadataSha256 = [string]$fixture.captureMetadataSha256
        mailbox = $fixture.mailbox
        disk = $disk
        smbios = $smbios
        secureBoot = $secureBoot
        testSigning = $bcd.testSigning
        bootOrder = $bootOrder
        bootNext = $bootNext
        customBootEntriesAbsent = $true
        espDelta = @($espDelta)
        stageAbsent = $true
        rebootRequired = $false
    }
}

function Invoke-StageSmbiosInventory {
    $journal = Get-BaselineJournal
    [void](Assert-JournalActionPassed 'VerifyReplacement')
    $identity = Assert-ExpectedIdentity
    $disk = Get-DiskReadback
    Assert-DiskMode $disk 'Replacement' 'Present'
    $smbios = Get-SmbiosReadback (Join-Path $script:EvidenceRoot 'smbios-pre-stage.json')
    Assert-SmbiosMode $smbios 'Original'
    Initialize-PathBLibrary
    $mount = Mount-Esp
    try {
        $stageRoot = Join-Path $mount.EspRoot 'EFI\HypersinvInventory'
        if (Test-Path -LiteralPath $stageRoot) { throw 'EFI\HypersinvInventory appeared after baseline capture; refusing stage replacement.' }
        $beforeBootManager = Get-Sha256 (Join-Path $mount.EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi')
        if ($beforeBootManager -cne [string]$journal.espSnapshot.bootManagerSha256) { throw 'Windows Boot Manager differs from the original VM snapshot.' }
        Stage-ToRoot $mount.EspRoot $mount.VolumeGuid ''
        if ((Get-Sha256 (Join-Path $mount.EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi')) -cne $beforeBootManager) { throw 'Windows Boot Manager changed during VM EFI staging.' }
    }
    finally { Dismount-Esp $mount }
    $receiptPath = Join-Path (Join-Path $script:StateRoot 'path-b\inventory') 'path-b-stage.json'
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) { throw 'Path-B stage did not write a receipt.' }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    $script:Journal.efiStageReceiptPath = $receiptPath
    $script:Journal.efiStageReceiptSha256 = Get-Sha256 $receiptPath
    $script:Journal.efiStageReceipt = $receipt
    return [ordered]@{ identity = $identity; stageDirectory = [string]$receipt.stageDirectory; loaderSha256 = [string]$receipt.loaderSha256; collectorSha256 = [string]$receipt.inventorySha256; bootManagerSha256 = [string]$receipt.bootManagerSha256; rebootRequired = $false }
}

function Invoke-ArmSmbiosInventory {
    $journal = Get-BaselineJournal
    [void](Assert-JournalActionPassed 'StageSmbiosInventory')
    $identity = Assert-ExpectedIdentity
    $disk = Get-DiskReadback
    Assert-DiskMode $disk 'Replacement' 'Present'
    Initialize-PathBLibrary
    $armOutput = @(Invoke-PowerShellAction { Invoke-Arm })
    $bootNext = Get-FirmwareVariable 'BootNext'
    if ([int]$bootNext.win32Error -ne 0) { throw ('BootNext did not become readable after the one-shot arm; ' + (Format-FirmwareReadStatus $bootNext)) }
    $script:Journal.bootNextAfterArm = $bootNext
    return [ordered]@{ identity = $identity; disk = $disk; armOutput = @($armOutput); bootNext = $bootNext; stageReceiptSha256 = [string]$journal.efiStageReceiptSha256; rebootRequired = $true }
}

function Invoke-CleanupSmbiosInventory {
    $journal = Get-BaselineJournal
    [void](Assert-BootedAfterAction 'ArmSmbiosInventory')
    $identity = Assert-ExpectedIdentity -AllowPatchedSmbios
    $disk = Get-DiskReadback
    Assert-DiskMode $disk 'Replacement' 'Present'
    Initialize-PathBLibrary
    $smbiosPath = Join-Path $script:EvidenceRoot 'smbios-after-efi-patch.json'
    $smbios = Get-SmbiosReadback $smbiosPath
    Assert-SmbiosMode $smbios 'Replacement'
    $captureOutput = @(Invoke-PowerShellAction { Invoke-Collect })
    $mount = Mount-Esp
    try {
        $stageRoot = Join-Path $mount.EspRoot 'EFI\HypersinvInventory'
        if (-not (Test-Path -LiteralPath $stageRoot -PathType Container)) { throw 'The owned VM SMBIOS stage is missing after Collect.' }
        $receipt = Read-Receipt
        $status = Get-MountedStageStatus $mount.EspRoot -AllowReport
        if ([string]$receipt.loaderSha256 -cne [string]$journal.efiStageReceipt.loaderSha256 -or
            [string]$receipt.inventorySha256 -cne [string]$journal.efiStageReceipt.inventorySha256 -or
            [string]$status.Sources.LoaderHash -cne [string]$receipt.loaderSha256 -or
            [string]$status.Sources.CollectorHash -cne [string]$receipt.inventorySha256) {
            throw 'EFI stage no longer matches the exact recorded lifecycle receipt; refusing cleanup.'
        }
        $rootReport = Join-Path $mount.EspRoot 'HypersinvEfiInventory.txt'
        if (Test-Path -LiteralPath $rootReport) { throw 'EFI root report remained after the Path-B collector cleanup.' }
        Remove-OwnedStage $stageRoot
        if (Test-Path -LiteralPath $stageRoot) { throw 'Owned EFI stage remains after cleanup.' }
    }
    finally { Dismount-Esp $mount }
    $captureRoot = Join-Path (Join-Path $script:StateRoot 'path-b\inventory') 'captures'
    $captures = @(Get-ChildItem -LiteralPath $captureRoot -File -ErrorAction SilentlyContinue | Sort-Object Name)
    $bootNext = Get-FirmwareVariable 'BootNext'
    if ([int]$bootNext.win32Error -ne 203) { throw ('Owned BootNext was not cleared by Collect; ' + (Format-FirmwareReadStatus $bootNext)) }
    $script:Journal.efiSmbiosCapturePath = $smbiosPath
    $script:Journal.efiSmbiosCaptureSha256 = Get-Sha256 $smbiosPath
    $script:Journal.efiCollectOutput = @($captureOutput)
    $script:Journal.bootNextAfterCollect = $bootNext
    return [ordered]@{ identity = $identity; smbios = $smbios; collectorOutput = @($captureOutput); capturedArtifacts = @($captures | ForEach-Object { [ordered]@{ path = $_.Name; sha256 = Get-Sha256 $_.FullName } }); bootNext = $bootNext; stageRemoved = $true; rebootRequired = $true }
}

function Restore-Policy {
    $journal = Get-BaselineJournal
    [void](Get-CleanupActionName)
    [void](Assert-JournalActionPassed 'VerifyReplacementRemoved')
    $identity = Assert-ExpectedIdentity
    $disk = Get-DiskReadback
    Assert-DiskMode $disk 'Original' 'Absent'
    $smbios = Get-SmbiosReadback (Join-Path $script:EvidenceRoot 'smbios-before-policy-restore.json')
    Assert-SmbiosMode $smbios 'Original'
    $bcd = Get-BcdSnapshot
    if ($bcd.testSigning.exists -ne $true -or [string]$bcd.testSigning.value -cne 'yes') { throw 'Guest test-signing value is not on before policy restore.' }
    $restoreArguments = Get-TestSigningRestoreArguments $journal.initialTestSigning
    $set = Invoke-NativeCommand (Join-Path $env:SystemRoot 'System32\bcdedit.exe') $restoreArguments
    $cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes([string]$script:Assets['hypersinv.cer']))
    if ($cert.Thumbprint -cne [string]$script:Config.signer.thumbprint) { throw 'Public certificate thumbprint differs during policy cleanup.' }
    $removals = [ordered]@{}
    foreach ($storeName in @('Root', 'TrustedPublisher')) {
        $shouldRemove = $journal.initialTrust.$storeName -ne $true
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($storeName, 'LocalMachine')
        $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        try {
            $matching = @($store.Certificates | Where-Object { $_.Thumbprint -ceq $cert.Thumbprint })
            if ($shouldRemove) { foreach ($item in $matching) { $store.Remove($item) } }
            $presentAfter = @($store.Certificates | Where-Object { $_.Thumbprint -ceq $cert.Thumbprint }).Count -gt 0
            if ([bool]$presentAfter -ne [bool]$journal.initialTrust.$storeName) { throw "Certificate trust membership did not return to baseline in $storeName." }
            $removals[$storeName] = [ordered]@{ removedByRun = [bool]$shouldRemove; presentBeforeRestore = ($matching.Count -gt 0); presentAfterRestore = [bool]$presentAfter }
        }
        finally { $store.Close() }
    }
    $script:Journal.testSigningRestoreCommand = $set
    $script:Journal.trustRestore = $removals
    $script:Journal.policyRestoreUtc = [DateTime]::UtcNow.ToString('o')
    $script:Journal.policyRestoreBootIdentityBefore = Get-CurrentBootIdentity
    return [ordered]@{ identity = $identity; disk = $disk; smbios = $smbios; bcdedit = $set; trust = $removals; rebootRequired = $true }
}

function Compare-EspSnapshot {
    param($Expected, $Actual)
    $expectedMap = @{}
    foreach ($file in $Expected.files) {
        if (Test-IsExcludedEspPath ([string]$file.path)) { continue }
        $expectedMap[[string]$file.path] = [string]$file.sha256
    }
    $actualMap = @{}
    foreach ($file in $Actual.files) {
        if (Test-IsExcludedEspPath ([string]$file.path)) { continue }
        $actualMap[[string]$file.path] = [string]$file.sha256
    }
    $delta = [Collections.Generic.List[string]]::new()
    foreach ($path in $expectedMap.Keys) {
        if (-not $actualMap.ContainsKey($path)) { $delta.Add("missing=$path"); continue }
        if ($expectedMap[$path] -cne $actualMap[$path]) { $delta.Add("hash=$path expected=$($expectedMap[$path]) actual=$($actualMap[$path])") }
    }
    foreach ($path in $actualMap.Keys) { if (-not $expectedMap.ContainsKey($path)) { $delta.Add("unexpected=$path") } }
    return @($delta.ToArray())
}

function Final-Verify {
    $journal = Get-BaselineJournal
    $finalBootIdentity = Assert-BootedAfterAction 'RestorePolicy'
    if ($null -eq $journal.policyRestoreBootIdentityBefore -or
        $null -eq $journal.policyRestoreBootIdentityBefore.lastBootUpTimeUtc) {
        throw 'The policy-restore boot identity was not recorded; final reboot freshness cannot be verified.'
    }
    $policyBoot = [DateTime]::Parse([string]$journal.policyRestoreBootIdentityBefore.lastBootUpTimeUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    $finalBoot = [DateTime]::Parse([string]$finalBootIdentity.lastBootUpTimeUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    if ($finalBoot -le $policyBoot) {
        throw "A normal Windows boot after policy restoration is required; policyRestoreBootUtc=$($policyBoot.ToString('o')) actualLastBootUtc=$($finalBoot.ToString('o'))."
    }
    $identity = Assert-ExpectedIdentity
    $secureBoot = Get-SecureBootSnapshot
    if ($secureBoot.enabled -ne [bool]$journal.secureBoot.enabled -or [string]$secureBoot.variable.bytesBase64 -cne [string]$journal.secureBoot.variable.bytesBase64) {
        throw ('Secure Boot state differs from baseline: expectedEnabled=' + [string]$journal.secureBoot.enabled + ' actualEnabled=' + [string]$secureBoot.enabled + ' expectedBytes=' + [string]$journal.secureBoot.variable.bytesHex + ' actualBytes=' + [string]$secureBoot.variable.bytesHex)
    }
    $bcd = Get-BcdSnapshot
    if ($bcd.testSigning.exists -ne [bool]$journal.initialTestSigning.exists -or
        ([bool]$journal.initialTestSigning.exists -and [string]$bcd.testSigning.value -cne [string]$journal.initialTestSigning.value)) {
        throw ('BCD testsigning differs from baseline: expected=' + [string]$journal.initialTestSigning.value + ' actual=' + [string]$bcd.testSigning.value)
    }
    $finalCodeIntegrity = [StorageHostReadNative]::ReadCodeIntegrityOptions()
    if ($finalCodeIntegrity.success -ne $true -or ([uint32]$finalCodeIntegrity.options -band 2) -ne 0) {
        throw ('Effective test-signing Code Integrity bit is not off after policy restore: success=' + [string]$finalCodeIntegrity.success + ' options=' + [string]$finalCodeIntegrity.options)
    }
    $baselineCurrentLines = @($journal.currentBcdLines)
    $finalCurrentLines = @($bcd.currentLines)
    $baselineBcdSemantics = @($baselineCurrentLines | Where-Object { [string]$_ -notmatch '^\s*testsigning\s+' }) -join "`n"
    $finalBcdSemantics = @($finalCurrentLines | Where-Object { [string]$_ -notmatch '^\s*testsigning\s+' }) -join "`n"
    if ($baselineBcdSemantics -cne $finalBcdSemantics) { throw 'BCD current-object values changed outside the testsigning field.' }
    if ([string]$bcd.firmwareSha256 -cne [string]$journal.firmwareEnumerationSha256) { throw 'Firmware boot-entry enumeration differs from the captured baseline.' }
    $finalBcdExport = Join-Path $script:RecoveryDirectory 'bcd-final.bak'
    [void](Invoke-NativeCommand (Join-Path $env:SystemRoot 'System32\bcdedit.exe') @('/export', $finalBcdExport))
    if (-not (Test-Path -LiteralPath $finalBcdExport -PathType Leaf)) { throw 'Final BCD export was not created.' }
    $finalBcdHash = Get-Sha256 $finalBcdExport
    $initialBcdHash = Get-Sha256 ([string]$journal.bcdExportPath)
    $trust = Get-TrustMembership ([string]$script:Config.signer.thumbprint)
    foreach ($store in @('Root', 'TrustedPublisher')) {
        if ([bool]$trust[$store] -ne [bool]$journal.initialTrust.$store) { throw "Signer trust store $store does not match its baseline membership." }
    }
    $disk = Get-DiskReadback
    Assert-DiskMode $disk 'Original' 'Absent'
    if ([string]$disk.nativeSerial -cne [string]$script:Config.disk.originalNative -or [string]$disk.wmiSerial -cne [string]$script:Config.disk.originalWmi) {
        throw 'Final native/WMI disk originals differ from the authenticated VM inventory.'
    }
    $smbiosPath = Join-Path $script:EvidenceRoot 'smbios-final-original.json'
    $smbios = Get-SmbiosReadback $smbiosPath
    Assert-SmbiosMode $smbios 'Original'
    $bootOrder = Get-FirmwareVariable 'BootOrder'
    if ([int]$bootOrder.win32Error -ne 0 -or [string]$bootOrder.bytesBase64 -cne [string]$journal.bootOrder.bytesBase64) {
        throw ('BootOrder bytes differ from baseline: expected=' + [string]$journal.bootOrder.bytesHex + ' actual=' + [string]$bootOrder.bytesHex)
    }
    $bootNext = Get-FirmwareVariable 'BootNext'
    if ([int]$bootNext.win32Error -ne [int]$journal.bootNext.win32Error -or [string]$bootNext.bytesBase64 -cne [string]$journal.bootNext.bytesBase64) {
        throw ('BootNext differs from baseline: expectedError=' + [string]$journal.bootNext.win32Error + ' expected=' + [string]$journal.bootNext.bytesHex + ' actualError=' + [string]$bootNext.win32Error + ' actual=' + [string]$bootNext.bytesHex)
    }
    $esp = Get-EspTreeSnapshot
    $espDelta = @(Compare-EspSnapshot $journal.espSnapshot $esp)
    if ($espDelta.Count -gt 0) { throw ('ESP final comparison failed: ' + ($espDelta -join '; ')) }
    if ([string]$esp.bootManagerSha256 -cne [string]$journal.espSnapshot.bootManagerSha256) { throw 'Windows Boot Manager SHA-256 differs from the VM snapshot.' }
    $services = @(Get-CimInstance -ClassName Win32_SystemDriver -Filter "Name='client_storage_filter'" -ErrorAction Stop)
    if ($services.Count -gt 0 -or (Test-Path -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\client_storage_filter')) { throw 'Storage filter service or registry entry remains after cleanup.' }
    $espExclusions = @(
        'EFI\Microsoft\Boot\BOOTSTAT.DAT: mutable boot bookkeeping; raw hash excluded',
        'EFI\Microsoft\Boot\BCD: Windows-managed; BCD semantics verified through bcdedit output and export evidence',
        'EFI\Microsoft\Boot\BCD.LOG, BCD.LOG1, BCD.LOG2: Windows-managed transaction files; raw hashes excluded'
    )
    $script:Journal.finalSnapshot = [ordered]@{ completedUtc = [DateTime]::UtcNow.ToString('o'); bootIdentity = $finalBootIdentity; effectiveCodeIntegrity = $finalCodeIntegrity; identity = $identity; secureBoot = $secureBoot; bcd = $bcd; bcdExportPath = $finalBcdExport; bcdExportSha256 = $finalBcdHash; bcdExportByteIdentical = ($finalBcdHash -ceq $initialBcdHash); bcdExportByteIdentityIsEvidenceOnly = $true; bcdNonTestSigningPropertiesIdentical = $true; trust = $trust; disk = $disk; smbios = $smbios; bootOrder = $bootOrder; bootNext = $bootNext; esp = $esp; espDelta = $espDelta; espHashExclusions = $espExclusions }
    $script:Journal.status = 'FINAL_VERIFY_PASS'
    return [ordered]@{ status = 'FINAL_VERIFY_PASS'; identity = $identity; bootIdentity = $finalBootIdentity; effectiveCodeIntegrity = $finalCodeIntegrity; secureBoot = $secureBoot; bcdTestSigning = $bcd.testSigning; bcdNonTestSigningPropertiesIdentical = $true; bcdExportByteIdentical = ($finalBcdHash -ceq $initialBcdHash); bcdExportByteIdentityIsEvidenceOnly = $true; trust = $trust; disk = $disk; smbios = $smbios; bootOrder = $bootOrder; bootNext = $bootNext; espBootManagerSha256 = $esp.bootManagerSha256; espDelta = $espDelta; espHashExclusions = $espExclusions; filterServices = $services }
}

function Initialize-FirmwareReader {
    if ('CombinedVmFirmware' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class CombinedVmFirmware {
    private const uint TOKEN_QUERY = 0x0008;
    private const uint TOKEN_ADJUST_PRIVILEGES = 0x0020;
    private const uint SE_PRIVILEGE_ENABLED = 0x00000002;
    private const int ERROR_NOT_ALL_ASSIGNED = 1300;
    [StructLayout(LayoutKind.Sequential)] private struct Luid { public uint LowPart; public int HighPart; }
    [StructLayout(LayoutKind.Sequential)] private struct LuidAndAttributes { public Luid Luid; public uint Attributes; }
    [StructLayout(LayoutKind.Sequential)] private struct TokenPrivileges { public uint PrivilegeCount; public LuidAndAttributes Privileges; }
    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError=true, EntryPoint="SetLastError")] private static extern void ClearLastError(uint error);
    [DllImport("advapi32.dll", SetLastError=true)] private static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="LookupPrivilegeValueW")] private static extern bool LookupPrivilegeValue(string systemName, string name, out Luid luid);
    [DllImport("advapi32.dll", SetLastError=true)] private static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TokenPrivileges newState, uint bufferLength, out TokenPrivileges previousState, out uint returnLength);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="GetFirmwareEnvironmentVariableExW")]
    private static extern uint Get(string name, string guid, byte[] buffer, uint size, out uint attributes);
    public static object Read(string name, string guid) {
        IntPtr token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, out token)) {
            int error = Marshal.GetLastWin32Error();
            return new { name = name, supported = false, win32Error = error, win32ErrorText = new Win32Exception(error).Message,
                size = 0, attributes = (uint?)null, bytesHex = (string)null, bytesBase64 = (string)null,
                privilegeEnabled = false, privilegeError = error, privilegeRestored = true, privilegeRestoreError = 0 };
        }
        try {
            Luid privilege;
            if (!LookupPrivilegeValue(null, "SeSystemEnvironmentPrivilege", out privilege)) {
                int error = Marshal.GetLastWin32Error();
                return new { name = name, supported = false, win32Error = error, win32ErrorText = new Win32Exception(error).Message,
                    size = 0, attributes = (uint?)null, bytesHex = (string)null, bytesBase64 = (string)null,
                    privilegeEnabled = false, privilegeError = error, privilegeRestored = true, privilegeRestoreError = 0 };
            }
            TokenPrivileges requested = new TokenPrivileges();
            requested.PrivilegeCount = 1;
            requested.Privileges.Luid = privilege;
            requested.Privileges.Attributes = SE_PRIVILEGE_ENABLED;
            TokenPrivileges previous;
            uint previousLength;
            ClearLastError(0);
            bool adjusted = AdjustTokenPrivileges(token, false, ref requested, (uint)Marshal.SizeOf(typeof(TokenPrivileges)), out previous, out previousLength);
            int privilegeError = Marshal.GetLastWin32Error();
            uint size = 0;
            uint attributes = 0;
            byte[] actual = new byte[0];
            int readError = 0;
            bool privilegeEnabled = adjusted && privilegeError != ERROR_NOT_ALL_ASSIGNED;
            if (!adjusted) privilegeError = Marshal.GetLastWin32Error();
            if (privilegeEnabled) {
                byte[] buffer = new byte[4096];
                ClearLastError(0);
                size = Get(name, guid, buffer, (uint)buffer.Length, out attributes);
                readError = size == 0 ? Marshal.GetLastWin32Error() : 0;
                actual = size == 0 ? new byte[0] : new byte[(int)size];
                if (size != 0) Array.Copy(buffer, actual, (int)size);
            }
            int restoreError = 0;
            if (adjusted) {
                TokenPrivileges ignored;
                uint ignoredLength;
                ClearLastError(0);
                if (!AdjustTokenPrivileges(token, false, ref previous, (uint)Marshal.SizeOf(typeof(TokenPrivileges)), out ignored, out ignoredLength)) restoreError = Marshal.GetLastWin32Error();
                else if (Marshal.GetLastWin32Error() == ERROR_NOT_ALL_ASSIGNED) restoreError = ERROR_NOT_ALL_ASSIGNED;
            }
            int finalError = restoreError != 0 ? restoreError : (privilegeError != 0 ? privilegeError : readError);
            string finalErrorText = finalError == 0 ? null : new Win32Exception(finalError).Message;
            bool supported = finalError == 0 && size != 0;
            return new { name = name, supported = supported, win32Error = finalError, win32ErrorText = finalErrorText,
                size = supported ? (int)size : 0, attributes = supported ? (uint?)attributes : null,
                bytesHex = supported ? BitConverter.ToString(actual).Replace("-", "") : (string)null,
                bytesBase64 = supported ? Convert.ToBase64String(actual) : (string)null,
                privilegeEnabled = privilegeEnabled, privilegeError = privilegeError,
                privilegeRestored = restoreError == 0, privilegeRestoreError = restoreError };
        } finally { CloseHandle(token); }
    }
}
'@
}

$status = 'FAIL'
$resultDetails = $null
try {
    if ($script:Config.schema -cne 'combined-vm-lifecycle-config.v1' -or $script:LifecycleAction -notin @(
        'CaptureOriginal','EnableTestMode','InstallTransparent','VerifyTransparent','RemoveTransparent','VerifyTransparentRemoved',
        'InstallReplacement','VerifyReplacement','RemoveReplacement','VerifyReplacementRemoved','StageSmbiosInventory','ArmSmbiosInventory',
        'CleanupSmbiosInventory','VerifyCorrectedCollectorCleanup','RestorePolicy','FinalVerify')) { throw 'Lifecycle configuration schema or action is invalid.' }
    if (-not $script:RunRootFull.StartsWith('C:\ProgramData\UEFILoaderRunner\runs\', [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $script:RunRootFull -PathType Container)) { throw 'RunRoot must be an existing authenticated guest mailbox run directory.' }
    if ($script:Config.vmIdentity.original.manufacturer -cne 'VMware, Inc.' -or $script:Config.vmIdentity.original.model -cne 'VMware20,1' -or
        [string]$script:Config.vmIdentity.original.uuid -notmatch '\A[0-9A-F]{8}-(?:[0-9A-F]{4}-){3}[0-9A-F]{12}\z' -or
        $script:Config.vmIdentity.patched.manufacturer -cne 'NORTHLAB' -or $script:Config.vmIdentity.patched.model -cne 'NODE' -or
        $script:Config.vmIdentity.patched.uuid -cne 'A17F24D0-68E2-4A09-9C31-2B7D8450E6F9') { throw 'VM identity or exact SMBIOS replacement guard is invalid.' }
    if ($script:Config.disk.replacement -cne '1111_1111_1111_1111_A1B2_C3D4_5566_7788.' -or
        $script:Config.signer.thumbprint -cne '59DBD6780CFE60C020B3F4511211FEC22C397B85' -or
        $script:Config.signer.certificateSha256 -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE') { throw 'Locked replacement or existing signer pin drifted.' }
    Assert-GuestGateArguments -Action $Action -ExpectedProbeVerdict $ExpectedProbeVerdict
    Assert-Admin
    if ($script:LifecycleAction -eq 'CaptureOriginal' -and (Test-Path -LiteralPath $script:StateRoot)) {
        $existingState = @(Get-ChildItem -LiteralPath $script:StateRoot -Force -ErrorAction Stop)
        if ($existingState.Count -gt 0) { throw 'Lifecycle state directory is not empty; use a new LifecycleId.' }
    }
    Initialize-FirmwareReader
    Initialize-GuestAssets
    $script:DependencyBootstrap = {
        . $script:StorageRead
        Import-Module -Name $script:StorageModel -Global -Force -ErrorAction Stop
        Initialize-PathBLibrary
        $loaderBuild = Join-Path $script:StateRoot 'path-b\loader'
        $inventoryBuild = Join-Path $script:StateRoot 'path-b\inventory'
        . $script:PathBScript -Mode Status -LoaderBuildDir $loaderBuild -InventoryBuildDir $inventoryBuild -MountLetter ([string]$script:Config.pathBMountLetter) -LibraryOnly
        Initialize-LifecycleDependencyScope
    }
    . $script:DependencyBootstrap
    if ($script:LifecycleAction -eq 'CaptureOriginal') {
        $resultDetails = Capture-Original
    }
    else {
        $journal = Get-BaselineJournal
        Start-JournalAction $journal
        switch ($script:LifecycleAction) {
            'EnableTestMode' { $resultDetails = Enable-TestMode }
            { $_ -in @('InstallTransparent','VerifyTransparent','RemoveTransparent','VerifyTransparentRemoved','InstallReplacement','VerifyReplacement','RemoveReplacement','VerifyReplacementRemoved') } { $resultDetails = Invoke-DriverPhase $script:LifecycleAction }
            'StageSmbiosInventory' { $resultDetails = Invoke-StageSmbiosInventory }
            'ArmSmbiosInventory' { $resultDetails = Invoke-ArmSmbiosInventory }
            'CleanupSmbiosInventory' { $resultDetails = Invoke-CleanupSmbiosInventory }
            'VerifyCorrectedCollectorCleanup' { $resultDetails = Invoke-VerifyCorrectedCollectorCleanup }
            'RestorePolicy' { $resultDetails = Restore-Policy }
            'FinalVerify' { $resultDetails = Final-Verify }
        }
        Complete-JournalAction -Status 'PASS' -Details $resultDetails
    }
    $status = if ($script:LifecycleAction -eq 'FinalVerify') { 'FINAL_VERIFY_PASS' } else { 'PASS' }
}
catch {
    $script:Failure = $_.Exception.ToString()
    if ($null -ne $script:Journal) {
        try {
            $actions = @($script:Journal.actions)
            if ($actions.Count -gt 0 -and [string]$actions[-1].state -ceq 'started') {
                $actions[-1].state = 'failed'
                $actions[-1].status = 'FAIL'
                $actions[-1].completedUtc = [DateTime]::UtcNow.ToString('o')
                $actions[-1].error = $script:Failure
                $script:Journal.actions = $actions
            }
            $script:Journal.lastError = $script:Failure
            $script:Journal.status = 'FAILED_REQUIRES_REVIEW_OR_VM_SNAPSHOT_RESTORE'
            Save-Journal
        }
        catch { $script:Failure += '; journal persistence failure: ' + $_.Exception.ToString() }
    }
    $status = 'FAIL'
    $resultDetails = $null
}
finally {
    $record = [ordered]@{
        schema = 'combined-vm-lifecycle-result.v1'; action = $script:LifecycleAction; lifecycleId = [string]$script:Config.lifecycleId
        status = $status; result = $resultDetails; failure = $script:Failure; commands = @($script:CommandRecords.ToArray())
        diagnostics = @($script:Diagnostics.ToArray()); completedUtc = [DateTime]::UtcNow.ToString('o')
        journalPath = if (Test-Path -LiteralPath $script:JournalPath -PathType Leaf) { $script:JournalPath } else { $null }
    }
    try {
        if (-not (Test-Path -LiteralPath $script:RunRootFull -PathType Container)) { throw 'Authenticated result directory is missing; cannot publish lifecycle evidence.' }
        $journalContent = if (Test-Path -LiteralPath $script:JournalPath -PathType Leaf) { Get-Content -LiteralPath $script:JournalPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop } else { $null }
        $embeddedEvidence = New-EmbeddedLifecycleEvidence
        $metadata = New-LifecycleMetadata -Action $script:LifecycleAction -LifecycleId ([string]$script:Config.lifecycleId) -OutcomeStatus $status -Result $record -Journal $journalContent -RawEvidence $embeddedEvidence
        $metadataJson = $metadata | ConvertTo-Json -Depth 60
        if ([Text.Encoding]::UTF8.GetByteCount($metadataJson) -gt (7MB)) { throw 'Lifecycle metadata exceeds the 7 MiB transport cap.' }
        $resultMessage = if ($null -eq $script:Failure) { 'Combined VM lifecycle action ' + $script:LifecycleAction + ' completed with outcome ' + $status + '.' } else { [string]$script:Failure }
        $transportStatus = if ($null -eq $script:Failure) { 'PASS' } else { 'FAIL' }
        $transportResult = New-RunnerResultEnvelope -RunRoot $script:RunRootFull -Status $transportStatus -Message $resultMessage
        $stdoutText = @('VM LIFECYCLE ' + $script:LifecycleAction + ' ' + [string]$transportResult.Status) + @($script:Diagnostics.ToArray())
        if ($null -ne $resultDetails) { $stdoutText += ($resultDetails | ConvertTo-Json -Depth 30 -Compress) }
        Write-RunFile -Name 'kernel-metadata.json' -Text ($metadataJson + [Environment]::NewLine)
        Write-RunFile -Name 'stdout.txt' -Text (($stdoutText -join [Environment]::NewLine) + [Environment]::NewLine)
        Write-RunFile -Name 'stderr.txt' -Text $(if ($transportResult.Status -eq 'PASS') { '' } else { $resultMessage + [Environment]::NewLine })
        Write-JsonAtomic -Path (Join-Path $script:RunRootFull 'result.json') -Value $transportResult
    }
    catch {
        $transportFailure = 'Lifecycle evidence transport failed: ' + $_.Exception.ToString()
        $script:Failure = if ([string]::IsNullOrWhiteSpace([string]$script:Failure)) { $transportFailure } else { [string]$script:Failure + '; ' + $transportFailure }
        try {
            $transportResult = New-RunnerResultEnvelope -RunRoot $script:RunRootFull -Status 'FAIL' -Message $script:Failure
            $fallbackMetadata = New-LifecycleMetadata -Action $script:LifecycleAction -LifecycleId ([string]$script:Config.lifecycleId) -OutcomeStatus 'FAIL' -Result $record -Journal $null -RawEvidence $null
            $fallbackMetadata['evidenceError'] = $transportFailure
            Write-RunFile -Name 'kernel-metadata.json' -Text (($fallbackMetadata | ConvertTo-Json -Depth 20) + [Environment]::NewLine)
            Write-RunFile -Name 'stdout.txt' -Text ('VM LIFECYCLE ' + $script:LifecycleAction + ' FAIL' + [Environment]::NewLine)
            Write-RunFile -Name 'stderr.txt' -Text ($script:Failure + [Environment]::NewLine)
            Write-JsonAtomic -Path (Join-Path $script:RunRootFull 'result.json') -Value $transportResult
        }
        catch { Write-Error ('Lifecycle result publication failed: ' + $_.Exception.ToString()) }
    }
}

if ($status -eq 'FAIL') { Write-Error "VM lifecycle action failed: $script:Failure"; exit 1 }
Write-Output "VM LIFECYCLE $script:LifecycleAction $status"
Write-Output "Evidence: $script:RunRootFull"
if ($resultDetails) { Write-Output ($resultDetails | ConvertTo-Json -Depth 20 -Compress) }
