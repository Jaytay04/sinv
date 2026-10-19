[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [Parameter(Mandatory = $true)]
    [ValidateSet('Probe', 'Capture')]
    [string]$Action,
    [Parameter(Mandatory = $true)]
    [ValidateSet('MET', 'NOT_MET')]
    [string]$ExpectedProbeVerdict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The controller replaces every token exactly once before placing this script
# in the authenticated UEFILoader GuestInfo package as Invoke-WindowsGuestTest.ps1.
$GpuMode = '__GPU_MODE__'
$LoaderDeflateBase64 = '__GPU_LOADER_DEFLATE_BASE64__'
$EfiDeflateBase64 = '__GPU_EFI_DEFLATE_BASE64__'
$InstallerDeflateBase64 = '__GPU_INSTALLER_DEFLATE_BASE64__'
$StartupBase64 = '__GPU_STARTUP_BASE64__'
$ExpectedBuild = [int]'__GPU_EXPECTED_BUILD__'
$ExpectedUbr = [int]'__GPU_EXPECTED_UBR__'
$ExpectedCpus = [int]'__GPU_EXPECTED_CPUS__'
$ExpectedEfiSha256 = '__GPU_EFI_SHA256__'

$RunRoot = [IO.Path]::GetFullPath($RunRoot)
$RunId = Split-Path -Leaf $RunRoot
$ExpectedRunRoot = [IO.Path]::GetFullPath((Join-Path $env:ProgramData `
    'UEFILoaderRunner\runs')).TrimEnd('\') + '\'
$ResultPath = Join-Path $RunRoot 'result.json'
$StdoutPath = Join-Path $RunRoot 'stdout.txt'
$StderrPath = Join-Path $RunRoot 'stderr.txt'
$Result = [ordered]@{
    SchemaVersion = 1
    RunId = $RunId
    Status = 'FAIL'
    Message = 'GPU HAL Windows guest workflow failed.'
    CompletedUtc = $null
    Mode = $GpuMode
    Action = $Action
    ExpectedProbeVerdict = $ExpectedProbeVerdict
    CurrentBuild = $null
    Ubr = $null
    DisplayVersion = $null
    ExpectedBuild = $ExpectedBuild
    ExpectedUbr = $ExpectedUbr
    ExpectedCpus = $ExpectedCpus
    BootManagerSha256 = $null
    SecureBoot = $null
    BitLockerStatus = $null
    BcdBefore = $null
    BcdAfter = $null
    StageRoot = $null
    FailureScriptStackTrace = $null
    SnapshotStep = $null
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text
    )
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Write-Result {
    $Result.CompletedUtc = [DateTime]::UtcNow.ToString('o')
    $Temporary = $ResultPath + '.tmp'
    Write-Utf8NoBom -Path $Temporary -Text ($Result | ConvertTo-Json -Depth 10)
    Move-Item -LiteralPath $Temporary -Destination $ResultPath -Force
}

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $Builder = [Text.StringBuilder]::new()
    [void]$Builder.Append('"')
    $Backslashes = 0
    foreach ($Character in $Value.ToCharArray()) {
        if ($Character -eq [char]92) {
            $Backslashes++
            continue
        }
        if ($Character -eq [char]34) {
            [void]$Builder.Append(('\' * (2 * $Backslashes + 1)))
            [void]$Builder.Append('"')
            $Backslashes = 0
            continue
        }
        if ($Backslashes -gt 0) {
            [void]$Builder.Append(('\' * $Backslashes))
            $Backslashes = 0
        }
        [void]$Builder.Append($Character)
    }
    if ($Backslashes -gt 0) { [void]$Builder.Append(('\' * (2 * $Backslashes))) }
    [void]$Builder.Append('"')
    return $Builder.ToString()
}

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $Info = [Diagnostics.ProcessStartInfo]::new()
    $Info.FileName = $FilePath
    $Info.Arguments = (($Arguments | ForEach-Object {
        Quote-ProcessArgument ([string]$_)
    }) -join ' ')
    $Info.WorkingDirectory = Split-Path -Parent $FilePath
    $Info.UseShellExecute = $false
    $Info.CreateNoWindow = $true
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Process = [Diagnostics.Process]::new()
    $Process.StartInfo = $Info
    if (-not $Process.Start()) { throw "$Label did not start." }
    $StdoutTask = $Process.StandardOutput.ReadToEndAsync()
    $StderrTask = $Process.StandardError.ReadToEndAsync()
    if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
        try { $Process.Kill() } catch {}
        throw "$Label exceeded its ${TimeoutSeconds}-second deadline."
    }
    $Process.WaitForExit()
    $Captured = [pscustomobject]@{
        ExitCode = [int]$Process.ExitCode
        Stdout = [string]$StdoutTask.Result
        Stderr = [string]$StderrTask.Result
    }
    $Process.Dispose()
    return $Captured
}

function Assert-VmLocalSystem {
    $Bios = Get-ItemProperty -LiteralPath 'HKLM:\HARDWARE\DESCRIPTION\System\BIOS'
    if ($null -eq $Bios.PSObject.Properties['SystemManufacturer'] -or
        ([string]$Bios.SystemManufacturer) -notmatch '(?i)^VMware') {
        throw 'GPU Windows guest workflow requires a VMware virtual machine.'
    }
    $Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if ($Identity.User.Value -cne 'S-1-5-18') {
        throw 'GPU Windows guest workflow requires the LocalSystem account.'
    }
}

function Assert-AuthenticatedGuestPackage {
    if ($RunId -cnotmatch `
        '^uefi-windows-(probe|capture)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$' -or
        -not $RunRoot.StartsWith($ExpectedRunRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'GuestInfo RunRoot is outside the fixed UEFILoader windows probe/capture run namespace.'
    }
    $ManifestPath = Join-Path $RunRoot 'SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw 'The authenticated GuestInfo SHA256SUMS.txt manifest is missing.'
    }
    $Expected = @{}
    foreach ($Line in Get-Content -LiteralPath $ManifestPath) {
        if ($Line -cnotmatch '^([0-9A-F]{64})  ([A-Za-z0-9_.-]+)$') {
            throw 'The authenticated GuestInfo SHA-256 manifest is malformed.'
        }
        if ($Expected.ContainsKey($Matches[2])) {
            throw 'The authenticated GuestInfo SHA-256 manifest contains duplicate names.'
        }
        $Expected[$Matches[2]] = $Matches[1]
    }
    foreach ($Name in @('ib-tcg-dump.exe', 'Invoke-WindowsGuestTest.ps1')) {
        $Path = Join-Path $RunRoot $Name
        if (-not $Expected.ContainsKey($Name) -or
            -not (Test-Path -LiteralPath $Path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -cne
                $Expected[$Name]) {
            throw "Authenticated GuestInfo package hash validation failed for $Name."
        }
    }
    if ($PSCommandPath -cne (Join-Path $RunRoot 'Invoke-WindowsGuestTest.ps1')) {
        throw 'The GPU guest template must run under the fixed authenticated worker filename.'
    }
}

function Get-SystemVersionInfo {
    $CurrentVersion = Get-ItemProperty `
        -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $BuildValue = if ($CurrentVersion.PSObject.Properties['CurrentBuildNumber']) {
        [string]$CurrentVersion.CurrentBuildNumber
    } else {
        [string]$CurrentVersion.CurrentBuild
    }
    return [pscustomobject]@{
        Build = [int]$BuildValue
        Ubr = [int]$CurrentVersion.UBR
        DisplayVersion = [string]$CurrentVersion.DisplayVersion
    }
}

function Publish-SetupDiagnostic {
    param([Parameter(Mandatory = $true)][ValidateSet('Entry', 'Progress', 'Result')][string]$Phase)
    $Key = $null
    $Hmac = $null
    $Port = $null
    try {
        $Version = Get-SystemVersionInfo
        $Workers = @([Diagnostics.Process]::GetProcessesByName('powershell') |
            Select-Object Id, StartTime)
        $Services = @(Get-Service -Name 'UEFILoader*' -ErrorAction SilentlyContinue |
            Select-Object Name, Status)
        $WorkerScriptPath = 'C:\ProgramData\UEFILoaderRunner\Invoke-UEFILoaderGuestInfo.ps1'
        $WorkerStatusPath = 'C:\ProgramData\UEFILoaderRunner\worker-status.json'
        $WorkerHash = if (Test-Path -LiteralPath $WorkerScriptPath) {
            (Get-FileHash -LiteralPath $WorkerScriptPath -Algorithm SHA256).Hash
        } else { $null }
        $WorkerStatus = if (Test-Path -LiteralPath $WorkerStatusPath) {
            Get-Content -Raw -LiteralPath $WorkerStatusPath | ConvertFrom-Json
        } else { $null }
        $Diagnostic = [ordered]@{
            SchemaVersion = 1
            Phase = $Phase
            Mode = $GpuMode
            RunId = $RunId
            CurrentBuild = $Version.Build
            Ubr = $Version.Ubr
            ProcessId = $PID
            Workers = $Workers
            Services = $Services
            WorkerScriptSha256 = $WorkerHash
            WorkerStatus = $WorkerStatus
            SerialPorts = @([IO.Ports.SerialPort]::GetPortNames())
            Status = [string]$Result.Status
            Message = [string]$Result.Message
            FailureScriptStackTrace = $Result.FailureScriptStackTrace
            SnapshotStep = $Result.SnapshotStep
            Stdout = $OutputBuilder.ToString()
            Stderr = $ErrorBuilder.ToString()
        }
        $Json = $Diagnostic | ConvertTo-Json -Compress -Depth 6
        $Payload = [Text.UTF8Encoding]::new($false).GetBytes($Json)
        if ($Payload.Length -gt 16000) { return }
        $Key = [IO.File]::ReadAllBytes('C:\ProgramData\UEFILoaderRunner\mailbox.key')
        if ($Key.Length -ne 32) { return }
        $Hmac = [Security.Cryptography.HMACSHA256]::new($Key)
        $Digest = ([BitConverter]::ToString($Hmac.ComputeHash($Payload))).Replace('-', '')
        $Frame = 'GPU_HAL_WINDOWS:SETUP_DIAG:' + [Convert]::ToBase64String($Payload) + '.' + $Digest
        $Vmtoolsd = 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'
        if (Test-Path -LiteralPath $Vmtoolsd -PathType Leaf) {
            # Use the existing per-run direct evidence slot for diagnostics only.
            $Command = 'info-set guestinfo.uefiloader.direct.stage2.' + $RunId + ' ' + $Frame
            $null = Invoke-CapturedProcess -FilePath $Vmtoolsd `
                -Arguments @('--cmd', $Command) -TimeoutSeconds 10 -Label 'publish setup diagnostic'
        }
        if (-not $script:VmGuardPassed) { return }
        $Port = [IO.Ports.SerialPort]::new('COM2', 115200,
            [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
        $Port.Handshake = [IO.Ports.Handshake]::None
        $Port.WriteTimeout = 2000
        $Port.Open()
        $Port.WriteLine($Frame)
        $Port.BaseStream.Flush()
    }
    catch {
        # Diagnostic delivery never changes the authenticated test verdict.
    }
    finally {
        if ($null -ne $Port) { $Port.Dispose() }
        if ($null -ne $Hmac) { $Hmac.Dispose() }
        if ($null -ne $Key) { [Array]::Clear($Key, 0, $Key.Length) }
    }
}

function Get-SecureBootValue {
    try {
        $State = Get-Item -LiteralPath `
            'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' -ErrorAction Stop
        $Value = $State.GetValue('UEFISecureBootEnabled', $null)
        if ($null -eq $Value -or $Value -notin @(0, 1)) { return $null }
        return [bool]$Value
    }
    catch {
        return $null
    }
}

function Get-BitLockerStatusText {
    $ManageBde = Join-Path $env:SystemRoot 'System32\manage-bde.exe'
    $Captured = Invoke-CapturedProcess -FilePath $ManageBde `
        -Arguments @('-status', 'C:') -TimeoutSeconds 30 -Label 'manage-bde status'
    if ($Captured.ExitCode -ne 0) {
        throw "manage-bde status exited with code $($Captured.ExitCode)."
    }
    return ($Captured.Stdout + $Captured.Stderr).Trim()
}

function Invoke-WithEsp {
    param([Parameter(Mandatory = $true)][scriptblock]$Operation)
    $EspDrive = $null
    foreach ($Letter in @('R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z')) {
        if (-not (Test-Path -LiteralPath ($Letter + ':\'))) {
            $EspDrive = $Letter + ':'
            break
        }
    }
    if ($null -eq $EspDrive) { throw 'No bounded ESP mount letter is available.' }
    try {
        $Mount = Invoke-CapturedProcess -FilePath `
            (Join-Path $env:SystemRoot 'System32\mountvol.exe') `
            -Arguments @($EspDrive, '/S') -TimeoutSeconds 30 -Label 'mountvol /S'
        if ($Mount.ExitCode -ne 0) { throw 'mountvol /S failed.' }
        & $Operation $EspDrive
    }
    finally {
        $Unmount = Invoke-CapturedProcess -FilePath `
            (Join-Path $env:SystemRoot 'System32\mountvol.exe') `
            -Arguments @($EspDrive, '/D') -TimeoutSeconds 30 -Label 'mountvol /D'
        if ($Unmount.ExitCode -ne 0) { throw 'mountvol /D failed to release the ESP.' }
    }
}

function Get-BootManagerHash {
    return [string](Invoke-WithEsp {
        param($EspDrive)
        $Path = $EspDrive + '\EFI\Microsoft\Boot\bootmgfw.efi'
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw 'Windows boot manager is absent from the mounted ESP.'
        }
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    })
}

function Get-BcdText {
    $BcdEdit = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
    $Captured = Invoke-CapturedProcess -FilePath $BcdEdit `
        -Arguments @('/enum', 'all') -TimeoutSeconds 30 -Label 'bcdedit /enum all'
    if ($Captured.ExitCode -ne 0) {
        throw "bcdedit /enum all exited with code $($Captured.ExitCode)."
    }
    return $Captured.Stdout
}

function Get-LogicalProcessorCount {
    $Count = [Environment]::ProcessorCount
    if ($Count -lt 1 -or $Count -gt 256) {
        throw "Windows reported an invalid logical processor count: $Count."
    }
    return $Count
}

function ConvertFrom-BoundedDeflateBase64 {
    param(
        [Parameter(Mandatory = $true)][string]$Encoded,
        [Parameter(Mandatory = $true)][int]$MaximumBytes,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($Encoded.Length -eq 0 -or $Encoded.Length -gt 64MB) {
        throw "$Label compressed input is empty or exceeds its encoded-size bound."
    }
    $Compressed = [Convert]::FromBase64String($Encoded)
    $Input = [IO.MemoryStream]::new($Compressed, $false)
    $Inflater = [IO.Compression.DeflateStream]::new(
        $Input, [IO.Compression.CompressionMode]::Decompress)
    $Output = [IO.MemoryStream]::new()
    $Buffer = New-Object byte[] 65536
    try {
        while (($Read = $Inflater.Read($Buffer, 0, $Buffer.Length)) -gt 0) {
            if (($Output.Length + $Read) -gt $MaximumBytes) {
                throw "$Label expanded beyond its $MaximumBytes-byte bound."
            }
            $Output.Write($Buffer, 0, $Read)
        }
        if ($Output.Length -eq 0) { throw "$Label expanded to an empty file." }
        return ,$Output.ToArray()
    }
    finally {
        [Array]::Clear($Buffer, 0, $Buffer.Length)
        [Array]::Clear($Compressed, 0, $Compressed.Length)
        $Inflater.Dispose()
        $Input.Dispose()
        $Output.Dispose()
    }
}

function Set-PrivateDirectoryAcl {
    param([Parameter(Mandatory = $true)][string]$Path)
    $Acl = Get-Acl -LiteralPath $Path
    $Acl.SetAccessRuleProtection($true, $false)
    $Acl.Access | ForEach-Object { [void]$Acl.RemoveAccessRuleAll($_) }
    $SystemSid = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
    $AdministratorsSid = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
    foreach ($Sid in @($SystemSid, $AdministratorsSid)) {
        $Rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $Sid,
            [Security.AccessControl.FileSystemRights]::FullControl,
            ([Security.AccessControl.InheritanceFlags]::ContainerInherit -bor
                [Security.AccessControl.InheritanceFlags]::ObjectInherit),
            [Security.AccessControl.PropagationFlags]::None,
            [Security.AccessControl.AccessControlType]::Allow)
        [void]$Acl.AddAccessRule($Rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $Acl
}

function Get-GraphicsInventory {
    # Registry enumeration does not attest device ownership or driver activity.
    $PciRoot = 'HKLM:\SYSTEM\CurrentControlSet\Enum\PCI'
    foreach ($Device in @(Get-ChildItem -LiteralPath $PciRoot)) {
        foreach ($Instance in @(Get-ChildItem -LiteralPath $Device.PSPath)) {
            if ([string]$Instance.GetValue('ClassGUID') -ine
                '{4d36e968-e325-11ce-bfc1-08002be10318}') { continue }
            $Driver = [string]$Instance.GetValue('Driver')
            $DriverKey = $null
            if ($Driver -match '^\{[0-9a-fA-F-]{36}\}\\[0-9]{4}$') {
                $DriverKey = Get-Item -LiteralPath (
                    'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $Driver) `
                    -ErrorAction SilentlyContinue
            }
            [pscustomobject]@{
                Name = if ($null -ne $DriverKey) {
                    [string]$DriverKey.GetValue('DriverDesc')
                } else { [string]$Instance.GetValue('DeviceDesc') }
                PnpDeviceId = 'PCI\' + $Device.PSChildName + '\' + $Instance.PSChildName
                DriverVersion = if ($null -ne $DriverKey) {
                    [string]$DriverKey.GetValue('DriverVersion')
                } else { $null }
                Status = 'RegistryEnumerated'
                VideoProcessor = $null
            }
        }
    }
}

function Get-InspectSnapshot {
    $Version = Get-SystemVersionInfo
    $Result.SnapshotStep = 'SecureBoot'
    Publish-SetupDiagnostic -Phase Progress
    $SecureBoot = Get-SecureBootValue
    $Result.SnapshotStep = 'BitLocker'
    Publish-SetupDiagnostic -Phase Progress
    $BitLocker = Get-BitLockerStatusText
    $Result.SnapshotStep = 'Bcd'
    Publish-SetupDiagnostic -Phase Progress
    $Bcd = Get-BcdText
    $Result.SnapshotStep = 'BootManagerHash'
    Publish-SetupDiagnostic -Phase Progress
    $BootManagerHash = Get-BootManagerHash
    $Result.SnapshotStep = 'PciRegistry'
    Publish-SetupDiagnostic -Phase Progress
    $Graphics = @(Get-GraphicsInventory)
    $Result.SnapshotStep = 'Complete'
    return [pscustomobject]@{
        Version = $Version
        SecureBoot = $SecureBoot
        BitLocker = $BitLocker
        Bcd = $Bcd
        BootManagerSha256 = $BootManagerHash
        LogicalProcessorCount = Get-LogicalProcessorCount
        GraphicsInventory = $Graphics
    }
}

function Invoke-GuestCapture {
    param([Parameter(Mandatory = $true)][string]$Executable)
    $LogPath = Join-Path $RunRoot 'tcglog.ib'
    $FullLogPath = Join-Path $RunRoot 'tcglog-full.bin'
    $Captured = Invoke-CapturedProcess -FilePath $Executable `
        -Arguments @('capture', '--out', $LogPath, '--full', $FullLogPath) `
        -TimeoutSeconds 120 -Label 'ib-tcg-dump capture'
    if ($Captured.ExitCode -ne 0) {
        throw "ib-tcg-dump capture exited with code $($Captured.ExitCode)."
    }
    foreach ($Path in @($LogPath, $FullLogPath)) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
            (Get-Item -LiteralPath $Path).Length -eq 0) {
            throw "ib-tcg-dump capture did not create nonempty $([IO.Path]::GetFileName($Path))."
        }
    }
    return $Captured
}

