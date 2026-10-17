[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Enable','Run','Resume','Rollback','Verify','Status')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$ConfigPath,
    [string]$ExpectedManifestSha256,
    [switch]$Restart
)

# Incident quarantine: 0x7B followed boot-disk filter installation. Preserve source; do not reactivate.
throw 'This device reboot cycle is disabled after INACCESSIBLE_BOOT_DEVICE (0x7B). No host change was attempted. See spoof-handoff/reports/incident-recovery-20261017/REPORT.md.'
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$script:cycleRunDirectory = $null

$cycleRuntimeModule = Join-Path $PSScriptRoot 'runtime.psm1'
Import-Module -Name $cycleRuntimeModule -Force -ErrorAction Stop

function Write-CycleJsonNew {
    param([Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)]$Value)
    $directory = Split-Path -Parent $Path
    if (-not [IO.Directory]::Exists($directory)) { [void][IO.Directory]::CreateDirectory($directory) }
    $json = ConvertTo-Json -InputObject $Value -Depth 30
    $stream = [IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json); $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    return $Path
}

function Write-CycleTextNew {
    param([Parameter(Mandatory=$true)][string]$Path,[AllowEmptyString()][string]$Value)
    $directory = Split-Path -Parent $Path
    if (-not [IO.Directory]::Exists($directory)) { [void][IO.Directory]::CreateDirectory($directory) }
    [IO.File]::WriteAllText($Path,$Value,[Text.UTF8Encoding]::new($false))
    return $Path
}

function New-CycleEvidencePath {
    param([Parameter(Mandatory=$true)][string]$Name,[Parameter(Mandatory=$true)][string]$Extension)
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $nonce = [Guid]::NewGuid().ToString('N')
    return Join-Path $script:cycleRunDirectory ($Name + '.' + $stamp + '.' + $nonce + '.' + $Extension)
}

function ConvertTo-CycleArgument {
    param([AllowEmptyString()][string]$Value)
    if ($null -eq $Value) { $Value = '' }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]'\') { $slashes++; continue }
        if ($character -eq [char]'"') {
            [void]$builder.Append(('\' * (($slashes * 2) + 1)))
            [void]$builder.Append('"')
            $slashes = 0
            continue
        }
        if ($slashes -gt 0) { [void]$builder.Append(('\' * $slashes)); $slashes = 0 }
        [void]$builder.Append($character)
    }
    if ($slashes -gt 0) { [void]$builder.Append(('\' * ($slashes * 2))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-CycleProcess {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string]$Executable,
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [string]$WorkingDirectory=$script:cycleRunDirectory
    )
    $fullExe = [IO.Path]::GetFullPath($Executable)
    if (-not [IO.File]::Exists($fullExe)) { throw "Command executable is missing: $fullExe" }
    foreach ($argument in $Arguments) { if ($argument -match '[\r\n\0]') { throw 'Command arguments may not contain control characters.' } }
    $commandDirectory = Join-Path $script:cycleRunDirectory 'commands'
    if (-not [IO.Directory]::Exists($commandDirectory)) { [void][IO.Directory]::CreateDirectory($commandDirectory) }
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $stem = $Name + '.' + $stamp + '.' + [Guid]::NewGuid().ToString('N')
    $stdoutPath = Join-Path $commandDirectory ($stem + '.stdout.txt')
    $stderrPath = Join-Path $commandDirectory ($stem + '.stderr.txt')
    $resultPath = Join-Path $commandDirectory ($stem + '.result.json')
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $fullExe
    $start.WorkingDirectory = [IO.Path]::GetFullPath($WorkingDirectory)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Arguments = (($Arguments | ForEach-Object { ConvertTo-CycleArgument ([string]$_) }) -join ' ')
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "Failed to start command: $fullExe" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $exitCode = $process.ExitCode
    $process.Dispose()
    Write-CycleTextNew $stdoutPath $stdout | Out-Null
    Write-CycleTextNew $stderrPath $stderr | Out-Null
    $record = [ordered]@{name=$Name;executable=$fullExe;arguments=@($Arguments);workingDirectory=$start.WorkingDirectory;exitCode=$exitCode;stdoutPath=$stdoutPath;stderrPath=$stderrPath;capturedUtc=[DateTime]::UtcNow.ToString('o')}
    Write-CycleJsonNew $resultPath $record | Out-Null
    $record.resultPath = $resultPath
    $record.stdoutText = $stdout
    $record.stderrText = $stderr
    return [pscustomobject]$record
}

function Save-CycleState {
    param([Parameter(Mandatory=$true)]$State)
    [void](Write-DeviceCycleJsonAtomic -Path $script:cycleStatePath -Value $State)
}

function Add-CycleStateValue {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)][string]$Name,[AllowNull()][object]$Value)
    $State | Add-Member -MemberType NoteProperty -Name $Name -Value $Value -Force
}

function Open-CycleLock {
    $lockPath = Join-Path $script:cycleRunDirectory 'runtime.lock'
    try { return [IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
    catch { throw "Another device-cycle command owns the exclusive run lock: $lockPath; $($_.Exception.Message)" }
}

function Assert-CycleAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'This mode requires elevated PowerShell.' }
}

function Get-CycleSecureBoot {
    try { return [bool](Confirm-SecureBootUEFI -ErrorAction Stop) }
    catch { throw ('Confirm-SecureBootUEFI failed; Secure Boot state is unknown: ' + $_.Exception.Message) }
}

function Get-CycleBcdExecutable { return Join-Path $env:SystemRoot 'System32\bcdedit.exe' }

function Get-CycleBcdEnumeration {
    param([Parameter(Mandatory=$true)][string]$ObjectId,[Parameter(Mandatory=$true)][string]$Name)
    $result = Invoke-CycleProcess $Name (Get-CycleBcdExecutable) @('/enum',$ObjectId,'/v')
    if ($result.exitCode -ne 0) { throw ("bcdedit failed: object={0} exit={1} stdout={2} stderr={3}" -f $ObjectId,$result.exitCode,$result.stdoutPath,$result.stderrPath) }
    return [pscustomobject]@{lines=@($result.stdoutText -split "`r?`n");result=$result}
}

function Get-CycleCurrentBcdObject {
    $current = Get-CycleBcdEnumeration '{current}' 'bcd-current-enum'
    $identifier = $null
    foreach ($line in $current.lines) { if ($line -match '^\s*identifier\s+(.+?)\s*$') { $identifier = $Matches[1].Trim(); break } }
    if ([string]::IsNullOrWhiteSpace($identifier)) { throw 'bcdedit /enum {current} did not expose its identifier line.' }
    $setting = Get-DeviceCycleBcdSetting -Enumeration $current.lines -Name 'testsigning'
    return [pscustomobject]@{identifier=$identifier;testsigning=$setting;enumerationPath=$current.result.stdoutPath}
}

function Get-CycleBcdAll {
    $result = Invoke-CycleProcess 'bcd-all-enum' (Get-CycleBcdExecutable) @('/enum','all','/v')
    if ($result.exitCode -ne 0) { throw ("bcdedit /enum all /v failed: exit={0}; stdout={1}; stderr={2}" -f $result.exitCode,$result.stdoutPath,$result.stderrPath) }
    $path = New-CycleEvidencePath 'bcd-all' 'txt'
    Write-CycleTextNew $path $result.stdoutText | Out-Null
    return [pscustomobject]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;lines=@($result.stdoutText -split "`r?`n");exitCode=$result.exitCode}
}

function Get-CycleDriverInventory {
    $rows = @(Get-WindowsDriver -Online -ErrorAction Stop | Sort-Object Driver | ForEach-Object {
        $original = [string]$_.OriginalFileName
        $hash = $null
        if ($original -and [IO.File]::Exists($original)) { $hash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash }
        [pscustomobject]@{publishedName=[string]$_.Driver;originalFile=$original;infSha256=$hash;provider=[string]$_.ProviderName;class=[string]$_.ClassName;version=[string]$_.Version}
    })
    return $rows
}

function Get-CycleTrustMembership {
    param([Parameter(Mandatory=$true)][string]$Thumbprint)
    $rows = @()
    foreach ($store in @('Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPublisher')) {
        $present = $false
        if (Test-Path -LiteralPath $store) { $present = @((Get-ChildItem -LiteralPath $store -ErrorAction Stop | Where-Object { $_.Thumbprint -ieq $Thumbprint })).Count -gt 0 }
        $rows += [pscustomobject]@{store=$store;certificatePresent=$present}
    }
    return $rows
}

function Get-CycleOriginalTrustMembership {
    param([Parameter(Mandatory=$true)]$State)
    if ($null -ne $State.trustBefore -and @($State.trustBefore).Count -eq 2) { return @($State.trustBefore) }
    $path = Join-Path $script:cycleRunDirectory 'prepare-trust-before.json'
    if (-not [IO.File]::Exists($path)) { throw "Original certificate-store membership evidence is missing: $path" }
    $prepared = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -ErrorAction Stop
    $rows = @()
    foreach ($storeName in @('Root','TrustedPublisher')) {
        $entry = @($prepared | Where-Object { [string]$_.store -ceq $storeName })
        if ($entry.Count -ne 1) { throw "Prepare-time certificate membership must contain one $storeName row." }
        $rows += [pscustomobject]@{store=('Cert:\LocalMachine\'+$storeName);certificatePresent=[bool]$entry[0].targetPresent}
    }
    return $rows
}

function Get-CycleAdapterBindings {
    param([Parameter(Mandatory=$true)]$LockedMap,[string]$ComponentId='HypersinvNicAddressFilter')
    $adapters = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop)
    $rows = @()
    foreach ($locked in $LockedMap.adapters) {
        $matches = @($adapters | Where-Object { [string]$_.PnPDeviceID -ieq [string]$locked.pnpDeviceId })
        if ($matches.Count -ne 1) { $rows += [pscustomobject]@{pnpDeviceId=$locked.pnpDeviceId;adapterCount=$matches.Count;name=$null;interfaceDescription=$null;componentId=$ComponentId;bindingCount=0;enabled=$false}; continue }
        $bindings = @(Get-NetAdapterBinding -Name $matches[0].Name -AllBindings -ErrorAction Stop | Where-Object { [string]$_.ComponentID -ieq $ComponentId })
        $rows += [pscustomobject]@{pnpDeviceId=$locked.pnpDeviceId;adapterCount=1;name=[string]$matches[0].Name;interfaceDescription=[string]$matches[0].InterfaceDescription;componentId=$ComponentId;bindingCount=$bindings.Count;enabled=$(if ($bindings.Count -eq 1) {[bool]$bindings[0].Enabled} else {$null})}
    }
    return $rows
}

function Get-CycleNetCfgComponentState {
    param([string]$ComponentId='HypersinvNicAddressFilter')
    $result = Invoke-CycleProcess 'netcfg-components' (Join-Path $env:SystemRoot 'System32\netcfg.exe') @('/s','n')
    if ($result.exitCode -ne 0) { throw ("netcfg /s n failed: exit={0}; stdout={1}; stderr={2}" -f $result.exitCode,$result.stdoutPath,$result.stderrPath) }
    $present = $false
    foreach ($line in ($result.stdoutText -split "`r?`n")) {
        if ($line -match ('(?i)(^|[^A-Za-z0-9_])' + [regex]::Escape($ComponentId) + '([^A-Za-z0-9_]|$)')) { $present = $true; break }
    }
    return [pscustomobject]@{componentId=$ComponentId;present=$present;outputPath=$result.stdoutPath;sha256=(Get-FileHash -LiteralPath $result.stdoutPath -Algorithm SHA256).Hash}
}

function Get-CycleSystemDriver {
    param([Parameter(Mandatory=$true)][string]$Name)
    $rows = @(Get-CimInstance -ClassName Win32_SystemDriver -Filter ("Name='{0}'" -f $Name.Replace("'","''")) -OperationTimeoutSec 20 -ErrorAction Stop)
    return @($rows | ForEach-Object { [pscustomobject]@{name=[string]$_.Name;state=[string]$_.State;started=[bool]$_.Started;startMode=[string]$_.StartMode;pathName=[string]$_.PathName} })
}

function Get-CycleServiceRegistryState {
    $services = @('client_storage_filter','HypersinvNicAddressFilter')
    return @($services | ForEach-Object {
        $path = 'HKLM:\SYSTEM\CurrentControlSet\Services\' + $_
        [pscustomobject]@{name=$_;registryPath=$path;present=(Test-Path -LiteralPath $path)}
    })
}

function Get-CycleMachineSnapshot {
    param([Parameter(Mandatory=$true)]$LockedMap)
    $bcd = Get-CycleCurrentBcdObject
    $allBcd = Get-CycleBcdAll
    $snapshot = [ordered]@{
        schema='device-cycle-machine-snapshot.v1'
        capturedUtc=[DateTime]::UtcNow.ToString('o')
        secureBoot=(Get-CycleSecureBoot)
        currentBcdIdentifier=$bcd.identifier
        testSigningOriginal=$bcd.testsigning
        bcdAllPath=$allBcd.path
        bcdAllSha256=$allBcd.sha256
        certificateStores=(Get-CycleTrustMembership $script:cycleConfig.certificateThumbprint)
        driverStore=(Get-CycleDriverInventory)
        adapterBindings=(Get-CycleAdapterBindings -LockedMap $LockedMap)
        netcfgComponent=(Get-CycleNetCfgComponentState)
        storageService=(Get-CycleSystemDriver 'client_storage_filter')
        nicService=(Get-CycleSystemDriver 'HypersinvNicAddressFilter')
        serviceRegistry=(Get-CycleServiceRegistryState)
    }
    return $snapshot
}

function Test-CycleNoOwnedPreExistingState {
    param([Parameter(Mandatory=$true)]$Snapshot)
    $errors = New-Object 'System.Collections.Generic.List[string]'
    foreach ($row in $Snapshot.driverStore) {
        if ([IO.Path]::GetFileName([string]$row.originalFile) -in @('client_storage_filter.inf','client_nic_filter.inf')) { $errors.Add(('Pre-existing owned Driver Store package: '+$row.publishedName+' '+$row.originalFile)) }
    }
    if (@($Snapshot.storageService).Count -gt 0) { $errors.Add('The storage filter service already exists.') }
    if (@($Snapshot.nicService).Count -gt 0) { $errors.Add('The NIC filter service already exists.') }
    if (@($Snapshot.serviceRegistry | Where-Object { $_.present }).Count -gt 0) { $errors.Add('A target filter service registry key already exists.') }
    if (@($Snapshot.adapterBindings | Where-Object { $_.adapterCount -ne 1 }).Count -gt 0) { $errors.Add('One or more locked adapter identities were not uniquely visible in the live adapter inventory.') }
    if (@($Snapshot.adapterBindings | Where-Object { $_.bindingCount -gt 0 -and $_.enabled }).Count -gt 0) { $errors.Add('The NIC filter is already bound to a selected adapter.') }
    if ($Snapshot.netcfgComponent.present) { $errors.Add('The NIC NetCfg component is already installed.') }
    if ($errors.Count) { throw ('Pre-existing owned state requires review; no new cycle changes were made: ' + ($errors -join '; ')) }
}

function Assert-CycleSignedPayload {
    param([Parameter(Mandatory=$true)]$Manifest)
    $signatureProofPath = Join-Path $script:cycleConfig.runDirectory 'payload-signature-proof.json'
    $required = @($script:cycleConfigPath,$script:cycleRuntimeModule,$script:cycleConfig.runnerPath,$script:cycleConfig.proofModulePath,$script:cycleConfig.snapshotLibraryPath,$script:cycleConfig.espPolicyPath,$script:cycleConfig.readerPath,$script:cycleConfig.lockedTsvPath,$script:cycleConfig.certificatePath,$signatureProofPath)
    foreach ($path in $required) {
        $full = [IO.Path]::GetFullPath([string]$path)
        if (-not (@($Manifest.pins | Where-Object { [string]::Equals([IO.Path]::GetFullPath($_.path),$full,[StringComparison]::OrdinalIgnoreCase) }).Count -eq 1)) { throw "Required runtime input is not included in the protected pin manifest: $full" }
    }
    if ([IO.Path]::GetFileName([string]$script:cycleConfig.readerPath) -cne 'native_device_io.exe') { throw 'Reader path must name the independently built native_device_io.exe.' }
    if ([IO.Path]::GetFileName([string]$script:cycleConfig.proofModulePath) -cne 'device-cycle-proof.psm1') { throw 'The pinned native proof module is required.' }
    if (-not [IO.File]::Exists($signatureProofPath)) { throw "Prepare-time Authenticode proof is missing: $signatureProofPath" }
    $signatureRows = Get-Content -LiteralPath $signatureProofPath -Raw | ConvertFrom-Json -ErrorAction Stop
    foreach ($path in @($script:cycleConfig.runnerPath,$script:cycleRuntimeModule,$script:cycleConfig.proofModulePath,$script:cycleConfig.snapshotLibraryPath,$script:cycleConfig.espPolicyPath,$script:cycleConfig.readerPath)) {
        $full = [IO.Path]::GetFullPath($path)
        $pin = @($Manifest.pins | Where-Object { [string]::Equals([IO.Path]::GetFullPath($_.path),$full,[StringComparison]::OrdinalIgnoreCase) })
        if ($pin.Count -ne 1) { throw "Signed startup dependency is not uniquely pinned: $full" }
        $receipt = @($signatureRows | Where-Object { [string]::Equals([IO.Path]::GetFullPath([string]$_.path),$full,[StringComparison]::OrdinalIgnoreCase) })
        if ($receipt.Count -ne 1 -or -not [bool]$receipt[0].pass -or [string]$receipt[0].status -cne 'Valid' -or [string]$receipt[0].thumbprint -ine $script:cycleConfig.certificateThumbprint) {
            throw ("Prepare-time signature receipt is not a unique valid proof for {0}: rows={1}; status={2}; signer={3}" -f $full,$receipt.Count,$(if ($receipt.Count) {$receipt[0].status} else {'<missing>'}),$(if ($receipt.Count) {$receipt[0].thumbprint} else {'<missing>'}))
        }
        $actual = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash
        if ([string]$receipt[0].sha256 -cne $actual -or [string]$pin[0].sha256 -ine $actual) { throw ("Signed startup dependency hash differs from its prepare receipt or protected manifest: {0}; receipt={1}; manifest={2}; actual={3}" -f $full,$receipt[0].sha256,$pin[0].sha256,$actual) }
        $signature = Get-AuthenticodeSignature -LiteralPath $full
        $signatureDisposition = Test-DeviceCyclePreTrustSignatureStatus -Status ([string]$signature.Status) -StatusMessage ([string]$signature.StatusMessage) -ExpectedThumbprint $script:cycleConfig.certificateThumbprint -ActualThumbprint $(if ($signature.SignerCertificate) {[string]$signature.SignerCertificate.Thumbprint} else {''})
        if (-not $signatureDisposition.accepted) {
            throw ("Pinned signed dependency no longer verifies under the recorded signer: {0}; status={1}; statusMessage={2}; signer={3}; reason={4}" -f $full,$signature.Status,$signature.StatusMessage,$(if ($signature.SignerCertificate) {$signature.SignerCertificate.Thumbprint} else {'<missing>'}),$signatureDisposition.reason)
        }
    }
}

function Assert-CycleStartupSignaturesTrusted {
    foreach ($path in @($script:cycleConfig.runnerPath,$script:cycleRuntimeModule,$script:cycleConfig.proofModulePath,$script:cycleConfig.snapshotLibraryPath,$script:cycleConfig.espPolicyPath,$script:cycleConfig.readerPath)) {
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid -or $null -eq $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ine $script:cycleConfig.certificateThumbprint) {
            throw ("Signed startup dependency is not trusted after certificate import: {0}; status={1}; signer={2}" -f $path,$signature.Status,$(if ($signature.SignerCertificate) {$signature.SignerCertificate.Thumbprint} else {'<missing>'}))
        }
    }
}

function Assert-CycleStateEvidencePins {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)]$Manifest)
    $required = @([string]$State.originalBeforePath)
    foreach ($name in @('snapshot.json','firmware-variables.json','esp-layout.json','bcd-all.txt')) { $required += Join-Path $script:cycleConfig.snapshotPath $name }
    foreach ($path in $required) {
        $full = [IO.Path]::GetFullPath($path)
        if (-not (@($Manifest.pins | Where-Object { [string]::Equals([IO.Path]::GetFullPath($_.path),$full,[StringComparison]::OrdinalIgnoreCase) }).Count -eq 1)) { throw "Fresh protected state evidence is not pinned by the signed manifest: $full" }
    }
}

