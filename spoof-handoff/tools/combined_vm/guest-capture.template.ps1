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
    Message = 'Fresh SMBIOS capture did not complete.'
    ExpectedProbeVerdict = $ExpectedProbeVerdict
}
$stdoutPath = Join-Path $runRootFull 'stdout.txt'
$stderrPath = Join-Path $runRootFull 'stderr.txt'
$metadataPath = Join-Path $runRootFull 'kernel-metadata.json'
$capturePath = Join-Path $runRootFull 'smbios-capture.json'
$readerPath = Join-Path $runRootFull 'smbios_windows_readback.exe'
$diagnostics = [Collections.Generic.List[string]]::new()

try {
    if (-not $runRootFull.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $runRootFull -PathType Container)) {
        throw 'Capture RunRoot is outside the authenticated guest mailbox run directory.'
    }
    if ($ExpectedProbeVerdict -notin @('MET', 'NOT_MET')) { throw 'Unexpected probe verdict.' }
    $computer = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
    if ([string]$computer.Manufacturer -notmatch '(?i)^VMware') {
        throw ('Fresh guest identity is not VMware: manufacturer=' + [string]$computer.Manufacturer + '; model=' + [string]$computer.Model)
    }
    $readerBytes = [Convert]::FromBase64String('__CAPTURE_READER_BASE64__')
    try {
        [IO.File]::WriteAllBytes($readerPath, $readerBytes)
    }
    finally {
        [Array]::Clear($readerBytes, 0, $readerBytes.Length)
    }
    $readerSha256 = (Get-FileHash -LiteralPath $readerPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($readerSha256 -cne '__CAPTURE_READER_SHA256__') { throw 'Embedded SMBIOS capture reader hash mismatch.' }

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $readerOutput = @(& $readerPath --capture --output $capturePath 2>&1 | ForEach-Object { [string]$_ })
        $readerExit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    $diagnostics.AddRange([string[]]$readerOutput)
    if ($readerExit -ne 0) { throw ('SMBIOS reader failed with exit=' + $readerExit + '; output=' + ($readerOutput -join ' ')) }
    if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf)) { throw 'SMBIOS reader succeeded without writing its capture.' }
    $capture = Get-Content -LiteralPath $capturePath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($capture.schema -cne 'smbios-windows-capture.v1' -or
        $capture.mode -cne 'capture' -or $capture.result -cne 'PASS') {
        throw 'SMBIOS capture report schema, mode, or result is invalid.'
    }
    foreach ($field in @('system_serial','system_vendor','system_product','board_serial','board_vendor','board_product','system_uuid')) {
        $record = $capture.fields.$field
        if ($null -eq $record -or $record.nativeMatchesWmi -ne $true -or
            [string]$record.observedNative -cne [string]$record.observedWmi -or
            [string]::IsNullOrWhiteSpace([string]$record.observedNative)) {
            throw ('Fresh RSMB/WMI capture is missing or disagrees for ' + $field)
        }
    }
    if ([string]$capture.machineIdentity.systemVendorNative -cne [string]$capture.fields.system_vendor.observedNative -or
        [string]$capture.machineIdentity.systemVendorWmi -cne [string]$capture.fields.system_vendor.observedWmi -or
        [string]$capture.machineIdentity.systemProductNative -cne [string]$capture.fields.system_product.observedNative -or
        [string]$capture.machineIdentity.systemProductWmi -cne [string]$capture.fields.system_product.observedWmi) {
        throw 'SMBIOS machine identity fields differ from the seven-field capture.'
    }
    [IO.File]::Copy($capturePath, $metadataPath, $true)
    $result.Status = 'PASS'
    $result.Message = 'Fresh RSMB and WMI capture completed for all seven SMBIOS fields.'
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
