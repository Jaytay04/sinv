[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [string]$Action = 'Probe',
    [string]$ExpectedProbeVerdict = 'NOT_MET'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$runRootFull = [IO.Path]::GetFullPath($RunRoot)
$allowedRoot = 'C:\ProgramData\UEFILoaderRunner\runs\'
$result = [ordered]@{
    SchemaVersion = 1
    RunId = Split-Path -Leaf $runRootFull
    Status = 'FAIL'
    Message = 'Fresh combined Windows inventory did not complete.'
    ExpectedProbeVerdict = $ExpectedProbeVerdict
}
$stdoutPath = Join-Path $runRootFull 'stdout.txt'
$stderrPath = Join-Path $runRootFull 'stderr.txt'
$metadataPath = Join-Path $runRootFull 'kernel-metadata.json'
$smbiosExe = Join-Path $runRootFull 'smbios_windows_readback.exe'
$nicExe = Join-Path $runRootFull 'nic_native_read.exe'
$storageScript = Join-Path $runRootFull 'storage-read.ps1'
$diagnostics = [Collections.Generic.List[string]]::new()

try {
    if (-not $runRootFull.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $runRootFull -PathType Container)) {
        throw 'Inventory RunRoot is outside the authenticated guest mailbox run directory.'
    }
    if ($ExpectedProbeVerdict -notin @('MET', 'NOT_MET')) { throw 'Unexpected probe verdict.' }
    $computer = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    if ([string]$computer.Manufacturer -notmatch '(?i)^VMware') {
        throw ('Fresh guest identity is not VMware: manufacturer=' + [string]$computer.Manufacturer + '; model=' + [string]$computer.Model)
    }
    $smbiosBytes = [Convert]::FromBase64String('__SMBIOS_READER_BASE64__')
    $nicBytes = [Convert]::FromBase64String('__NIC_READER_BASE64__')
    $storageBytes = [Convert]::FromBase64String('__STORAGE_READER_BASE64__')
    try {
        [IO.File]::WriteAllBytes($smbiosExe, $smbiosBytes)
        [IO.File]::WriteAllBytes($nicExe, $nicBytes)
        [IO.File]::WriteAllBytes($storageScript, $storageBytes)
    }
    finally {
        [Array]::Clear($smbiosBytes, 0, $smbiosBytes.Length)
        [Array]::Clear($nicBytes, 0, $nicBytes.Length)
        [Array]::Clear($storageBytes, 0, $storageBytes.Length)
    }
    $expectedHashes = [ordered]@{
        smbiosReader = '__SMBIOS_READER_SHA256__'
        nicReader = '__NIC_READER_SHA256__'
        storageReader = '__STORAGE_READER_SHA256__'
    }
    $actualHashes = [ordered]@{
        smbiosReader = (Get-FileHash -LiteralPath $smbiosExe -Algorithm SHA256).Hash.ToUpperInvariant()
        nicReader = (Get-FileHash -LiteralPath $nicExe -Algorithm SHA256).Hash.ToUpperInvariant()
        storageReader = (Get-FileHash -LiteralPath $storageScript -Algorithm SHA256).Hash.ToUpperInvariant()
    }
    foreach ($name in $expectedHashes.Keys) {
        if ($actualHashes[$name] -cne $expectedHashes[$name]) { throw "Embedded reader hash mismatch: $name" }
    }

    $smbiosPath = Join-Path $runRootFull 'guest-smbios-original.json'
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $smbiosOutput = @(& $smbiosExe --capture --output $smbiosPath 2>&1 | ForEach-Object { [string]$_ })
        $smbiosExit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    $diagnostics.AddRange([string[]]$smbiosOutput)
    if ($smbiosExit -ne 0) { throw ('SMBIOS capture failed; exit=' + $smbiosExit + '; output=' + ($smbiosOutput -join ' ')) }
    $smbios = Get-Content -LiteralPath $smbiosPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($smbios.schema -cne 'smbios-windows-capture.v1' -or $smbios.result -cne 'PASS') { throw 'SMBIOS original capture schema/result is invalid.' }
    foreach ($field in @('system_serial','system_vendor','system_product','board_serial','board_vendor','board_product','system_uuid')) {
        $fieldRecord = $smbios.fields.$field
        if ($fieldRecord.nativeMatchesWmi -ne $true -or [string]$fieldRecord.observedNative -cne [string]$fieldRecord.observedWmi) {
            throw ('SMBIOS original native/WMI mismatch: ' + $field)
        }
    }

    . $storageScript
    $diskPath = '\\.\PHYSICALDRIVE0'
    $diskWmiRows = @(Get-CimInstance -ClassName Win32_DiskDrive -ErrorAction Stop | Where-Object { [string]$_.DeviceID -ceq $diskPath })
    if ($diskWmiRows.Count -ne 1) { throw "Expected exactly one PHYSICALDRIVE0 WMI row; count=$($diskWmiRows.Count)." }
    $diskNative = [StorageHostReadNative]::ReadStorageSerial($diskPath)
    $diskWmi = ([string]$diskWmiRows[0].SerialNumber).Trim()
    if ($diskNative -cne $diskWmi) { throw "Disk native/WMI original mismatch: native=$diskNative wmi=$diskWmi" }
    $diskNumber = [int]$diskWmiRows[0].Index
    if ($diskNumber -ne 0) { throw "PHYSICALDRIVE0 WMI row resolved to unexpected disk number $diskNumber." }
    $diskDetails = Get-Disk -Number $diskNumber -ErrorAction Stop
    $diskHardwareProperty = Get-PnpDeviceProperty -InstanceId ([string]$diskWmiRows[0].PNPDeviceID) `
        -KeyName 'DEVPKEY_Device_HardwareIds' -ErrorAction Stop
    $diskHardwareIds = @($diskHardwareProperty.Data | ForEach-Object { [string]$_ } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($diskHardwareIds.Count -eq 0) { throw 'PHYSICALDRIVE0 has no fresh DEVPKEY_Device_HardwareIds values.' }

    $nicCapturePath = Join-Path $runRootFull 'guest-nic-native-original.json'
    $nicStdoutPath = Join-Path $runRootFull 'guest-nic-native.stdout.txt'
    $nicStderrPath = Join-Path $runRootFull 'guest-nic-native.stderr.txt'
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $nicExe --capture $nicCapturePath 1> $nicStdoutPath 2> $nicStderrPath
        $nicExit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    $nicOutput = @()
    if (Test-Path -LiteralPath $nicStdoutPath -PathType Leaf) { $nicOutput += Get-Content -LiteralPath $nicStdoutPath }
    if (Test-Path -LiteralPath $nicStderrPath -PathType Leaf) { $nicOutput += Get-Content -LiteralPath $nicStderrPath }
    if ($nicExit -ne 0) { throw ('NIC native baseline failed; exit=' + $nicExit + '; output=' + ($nicOutput -join ' ')) }
    $nicNative = Get-Content -LiteralPath $nicCapturePath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    $nicWmi = @(Get-CimInstance -ClassName Win32_NetworkAdapter -ErrorAction Stop |
        Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_.PNPDeviceID) -and
            -not [string]::IsNullOrWhiteSpace([string]$_.MACAddress) } |
        Select-Object DeviceID, PNPDeviceID, GUID, Name, MACAddress, PhysicalAdapter, NetEnabled, InterfaceIndex)
    $nicNetAdapter = @()
    $getNetAdapterError = $null
    try {
        $nicNetAdapter = @(Get-NetAdapter -IncludeHidden -ErrorAction Stop |
            Select-Object Name, InterfaceDescription, MacAddress, InterfaceGuid, ifIndex, Status, HardwareInterface, Virtual, PnPDeviceID)
    }
    catch { $getNetAdapterError = $_.Exception.ToString() }

    $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
    $product = Get-CimInstance -ClassName Win32_ComputerSystemProduct -ErrorAction Stop
    $windowsBuild = [int]$os.BuildNumber
    $windowsCurrentVersion = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' `
        -Name UBR -ErrorAction Stop
    $windowsUbr = [int]$windowsCurrentVersion.UBR
    if ($windowsBuild -lt 1 -or $windowsUbr -lt 0) { throw 'Fresh Windows build metadata is invalid.' }
    $vmDeviceInventoryPath = Join-Path $runRootFull 'vm-device-inventory.json'
    $vmDeviceInventory = [ordered]@{
        schema = 'vm-device-inventory.v1'
        capturedUtc = [DateTime]::UtcNow.ToString('o')
        machine = [ordered]@{
            manufacturer = [string]$computer.Manufacturer
            model = [string]$computer.Model
            uuid = [string]$product.UUID
        }
        windows = [ordered]@{ build = $windowsBuild; ubr = $windowsUbr }
        disk = [ordered]@{
            number = $diskNumber
            isBoot = [bool]$diskDetails.IsBoot
            isSystem = [bool]$diskDetails.IsSystem
            devicePath = $diskPath
            pnpDeviceId = [string]$diskWmiRows[0].PNPDeviceID
            hardwareIds = @($diskHardwareIds)
            serial = $diskNative
            wmiSerial = $diskWmi
        }
    }
    [IO.File]::WriteAllText($vmDeviceInventoryPath, ($vmDeviceInventory | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
    $record = [ordered]@{
        schema = 'combined-vm-original-inventory.v1'
        capturedUtc = [DateTime]::UtcNow.ToString('o')
        machine = [ordered]@{
            manufacturer = [string]$computer.Manufacturer
            model = [string]$computer.Model
            systemVendor = [string]$smbios.machineIdentity.systemVendorNative
            systemProduct = [string]$smbios.machineIdentity.systemProductNative
            uuidWmi = [string]$product.UUID
            uuidSmbios = [string]$smbios.fields.system_uuid.observedNative
        }
        bootIdentity = [ordered]@{ windowsLastBootUpTimeUtc = $os.LastBootUpTime.ToUniversalTime().ToString('o') }
        windows = [ordered]@{ build = $windowsBuild; ubr = $windowsUbr }
        smbiosCapture = $smbios
        disk = [ordered]@{
            path = $diskPath
            number = $diskNumber
            isBoot = [bool]$diskDetails.IsBoot
            isSystem = [bool]$diskDetails.IsSystem
            hardwareIds = @($diskHardwareIds)
            pnpDeviceId = [string]$diskWmiRows[0].PNPDeviceID
            nativeSerial = $diskNative
            wmiSerial = $diskWmi
            nativeSource = 'CreateFileW + IOCTL_STORAGE_QUERY_PROPERTY / StorageDeviceProperty'
            wmiSource = 'Fresh Win32_DiskDrive CIM query'
        }
        vmDeviceInventoryPath = $vmDeviceInventoryPath
        vmDeviceInventorySha256 = (Get-FileHash -LiteralPath $vmDeviceInventoryPath -Algorithm SHA256).Hash.ToUpperInvariant()
        vmDeviceInventory = $vmDeviceInventory
        nic = [ordered]@{
            nativeCapture = $nicNative
            nativeCapturePath = $nicCapturePath
            nativeCaptureSha256 = (Get-FileHash -LiteralPath $nicCapturePath -Algorithm SHA256).Hash.ToUpperInvariant()
            wmiAdapters = $nicWmi
            getNetAdapter = $nicNetAdapter
            getNetAdapterError = $getNetAdapterError
        }
        embeddedInputs = $actualHashes
        testSigningChange = $false
        secureBootChange = $false
        networkChange = $false
        driverChange = $false
    }
    [IO.File]::WriteAllText($metadataPath, ($record | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
    $result.Status = 'PASS'
    $result.Message = 'Fresh VM originals captured through SMBIOS/RSMB+WMI, storage IOCTL+WMI, NIC native capture, and adapter WMI.'
}
catch {
    $result.Message = $_.Exception.ToString()
}
finally {
    [IO.File]::WriteAllText($stdoutPath, (($diagnostics.ToArray() -join [Environment]::NewLine) + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderrPath, $(if ($result.Status -eq 'PASS') { '' } else { [string]$result.Message }), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $runRootFull 'result.json'), ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
}
if ($result.Status -cne 'PASS') { exit 1 }