function Import-CycleProofModule {
    Import-Module -Name $script:cycleConfig.proofModulePath -Force -DisableNameChecking -ErrorAction Stop
    foreach ($name in @('Test-DeviceCycleNativeCapture','Test-DeviceCycleNativeRoundTrip','Test-DeviceCycleWmiOriginals','Assert-DeviceCycleProof')) {
        if (-not (Get-Command -Name $name -ErrorAction SilentlyContinue)) { throw "Required proof function is missing from signed module: $name" }
    }
}

function Get-CycleLockedMap {
    $path = [IO.Path]::GetFullPath($script:cycleConfig.lockedTsvPath)
    if (-not [IO.File]::Exists($path)) { throw "Locked value TSV is missing: $path" }
    $source = @(Import-Csv -LiteralPath $path -Delimiter "`t")
    $diskRows = @($source | Where-Object { $_.member -ceq 'FakeDiskSerial' })
    $adapterRows = @($source | Where-Object { $_.member -ceq 'FakeMacAddress' })
    if ($diskRows.Count -ne 1 -or $adapterRows.Count -ne 4) { throw "Locked TSV must contain exactly one disk and four adapter rows; disk=$($diskRows.Count) adapters=$($adapterRows.Count)" }
    if ($diskRows[0].device -cne 'disk:\\.\PHYSICALDRIVE0') { throw ('Locked disk row changed: '+$diskRows[0].device) }
    $disk = [pscustomobject]@{member=$diskRows[0].member;device=$diskRows[0].device;original=$diskRows[0].original;replacement=$diskRows[0].replacement;pnpDeviceId='SCSI\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\5&8432851&0&000000';devicePath='\\.\PHYSICALDRIVE0'}
    $adapters = @()
    foreach ($row in $adapterRows) {
        if ($row.device -notmatch '^adapter:(\d+)\|(.+)$') { throw ('Malformed locked adapter identity: '+$row.device) }
        $sourceIndex = [int]$Matches[1]
        $pnpDeviceId = [string]$Matches[2]
        if ($row.original -notmatch '^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$' -or $row.replacement -notmatch '^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$') { throw ('Malformed locked MAC values for '+$row.device) }
        $adapters += [pscustomobject]@{member=$row.member;device=$row.device;original=$row.original;replacement=$row.replacement;sourceIndex=$sourceIndex;pnpDeviceId=$pnpDeviceId}
    }
    if (@($adapters.pnpDeviceId | Select-Object -Unique).Count -ne 4) { throw 'Locked TSV contains duplicate adapter PnP identities.' }
    return [pscustomobject]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;disk=$disk;adapters=$adapters}
}

function Invoke-CycleNativeCapture {
    param([Parameter(Mandatory=$true)][string]$Name)
    $capturePath = New-CycleEvidencePath $Name 'json'
    $result = Invoke-CycleProcess ('native-reader-' + $Name) $script:cycleConfig.readerPath @('--capture',$capturePath)
    if ($result.exitCode -ne 0) { throw ("Independent native reader failed: exit={0}; stdout={1}; stderr={2}" -f $result.exitCode,$result.stdoutPath,$result.stderrPath) }
    if (-not [IO.File]::Exists($capturePath)) { throw "Independent native reader exited 0 but did not create its evidence: $capturePath" }
    return [pscustomobject]@{path=$capturePath;sha256=(Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash;command=$result}
}

function Assert-CycleNativeCapture {
    param([Parameter(Mandatory=$true)][string]$CapturePath,[Parameter(Mandatory=$true)][ValidateSet('Original','Replacement')][string]$ExpectedMode,[switch]$AllowFailure)
    $proof = Test-DeviceCycleNativeCapture -CapturePath $CapturePath -LockedTsvPath $script:cycleConfig.lockedTsvPath -Mode $ExpectedMode -NativeReaderPath $script:cycleConfig.readerPath
    $proofPath = New-CycleEvidencePath ('native-' + $ExpectedMode.ToLowerInvariant() + '-proof') 'json'
    Write-CycleJsonNew $proofPath $proof | Out-Null
    $pass = [bool]$proof.pass
    if (-not $pass -and -not $AllowFailure) { $failed = @($proof.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.line }); throw ('Native ' + $ExpectedMode + ' proof failed:' + [Environment]::NewLine + ($failed -join [Environment]::NewLine) + [Environment]::NewLine + 'Proof='+$proofPath) }
    return [pscustomobject]@{path=$CapturePath;sha256=(Get-FileHash -LiteralPath $CapturePath -Algorithm SHA256).Hash;proofPath=$proofPath;proofSha256=(Get-FileHash -LiteralPath $proofPath -Algorithm SHA256).Hash;pass=$pass;checks=@($proof.checks)}
}

function New-CycleWmiCapture {
    param([Parameter(Mandatory=$true)][string]$Name)
    $path = New-CycleEvidencePath $Name 'json'
    $diskError = $null; $adapterError = $null
    $disks = @(); $adapters = @()
    try { $disks = @(Get-CimInstance -ClassName Win32_DiskDrive -OperationTimeoutSec 20 -ErrorAction Stop | Select-Object DeviceID,PNPDeviceID,SerialNumber,Index) }
    catch { $diskError = $_.Exception.Message }
    try { $adapters = @(Get-CimInstance -ClassName Win32_NetworkAdapter -OperationTimeoutSec 20 -ErrorAction Stop | Select-Object DeviceID,Index,InterfaceIndex,GUID,PNPDeviceID,Name,MACAddress,PhysicalAdapter,NetEnabled) }
    catch { $adapterError = $_.Exception.Message }
    $complete = ($null -eq $diskError -and $null -eq $adapterError)
    $document = [ordered]@{
        schema='device-cycle-wmi-readback.v1'
        capturedUtc=[DateTime]::UtcNow.ToString('o')
        captureComplete=$complete
        wmiSource='Get-CimInstance Win32_DiskDrive; Get-CimInstance Win32_NetworkAdapter'
        disks=@($disks)
        adapters=@($adapters)
        errors=[ordered]@{disks=$diskError;adapters=$adapterError}
        activeReplacementClaim=$false
        note='WMI is used for the final original-value check only; active WMI cache values are not treated as replacement evidence.'
    }
    Write-CycleJsonNew $path $document | Out-Null
    return [pscustomobject]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;captureComplete=$complete;document=$document}
}

function Assert-CycleWmiOriginalCapture {
    param([Parameter(Mandatory=$true)][string]$CapturePath,[Parameter(Mandatory=$true)][string]$NotBeforeUtc,[switch]$AllowFailure)
    $proof = Test-DeviceCycleWmiOriginals -CapturePath $CapturePath -LockedTsvPath $script:cycleConfig.lockedTsvPath -NotBeforeUtc $NotBeforeUtc
    $proofPath = New-CycleEvidencePath 'wmi-original-proof' 'json'
    Write-CycleJsonNew $proofPath $proof | Out-Null
    $pass = [bool]$proof.pass
    if (-not $pass -and -not $AllowFailure) { $failed = @($proof.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.line }); throw ('Fresh WMI originals proof failed:' + [Environment]::NewLine + ($failed -join [Environment]::NewLine) + [Environment]::NewLine + 'Proof='+$proofPath) }
    return [pscustomobject]@{path=$CapturePath;sha256=(Get-FileHash -LiteralPath $CapturePath -Algorithm SHA256).Hash;proofPath=$proofPath;proofSha256=(Get-FileHash -LiteralPath $proofPath -Algorithm SHA256).Hash;pass=$pass;checks=@($proof.checks)}
}

function Invoke-CycleBcdSet {
    param([Parameter(Mandatory=$true)][string]$ObjectId,[Parameter(Mandatory=$true)][string]$Value)
    $result = Invoke-CycleProcess ('bcd-set-testsigning-' + $Value) (Get-CycleBcdExecutable) @('/set',$ObjectId,'testsigning',$Value)
    if ($result.exitCode -ne 0) { throw ("bcdedit /set {0} testsigning {1} failed: exit={2}; stdout={3}; stderr={4}" -f $ObjectId,$Value,$result.exitCode,$result.stdoutPath,$result.stderrPath) }
    return $result
}

function Invoke-CycleBcdRestore {
    param([Parameter(Mandatory=$true)]$Original,[Parameter(Mandatory=$true)][string]$ObjectId)
    $arguments = @(Get-DeviceCycleBcdRestoreArguments -ObjectId $ObjectId -Original $Original)
    $result = Invoke-CycleProcess 'bcd-restore-testsigning' (Get-CycleBcdExecutable) $arguments
    if ($result.exitCode -ne 0) { throw ("BCD TESTSIGNING restoration failed: args={0}; exit={1}; stdout={2}; stderr={3}" -f ($arguments -join ' '),$result.exitCode,$result.stdoutPath,$result.stderrPath) }
    $actual = Get-CycleBcdCurrentObject
    if ([bool]$actual.testsigning.present -ne [bool]$Original.present -or ([bool]$Original.present -and [bool]$actual.testsigning.value -ne [bool]$Original.value)) {
        throw ("BCD TESTSIGNING restore mismatch: expectedPresent={0} expectedValue={1} actualPresent={2} actualValue={3}" -f $Original.present,$Original.value,$actual.testsigning.present,$actual.testsigning.value)
    }
    return $result
}

function Get-CyclePublishedStoragePackage {
    param([Parameter(Mandatory=$true)]$State)
    $rows = @(Get-CycleDriverInventory | Where-Object { [IO.Path]::GetFileName([string]$_.originalFile) -ceq 'client_storage_filter.inf' })
    if ($rows.Count -gt 1) { throw ('More than one storage package is present: '+(($rows | ForEach-Object {$_.publishedName}) -join ', ')) }
    if ($rows.Count -eq 1) {
        $ownedNames = @($State.ownedPackages | ForEach-Object { [string]$_.publishedName })
        if ($ownedNames -notcontains [string]$rows[0].publishedName) { throw ('Storage package exists but is not owned by this run; refusing removal: '+$rows[0].publishedName) }
    }
    return $rows
}