function Save-ProbeOrCaptureOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)]$Captured
    )
    $OutputBuilder.AppendLine("===== $Label stdout =====") | Out-Null
    $OutputBuilder.AppendLine([string]$Captured.Stdout) | Out-Null
    if (-not [string]::IsNullOrEmpty([string]$Captured.Stderr)) {
        $ErrorBuilder.AppendLine("===== $Label stderr =====") | Out-Null
        $ErrorBuilder.AppendLine([string]$Captured.Stderr) | Out-Null
    }
}

function Invoke-Stage {
    $NativeRoot = 'C:\ProgramData\HvGpuHalVmTest'
    if (Test-Path -LiteralPath $NativeRoot) {
        throw 'The private GPU VM test staging root already exists; refusing reuse.'
    }
    if ($ExpectedBuild -lt 22000 -or $ExpectedBuild -gt 99999 -or
        $ExpectedUbr -lt 0 -or $ExpectedUbr -gt 999999 -or
        $ExpectedCpus -lt 1 -or $ExpectedCpus -gt 256 -or
        $ExpectedEfiSha256 -cnotmatch '^[0-9A-F]{64}$') {
        throw 'The rendered GPU VM test expectations are invalid.'
    }

    $Version = Get-SystemVersionInfo
    $ActualCpus = Get-LogicalProcessorCount
    $Result.CurrentBuild = $Version.Build
    $Result.Ubr = $Version.Ubr
    $Result.DisplayVersion = $Version.DisplayVersion
    if ($Version.Build -ne $ExpectedBuild -or $Version.Ubr -ne $ExpectedUbr) {
        throw "Windows build $($Version.Build).$($Version.Ubr) does not match the requested $ExpectedBuild.$ExpectedUbr target."
    }
    if ($ActualCpus -ne $ExpectedCpus) {
        throw "Windows reports $ActualCpus logical CPUs; expected $ExpectedCpus."
    }

    $LoaderBytes = ConvertFrom-BoundedDeflateBase64 `
        -Encoded $LoaderDeflateBase64 -MaximumBytes 16MB -Label 'loader'
    $EfiBytes = ConvertFrom-BoundedDeflateBase64 `
        -Encoded $EfiDeflateBase64 -MaximumBytes 32MB -Label 'EFI test image'
    $InstallerBytes = ConvertFrom-BoundedDeflateBase64 `
        -Encoded $InstallerDeflateBase64 -MaximumBytes 16MB -Label 'installer'
    $StartupBytes = [Convert]::FromBase64String($StartupBase64)
    if ($StartupBytes.Length -eq 0 -or $StartupBytes.Length -gt 2MB) {
        throw 'The embedded startup script is empty or exceeds its size bound.'
    }
    $EfiSha = [Security.Cryptography.SHA256]::Create()
    $EfiDigest = $null
    try {
        $EfiDigest = $EfiSha.ComputeHash($EfiBytes)
        $ActualEfiHash = ([BitConverter]::ToString($EfiDigest)).Replace('-', '')
    }
    finally {
        $EfiSha.Dispose()
        if ($null -ne $EfiDigest) { [Array]::Clear($EfiDigest, 0, $EfiDigest.Length) }
    }
    if ($ActualEfiHash -cne $ExpectedEfiSha256) {
        throw 'The embedded EFI test image does not match its expected SHA-256.'
    }
    if ($LoaderBytes.Length -lt 1024 -or $LoaderBytes[0] -ne 0x4d -or
        $LoaderBytes[1] -ne 0x5a) {
        throw 'The embedded direct test loader is not a PE image.'
    }
    if ($InstallerBytes.Length -lt 1024 -or $InstallerBytes[0] -ne 0x4d -or
        $InstallerBytes[1] -ne 0x5a) {
        throw 'The embedded ib-install program is not a PE image.'
    }
    if ($EfiBytes.Length -lt 1024 -or $EfiBytes[0] -ne 0x4d -or
        $EfiBytes[1] -ne 0x5a) {
        throw 'The embedded EFI test image is not a PE image.'
    }

    $SecureBootBefore = Get-SecureBootValue
    $BitLockerBefore = Get-BitLockerStatusText
    $BcdBefore = Get-BcdText
    $BootManagerBefore = Get-BootManagerHash
    $Result.SecureBoot = $SecureBootBefore
    $Result.BitLockerStatus = $BitLockerBefore
    $Result.BcdBefore = $BcdBefore
    $Result.BootManagerSha256 = $BootManagerBefore

    $NativeItem = New-Item -ItemType Directory -Path $NativeRoot
    Set-PrivateDirectoryAcl -Path $NativeRoot
    $LoaderPath = Join-Path $NativeRoot 'gpu-test-loader.efi'
    $EfiPath = Join-Path $NativeRoot 'payload.efi'
    $InstallerPath = Join-Path $NativeRoot 'ib-install.exe'
    $StartupPath = Join-Path $NativeRoot 'Invoke-GpuHalWindowsStartup.ps1'
    $DumpSource = Join-Path $RunRoot 'ib-tcg-dump.exe'
    if (-not (Test-Path -LiteralPath $DumpSource -PathType Leaf)) {
        throw 'ib-tcg-dump.exe is absent from the authenticated GuestInfo run root.'
    }
    $DumpPath = Join-Path $NativeRoot 'ib-tcg-dump.exe'
    [IO.File]::WriteAllBytes($LoaderPath, $LoaderBytes)
    [IO.File]::WriteAllBytes($EfiPath, $EfiBytes)
    [IO.File]::WriteAllBytes($InstallerPath, $InstallerBytes)
    [IO.File]::WriteAllBytes($StartupPath, $StartupBytes)
    Copy-Item -LiteralPath $DumpSource -Destination $DumpPath

    $StartupText = [IO.File]::ReadAllText($StartupPath, [Text.Encoding]::UTF8)
    if ($StartupText -match '__GPU_[A-Z0-9_]+__') {
        throw 'The embedded startup script still contains an unresolved template token.'
    }

    $BitLockerProtectionOn = $BitLockerBefore -match `
        '(?im)Protection Status:\s*Protection On'
    $BitLockerProtectionOff = $BitLockerBefore -match `
        '(?im)Protection Status:\s*Protection Off'
    if (-not $BitLockerProtectionOn -and -not $BitLockerProtectionOff) {
        throw 'BitLocker protection state could not be classified; refusing to stage BootNext.'
    }
    $BitLockerSuspended = $false
    if ($BitLockerProtectionOn) {
        $Suspend = Get-Command -Name Suspend-BitLocker -ErrorAction SilentlyContinue
        if ($null -eq $Suspend) {
            throw 'BitLocker protection is active, but Suspend-BitLocker is unavailable.'
        }
        Suspend-BitLocker -MountPoint 'C:' -RebootCount 2 -ErrorAction Stop |
            Out-Null
        $BitLockerSuspended = $true
        $BitLockerAfter = Get-BitLockerStatusText
        if ($BitLockerAfter -notmatch '(?im)Protection Status:\s*Protection Off') {
            throw 'BitLocker did not report protection suspended for the two test reboots.'
        }
    } else {
        $BitLockerAfter = $BitLockerBefore
    }

    # Suspend protection first so the guest-only BCD change cannot provoke a
    # recovery prompt on the next test boot. Restore comes from VM snapshot.
    # A default / absent value permits Windows to start its own hypervisor.
    # The test VM needs the EFI hypervisor to own VMX; change only this guest BCD.
    $BcdAfter = $BcdBefore
    $CurrentBcd = Invoke-CapturedProcess -FilePath `
        (Join-Path $env:SystemRoot 'System32\bcdedit.exe') `
        -Arguments @('/enum', '{current}') -TimeoutSeconds 30 `
        -Label 'bcdedit current-entry before staging'
    if ($CurrentBcd.ExitCode -ne 0) {
        throw 'Could not inspect the current Windows boot entry.'
    }
    if ($CurrentBcd.Stdout -notmatch '(?im)^\s*hypervisorlaunchtype\s+off\s*$') {
        $BcdEdit = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
        $BcdSet = Invoke-CapturedProcess -FilePath $BcdEdit `
            -Arguments @('/set', '{current}', 'hypervisorlaunchtype', 'off') `
            -TimeoutSeconds 30 -Label 'bcdedit hypervisorlaunchtype off'
        if ($BcdSet.ExitCode -ne 0) {
            throw "bcdedit hypervisorlaunchtype off exited with code $($BcdSet.ExitCode)."
        }
        $CurrentBcdAfter = Invoke-CapturedProcess -FilePath $BcdEdit `
            -Arguments @('/enum', '{current}') -TimeoutSeconds 30 `
            -Label 'bcdedit current-entry readback'
        if ($CurrentBcdAfter.ExitCode -ne 0 -or
            $CurrentBcdAfter.Stdout -notmatch '(?im)^\s*hypervisorlaunchtype\s+off\s*$') {
            throw 'Guest BCD readback did not confirm hypervisorlaunchtype off.'
        }
        $BcdAfter = Get-BcdText
        Save-ProbeOrCaptureOutput -Label 'bcdedit hypervisorlaunchtype off' -Captured $BcdSet
    }

    $FirmwareBefore = Invoke-CapturedProcess -FilePath `
        (Join-Path $env:SystemRoot 'System32\bcdedit.exe') `
        -Arguments @('/enum', '{fwbootmgr}') -TimeoutSeconds 30 `
        -Label 'bcdedit firmware boot-manager before'
    if ($FirmwareBefore.ExitCode -ne 0) {
        throw 'Could not capture firmware BootOrder before installing BootNext.'
    }

    $StageDetails = Invoke-WithEsp {
        param($EspDrive)
        $StageDirectory = $EspDrive + '\EFI\insecure-boot'
        if (Test-Path -LiteralPath $StageDirectory) {
            throw 'EFI\insecure-boot already exists; refusing to overwrite an ESP stage.'
        }
        New-Item -ItemType Directory -Path $StageDirectory | Out-Null
        $ShimPath = Join-Path $StageDirectory 'shimx64.efi'
        $GrubPath = Join-Path $StageDirectory 'grubx64.efi'
        # The vm-direct profile launches grubx64.efi at its existing allowlisted
        # path. The identical shim-named copy only satisfies this VM fixture's
        # shared manifest preflight; it is not an executable shim chain.
        Copy-Item -LiteralPath $LoaderPath -Destination $ShimPath
        Copy-Item -LiteralPath $LoaderPath -Destination $GrubPath
        Copy-Item -LiteralPath $EfiPath `
            -Destination (Join-Path $StageDirectory 'payload.efi')
        $ReferencePath = Join-Path $RunRoot 'tcglog.ib'
        if ((Test-Path -LiteralPath $ReferencePath -PathType Leaf) -and
            (Get-Item -LiteralPath $ReferencePath).Length -gt 0) {
            Copy-Item -LiteralPath $ReferencePath `
                -Destination (Join-Path $StageDirectory 'tcglog.ib')
        } else {
            $Sentinel = [Text.Encoding]::ASCII.GetBytes(
                'GPU VM replay-independent fixture; replay intentionally skipped.' + "`r`n")
            [IO.File]::WriteAllBytes((Join-Path $StageDirectory 'tcglog.ib'), $Sentinel)
            [Array]::Clear($Sentinel, 0, $Sentinel.Length)
        }
        $ManifestLines = @('schema=1')
        foreach ($Name in @('shimx64.efi', 'grubx64.efi')) {
            $Path = Join-Path $StageDirectory $Name
            $Item = Get-Item -LiteralPath $Path
            $Digest = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
            $ManifestLines += "$Name size=$($Item.Length) sha256=$Digest"
        }
        Write-Utf8NoBom -Path (Join-Path $StageDirectory 'manifest.sha256') `
            -Text (($ManifestLines -join "`r`n") + "`r`n")
        $BootManagerPath = $EspDrive + '\EFI\Microsoft\Boot\bootmgfw.efi'
        $BootManagerAtInstall = (Get-FileHash -LiteralPath $BootManagerPath `
            -Algorithm SHA256).Hash
        if ($BootManagerAtInstall -cne $BootManagerBefore) {
            throw 'bootmgfw.efi changed after initial inspection.'
        }
        $Install = Invoke-CapturedProcess -FilePath $InstallerPath `
            -Arguments @('oneshot', '--mode', 'bootnext', '--profile', 'vm-direct',
                '--esp-volume', 'auto') `
            -TimeoutSeconds 90 -Label 'ib-install BootNext'
        Save-ProbeOrCaptureOutput -Label 'ib-install BootNext' -Captured $Install
        if ($Install.ExitCode -ne 0) {
            throw "ib-install oneshot --mode bootnext --profile vm-direct exited with code $($Install.ExitCode)."
        }
        if ($Install.Stdout -notmatch `
            '(?m)^BootNext set to owned Boot[0-9A-F]{4}; BootOrder was not changed\.\r?$') {
            throw 'ib-install BootNext output did not confirm its owned entry and unchanged BootOrder.'
        }
        $BootManagerAfterInstall = (Get-FileHash -LiteralPath $BootManagerPath `
            -Algorithm SHA256).Hash
        if ($BootManagerAfterInstall -cne $BootManagerBefore) {
            throw 'The BootNext setup changed bootmgfw.efi.'
        }
        [pscustomobject]@{
            EspDrive = $EspDrive
            BootNextOutput = $Install.Stdout
            BootNextError = $Install.Stderr
        }
    }
    $BootNextOutput = [string]$StageDetails.BootNextOutput
    $BootNextError = [string]$StageDetails.BootNextError

    $FirmwareAfter = Invoke-CapturedProcess -FilePath `
        (Join-Path $env:SystemRoot 'System32\bcdedit.exe') `
        -Arguments @('/enum', '{fwbootmgr}') -TimeoutSeconds 30 `
        -Label 'bcdedit firmware boot-manager after'
    if ($FirmwareAfter.ExitCode -ne 0) {
        throw 'Could not read back firmware BootOrder after installing BootNext.'
    }
    Save-ProbeOrCaptureOutput -Label 'firmware BootOrder before' -Captured $FirmwareBefore
    Save-ProbeOrCaptureOutput -Label 'firmware BootOrder after' -Captured $FirmwareAfter
    $Metadata = [ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        CreatedUtc = [DateTime]::UtcNow.ToString('o')
        CurrentBuild = $Version.Build
        Ubr = $Version.Ubr
        DisplayVersion = $Version.DisplayVersion
        ExpectedBuild = $ExpectedBuild
        ExpectedUbr = $ExpectedUbr
        ExpectedCpus = $ExpectedCpus
        LogicalProcessorCount = $ActualCpus
        EfiSha256 = $ActualEfiHash
        LoaderSha256 = (Get-FileHash -LiteralPath $LoaderPath -Algorithm SHA256).Hash
        InstallerSha256 = (Get-FileHash -LiteralPath $InstallerPath -Algorithm SHA256).Hash
        BootManagerSha256Before = $BootManagerBefore
        BcdBefore = $BcdBefore
        BcdAfter = $BcdAfter
        SecureBootBefore = $SecureBootBefore
        BitLockerBefore = $BitLockerBefore
        BitLockerAfter = $BitLockerAfter
        BitLockerSuspendedForTwoReboots = $BitLockerSuspended
        FirmwareBootManagerBefore = $FirmwareBefore.Stdout
        FirmwareBootManagerAfter = $FirmwareAfter.Stdout
        FirmwareBootManagerReadbackSucceeded = $true
        BootNextOutput = $BootNextOutput
        BootNextError = $BootNextError
        BootNextSet = $true
        PrivateRoot = $NativeRoot
    }
    Write-Utf8NoBom -Path (Join-Path $NativeRoot 'metadata.json') `
        -Text ($Metadata | ConvertTo-Json -Depth 8)

    $PowerShellPath = Join-Path $env:SystemRoot `
        'System32\WindowsPowerShell\v1.0\powershell.exe'
    $TaskCommand = '"{0}" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{1}"' -f `
        $PowerShellPath, $StartupPath
    $Task = Invoke-CapturedProcess -FilePath `
        (Join-Path $env:SystemRoot 'System32\schtasks.exe') `
        -Arguments @('/Create', '/TN', 'HvGpuHalVmVerify', '/SC', 'ONSTART',
            '/RU', 'SYSTEM', '/RL', 'HIGHEST', '/TR', $TaskCommand, '/F') `
        -TimeoutSeconds 30 -Label 'register HvGpuHalVmVerify'
    Save-ProbeOrCaptureOutput -Label 'register startup verification task' -Captured $Task
    if ($Task.ExitCode -ne 0) {
        throw "Could not register the SYSTEM startup task (exit $($Task.ExitCode))."
    }
    $TaskQuery = Invoke-CapturedProcess -FilePath `
        (Join-Path $env:SystemRoot 'System32\schtasks.exe') `
        -Arguments @('/Query', '/TN', 'HvGpuHalVmVerify', '/XML') `
        -TimeoutSeconds 30 -Label 'verify HvGpuHalVmVerify registration'
    Save-ProbeOrCaptureOutput -Label 'verify startup verification task' -Captured $TaskQuery
    if ($TaskQuery.ExitCode -ne 0 -or
        $TaskQuery.Stdout -notmatch 'HvGpuHalVmVerify') {
        throw 'The SYSTEM startup verification task could not be read back.'
    }

    $Result.StageRoot = $NativeRoot
    $Result.BcdAfter = $BcdAfter
    $Result.BitLockerStatus = $BitLockerAfter
    $Result.Message = 'Windows VM staged for one-shot GPU HAL verification.'
}

$OutputBuilder = [Text.StringBuilder]::new()
$ErrorBuilder = [Text.StringBuilder]::new()
$script:VmGuardPassed = $false
try {
    if ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value -cne 'S-1-5-18' -or
        $RunId -cnotmatch `
        '^uefi-windows-(probe|capture)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$' -or
        -not $RunRoot.StartsWith($ExpectedRunRoot, [StringComparison]::OrdinalIgnoreCase)) {
        exit 2
    }
}
catch {
    exit 2
}

try {
    Assert-VmLocalSystem
    $script:VmGuardPassed = $true
    if ($GpuMode -notin @('Inspect', 'Stage', 'Diagnose')) {
        throw 'Rendered GPU mode must be Inspect, Stage, or Diagnose.'
    }
    if (-not (Test-Path -LiteralPath $RunRoot -PathType Container)) {
        throw 'GuestInfo RunRoot does not exist.'
    }
    Assert-AuthenticatedGuestPackage
    Publish-SetupDiagnostic -Phase Entry
    if ($GpuMode -ceq 'Diagnose') {
        $EventQueries = [ordered]@{
            'Application' = '*[System[(EventID=1000 or EventID=1001 or EventID=1026)]]'
            'System' = '*[System[(EventID=7031 or EventID=7034 or EventID=7036)]]'
            'Microsoft-Windows-Windows Defender/Operational' = '*[System[(EventID=1116 or EventID=1117 or EventID=1121)]]'
            'Windows PowerShell' = '*[System[(EventID=400 or EventID=403)]]'
        }
        foreach ($Channel in $EventQueries.Keys) {
            $Events = Invoke-CapturedProcess -FilePath (
                Join-Path $env:SystemRoot 'System32\wevtutil.exe') `
                -Arguments @('qe', $Channel, ('/q:' + $EventQueries[$Channel]),
                    '/rd:true', '/c:8', '/f:xml') -TimeoutSeconds 10 `
                -Label ('read events ' + $Channel)
            Save-ProbeOrCaptureOutput -Label $Channel -Captured $Events
        }
        $PriorRuns = @(Get-ChildItem -LiteralPath $ExpectedRunRoot -Directory |
            Where-Object { $_.Name -ne $RunId -and
                $_.Name -match '^uefi-windows-(probe|capture)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$' -and
                $_.LastWriteTimeUtc -ge [DateTime]::UtcNow.AddMinutes(-10) } |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 2)
        foreach ($Prior in $PriorRuns) {
            foreach ($Name in @('result.json', 'runner-stdout.txt',
                    'runner-stderr.txt', 'stdout.txt', 'stderr.txt')) {
                $PriorPath = Join-Path $Prior.FullName $Name
                if ((Test-Path -LiteralPath $PriorPath -PathType Leaf) -and
                    (Get-Item -LiteralPath $PriorPath).Length -le 65536) {
                    $OutputBuilder.AppendLine("===== prior $($Prior.Name) $Name =====") | Out-Null
                    $OutputBuilder.AppendLine([IO.File]::ReadAllText($PriorPath)) | Out-Null
                }
            }
        }
        $Result.Message = 'Read-only VM worker event diagnostics collected.'
        $Result.Status = 'PASS'
        Write-Utf8NoBom -Path $StdoutPath -Text $OutputBuilder.ToString()
        Write-Utf8NoBom -Path $StderrPath -Text $ErrorBuilder.ToString()
        Write-Result
        exit 0
    }
    if ($ExpectedCpus -lt 1 -or $ExpectedCpus -gt 256) {
        throw 'Expected CPU count is outside its supported bound.'
    }
    if ($ExpectedBuild -lt 22000 -or $ExpectedBuild -gt 99999 -or
        $ExpectedUbr -lt 0 -or $ExpectedUbr -gt 999999) {
        throw 'Expected Windows build and UBR are outside the supported bounds.'
    }

    $Executable = Join-Path $RunRoot 'ib-tcg-dump.exe'
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw 'ib-tcg-dump.exe is absent from the authenticated GuestInfo run root.'
    }
    if ($Action -eq 'Capture') {
        $Capture = Invoke-GuestCapture -Executable $Executable
        Save-ProbeOrCaptureOutput -Label 'ib-tcg-dump capture' -Captured $Capture
    }
    # Probe is the fixed authenticated transport action. This GPU test does
    # not depend on the unrelated TPM empty-bank precondition.

    $Snapshot = Get-InspectSnapshot
    $Result.CurrentBuild = $Snapshot.Version.Build
    $Result.Ubr = $Snapshot.Version.Ubr
    $Result.DisplayVersion = $Snapshot.Version.DisplayVersion
    $Result.BootManagerSha256 = $Snapshot.BootManagerSha256
    $Result.SecureBoot = $Snapshot.SecureBoot
    $Result.BitLockerStatus = $Snapshot.BitLocker
    $Result.BcdBefore = $Snapshot.Bcd
    $Result.LogicalProcessorCount = $Snapshot.LogicalProcessorCount
    $Result.GraphicsInventory = $Snapshot.GraphicsInventory
    $OutputBuilder.AppendLine("WindowsBuild=$($Snapshot.Version.Build).$($Snapshot.Version.Ubr)") | Out-Null
    $OutputBuilder.AppendLine("DisplayVersion=$($Snapshot.Version.DisplayVersion)") | Out-Null
    $OutputBuilder.AppendLine("ExpectedBuild=$ExpectedBuild.$ExpectedUbr") | Out-Null
    $OutputBuilder.AppendLine("ExpectedCpus=$ExpectedCpus ActualCpus=$($Snapshot.LogicalProcessorCount)") | Out-Null
    $OutputBuilder.AppendLine("BootManagerSha256=$($Snapshot.BootManagerSha256)") | Out-Null
    $OutputBuilder.AppendLine("SecureBoot=$($Snapshot.SecureBoot)") | Out-Null
    $OutputBuilder.AppendLine('BCD_BEGIN') | Out-Null
    $OutputBuilder.AppendLine($Snapshot.Bcd) | Out-Null
    $OutputBuilder.AppendLine('BCD_END') | Out-Null
    $OutputBuilder.AppendLine('BITLOCKER_BEGIN') | Out-Null
    $OutputBuilder.AppendLine($Snapshot.BitLocker) | Out-Null
    $OutputBuilder.AppendLine('BITLOCKER_END') | Out-Null
    $OutputBuilder.AppendLine('GRAPHICS_BEGIN') | Out-Null
    $OutputBuilder.AppendLine(($Snapshot.GraphicsInventory | ConvertTo-Json -Depth 5)) | Out-Null
    $OutputBuilder.AppendLine('GRAPHICS_END') | Out-Null

    if ($GpuMode -eq 'Stage') {
        Invoke-Stage
    } else {
        if ($Snapshot.Version.Build -ne $ExpectedBuild -or
            $Snapshot.Version.Ubr -ne $ExpectedUbr) {
            throw "Inspected Windows build $($Snapshot.Version.Build).$($Snapshot.Version.Ubr) does not match the target $ExpectedBuild.$ExpectedUbr."
        }
        if ($Snapshot.LogicalProcessorCount -ne $ExpectedCpus) {
            throw "Inspected Windows CPU count $($Snapshot.LogicalProcessorCount) does not match the target $ExpectedCpus."
        }
        $Result.Message = 'Read-only Windows guest inspection matched the requested VM profile.'
    }

    $Result.Status = 'PASS'
    Write-Utf8NoBom -Path $StdoutPath -Text $OutputBuilder.ToString()
    Write-Utf8NoBom -Path $StderrPath -Text $ErrorBuilder.ToString()
    Write-Result
    exit 0
}
catch {
    $Result.Status = 'FAIL'
    $Result.Message = $_.Exception.Message
    $FailureScriptStackTrace = [string]$_.ScriptStackTrace
    if ($FailureScriptStackTrace.Length -gt 4096) {
        $FailureScriptStackTrace = $FailureScriptStackTrace.Substring(0, 4096)
    }
    $Result.FailureScriptStackTrace = $FailureScriptStackTrace
    $ErrorBuilder.AppendLine($_.Exception.ToString()) | Out-Null
    try {
        if (Test-Path -LiteralPath $RunRoot -PathType Container) {
            Write-Utf8NoBom -Path $StdoutPath -Text $OutputBuilder.ToString()
            Write-Utf8NoBom -Path $StderrPath -Text $ErrorBuilder.ToString()
            Write-Result
        }
    } catch {}
    exit 1
}
finally {
    Publish-SetupDiagnostic -Phase Result
}