function Invoke-CyclePnpAddStorage {
    param([Parameter(Mandatory=$true)]$State)
    $folder = Join-Path $script:cycleConfig.packageDirectory 'storage'
    $inf = Join-Path $folder 'client_storage_filter.inf'
    $sys = Join-Path $folder 'client_storage_filter.sys'
    $cat = Join-Path $folder 'client_storage_filter.cat'
    foreach ($path in @($inf,$sys,$cat)) { if (-not [IO.File]::Exists($path)) { throw "Storage package is incomplete: $path" } }
    $infHash = (Get-FileHash -LiteralPath $inf -Algorithm SHA256).Hash
    Add-CycleStateValue $State 'storageInstallStarted' $true
    Add-CycleStateValue $State 'storageInstallInfSha256' $infHash
    Save-CycleState $State
    $result = Invoke-CycleProcess 'pnputil-add-storage-install' (Join-Path $env:SystemRoot 'System32\pnputil.exe') @('/add-driver',$inf,'/install')
    $after = Get-CycleDriverInventory
    Add-CycleStateValue $State 'storageInstallAfterInventory' $after
    Save-CycleState $State
    $delta = Test-DeviceCycleOwnedPackageDelta -Before @($State.driverStoreBefore) -After $after -InfName 'client_storage_filter.inf' -InfSha256 $infHash
    if (-not $delta.pass) { throw ('Could not establish exact ownership of the newly staged storage INF: '+($delta.errors -join '; ')) }
    if ($delta.owned.Count -eq 1) {
        $owned = $State.ownedPackages | Where-Object { $_.publishedName -ceq $delta.owned[0].publishedName }
        if (-not $owned) { $State.ownedPackages += @([ordered]@{publishedName=$delta.owned[0].publishedName;originalFile=$delta.owned[0].originalFile;infSha256=$infHash;infPath=$inf}) }
        Save-CycleState $State
    }
    $disposition = Get-DeviceCyclePnpExitDisposition -ExitCode $result.exitCode
    Add-CycleStateValue $State 'storageInstallCommandDisposition' $disposition
    Save-CycleState $State
    if (-not $disposition.accepted) { throw ("pnputil /add-driver /install failed after ownership inventory: exit={0}; stdout={1}; stderr={2}" -f $result.exitCode,$result.stdoutPath,$result.stderrPath) }
    if ($delta.owned.Count -ne 1) { throw ("PnPUtil returned exit {0}, but no single new storage package was attributable to this run." -f $result.exitCode) }
    return [pscustomobject]@{publishedName=$delta.owned[0].publishedName;infSha256=$infHash;package=$delta.owned[0];command=$result;rebootRequired=$disposition.rebootRequired}
}

function Install-CycleTrust {
    param([Parameter(Mandatory=$true)]$State)
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes($script:cycleConfig.certificatePath))
    if ($certificate.Thumbprint -ine $script:cycleConfig.certificateThumbprint) { throw ('Existing certificate thumbprint mismatch: expected='+$script:cycleConfig.certificateThumbprint+' actual='+$certificate.Thumbprint) }
    $before = @(Get-CycleTrustMembership $certificate.Thumbprint)
    Add-CycleStateValue $State 'trustBefore' $before
    Save-CycleState $State
    foreach ($store in @('Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPublisher')) {
        $entry = @($before | Where-Object { $_.store -ceq $store })[0]
        if (-not $entry.certificatePresent) {
            $result = Invoke-CycleProcess ('certutil-add-' + $(if ($store -like '*Root') {'root'} else {'trustedpublisher'})) (Join-Path $env:SystemRoot 'System32\certutil.exe') @('-addstore','-f',$(if ($store -like '*Root') {'Root'} else {'TrustedPublisher'}),$script:cycleConfig.certificatePath)
            if ($result.exitCode -ne 0) { throw ("certutil import failed: store={0} exit={1}; stdout={2}; stderr={3}" -f $store,$result.exitCode,$result.stdoutPath,$result.stderrPath) }
        }
    }
    $after = @(Get-CycleTrustMembership $certificate.Thumbprint)
    foreach ($row in $after) { if (-not $row.certificatePresent) { throw ('Certificate import did not result in expected trust membership: '+$row.store) } }
    Add-CycleStateValue $State 'trustAfterImport' $after
    Save-CycleState $State
}

function Restore-CycleTrust {
    param([Parameter(Mandatory=$true)]$State)
    if ($null -eq $State.trustBefore) { return }
    $current = @(Get-CycleTrustMembership $script:cycleConfig.certificateThumbprint)
    foreach ($row in $current) {
        $original = @($State.trustBefore | Where-Object { $_.store -ceq $row.store })
        if ($original.Count -ne 1) { throw ('No captured trust membership for '+$row.store) }
        if (-not [bool]$original[0].certificatePresent -and $row.certificatePresent) {
            $storePath = $row.store
            $matches = @(Get-ChildItem -LiteralPath $storePath -ErrorAction Stop | Where-Object { $_.Thumbprint -ieq $script:cycleConfig.certificateThumbprint })
            if ($matches.Count -gt 1) { throw ('Duplicate exact signer entries in store; manual review required: '+$storePath) }
            if ($matches.Count -eq 1) { Remove-Item -LiteralPath $matches[0].PSPath -ErrorAction Stop }
        }
    }
    $after = @(Get-CycleTrustMembership $script:cycleConfig.certificateThumbprint)
    foreach ($row in $after) {
        $original = @($State.trustBefore | Where-Object { $_.store -ceq $row.store })[0]
        if ([bool]$row.certificatePresent -ne [bool]$original.certificatePresent) { throw ("Trust membership restore mismatch: store={0} expected={1} actual={2}" -f $row.store,$original.certificatePresent,$row.certificatePresent) }
    }
    Add-CycleStateValue $State 'trustAfterRestore' $after
    Save-CycleState $State
}

function Register-CycleStartupTask {
    param([Parameter(Mandatory=$true)]$State)
    $taskName = [string]$script:cycleConfig.taskName
    if ([string]::IsNullOrWhiteSpace($taskName)) { throw 'The protected taskName is missing.' }
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) { throw ('Scheduled task already exists and is not owned by this run: '+$taskName) }
    $pwsh = Join-Path $PSHOME 'powershell.exe'
    $arguments = @('-NoProfile','-ExecutionPolicy','AllSigned','-File',$script:cycleConfig.runnerPath,'-Mode','Resume','-ConfigPath',$script:cycleConfigPath,'-ExpectedManifestSha256',$script:cycleManifest.sha256)
    $argumentText = ($arguments | ForEach-Object { ConvertTo-CycleArgument ([string]$_) }) -join ' '
    $action = New-ScheduledTaskAction -Execute $pwsh -Argument $argumentText
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 20)
    $task = Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description ('One-shot machine-identifier device filter cleanup for run '+$State.runId) -ErrorAction Stop
    $State.task = [ordered]@{name=$taskName;owned=$true;actionExecutable=$pwsh;actionArguments=$arguments;actionText=$argumentText;expectedManifestSha256=$script:cycleManifest.sha256;registeredUtc=[DateTime]::UtcNow.ToString('o')}
    Save-CycleState $State
    return [pscustomobject]@{taskName=$task.TaskName;taskPath=$task.TaskPath;actionArguments=$arguments}
}

function Assert-CycleStartupTaskOwnership {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)]$Task)
    if ($null -eq $State.task -or -not [bool]$State.task.owned -or [string]$State.task.name -cne [string]$script:cycleConfig.taskName) {
        throw 'A startup task exists but this run has no matching ownership record; refusing to remove or adopt it.'
    }
    $actions = @($Task.Actions)
    if ($actions.Count -ne 1) { throw 'Owned startup task action count changed; refusing to remove the task.' }
    $expectedExecutable = [IO.Path]::GetFullPath([string]$State.task.actionExecutable)
    $actualExecutable = [IO.Path]::GetFullPath([string]$actions[0].Execute)
    if (-not [string]::Equals($actualExecutable,$expectedExecutable,[StringComparison]::OrdinalIgnoreCase) -or [string]$actions[0].Arguments -cne [string]$State.task.actionText) {
        throw 'Owned startup task command changed; refusing to remove the task.'
    }
    $userId = [string]$Task.Principal.UserId
    if ($userId -notin @('SYSTEM','NT AUTHORITY\SYSTEM') -or [string]$Task.Principal.LogonType -notin @('ServiceAccount','Service Account')) {
        throw 'Owned startup task principal changed; refusing to remove the task.'
    }
}

function Adopt-CycleStartupTaskIfExact {
    param([Parameter(Mandatory=$true)]$State)
    $taskName = [string]$script:cycleConfig.taskName
    $existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if (-not $existing) { return $false }
    $pwsh = Join-Path $PSHOME 'powershell.exe'
    $arguments = @('-NoProfile','-ExecutionPolicy','AllSigned','-File',$script:cycleConfig.runnerPath,'-Mode','Resume','-ConfigPath',$script:cycleConfigPath,'-ExpectedManifestSha256',$script:cycleManifest.sha256)
    $argumentText = ($arguments | ForEach-Object { ConvertTo-CycleArgument ([string]$_) }) -join ' '
    $State.task = [ordered]@{name=$taskName;owned=$true;actionExecutable=$pwsh;actionArguments=$arguments;actionText=$argumentText;expectedManifestSha256=$script:cycleManifest.sha256;recoveredFromProtectedRunIdentity=$true}
    Assert-CycleStartupTaskOwnership $State $existing
    Save-CycleState $State
    return $true
}

function Unregister-CycleStartupTask {
    param([Parameter(Mandatory=$true)]$State)
    if ($null -eq $State.task -or -not [bool]$State.task.owned) { return }
    $task = Get-ScheduledTask -TaskName $State.task.name -ErrorAction SilentlyContinue
    if ($task) {
        Assert-CycleStartupTaskOwnership $State $task
        Unregister-ScheduledTask -TaskName $State.task.name -Confirm:$false -ErrorAction Stop
    }
    if (Get-ScheduledTask -TaskName $State.task.name -ErrorAction SilentlyContinue) { throw ('Owned startup task remains after deletion: '+$State.task.name) }
    Add-CycleStateValue $State 'taskRemovedUtc' ([DateTime]::UtcNow.ToString('o'))
    Save-CycleState $State
}

function Assert-CycleBootTransition {
    param([Parameter(Mandatory=$true)]$State)
    if ([string]::IsNullOrWhiteSpace([string]$State.enableBootIdentity)) { throw 'The pre-enable boot identity was not recorded.' }
    $os = Get-CimInstance -ClassName Win32_OperatingSystem -OperationTimeoutSec 20 -ErrorAction Stop
    $actual = ([DateTime]$os.LastBootUpTime).ToUniversalTime().ToString('o')
    if ($actual -ceq [string]$State.enableBootIdentity) { throw ('Expected a reboot after TESTSIGNING change; boot identity is unchanged: '+$actual) }
    Add-CycleStateValue $State 'policyBootIdentity' $actual
    Save-CycleState $State
}

function Enable-Cycle {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)]$LockedMap)
    Assert-CycleAdmin
    if (Get-CycleSecureBoot) { throw 'Secure Boot must be Disabled in firmware before Enable; no machine changes were attempted.' }
    if ($State.phase -cne 'Prepared') { throw ('Enable requires Prepared state; actual='+$State.phase) }
    $snapshot = Get-CycleMachineSnapshot $LockedMap
    if ($snapshot.secureBoot) { throw 'Secure Boot state changed during Enable preflight.' }
    Test-CycleNoOwnedPreExistingState $snapshot
    $initialProofPath = [string]$State.originalBeforeProofPath
    if (-not [string]::IsNullOrWhiteSpace($initialProofPath)) {
        Assert-DeviceCycleProof (Test-DeviceCycleNativeCapture -CapturePath $State.originalBeforePath -LockedTsvPath $script:cycleConfig.lockedTsvPath -Mode Original -NativeReaderPath $script:cycleConfig.readerPath) | Out-Null
    } else {
        $initialProof = Assert-CycleNativeCapture -CapturePath $State.originalBeforePath -ExpectedMode Original
        Add-CycleStateValue $State 'originalBeforeProofPath' $initialProof.proofPath
    }
    $stateSnapshotPath = New-CycleEvidencePath 'machine-before-enable' 'json'
    Write-CycleJsonNew $stateSnapshotPath $snapshot | Out-Null
    $State | Add-Member -MemberType NoteProperty -Name machineBeforePath -Value $stateSnapshotPath -Force
    $State | Add-Member -MemberType NoteProperty -Name secureBootOffSeen -Value $true -Force
    $State | Add-Member -MemberType NoteProperty -Name cycleStartUtc -Value ([DateTime]::UtcNow.ToString('o')) -Force
    $State | Add-Member -MemberType NoteProperty -Name originalBcd -Value $snapshot.testSigningOriginal -Force
    $State | Add-Member -MemberType NoteProperty -Name originalBcdIdentifier -Value $snapshot.currentBcdIdentifier -Force
    $State | Add-Member -MemberType NoteProperty -Name driverStoreBefore -Value @($snapshot.driverStore) -Force
    $State | Add-Member -MemberType NoteProperty -Name ownedPackages -Value @() -Force
    $State | Add-Member -MemberType NoteProperty -Name commands -Value @() -Force
    Set-DeviceCyclePhase $State 'Enabling' | Out-Null
    Add-CycleStateValue $State 'mutationIntent' ([ordered]@{atUtc=[DateTime]::UtcNow.ToString('o');trustImport=$true;testSigning=$true;startupTask=$true;storagePackageInstall=$true;storageInfPath=(Join-Path (Join-Path $script:cycleConfig.packageDirectory 'storage') 'client_storage_filter.inf')})
    Save-CycleState $State
    try {
        Install-CycleTrust $State
        Assert-CycleStartupSignaturesTrusted
        $current = Get-CycleCurrentBcdObject
        if ($current.identifier -cne $snapshot.currentBcdIdentifier) { throw 'Current boot object identifier changed after preflight.' }
        Add-CycleStateValue $State 'enableBootIdentity' (([DateTime](Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 20).LastBootUpTime).ToUniversalTime().ToString('o'))
        Save-CycleState $State
        Register-CycleStartupTask $State | Out-Null
        Invoke-CycleBcdSet -ObjectId $current.identifier -Value 'on' | Out-Null
        Add-CycleStateValue $State 'testSigningEnabled' $true
        Save-CycleState $State
        $afterBcd = Get-CycleCurrentBcdObject
        if (-not $afterBcd.testsigning.present -or -not $afterBcd.testsigning.value) { throw 'bcdedit returned success, but testsigning is not Yes in the current object.' }
        $storage = Invoke-CyclePnpAddStorage $State
        Add-CycleStateValue $State 'ownedStoragePackage' $storage
        Set-DeviceCyclePhase $State 'PolicyBootPending' | Out-Null
        Add-CycleStateValue $State 'ready' $false
        Add-CycleStateValue $State 'liveRoundTripProven' $false
        Add-CycleStateValue $State 'nextAction' 'Restart Windows once with Secure Boot Disabled; then run the signed runner in Mode Run.'
        Save-CycleState $State
        if ($Restart) { Restart-Computer -Force }
        return [pscustomobject]@{result='POLICY-BOOT-PENDING';statePath=$script:cycleStatePath;storagePublishedInf=$storage.publishedName;restartRequested=[bool]$Restart;nextAction=$State.nextAction}
    } catch {
        $failure = $_.Exception.Message
        Add-CycleStateValue $State 'lastFailure' ([ordered]@{phase='Enable';message=$failure;atUtc=[DateTime]::UtcNow.ToString('o')})
        $owned = @($State.ownedPackages)
        $startupTaskPresent = $false
        $startupTask = Get-ScheduledTask -TaskName $script:cycleConfig.taskName -ErrorAction SilentlyContinue
        if ($startupTask) {
            try { Assert-CycleStartupTaskOwnership $State $startupTask; $startupTaskPresent = $true }
            catch { Add-CycleStateValue $State 'startupTaskOwnershipFailure' $_.Exception.Message }
        }
        $failureDisposition = Get-DeviceCycleEnableFailureDisposition -StorageInstallStarted ([bool]$State.storageInstallStarted) -OwnedStoragePackageCount $owned.Count -OwnedStartupTaskPresent $startupTaskPresent
        Add-CycleStateValue $State 'enableFailureDisposition' $failureDisposition
        if ($failureDisposition.recoveryBootRequired) {
            Set-DeviceCyclePhase $State 'RecoveryBootPending' | Out-Null
            Add-CycleStateValue $State 'nextAction' 'Keep Secure Boot Disabled; restart once so the verified run-owned signed startup task can inspect and remove only run-owned packages, then run Mode Status.'
        } elseif ($failureDisposition.immediateRollback) {
            try {
                $liveBcd = Get-CycleCurrentBcdObject
                if ($liveBcd.testsigning.present -ne $State.originalBcd.present -or ($State.originalBcd.present -and $liveBcd.testsigning.value -ne $State.originalBcd.value)) { Invoke-CycleBcdRestore $State.originalBcd $State.originalBcdIdentifier | Out-Null }
                Unregister-CycleStartupTask $State
                Restore-CycleTrust $State
                $restoredBcd = Get-CycleCurrentBcdObject
                if ($restoredBcd.testsigning.present -ne $State.originalBcd.present -or ($State.originalBcd.present -and $restoredBcd.testsigning.value -ne $State.originalBcd.value)) { throw 'Immediate TESTSIGNING rollback did not restore its recorded representation.' }
                Set-DeviceCyclePhase $State 'RolledBackAwaitingSecureBootOn' | Out-Null
                Add-CycleStateValue $State 'nextAction' 'No driver package installation was attempted; temporary task, TESTSIGNING and trust changes were restored. Re-enable Secure Boot in firmware, then run Mode Verify.'
            }
            catch {
                Add-CycleStateValue $State 'rollbackFailure' $_.Exception.Message
                Set-DeviceCyclePhase $State 'RecoveryBootPending' | Out-Null
                if ($null -ne (Get-ScheduledTask -TaskName $script:cycleConfig.taskName -ErrorAction SilentlyContinue)) { Add-CycleStateValue $State 'nextAction' 'No storage installation was attempted, but immediate rollback failed. The signed task remains available if its trust is intact; inspect rollbackFailure and retry Mode Rollback before re-enabling Secure Boot.' }
                else { Add-CycleStateValue $State 'nextAction' 'No storage installation was attempted, but immediate rollback failed and no startup task remains. Inspect rollbackFailure, restore the recorded TESTSIGNING/trust values manually, and then run Mode Verify.' }
            }
        } else {
            # Storage mutation may have started, but an absent or mismatched task
            # cannot safely be called a reboot-recovery path. Preserve the recorded
            # policy/trust and leave the state in Enabling so Mode Rollback can be
            # invoked after an operator reviews the exact package inventory.
            Add-CycleStateValue $State 'nextAction' 'A storage install may have started, but there is no verified run-owned signed startup task. Keep Secure Boot Disabled and TESTSIGNING enabled. Review the recorded exact Driver Store delta and task ownership; do not reboot or remove trust until a valid cleanup path is established.'
        }
        Save-CycleState $State
        throw
    }
}

function Install-CycleNicFilter {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)]$LockedMap)
    $folder = Join-Path $script:cycleConfig.packageDirectory 'nic'
    $inf = Join-Path $folder 'client_nic_filter.inf'
    foreach ($name in @('client_nic_filter.inf','client_nic_filter.sys','client_nic_filter.cat')) { if (-not [IO.File]::Exists((Join-Path $folder $name))) { throw "NIC package is incomplete: $(Join-Path $folder $name)" } }
    $before = @(Get-CycleDriverInventory)
    $componentBefore = Get-CycleNetCfgComponentState
    if ($componentBefore.present) { throw 'NIC NetCfg component was already present before this run; refusing to adopt or remove it.' }
    $infHash = (Get-FileHash -LiteralPath $inf -Algorithm SHA256).Hash
    $intent = [ordered]@{requestedUtc=[DateTime]::UtcNow.ToString('o');componentId='HypersinvNicAddressFilter';infPath=$inf;infSha256=$infHash;componentBefore=$componentBefore;driverStoreBefore=@($before)}
    Add-CycleStateValue $State 'nicInstallIntent' $intent
    Add-CycleStateValue $State 'ownedNicComponent' ([ordered]@{componentId='HypersinvNicAddressFilter';owned=$false;beforePresent=$false;afterPresent=$null;beforePath=$componentBefore.outputPath;afterPath=$null;infPath=$inf;infSha256=$infHash})
    Add-CycleStateValue $State 'ownedNicPackages' @()
    Add-CycleStateValue $State 'ownedNicPackage' $null
    Save-CycleState $State

    $result = $null; $commandError = $null
    try { $result = Invoke-CycleProcess 'netcfg-install-nic' (Join-Path $env:SystemRoot 'System32\netcfg.exe') @('/l',$inf,'/c','s','/i','HypersinvNicAddressFilter') }
    catch { $commandError = $_.Exception.ToString() }

    $after = $null; $inventoryError = $null
    try { $after = @(Get-CycleDriverInventory) }
    catch { $inventoryError = $_.Exception.ToString() }

    $componentAfter = $null; $componentError = $null
    try { $componentAfter = Get-CycleNetCfgComponentState }
    catch { $componentError = $_.Exception.ToString() }

    $delta = $null; $deltaError = $null
    if ($null -ne $after) {
        try { $delta = Test-DeviceCycleOwnedPackageDelta -Before $before -After $after -InfName 'client_nic_filter.inf' -InfSha256 $infHash }
        catch { $deltaError = $_.Exception.ToString() }
    }
    $exitCode = if ($null -ne $result) {[int]$result.exitCode} else {-1}
    $componentAfterPresent = if ($null -ne $componentAfter) {[bool]$componentAfter.present} else {$null}
    $decision = Get-DeviceCycleNicInstallDisposition -ExitCode $exitCode -ComponentBeforePresent ([bool]$componentBefore.present) -ComponentAfterPresent $componentAfterPresent -PackageDelta $delta -InfName 'client_nic_filter.inf' -InfPath $inf -InfSha256 $infHash -CommandError $commandError -InventoryError $inventoryError -ComponentError $componentError
    $observation = [ordered]@{capturedUtc=[DateTime]::UtcNow.ToString('o');exitCode=$exitCode;command=$result;commandError=$commandError;driverStoreBefore=@($before);driverStoreAfter=$(if ($null -ne $after) {@($after)} else {$null});driverStoreError=$inventoryError;netcfgBefore=$componentBefore;netcfgAfter=$componentAfter;netcfgError=$componentError;packageDelta=$delta;packageDeltaError=$deltaError;disposition=$decision}
    Set-DeviceCycleNicInstallJournal -State $State -Disposition $decision -Intent $intent -Observation $observation | Out-Null
    if (-not $decision.pass) {
        Add-CycleStateValue $State 'nextAction' $(if ($decision.cleanupRequired) {'The NIC install did not pass, but attributable post-install state was journaled. The Run failure path will remove only the newly observed component/package; inspect nicInstallObservation if cleanup reports a mismatch.'} else {'The NIC install outcome could not be attributed from post-state queries. Preserve TESTSIGNING, signer trust, and the recovery task; inspect nicInstallObservation before any manual cleanup.'})
    }
    Save-CycleState $State
    if (-not $decision.pass) { throw ('NIC install postcondition failed: '+($decision.failures -join '; ')+'; evidence=state.json') }

    $bindings = @(Get-CycleAdapterBindings -LockedMap $LockedMap)
    $bad = @($bindings | Where-Object { $_.adapterCount -ne 1 -or $_.bindingCount -ne 1 -or -not $_.enabled })
    if ($bad.Count) { throw ('NIC filter binding proof failed: '+(($bad | ConvertTo-Json -Depth 5 -Compress))) }
    $services = @(Get-CycleSystemDriver 'HypersinvNicAddressFilter')
    if ($services.Count -ne 1 -or -not $services[0].started) { throw 'NIC filter service is not started after netcfg installation.' }
    $bindingProofPath = New-CycleEvidencePath 'nic-bindings' 'json'
    Write-CycleJsonNew $bindingProofPath ([ordered]@{result='PASS';bindings=$bindings;service=$services;packageOwned=@($delta.owned);capturedUtc=[DateTime]::UtcNow.ToString('o')}) | Out-Null
    Add-CycleStateValue $State 'nicBindingProofPath' $bindingProofPath
    Save-CycleState $State
}

function Remove-CycleNicFilter {
    param([Parameter(Mandatory=$true)]$State)
    $cleanupPlan = Get-DeviceCycleNicCleanupPlan -State $State
    if (-not $cleanupPlan.unbindComponent) { return }
    $componentId = 'HypersinvNicAddressFilter'
    $componentState = Get-CycleNetCfgComponentState $componentId
    if ($componentState.present) {
        $result = Invoke-CycleProcess 'netcfg-remove-nic' (Join-Path $env:SystemRoot 'System32\netcfg.exe') @('/u',$componentId)
        if ($result.exitCode -notin @(0,3010)) { throw ("netcfg /u failed: exit={0}; stdout={1}; stderr={2}" -f $result.exitCode,$result.stdoutPath,$result.stderrPath) }
        Add-CycleStateValue $State 'nicRemovalCommand' $result
        Add-CycleStateValue $State 'nicRemovalRebootRequired' ($result.exitCode -eq 3010)
        Save-CycleState $State
    }
    $componentAfter = Get-CycleNetCfgComponentState $componentId
    if ($componentAfter.present -and -not [bool]$State.nicRemovalRebootRequired) { throw 'NetCfg still lists the owned NIC component after netcfg /u.' }
    if (-not $componentAfter.present) {
        $bindings = @(Get-CycleAdapterBindings -LockedMap (Get-CycleLockedMap) -ComponentId $componentId)
        if (@($bindings | Where-Object { $_.bindingCount -gt 0 -and $_.enabled }).Count -gt 0) { throw 'NIC filter still has an enabled selected binding after netcfg removal.' }
    }
    Add-CycleStateValue $State 'nicUnbindRequestedUtc' ([DateTime]::UtcNow.ToString('o'))
    Save-CycleState $State
}

function Queue-CycleStorageRemoval {
    param([Parameter(Mandatory=$true)]$State)
    $owned = @($State.ownedPackages | Where-Object { $_.publishedName -match '^oem[0-9]+\.inf$' })
    if ($owned.Count -gt 1) { throw 'More than one storage package is owned; refusing broad removal.' }
    if ($owned.Count -eq 1) {
        $rows = @(Get-CyclePublishedStoragePackage $State | Where-Object publishedName -CEQ $owned[0].publishedName)
        if ($rows.Count -eq 1) {
            $actualHash = (Get-FileHash -LiteralPath $rows[0].originalFile -Algorithm SHA256).Hash
            if ($actualHash -cne $owned[0].infSha256) { throw ("Owned storage INF changed before removal: expected={0} actual={1}" -f $owned[0].infSha256,$actualHash) }
            $result = Invoke-CycleProcess ('pnputil-remove-storage-' + $owned[0].publishedName) (Join-Path $env:SystemRoot 'System32\pnputil.exe') @('/delete-driver',$owned[0].publishedName,'/uninstall')
            Add-CycleStateValue $State 'storageRemovalCommand' $result
            Save-CycleState $State
            $disposition = Get-DeviceCyclePnpExitDisposition -ExitCode $result.exitCode
            Add-CycleStateValue $State 'storageRemovalCommandDisposition' $disposition
            Add-CycleStateValue $State 'storageRemovalRebootRequired' $disposition.rebootRequired
            Save-CycleState $State
            if (-not $disposition.accepted) { throw ("pnputil /delete-driver /uninstall failed; no /force or /reboot used: exit={0}; stdout={1}; stderr={2}" -f $result.exitCode,$result.stdoutPath,$result.stderrPath) }
        }
    }
    Add-CycleStateValue $State 'storageRemovalQueuedUtc' ([DateTime]::UtcNow.ToString('o'))
    Save-CycleState $State
}

function Run-Cycle {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)]$LockedMap)
    Assert-CycleAdmin
    if ($State.phase -cne 'PolicyBootPending') { throw ('Run requires PolicyBootPending; actual='+$State.phase) }
    if (Get-CycleSecureBoot) { throw 'Secure Boot must remain Disabled while the test-signed filters are active.' }
    $bcd = Get-CycleCurrentBcdObject
    if (-not $bcd.testsigning.present -or -not $bcd.testsigning.value) { throw 'TESTSIGNING is not Yes in the live current boot object after the required restart.' }
    Assert-CycleBootTransition $State
    Set-DeviceCyclePhase $State 'Running' | Out-Null
    Add-CycleStateValue $State 'runStartedUtc' ([DateTime]::UtcNow.ToString('o'))
    Save-CycleState $State
    try {
        Install-CycleNicFilter $State $LockedMap
        $replacement = Invoke-CycleNativeCapture 'native-replacement'
        $replacementProof = Assert-CycleNativeCapture -CapturePath $replacement.path -ExpectedMode Replacement -AllowFailure
        Add-CycleStateValue $State 'replacementCapturePath' $replacement.path
        Add-CycleStateValue $State 'replacementCaptureSha256' $replacement.sha256
        Add-CycleStateValue $State 'replacementProofPath' $replacementProof.proofPath
        Add-CycleStateValue $State 'replacementProofSha256' $replacementProof.proofSha256
        Add-CycleStateValue $State 'nativeReplacementProven' $replacementProof.pass
        Add-CycleStateValue $State 'wmiActiveCacheClaim' $false
        Save-CycleState $State
        if (-not $replacementProof.pass) { $failed = @($replacementProof.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.line }); throw ('Native Replacement proof failed:' + [Environment]::NewLine + ($failed -join [Environment]::NewLine) + [Environment]::NewLine + 'Capture=' + $replacement.path + [Environment]::NewLine + 'Proof=' + $replacementProof.proofPath) }
        Remove-CycleNicFilter $State
        Queue-CycleStorageRemoval $State
        Set-DeviceCyclePhase $State 'RemovalBootPending' | Out-Null
        Add-CycleStateValue $State 'nextAction' 'Restart Windows once; the signed SYSTEM startup task will verify both filters are gone, collect original native/WMI values, restore trust and TESTSIGNING, and remove itself.'
        Save-CycleState $State
        if ($Restart) { Restart-Computer -Force }
        return [pscustomobject]@{result='REMOVAL-BOOT-PENDING';replacementCapture=$replacement.path;replacementProof=$replacementProof.proofPath;restartRequested=[bool]$Restart;nextAction=$State.nextAction}
    } catch {
        Add-CycleStateValue $State 'lastFailure' ([ordered]@{phase='Run';message=$_.Exception.Message;atUtc=[DateTime]::UtcNow.ToString('o')})
        try {
            Remove-CycleNicFilter $State
            Queue-CycleStorageRemoval $State
            Set-DeviceCyclePhase $State 'RecoveryBootPending' | Out-Null
            if ($null -ne $State.nicInstallObservation -and -not [bool]$State.nicInstallObservation.disposition.pass) {
                if ([bool]$State.nicInstallObservation.disposition.cleanupRequired) {
                    Add-CycleStateValue $State 'nextAction' 'Keep Secure Boot Disabled and TESTSIGNING enabled; restart once so the signed startup task can remove only the exact NIC component/package attribution recorded in nicInstallObservation. Review that evidence and finalizer results before BIOS re-enable.'
                } else {
                    Add-CycleStateValue $State 'nextAction' 'Keep Secure Boot Disabled and TESTSIGNING enabled; restart once so the signed startup task can inspect the machine. NIC post-install state is unproven; the finalizer will refuse to remove or claim an unowned component/package. Review nicInstallObservation and resolve ownership before BIOS re-enable.'
                }
            } else {
                Add-CycleStateValue $State 'nextAction' 'Keep Secure Boot Disabled and TESTSIGNING enabled; restart once so the signed startup task can finish owned filter removal; inspect exact failure evidence before BIOS re-enable.'
            }
        }
        catch { Add-CycleStateValue $State 'cleanupQueueFailure' $_.Exception.Message; Set-DeviceCyclePhase $State 'RecoveryBootPending' | Out-Null; Add-CycleStateValue $State 'nextAction' 'Keep Secure Boot Disabled and TESTSIGNING enabled. Resolve the exact cleanup error in state.json, then run Mode Rollback and restart.' }
        Save-CycleState $State
        throw
    }
}

function Assert-CycleStorageAbsent {
    param([Parameter(Mandatory=$true)]$State)
    $remaining = @(Get-CycleDriverInventory | Where-Object { [IO.Path]::GetFileName([string]$_.originalFile) -ceq 'client_storage_filter.inf' })
    foreach ($row in $remaining) {
        $owned = @($State.ownedPackages | Where-Object { $_.publishedName -ceq $row.publishedName -and $_.infSha256 -ceq $row.infSha256 })
        if ($owned.Count -ne 1) { throw ('Unowned storage package appeared during cleanup; refusing removal: '+$row.publishedName) }
        $result = Invoke-CycleProcess ('pnputil-resume-remove-storage-' + $row.publishedName) (Join-Path $env:SystemRoot 'System32\pnputil.exe') @('/delete-driver',$row.publishedName,'/uninstall')
        $disposition = Get-DeviceCyclePnpExitDisposition -ExitCode $result.exitCode
        Add-CycleStateValue $State 'storageRemovalRetryDisposition' $disposition
        Save-CycleState $State
        if (-not $disposition.accepted) { throw ("Storage removal retry failed: exit={0}; stdout={1}; stderr={2}" -f $result.exitCode,$result.stdoutPath,$result.stderrPath) }
    }
    $remaining = @(Get-CycleDriverInventory | Where-Object { [IO.Path]::GetFileName([string]$_.originalFile) -ceq 'client_storage_filter.inf' })
    if ($remaining.Count -gt 0) { throw ('Storage package removal still pending after startup: '+(($remaining | ForEach-Object {$_.publishedName}) -join ', ')) }
    $systemDriver = @(Get-CycleSystemDriver 'client_storage_filter')
    if ($systemDriver.Count -gt 0) { throw ('Storage filter service remains registered after the removal reboot: '+(ConvertTo-Json -InputObject $systemDriver -Compress)) }
    return [pscustomobject]@{pass=$true;remainingPackages=0;remainingServiceCount=0}
}

function Remove-CycleOwnedNicPackage {
    param([Parameter(Mandatory=$true)]$State)
    $cleanupPlan = Get-DeviceCycleNicCleanupPlan -State $State
    $ownedPackages = @($cleanupPlan.ownedPackages)
    foreach ($ownedPackage in $ownedPackages) {
        $publishedName = [string]$ownedPackage.publishedName
        if ($publishedName -notmatch '^oem[0-9]+\.inf$') { throw ('Owned NIC package identifier is malformed: '+$publishedName) }
        $rows = @(Get-CycleDriverInventory | Where-Object { $_.publishedName -ceq $publishedName })
        if ($rows.Count -gt 1) { throw ('Duplicate Driver Store entry for owned NIC package '+$publishedName) }
        if ($rows.Count -eq 1) {
            if ([IO.Path]::GetFileName([string]$rows[0].originalFile) -cne 'client_nic_filter.inf' -or [string]$rows[0].infSha256 -cne [string]$ownedPackage.infSha256) {
                throw ('Owned NIC package changed before cleanup: '+$publishedName)
            }
            $result = Invoke-CycleProcess ('pnputil-resume-remove-nic-' + $publishedName) (Join-Path $env:SystemRoot 'System32\pnputil.exe') @('/delete-driver',$publishedName,'/uninstall')
            $disposition = Get-DeviceCyclePnpExitDisposition -ExitCode $result.exitCode
            Add-CycleStateValue $State 'nicPackageRemovalDisposition' $disposition
            Save-CycleState $State
            if (-not $disposition.accepted) { throw ("NIC package cleanup failed: exit={0}; stdout={1}; stderr={2}" -f $result.exitCode,$result.stdoutPath,$result.stderrPath) }
        }
        if (@(Get-CycleDriverInventory | Where-Object { $_.publishedName -ceq $publishedName }).Count -gt 0) { throw ('Owned NIC package remains after supported cleanup: '+$publishedName) }
    }
}

function Wait-CycleDriverSettle {
    param([Parameter(Mandatory=$true)][string]$Name,[int]$Attempts=6,[int]$DelaySeconds=5)
    for ($index=0; $index -lt $Attempts; $index++) {
        $rows = @(Get-CycleSystemDriver $Name)
        if (@($rows | Where-Object { $_.started -or $_.state -match 'Running|Start Pending|Stop Pending' }).Count -eq 0) { return [pscustomobject]@{settled=$true;attempts=$index+1;rows=$rows} }
        if ($index + 1 -lt $Attempts) { Start-Sleep -Seconds $DelaySeconds }
    }
    return [pscustomobject]@{settled=$false;attempts=$Attempts;rows=@(Get-CycleSystemDriver $Name)}
}

function Restore-CycleEnvironment {
    param([Parameter(Mandatory=$true)]$State)
    if ($null -eq $State.originalBcd) { throw 'Original TESTSIGNING representation is missing from the cycle journal.' }
    Invoke-CycleBcdRestore $State.originalBcd $State.originalBcdIdentifier | Out-Null
}

function Complete-CycleRemoval {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)]$LockedMap)
    Assert-CycleAdmin
    if (Get-CycleSecureBoot) { throw 'Secure Boot must remain Disabled until the test-signed filters and temporary trust are removed.' }
    $storage = Assert-CycleStorageAbsent $State
    Remove-CycleNicFilter $State
    $nicComponentAfterUnbind = Get-CycleNetCfgComponentState
    if ($nicComponentAfterUnbind.present) { throw 'NIC NetCfg component remains after the cleanup reboot; preserving trust, TESTSIGNING and startup task for another supported cleanup attempt.' }
    Remove-CycleOwnedNicPackage $State
    $nicWait = Wait-CycleDriverSettle 'HypersinvNicAddressFilter' 6 5
    if (-not $nicWait.settled) { throw 'NIC driver did not settle after removal boot; preserving trust, TESTSIGNING and startup task for recovery.' }
    $nicService = @(Get-CycleSystemDriver 'HypersinvNicAddressFilter')
    if ($nicService.Count -gt 0) { throw ('NIC filter service remains registered after netcfg removal and removal reboot: '+(ConvertTo-Json -InputObject $nicService -Compress)) }
    $bindings = @(Get-CycleAdapterBindings -LockedMap $LockedMap)
    if (@($bindings | Where-Object { $_.adapterCount -ne 1 }).Count -gt 0) { throw ('One or more selected adapters are not uniquely visible after cleanup: '+(($bindings | ConvertTo-Json -Depth 4 -Compress))) }
    if (@($bindings | Where-Object { $_.bindingCount -gt 0 -and $_.enabled }).Count -gt 0) { throw ('One or more selected adapters still bind the NIC filter: '+(($bindings | ConvertTo-Json -Depth 4 -Compress))) }
    $original = Invoke-CycleNativeCapture 'native-original-after-removal'
    Add-CycleStateValue $State 'originalAfterPath' $original.path
    Add-CycleStateValue $State 'originalAfterSha256' $original.sha256
    Save-CycleState $State
    $originalProof = Assert-CycleNativeCapture -CapturePath $original.path -ExpectedMode Original
    $wmi = New-CycleWmiCapture 'wmi-original-after-removal'
    Add-CycleStateValue $State 'wmiFinalPath' $wmi.path
    Add-CycleStateValue $State 'wmiFinalSha256' $wmi.sha256
    Save-CycleState $State
    if (-not $wmi.captureComplete) { throw ('Fresh WMI capture failed: '+(ConvertTo-Json -InputObject $wmi.document.errors -Compress)) }
    $wmiProof = Assert-CycleWmiOriginalCapture -CapturePath $wmi.path -NotBeforeUtc $State.cycleStartUtc
    Add-CycleStateValue $State 'originalAfterProofPath' $originalProof.proofPath
    Add-CycleStateValue $State 'wmiFinalProofPath' $wmiProof.proofPath
    Add-CycleStateValue $State 'storageRemovalVerification' $storage
    Add-CycleStateValue $State 'nicBindingsAfterRemoval' $bindings
    Save-CycleState $State

    $roundTrip = $null
    if (-not [string]::IsNullOrWhiteSpace([string]$State.replacementCapturePath) -and [IO.File]::Exists([string]$State.replacementCapturePath)) {
        $replacementHashActual = (Get-FileHash -LiteralPath $State.replacementCapturePath -Algorithm SHA256).Hash
        if ($replacementHashActual -cne [string]$State.replacementCaptureSha256) {
            $roundTrip = [pscustomobject]@{pass=$false;mode='Original->Replacement->Original';evidence=@();checks=@([pscustomobject]@{field='replacement.captureSha256';expected=[string]$State.replacementCaptureSha256;actual=$replacementHashActual;pass=$false;line=('replacement capture hash expected={0} actual={1}' -f $State.replacementCaptureSha256,$replacementHashActual)})}
        } else {
            $roundTrip = Test-DeviceCycleNativeRoundTrip -OriginalBeforePath $State.originalBeforePath -ReplacementPath $State.replacementCapturePath -OriginalAfterPath $original.path -LockedTsvPath $script:cycleConfig.lockedTsvPath -NativeReaderPath $script:cycleConfig.readerPath
        }
        $roundTripSummary = New-DeviceCycleRoundTripSummary $roundTrip
    } else {
        $roundTripSummary = [ordered]@{schema='device-cycle-native-round-trip-summary.v1';result='FAIL';mode='Original->Replacement->Original';liveNativeRoundTripProven=$false;evidence=@();checks=@([ordered]@{field='replacement.capture';expected='a fresh native capture while all owned filters are bound';actual='no replacement capture was recorded';pass=$false;line='No replacement capture exists; no live spoof result is claimed.'});wmiReplacementClaim=$false;wmiReplacementLimit='WMI is evaluated after removal for original values only; active cached WMI values are not presented as live replacement evidence.'}
    }
    $roundTripPath = New-CycleEvidencePath 'native-round-trip-proof' 'json'
    Write-CycleJsonNew $roundTripPath $roundTripSummary | Out-Null
    $roundTripPass = ([string]$roundTripSummary.result -ceq 'PASS' -and [bool]$roundTripSummary.liveNativeRoundTripProven)
    Add-CycleStateValue $State 'roundTripProofPath' $roundTripPath
    Add-CycleStateValue $State 'roundTripProofSha256' (Get-FileHash -LiteralPath $roundTripPath -Algorithm SHA256).Hash
    Add-CycleStateValue $State 'liveRoundTripProven' $roundTripPass
    if (-not $roundTripPass) {
        $failedLines = @($roundTripSummary.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.line })
        Add-CycleStateValue $State 'roundTripDisposition' ('Native device round trip did not pass: '+($failedLines -join '; '))
    }
    Save-CycleState $State

    # Only clean up temporary trust and boot policy after the filters are absent
    # and both independent native and WMI readers prove the original values.
    $machineBefore = Get-Content -LiteralPath $State.machineBeforePath -Raw | ConvertFrom-Json
    $componentRestored = (-not [bool]$machineBefore.netcfgComponent.present -and -not [bool]$nicComponentAfterUnbind.present)
    if (-not $componentRestored) { throw 'NetCfg component state differs from its original snapshot; preserving trust and startup recovery.' }
    $serviceRegistry = @(Get-CycleServiceRegistryState)
    if (@($serviceRegistry | Where-Object { $_.present }).Count -gt 0) { throw ('Filter service registry keys remain after package removal: '+(ConvertTo-Json -InputObject $serviceRegistry -Compress)) }
    $bindingBefore = @($machineBefore.adapterBindings)
    $bindingBeforeJson = ConvertTo-Json -InputObject @($bindingBefore | Sort-Object pnpDeviceId) -Depth 8 -Compress
    $bindingAfterJson = ConvertTo-Json -InputObject @($bindings | Sort-Object pnpDeviceId) -Depth 8 -Compress
    $bindingRestored = ($bindingBeforeJson -ceq $bindingAfterJson)
    if (-not $bindingRestored) { throw ('Selected adapter binding map differs from the pre-cycle capture; expected='+$bindingBeforeJson+' actual='+$bindingAfterJson) }
    $storeBefore = @($State.driverStoreBefore)
    $storeFinal = @(Get-CycleDriverInventory)
    $storeCanonical = @($storeFinal | ForEach-Object { '{0}|{1}|{2}|{3}|{4}' -f $_.publishedName,$_.originalFile,$_.infSha256,$_.provider,$_.class } | Sort-Object)
    $storeBeforeCanonical = @($storeBefore | ForEach-Object { '{0}|{1}|{2}|{3}|{4}' -f $_.publishedName,$_.originalFile,$_.infSha256,$_.provider,$_.class } | Sort-Object)
    $driverStoreRestored = (($storeCanonical -join "`n") -ceq ($storeBeforeCanonical -join "`n"))
    if (-not $driverStoreRestored) { throw ('Driver Store inventory differs from its pre-cycle capture; expectedCount={0} actualCount={1}' -f $storeBeforeCanonical.Count,$storeCanonical.Count) }

    $startupFiles = @($script:cycleConfig.runnerPath,$script:cycleRuntimeModule,$script:cycleConfig.proofModulePath,$script:cycleConfig.snapshotLibraryPath,$script:cycleConfig.espPolicyPath)
    $signatureChecks = @()
    foreach ($path in $startupFiles) {
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        $valid = ($signature.Status -eq [Management.Automation.SignatureStatus]::Valid -and $signature.SignerCertificate -and $signature.SignerCertificate.Thumbprint -ieq $script:cycleConfig.certificateThumbprint)
        $signatureChecks += [pscustomobject]@{path=$path;status=[string]$signature.Status;signerThumbprint=$(if ($signature.SignerCertificate) {$signature.SignerCertificate.Thumbprint} else {$null});pass=$valid}
        if (-not $valid) { throw ("Startup script signature is not valid before trust cleanup: {0}; status={1}" -f $path,$signature.Status) }
    }
    Add-CycleStateValue $State 'finalizerSignatureChecks' $signatureChecks
    Save-CycleState $State

    $policyRestored = $false
    $taskRemoved = $false
    $trustGood = $false
    try {
        Restore-CycleEnvironment $State
        $bcdAfter = Get-CycleCurrentBcdObject
        $policyRestored = ($bcdAfter.testsigning.present -eq $State.originalBcd.present -and (-not $State.originalBcd.present -or $bcdAfter.testsigning.value -eq $State.originalBcd.value))
        if (-not $policyRestored) { throw 'TESTSIGNING representation differs from its pre-cycle state after restoration.' }
        Unregister-CycleStartupTask $State
        $taskRemoved = ($null -eq (Get-ScheduledTask -TaskName $script:cycleConfig.taskName -ErrorAction SilentlyContinue))
        if (-not $taskRemoved) { throw 'Owned startup task remains after attempted removal.' }
        Restore-CycleTrust $State
        $storeAfter = @(Get-CycleTrustMembership $script:cycleConfig.certificateThumbprint)
        $trustGood = $true
        foreach ($row in $storeAfter) {
            $old = @($State.trustBefore | Where-Object store -CEQ $row.store)
            if ($old.Count -ne 1 -or [bool]$row.certificatePresent -ne [bool]$old[0].certificatePresent) { $trustGood=$false }
        }
        if (-not $trustGood) { throw 'Temporary certificate store entries differ from the recorded originals after removal.' }
    } catch {
        $cleanupError = $_.Exception.Message
        # If trust cleanup was partial, re-establish the pinned signer trust so the
        # still-owned startup task can retry on the next boot.
        try {
            foreach ($store in @('Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPublisher')) {
                $membership = @(Get-CycleTrustMembership $script:cycleConfig.certificateThumbprint | Where-Object { $_.store -ceq $store })
                if ($membership.Count -eq 1 -and -not $membership[0].certificatePresent) {
                    $storeName = if ($store -like '*Root') {'Root'} else {'TrustedPublisher'}
                    $add = Invoke-CycleProcess ('restore-recovery-trust-' + $storeName.ToLowerInvariant()) (Join-Path $env:SystemRoot 'System32\certutil.exe') @('-addstore','-f',$storeName,$script:cycleConfig.certificatePath)
                    if ($add.exitCode -ne 0) { throw ("Could not restore temporary signer trust for recovery: store={0}; exit={1}; stdout={2}; stderr={3}" -f $store,$add.exitCode,$add.stdoutPath,$add.stderrPath) }
                }
            }
            Assert-CycleStartupSignaturesTrusted
            if ($null -eq (Get-ScheduledTask -TaskName $script:cycleConfig.taskName -ErrorAction SilentlyContinue)) { Register-CycleStartupTask $State | Out-Null }
        } catch { Add-CycleStateValue $State 'recoveryTaskRestoreFailure' $_.Exception.Message }
        Add-CycleStateValue $State 'lastFailure' ([ordered]@{phase='RemovalFinalizer';message=$cleanupError;atUtc=[DateTime]::UtcNow.ToString('o')})
        Add-CycleStateValue $State 'nextAction' 'Cleanup did not complete. Keep Secure Boot Disabled; inspect the recorded error and preserve any recovered signed startup task and signer trust, then retry Mode Resume after reboot.'
        Set-DeviceCyclePhase $State 'RecoveryBootPending' -AllowSame | Out-Null
        Save-CycleState $State
        throw $cleanupError
    }

    $cleanup = [ordered]@{pass=$true;storagePackagesAbsent=$storage.pass;storageServiceAbsent=$true;nicServiceAbsent=$true;serviceRegistryAbsent=$true;nicComponentAbsent=$componentRestored;nicBindingsRestored=$bindingRestored;driverStoreRestored=$driverStoreRestored;nativeOriginals=$true;wmiOriginals=$true;nativeRoundTripProven=$roundTripPass;trustRestored=$trustGood;testSigningRestored=$policyRestored;taskRemoved=$taskRemoved;capturedUtc=[DateTime]::UtcNow.ToString('o')}
    $cleanupPath = New-CycleEvidencePath 'cleanup-proof' 'json'
    Write-CycleJsonNew $cleanupPath $cleanup | Out-Null
    Add-CycleStateValue $State 'cleanupProofPath' $cleanupPath
    Add-CycleStateValue $State 'cleanupProofSha256' (Get-FileHash -LiteralPath $cleanupPath -Algorithm SHA256).Hash
    Add-CycleStateValue $State 'cleanupPass' $true
    $cleanupMarkerPath = Join-Path $script:cycleRunDirectory 'CLEANUP-COMPLETE-AWAITING-SECURE-BOOT-ON.txt'
    $cleanupMarkerLines = @(
        'Cleanup phase completed after a signed SYSTEM startup task verified filter removal.',
        ('runId=' + [string]$State.runId),
        ('capturedUtc=' + [DateTime]::UtcNow.ToString('o')),
        ('nativeOriginalPass=' + [bool]$originalProof.pass),
        ('wmiOriginalPass=' + [bool]$wmiProof.pass),
        ('nativeRoundTripPass=' + [bool]$roundTripPass),
        ('cleanupProof=' + $cleanupPath),
        ('nativeOriginalCapture=' + $original.path),
        ('wmiOriginalCapture=' + $wmi.path),
        'Next action: re-enable Secure Boot in firmware, boot Windows, then run Mode Verify.'
    )
    Write-CycleTextNew $cleanupMarkerPath ($cleanupMarkerLines -join [Environment]::NewLine)
    Add-CycleStateValue $State 'cleanupMarkerPath' $cleanupMarkerPath
    Add-CycleStateValue $State 'cleanupMarkerSha256' (Get-FileHash -LiteralPath $cleanupMarkerPath -Algorithm SHA256).Hash
    Set-DeviceCyclePhase $State 'AwaitingSecureBootOn' | Out-Null
    Add-CycleStateValue $State 'nextAction' 'Secure Boot remains Disabled. Re-enable it in firmware, boot Windows, then run Mode Verify.'
    Save-CycleState $State
    return [pscustomobject]@{result='CLEANUP-PASS-AWAITING-SECUREBOOT-ON';cleanupProof=$cleanupPath;liveRoundTripProven=[bool]$State.liveRoundTripProven;nextAction=$State.nextAction}
}

function Resume-Cycle {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)]$LockedMap)
    Assert-CycleStartupSignaturesTrusted
    if ($State.phase -in @('PolicyBootPending','Enabling','Running','RemovalBootPending','RecoveryBootPending','RollingBack')) {
        if ($null -eq $State.task -or -not [bool]$State.task.owned) {
            if (-not (Adopt-CycleStartupTaskIfExact $State)) { throw ('Recovery task was invoked without a recoverable ownership record; expected task name='+$script:cycleConfig.taskName) }
        } else {
            $existingTask = Get-ScheduledTask -TaskName $State.task.name -ErrorAction SilentlyContinue
            if (-not $existingTask) { throw ('Owned startup task is absent during pending phase: '+$State.task.name) }
            Assert-CycleStartupTaskOwnership $State $existingTask
        }
    }
    if ($State.phase -ceq 'PolicyBootPending') {
        $bcd = Get-CycleCurrentBcdObject
        $secureBootOff = -not (Get-CycleSecureBoot)
        $testSigningOn = ($bcd.testsigning.present -and $bcd.testsigning.value)
        $startup = [ordered]@{result='AWAITING-USER-RUN';capturedUtc=[DateTime]::UtcNow.ToString('o');phase=$State.phase;secureBootOff=$secureBootOff;testSigningOn=$testSigningOn;currentBootObject=$bcd.identifier;action='No driver, trust or BCD changes were made by the startup task during the policy boot. Run the signed runner in Mode Run from elevated PowerShell.'}
        $path = New-CycleEvidencePath 'startup-policy-boot' 'json'; Write-CycleJsonNew $path $startup | Out-Null
        Add-CycleStateValue $State 'policyStartupEvidence' $path
        Add-CycleStateValue $State 'policyBootChecks' ([ordered]@{secureBootOff=$secureBootOff;testSigningOn=$testSigningOn})
        Save-CycleState $State
        return [pscustomobject]$startup
    }
    if ($State.phase -in @('Enabling','Running')) {
        if ([string]::IsNullOrWhiteSpace([string]$State.enableBootIdentity)) { throw 'Interrupted phase has no recorded boot identity; preserving the startup task and temporary trust for manual review.' }
        $os = Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 20 -ErrorAction Stop
        $currentIdentity = ([DateTime]$os.LastBootUpTime).ToUniversalTime().ToString('o')
        if ($currentIdentity -ceq [string]$State.enableBootIdentity) { throw 'The recovery finalizer ran before a new boot; filters may still be active, so temporary state is preserved.' }
        Set-DeviceCyclePhase $State 'RecoveryBootPending' | Out-Null
        Add-CycleStateValue $State 'nextAction' 'Startup detected an interrupted machine-change phase after a new boot and is running the owned cleanup path.'
        Save-CycleState $State
    }
    if ($State.phase -notin @('RemovalBootPending','RecoveryBootPending','RollingBack')) { return [pscustomobject]@{result='NO-OP';phase=$State.phase;reason='No cleanup reboot is pending.'} }
    $newBoot = $false
    if ($State.enableBootIdentity) {
        $os = Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 20 -ErrorAction Stop
        $currentIdentity = ([DateTime]$os.LastBootUpTime).ToUniversalTime().ToString('o')
        $newBoot = ($currentIdentity -cne [string]$State.enableBootIdentity)
    }
    if (-not $newBoot) { throw 'The removal finalizer ran before a cleanup reboot; it will preserve trust and TESTSIGNING and retry at the next startup.' }
    return Complete-CycleRemoval $State $LockedMap
}

function Rollback-Cycle {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)]$LockedMap)
    Assert-CycleAdmin
    $disposition = Get-DeviceCycleRollbackDisposition -Phase ([string]$State.phase) -NextAction ([string]$State.nextAction)
    if ($disposition.noOp) { return [pscustomobject]@{result='NO-OP';phase=$State.phase;statePath=$script:cycleStatePath;nextAction=$disposition.nextAction} }
    if ($disposition.noMutation) {
        foreach ($phase in $disposition.transitionPhases) { Set-DeviceCyclePhase $State $phase | Out-Null }
        Add-CycleStateValue $State 'roundTripDisposition' 'No cycle mutation occurred.'
        Add-CycleStateValue $State 'ready' $false
        Add-CycleStateValue $State 'liveRoundTripProven' $false
        Add-CycleStateValue $State 'nextAction' 'No device-cycle mutation occurred. Re-enable Secure Boot in firmware, boot Windows, then run Mode Verify.'
        Save-CycleState $State
        return [pscustomobject]@{result='NO-MUTATION';phase=$State.phase;statePath=$script:cycleStatePath;nextAction=$State.nextAction}
    }
    if (Get-CycleSecureBoot) { throw 'Rollback requires Secure Boot Disabled while the test-signed filters may be active.' }
    if ($State.phase -notin @('RemovalBootPending','RecoveryBootPending','RollingBack')) { Set-DeviceCyclePhase $State 'RollingBack' | Out-Null; Save-CycleState $State }
    try {
        Remove-CycleNicFilter $State
        Queue-CycleStorageRemoval $State
        Set-DeviceCyclePhase $State 'RecoveryBootPending' -AllowSame | Out-Null
        Add-CycleStateValue $State 'nextAction' 'Restart Windows once and let the signed SYSTEM task finish the supported cleanup; re-enable Secure Boot in firmware, boot Windows, then run Mode Verify.'
        Save-CycleState $State
        if ($Restart) { Restart-Computer -Force }
        return [pscustomobject]@{result='ROLLBACK-BOOT-PENDING';phase=$State.phase;restartRequested=[bool]$Restart;nextAction=$State.nextAction}
    } catch {
        Add-CycleStateValue $State 'lastFailure' ([ordered]@{phase='Rollback';message=$_.Exception.Message;atUtc=[DateTime]::UtcNow.ToString('o')})
        Add-CycleStateValue $State 'nextAction' 'Keep Secure Boot Disabled and TESTSIGNING enabled; do not remove the owned startup task or temporary trust. Resolve the exact error, then retry Mode Rollback.'
        Set-DeviceCyclePhase $State 'RecoveryBootPending' -AllowSame | Out-Null
        Save-CycleState $State
        throw
    }
}

function Get-CycleSnapshotVerification {
    param([Parameter(Mandatory=$true)]$State)
    $firmwareCompileDirectory = Join-Path $script:cycleRunDirectory 'firmware-reader-temporary'
    [void][IO.Directory]::CreateDirectory($firmwareCompileDirectory)
    $env:TEMP = $firmwareCompileDirectory
    $env:TMP = $firmwareCompileDirectory
    $snapshotPath = [IO.Path]::GetFullPath([string]$script:cycleConfig.snapshotPath)
    foreach ($name in @('snapshot.json','firmware-variables.json','esp-layout.json','bcd-all.txt')) { if (-not [IO.File]::Exists((Join-Path $snapshotPath $name))) { throw "Fresh boot-state snapshot is incomplete: $(Join-Path $snapshotPath $name)" } }
    $script:auditLog = New-CycleEvidencePath 'snapshot-verification-audit' 'log'
    [IO.File]::WriteAllText($script:auditLog,'Device-cycle final snapshot verification.'+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
    . $script:cycleConfig.espPolicyPath
    . $script:cycleConfig.snapshotLibraryPath -RepoRoot $script:cycleConfig.repoRoot -OutputBoundary (Join-Path $script:cycleConfig.repoRoot 'spoof-handoff') -LibraryOnly
    $metadata = Get-Content -LiteralPath (Join-Path $snapshotPath 'snapshot.json') -Raw | ConvertFrom-Json
    Initialize-ReadyFirmwareReader
    [ReadyToArmFirmwareRead]::Enable()
    $firmware = @()
    $firmwareRecords = Get-Content -LiteralPath (Join-Path $snapshotPath 'firmware-variables.json') -Raw | ConvertFrom-Json -ErrorAction Stop
    if ($null -eq $firmwareRecords) { throw 'The fresh firmware snapshot decoded to no records.' }
    $firmwareRecords = @($firmwareRecords)
    $requiredFirmwareNames = @('SecureBoot','BootOrder','BootNext')
    $firmwareComparisonRecords = @(Get-DeviceCycleFirmwareRecordsToCompare -Records $firmwareRecords)
    foreach ($record in $firmwareComparisonRecords) {
        [uint32]$attributes=0; [int]$win32=0
        $bytes = [ReadyToArmFirmwareRead]::Read([string]$record.name,[string]$record.guid,[ref]$attributes,[ref]$win32)
        $base64 = if ($null -ne $bytes) {[Convert]::ToBase64String($bytes)} else {$null}
        $expectedBase64 = Get-DeviceCycleProperty $record 'base64'
        $bytesMatch = if ($null -eq $expectedBase64) { $null -eq $base64 } else { [string]$base64 -ceq [string]$expectedBase64 }
        $pass = ($win32 -eq [int]$record.win32Error -and $attributes -eq [uint32]$record.attributes -and $bytesMatch)
        $firmware += [pscustomobject]@{name=$record.name;guid=$record.guid;expectedBase64=$expectedBase64;actualBase64=$base64;expectedAttributes=$record.attributes;actualAttributes=$attributes;expectedWin32=$record.win32Error;actualWin32=$win32;bytesMatch=$bytesMatch;pass=$pass}
    }
    $badFirmware = @($firmware | Where-Object { -not $_.pass })
    $excludedBootCurrentCount = @($firmwareRecords | Where-Object { [string]$_.name -ieq 'BootCurrent' }).Count
    $mount = $null
    try {
        $mount = Mount-Esp
        if ($mount.VolumeGuid -cne $metadata.volumeGuid) { throw 'Mounted EFI System Partition volume GUID differs from the fresh snapshot.' }
        $expected = @(ConvertFrom-RestoreLayoutJson ([IO.File]::ReadAllText((Join-Path $snapshotPath 'esp-layout.json'))))
        $actual = @(foreach ($rootItem in @(Get-ChildItem -LiteralPath $mount.EspRoot -Force)) {
            if ($rootItem.Name -eq 'System Volume Information') { continue }
            $tree = if ($rootItem.PSIsContainer) {@($rootItem)+@(Get-ChildItem -LiteralPath $rootItem.FullName -Recurse -Force)} else {@($rootItem)}
            foreach ($item in $tree) {
                if (Test-ReparsePoint $item.FullName) { throw ('ESP reparse point refused: '+$item.FullName) }
                $relative = $item.FullName.Substring($mount.EspRoot.Length)
                $hash = if (-not $item.PSIsContainer -and $relative -notmatch '^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[0-9]*)?$') { Get-Sha256 $item.FullName } else { $null }
                [pscustomobject]@{path=$relative;directory=$item.PSIsContainer;sha256=$hash}
            }
        })
        $esp = @(Compare-RestoreEspFiles $expected $actual)
        $bootHash = Get-BootManagerHash $mount.EspRoot
        if ($bootHash -cne [string]$metadata.bootManagerSha256) { throw ("Windows Boot Manager hash mismatch: expected={0} actual={1}" -f $metadata.bootManagerSha256,$bootHash) }
        $bootStatus = @($esp | Where-Object { $_.path -ieq 'EFI\Microsoft\Boot\BOOTSTAT.DAT' })
    } finally { if ($mount) { Dismount-Esp $mount } }
    $bcdBefore = [IO.File]::ReadAllLines((Join-Path $snapshotPath 'bcd-all.txt'))
    $bcdAfter = Get-CycleBcdAll
    $bcdLines = [IO.File]::ReadAllLines($bcdAfter.path)
    $bcdMatches = (($bcdBefore -join "`n") -ceq ($bcdLines -join "`n"))
    $badEsp = @($esp | Where-Object { -not $_.pass })
    $checks = [ordered]@{secureBootOn=(Get-CycleSecureBoot);firmware=$firmware;firmwareComparedCount=$firmware.Count;firmwareExcludedBootCurrentCount=$excludedBootCurrentCount;firmwareMandatoryNames=$requiredFirmwareNames;firmwarePass=($firmware.Count -gt 0 -and $badFirmware.Count -eq 0);bootOrderNextAndEntriesPass=($firmware.Count -gt 0 -and $badFirmware.Count -eq 0);esp=$esp;espPass=($badEsp.Count -eq 0);bootStatusMutable=($bootStatus.Count -eq 1 -and $bootStatus[0].classification -eq 'mutable_boot_status_changed');bootManagerExpectedSha256=$metadata.bootManagerSha256;bootManagerActualSha256=$bootHash;bcdMatches=$bcdMatches;bcdAfterPath=$bcdAfter.path;bcdAfterSha256=$bcdAfter.sha256}
    $pass = ($checks.secureBootOn -and $checks.firmwarePass -and $checks.espPass -and $checks.bcdMatches -and $bootHash -ceq [string]$metadata.bootManagerSha256)
    return [pscustomobject]@{pass=$pass;checks=$checks}
}

function Test-CycleBaselineAndSuites {
    param([Parameter(Mandatory=$true)]$State)
    $reportDirectory = Join-Path $script:cycleRunDirectory ('final-existing-checks.' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($reportDirectory)
    $fixtureRoot = Join-Path $reportDirectory 'fixtures'
    $pwsh = Join-Path $PSHOME 'powershell.exe'
    $result = Invoke-CycleProcess 'final-existing-suites' $pwsh @('-NoProfile','-ExecutionPolicy','Bypass','-File',$script:cycleConfig.testVerifierPath,'-ReportDirectory',$reportDirectory,'-FixtureRoot',$fixtureRoot)
    $verdictPath = Join-Path $reportDirectory 'verdict.json'
    if ($result.exitCode -ne 0 -or -not [IO.File]::Exists($verdictPath)) { throw ("Final suite verifier failed or omitted verdict: exit={0}; stdout={1}; stderr={2}" -f $result.exitCode,$result.stdoutPath,$result.stderrPath) }
    $verdict = Get-Content -LiteralPath $verdictPath -Raw | ConvertFrom-Json
    if ([string]$verdict.result -cne 'PASS' -or [int]$verdict.checks -lt 114) { throw ('Final test verifier did not prove 114 existing host checks: '+(ConvertTo-Json -InputObject $verdict -Depth 8 -Compress)) }
    $baseline = Get-Content -LiteralPath $script:cycleConfig.baselineReferencePath -Raw | ConvertFrom-Json
    $baselineHead = [string](Get-DeviceCycleProperty $baseline 'head')
    $expectedIndexSha256 = [string](Get-DeviceCycleProperty $baseline 'indexSha256')
    if ($baselineHead -notmatch '^[A-Fa-f0-9]{40,64}$' -or $expectedIndexSha256 -notmatch '^[A-Fa-f0-9]{64}$') { throw 'Main-checkout baseline is missing a valid HEAD or index SHA-256.' }
    $repoRoot = [IO.Path]::GetFullPath($script:cycleConfig.repoRoot)
    $gitCommand = Get-Command git -CommandType Application -ErrorAction Stop
    $safeDirectory = 'safe.directory=' + $repoRoot
    $headResult = Invoke-CycleProcess 'main-baseline-head' $gitCommand.Source @('-c',$safeDirectory,'-C',$repoRoot,'rev-parse','HEAD')
    if ($headResult.exitCode -ne 0) { throw ("Could not read the main checkout HEAD: exit={0}; stdout={1}; stderr={2}" -f $headResult.exitCode,$headResult.stdoutPath,$headResult.stderrPath) }
    $actualHead = $headResult.stdoutText.Trim()
    if ($actualHead -cne $baselineHead) { throw ("Main checkout HEAD mismatch: expected={0} actual={1}" -f $baselineHead,$actualHead) }
    $indexResult = Invoke-CycleProcess 'main-baseline-index-path' $gitCommand.Source @('-c',$safeDirectory,'-C',$repoRoot,'rev-parse','--git-path','index')
    if ($indexResult.exitCode -ne 0) { throw ("Could not locate the main checkout Git index: exit={0}; stdout={1}; stderr={2}" -f $indexResult.exitCode,$indexResult.stdoutPath,$indexResult.stderrPath) }
    $indexPath = $indexResult.stdoutText.Trim()
    if (-not [IO.Path]::IsPathRooted($indexPath)) { $indexPath = Join-Path $repoRoot $indexPath }
    $indexPath = [IO.Path]::GetFullPath($indexPath)
    Assert-DeviceCycleNoReparsePath -Path $indexPath -Boundary $repoRoot
    if (-not [IO.File]::Exists($indexPath)) { throw "Main checkout Git index is missing: $indexPath" }
    $actualIndexSha256 = (Get-FileHash -LiteralPath $indexPath -Algorithm SHA256).Hash
    if ($actualIndexSha256 -cne $expectedIndexSha256.ToUpperInvariant()) { throw ("Main checkout index mismatch: expected={0} actual={1}" -f $expectedIndexSha256,$actualIndexSha256) }
    $baselinePins = @((Get-DeviceCycleProperty $baseline 'files'))
    if ($baselinePins.Count -lt 1195) { throw ('Baseline reference does not contain 1195+ file pins: actual='+$baselinePins.Count) }
    $baselineFailures = New-Object 'System.Collections.Generic.List[string]'
    foreach ($pin in $baselinePins) {
        $path = [string]$pin.path
        if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $script:cycleConfig.repoRoot $path }
        if (-not (Test-DeviceCyclePathWithin -Path $path -Boundary $repoRoot)) { $baselineFailures.Add("outside repoRoot $path"); continue }
        Assert-DeviceCycleNoReparsePath -Path $path -Boundary $repoRoot
        if (-not [IO.File]::Exists($path)) { $baselineFailures.Add("missing $path"); continue }
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        $expected = [string]$pin.sha256
        if ($actual -cne $expected) { $baselineFailures.Add("$path expected=$expected actual=$actual") }
    }
    if ($baselineFailures.Count -gt 0) { throw ('Main-checkout baseline mismatch: '+($baselineFailures -join '; ')) }
    $smbios = Get-Content -LiteralPath $script:cycleConfig.smbiosStatePath -Raw | ConvertFrom-Json
    if ($smbios.allSevenLiveProven -ne $true -or [int]$smbios.mask -ne 127) { throw 'The pinned SMBIOS state does not report all seven live fields proven.' }
    $smbiosPins = @($smbios.pinnedFiles)
    if ($smbiosPins.Count -lt 46) { throw ('SMBIOS state contains fewer than 46 integrity pins: actual='+$smbiosPins.Count) }
    $smbiosFailures = New-Object 'System.Collections.Generic.List[string]'
    foreach ($pin in $smbiosPins) {
        if (-not [IO.File]::Exists([string]$pin.path)) { $smbiosFailures.Add('missing '+$pin.path); continue }
        $actual = (Get-FileHash -LiteralPath $pin.path -Algorithm SHA256).Hash
        if ($actual -cne [string]$pin.sha256) { $smbiosFailures.Add(('{0} expected={1} actual={2}' -f $pin.path,$pin.sha256,$actual)) }
    }
    if ($smbiosFailures.Count) { throw ('SMBIOS pin mismatch: '+($smbiosFailures -join '; ')) }
    return [pscustomobject]@{pass=$true;verdictPath=$verdictPath;verdictSha256=(Get-FileHash -LiteralPath $verdictPath -Algorithm SHA256).Hash;checks=[int]$verdict.checks;baselinePinCount=$baselinePins.Count;baselineHeadExpected=$baselineHead;baselineHeadActual=$actualHead;baselineIndexExpectedSha256=$expectedIndexSha256;baselineIndexActualSha256=$actualIndexSha256;smbiosPinCount=$smbiosPins.Count;baselineReferencePath=$script:cycleConfig.baselineReferencePath;smbiosStatePath=$script:cycleConfig.smbiosStatePath;reportDirectory=$reportDirectory;command=$result}
}

function Verify-Cycle {
    param([Parameter(Mandatory=$true)]$State,[Parameter(Mandatory=$true)]$LockedMap)
    Assert-CycleAdmin
    if ($State.phase -notin @('AwaitingSecureBootOn','RolledBackAwaitingSecureBootOn','Complete','RolledBack')) { throw ('Verify requires cleanup-complete state; actual='+$State.phase) }
    $failures = New-Object 'System.Collections.Generic.List[string]'
    $secureBootOn = $false
    try { $secureBootOn = Get-CycleSecureBoot } catch { $failures.Add(('Secure Boot read failed: '+$_.Exception.Message)) }

    $native = $null; $nativeProof = $null
    try {
        $native = Invoke-CycleNativeCapture 'native-final-original'
        $nativeProof = Assert-CycleNativeCapture -CapturePath $native.path -ExpectedMode Original -AllowFailure
        if (-not $nativeProof.pass) { foreach ($line in @($nativeProof.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.line })) { $failures.Add($line) } }
    } catch { $failures.Add(('Final native read failed: '+$_.Exception.Message)) }

    $wmi = $null; $wmiProof = $null
    try {
        $wmi = New-CycleWmiCapture 'wmi-final-original'
        $notBefore = if ([string]::IsNullOrWhiteSpace([string]$State.cycleStartUtc)) { [string]$State.createdUtc } else { [string]$State.cycleStartUtc }
        $wmiProof = Assert-CycleWmiOriginalCapture -CapturePath $wmi.path -NotBeforeUtc $notBefore -AllowFailure
        if (-not $wmi.captureComplete) { $failures.Add(('Final WMI capture incomplete: '+(ConvertTo-Json -InputObject $wmi.document.errors -Compress))) }
        if (-not $wmiProof.pass) { foreach ($line in @($wmiProof.checks | Where-Object { -not $_.pass } | ForEach-Object { $_.line })) { $failures.Add($line) } }
    } catch { $failures.Add(('Final WMI read failed: '+$_.Exception.Message)) }

    $snapshotProof = $null
    try {
        $snapshotProof = Get-CycleSnapshotVerification $State
        if (-not $snapshotProof.pass) { $failures.Add(('Fresh boot-state verification failed: '+(ConvertTo-Json -InputObject $snapshotProof.checks -Depth 12 -Compress))) }
    } catch { $failures.Add(('Fresh boot-state verification could not complete: '+$_.Exception.Message)) }

    $testSigningRestored = $false
    if ($null -ne $State.originalBcd) {
        try {
            $bcd = Get-CycleCurrentBcdObject
            $testSigningRestored = ($bcd.testsigning.present -eq $State.originalBcd.present -and (-not $State.originalBcd.present -or $bcd.testsigning.value -eq $State.originalBcd.value))
        } catch { $failures.Add(('TESTSIGNING read failed: '+$_.Exception.Message)) }
    } elseif ($snapshotProof) { $testSigningRestored = [bool]$snapshotProof.checks.bcdMatches }
    if (-not $testSigningRestored) { $failures.Add('TESTSIGNING presence/value is not proven to match the pre-cycle BCD snapshot.') }

    $trustRestored = $false
    try {
        $trustBaseline = @(Get-CycleOriginalTrustMembership $State)
        $trust = @(Get-CycleTrustMembership $script:cycleConfig.certificateThumbprint)
        $trustRestored = $true
        foreach ($row in $trust) {
            $old = @($trustBaseline | Where-Object store -CEQ $row.store)
            if ($old.Count -ne 1 -or [bool]$row.certificatePresent -ne [bool]$old[0].certificatePresent) { $trustRestored = $false; $failures.Add(('Trust-store mismatch: store={0} expected={1} actual={2}' -f $row.store,$(if ($old.Count) {$old[0].certificatePresent} else {'<missing>'}),$row.certificatePresent)) }
        }
    } catch { $failures.Add(('Trust-store verification failed: '+$_.Exception.Message)) }

    $taskAbsent = ($null -eq (Get-ScheduledTask -TaskName $script:cycleConfig.taskName -ErrorAction SilentlyContinue))
    if (-not $taskAbsent) { $failures.Add('The temporary SYSTEM startup task remains registered.') }
    $targetPackages = @(); $driverStoreRestored = $false; $serviceRegistryAbsent = $false; $servicesAbsent = $false
    try {
        $driverInventory = @(Get-CycleDriverInventory)
        $targetPackages = @($driverInventory | Where-Object { [IO.Path]::GetFileName([string]$_.originalFile) -in @('client_storage_filter.inf','client_nic_filter.inf') })
        $hasMachineBeforeSnapshot = (-not [string]::IsNullOrWhiteSpace([string]$State.machineBeforePath) -and (Test-Path -LiteralPath $State.machineBeforePath))
        if ($State.driverStoreBefore -and $hasMachineBeforeSnapshot) {
            $beforeRows = @($State.driverStoreBefore | ForEach-Object { '{0}|{1}|{2}|{3}|{4}' -f $_.publishedName,$_.originalFile,$_.infSha256,$_.provider,$_.class } | Sort-Object)
            $afterRows = @($driverInventory | ForEach-Object { '{0}|{1}|{2}|{3}|{4}' -f $_.publishedName,$_.originalFile,$_.infSha256,$_.provider,$_.class } | Sort-Object)
            $driverStoreRestored = (($beforeRows -join "`n") -ceq ($afterRows -join "`n"))
        } else { $driverStoreRestored = ($targetPackages.Count -eq 0) }
        if ($targetPackages.Count -gt 0) { $failures.Add(('Filter packages remain in Driver Store: '+(ConvertTo-Json -InputObject $targetPackages -Compress))) }
        if (-not $driverStoreRestored) { $failures.Add('Driver Store inventory differs from the pre-cycle rows.') }
        $serviceRegistry = @(Get-CycleServiceRegistryState)
        $serviceRegistryAbsent = (@($serviceRegistry | Where-Object { $_.present }).Count -eq 0)
        $storageService = @(Get-CycleSystemDriver 'client_storage_filter')
        $nicService = @(Get-CycleSystemDriver 'HypersinvNicAddressFilter')
        $servicesAbsent = ($storageService.Count -eq 0 -and $nicService.Count -eq 0)
        if (-not $serviceRegistryAbsent) { $failures.Add(('Filter service registry keys remain: '+(ConvertTo-Json -InputObject $serviceRegistry -Compress))) }
        if (-not $servicesAbsent) { $failures.Add(('Filter service entries remain: storage='+($storageService | ConvertTo-Json -Compress)+' NIC='+($nicService | ConvertTo-Json -Compress))) }
    } catch { $failures.Add(('Filter package/service verification failed: '+$_.Exception.Message)) }

    $nicComponentAbsent = $false; $bindingsRestored = $false
    try {
        $component = Get-CycleNetCfgComponentState
        $nicComponentAbsent = -not [bool]$component.present
        if (-not $nicComponentAbsent) { $failures.Add('The NIC NetCfg component remains installed.') }
        $bindings = @(Get-CycleAdapterBindings -LockedMap $LockedMap)
        if (@($bindings | Where-Object { $_.adapterCount -ne 1 }).Count -eq 0 -and $hasMachineBeforeSnapshot) {
            $bindingBefore = @((Get-Content -LiteralPath $State.machineBeforePath -Raw | ConvertFrom-Json).adapterBindings)
            $beforeJson = ConvertTo-Json -InputObject @($bindingBefore | Sort-Object pnpDeviceId) -Depth 8 -Compress
            $afterJson = ConvertTo-Json -InputObject @($bindings | Sort-Object pnpDeviceId) -Depth 8 -Compress
            $bindingsRestored = ($beforeJson -ceq $afterJson)
        } else {
            $bindingsRestored = (@($bindings | Where-Object { $_.adapterCount -ne 1 -or ($_.bindingCount -gt 0 -and $_.enabled) }).Count -eq 0)
        }
        if (-not $bindingsRestored) { $failures.Add(('Selected adapter binding map is not restored: '+(ConvertTo-Json -InputObject $bindings -Depth 8 -Compress))) }
    } catch { $failures.Add(('NIC component/binding verification failed: '+$_.Exception.Message)) }

    $roundTrip = $null; $roundTripPass = $false
    try {
        if (-not [string]::IsNullOrWhiteSpace([string]$State.roundTripProofPath) -and [IO.File]::Exists([string]$State.roundTripProofPath)) {
            $roundTripHash = (Get-FileHash -LiteralPath $State.roundTripProofPath -Algorithm SHA256).Hash
            if ($roundTripHash -ceq [string]$State.roundTripProofSha256) {
                $roundTrip = Get-Content -LiteralPath $State.roundTripProofPath -Raw | ConvertFrom-Json
                $roundTripPass = ([string]$roundTrip.result -ceq 'PASS' -and [bool]$roundTrip.liveNativeRoundTripProven)
            } else { $failures.Add(('Round-trip proof hash mismatch: expected={0} actual={1}' -f $State.roundTripProofSha256,$roundTripHash)) }
        } else { $failures.Add('No native Original -> Replacement -> Original proof artifact is recorded.') }
    } catch { $failures.Add(('Round-trip proof could not be validated: '+$_.Exception.Message)) }
    if (-not $roundTripPass -and $failures -notcontains 'No native Original -> Replacement -> Original proof artifact is recorded.') { $failures.Add('Native Original -> Replacement -> Original proof is absent or did not pass.') }

    $suites = $null; $suitesPass = $false
    try { $suites = Test-CycleBaselineAndSuites $State; $suitesPass = [bool]$suites.pass }
    catch { $failures.Add(('Baseline, suite, or SMBIOS pin verification failed: '+$_.Exception.Message)) }

    $nativePass = ($null -ne $nativeProof -and [bool]$nativeProof.pass)
    $wmiPass = ($null -ne $wmiProof -and [bool]$wmiProof.pass -and $wmi.captureComplete)
    $snapshotPass = ($null -ne $snapshotProof -and [bool]$snapshotProof.pass)
    $filtersAbsent = ($targetPackages.Count -eq 0 -and $servicesAbsent -and $serviceRegistryAbsent -and $nicComponentAbsent -and $bindingsRestored -and $driverStoreRestored)
    $cleanupDecision = Get-DeviceCycleCleanupDecision -FiltersAbsent $filtersAbsent -NativeOriginals $nativePass -WmiOriginals $wmiPass -RoundTripProven $roundTripPass
    $machineRestored = ($secureBootOn -and $snapshotPass -and $testSigningRestored -and $trustRestored -and $taskAbsent -and $cleanupDecision.cleanupMayProceed)
    $finalChecks = [pscustomobject]@{pass=($machineRestored -and $suitesPass);machineRestored=$machineRestored;suitesPass=$suitesPass}
    $gatePass = $false
    if ($null -ne $roundTrip) {
        try { $gatePass = [bool](Assert-DeviceCycleRoundTripReady -RoundTrip $roundTrip -Cleanup ([pscustomobject]@{pass=$cleanupDecision.cleanupMayProceed}) -FinalChecks $finalChecks) } catch { $failures.Add(('Final readiness gate: '+$_.Exception.Message)) }
    } else { $failures.Add('Final readiness gate requires a native round-trip proof document.') }
    $ready = ($gatePass -and $machineRestored -and $suitesPass)

    $final = [ordered]@{
        schema='device-cycle-final-verification.v1';result=$(if ($ready) {'PASS'} else {'NOTREADY'});capturedUtc=[DateTime]::UtcNow.ToString('o');secureBootOn=$secureBootOn
        machineRestored=$machineRestored;readinessGatePass=$ready;failures=@($failures.ToArray())
        nativeOriginal=$(if ($native) {[ordered]@{path=$native.path;sha256=$native.sha256;proofPath=$nativeProof.proofPath;proofSha256=$nativeProof.proofSha256;pass=$nativePass}} else {$null})
        wmiOriginal=$(if ($wmi) {[ordered]@{path=$wmi.path;sha256=$wmi.sha256;proofPath=$wmiProof.proofPath;proofSha256=$wmiProof.proofSha256;captureComplete=$wmi.captureComplete;pass=$wmiPass;claim='final originals only'}} else {$null})
        nativeRoundTrip=[ordered]@{path=$State.roundTripProofPath;sha256=$State.roundTripProofSha256;pass=$roundTripPass}
        bootState=$(if ($snapshotProof) {$snapshotProof.checks} else {$null});baselineAndSuites=$suites
        filtersAbsent=$filtersAbsent;temporaryTaskAbsent=$taskAbsent;trustRestored=$trustRestored;testSigningRestored=$testSigningRestored;driverStoreRestored=$driverStoreRestored;nicBindingsRestored=$bindingsRestored
    }
    $path = New-CycleEvidencePath 'final-verification' 'json'
    Write-CycleJsonNew $path $final | Out-Null
    $State | Add-Member -MemberType NoteProperty -Name finalVerificationPath -Value $path -Force
    $State | Add-Member -MemberType NoteProperty -Name finalVerificationSha256 -Value (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -Force
    Add-CycleStateValue $State 'finalMachineRestored' $machineRestored
    Add-CycleStateValue $State 'ready' $ready
    if ($ready -and $State.phase -ne 'Complete') { Set-DeviceCyclePhase $State 'Complete' | Out-Null }
    Save-CycleState $State
    return [pscustomobject]@{result=$(if ($ready) {'READY'} else {'NOTREADY'});machineRestored=$machineRestored;liveRoundTripProven=$roundTripPass;verificationPath=$path;sha256=$State.finalVerificationSha256;statePath=$script:cycleStatePath;failures=@($failures.ToArray())}
}

function Invoke-CycleMain {
    $script:cycleConfigPath = [IO.Path]::GetFullPath($ConfigPath)
    $script:cycleConfig = Get-Content -LiteralPath $script:cycleConfigPath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([int]$script:cycleConfig.schema -ne 1) { throw 'Runtime config schema must be 1.' }
    foreach ($key in @('repoRoot','runDirectory','snapshotPath','packageDirectory','readerPath','lockedTsvPath','proofModulePath','snapshotLibraryPath','espPolicyPath','payloadDirectory','manifestPath','certificatePath','certificateThumbprint','taskName','testVerifierPath','baselineReferencePath','smbiosStatePath')) {
        if ([string]::IsNullOrWhiteSpace([string]$script:cycleConfig.$key)) { throw "Runtime config is missing required path/value: $key" }
    }
    $script:cycleRunDirectory = [IO.Path]::GetFullPath($script:cycleConfig.runDirectory)
    $script:cycleStatePath = Join-Path $script:cycleRunDirectory 'state.json'
    $script:cycleRuntimeModule = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'runtime.psm1'))
    $script:cycleConfig | Add-Member -MemberType NoteProperty -Name runnerPath -Value ([IO.Path]::GetFullPath($PSCommandPath)) -Force
    $handoff = Join-Path ([IO.Path]::GetFullPath($script:cycleConfig.repoRoot)) 'spoof-handoff'
    foreach ($path in @($script:cycleConfigPath,$script:cycleRunDirectory,$script:cycleConfig.snapshotPath,$script:cycleConfig.packageDirectory,$script:cycleConfig.readerPath,$script:cycleConfig.lockedTsvPath,$script:cycleConfig.proofModulePath,$script:cycleConfig.snapshotLibraryPath,$script:cycleConfig.espPolicyPath,$script:cycleConfig.payloadDirectory,$script:cycleConfig.manifestPath,$script:cycleConfig.testVerifierPath,$script:cycleConfig.baselineReferencePath,$script:cycleConfig.smbiosStatePath,$script:cycleConfig.certificatePath)) {
        if (-not (Test-DeviceCyclePathWithin -Path ([IO.Path]::GetFullPath($path)) -Boundary $script:cycleConfig.repoRoot) -and -not [string]::Equals([IO.Path]::GetFullPath($path),[IO.Path]::GetFullPath($script:cycleConfig.repoRoot),[StringComparison]::OrdinalIgnoreCase)) { throw "Runtime path must remain within repoRoot: $path" }
        Assert-DeviceCycleNoReparsePath -Path ([IO.Path]::GetFullPath($path)) -Boundary $script:cycleConfig.repoRoot
    }
    foreach ($writablePath in @($script:cycleRunDirectory,[IO.Path]::GetFullPath($script:cycleConfig.payloadDirectory),[IO.Path]::GetFullPath($script:cycleConfig.snapshotPath))) {
        if (-not (Test-DeviceCyclePathWithin -Path $writablePath -Boundary $handoff)) { throw "Runtime-owned output or snapshot path must remain under spoof-handoff: $writablePath" }
    }
    foreach ($payloadInput in @($script:cycleConfigPath,[IO.Path]::GetFullPath($script:cycleConfig.manifestPath))) {
        if (-not (Test-DeviceCyclePathWithin -Path $payloadInput -Boundary $script:cycleConfig.payloadDirectory)) { throw "Protected config and manifest must remain under the payload directory: $payloadInput" }
    }
    Assert-DeviceCycleNoReparsePath $script:cycleConfigPath $script:cycleConfig.payloadDirectory
    Assert-DeviceCycleNoReparsePath $script:cycleRunDirectory $handoff
    if (-not [IO.Directory]::Exists($script:cycleRunDirectory)) { [void][IO.Directory]::CreateDirectory($script:cycleRunDirectory) }
    $script:cycleManifest = Assert-DeviceCycleManifest -ManifestPath $script:cycleConfig.manifestPath -PayloadDirectory $script:cycleConfig.payloadDirectory -RepoRoot $script:cycleConfig.repoRoot -ExpectedManifestSha256 $ExpectedManifestSha256
    Assert-CycleSignedPayload $script:cycleManifest
    Import-CycleProofModule
    if (-not [IO.File]::Exists($script:cycleStatePath)) { throw "Prepared cycle state is missing: $script:cycleStatePath" }
    $state = Get-Content -LiteralPath $script:cycleStatePath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([int]$state.schema -ne 1 -or [string]::IsNullOrWhiteSpace([string]$state.runId)) { throw 'Prepared cycle state schema or runId is invalid.' }
    $defaults = [ordered]@{
        originalBeforeProofPath=$null;originalBcd=$null;originalBcdIdentifier=$null;machineBeforePath=$null
        driverStoreBefore=@();ownedPackages=@();ownedNicPackage=$null;ownedNicPackages=@();ownedNicComponent=$null;nicInstallIntent=$null;nicInstallObservation=$null;task=$null
        trustBefore=$null;trustAfterImport=$null;testSigningEnabled=$false;enableBootIdentity=$null
        storageInstallStarted=$false;storageInstallInfSha256=$null;storageInstallAfterInventory=@();storageInstallCommandDisposition=$null
        storageRemovalRebootRequired=$false;nicRemovalRebootRequired=$false;nativeReplacementProven=$false
        replacementCapturePath=$null;replacementCaptureSha256=$null;replacementProofPath=$null;replacementProofSha256=$null
        originalAfterPath=$null;originalAfterSha256=$null;originalAfterProofPath=$null;wmiFinalPath=$null;wmiFinalProofPath=$null
        roundTripProofPath=$null;roundTripProofSha256=$null;liveRoundTripProven=$false;ready=$false;commands=@();cycleStartUtc=$null
        cleanupPass=$false;taskRemovedUtc=$null;roundTripDisposition=$null;lastFailure=$null;nextAction=$null
    }
    foreach ($entry in $defaults.GetEnumerator()) { if ($null -eq $state.PSObject.Properties[$entry.Key]) { $state | Add-Member -MemberType NoteProperty -Name $entry.Key -Value $entry.Value -Force } }
    if (-not [string]::Equals([IO.Path]::GetFullPath([string]$state.snapshotPath),[IO.Path]::GetFullPath([string]$script:cycleConfig.snapshotPath),[StringComparison]::OrdinalIgnoreCase)) { throw 'Prepared state snapshot path does not match protected runtime config.' }
    Assert-CycleStateEvidencePins $state $script:cycleManifest
    if ($Mode -ceq 'Status') { return [pscustomobject]@{result='STATUS';phase=$state.phase;ready=[bool]$state.ready;liveRoundTripProven=[bool]$state.liveRoundTripProven;statePath=$script:cycleStatePath;nextAction=$state.nextAction;lastFailure=$state.lastFailure} }
    $lock = Open-CycleLock
    try {
        $locked = Get-CycleLockedMap
        switch ($Mode) {
            'Enable' { return Enable-Cycle $state $locked }
            'Run' { return Run-Cycle $state $locked }
            'Resume' { return Resume-Cycle $state $locked }
            'Rollback' { return Rollback-Cycle $state $locked }
            'Verify' { return Verify-Cycle $state $locked }
        }
    } finally { $lock.Dispose() }
}

try {
    $result = Invoke-CycleMain
    $result | ConvertTo-Json -Depth 15
    if ($result.result -eq 'READY') { exit 0 }
    if ($result.result -in @('POLICY-BOOT-PENDING','REMOVAL-BOOT-PENDING','ROLLBACK-BOOT-PENDING','CLEANUP-PASS-AWAITING-SECUREBOOT-ON','NO-MUTATION','AWAITING-USER-RUN','NO-OP')) { exit 0 }
    exit 2
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    if ($script:cycleRunDirectory -and [IO.Directory]::Exists($script:cycleRunDirectory)) {
        try {
            $errorPath = New-CycleEvidencePath 'runtime-unhandled-error' 'txt'
            [IO.File]::WriteAllText($errorPath,($_.Exception.ToString()+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
            [Console]::Error.WriteLine('Detailed error: '+$errorPath)
        } catch { [Console]::Error.WriteLine('Could not save secondary error evidence: '+$_.Exception.Message) }
    }
    exit 1
}
