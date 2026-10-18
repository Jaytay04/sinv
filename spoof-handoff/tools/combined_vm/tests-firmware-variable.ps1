[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path $PSScriptRoot 'firmware-variable.psm1'
if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) { throw "Missing module: $modulePath" }
Import-Module $modulePath -Force

$checks = [Collections.Generic.List[object]]::new()
foreach ($name in @('Read-CombinedFirmwareVariable', 'Write-CombinedFirmwareVariable', 'Remove-CombinedFirmwareVariable')) {
    $command = Get-Command $name -ErrorAction Stop
    if ($command.CommandType -ne 'Function') { throw "Unexpected command kind for ${name}: $($command.CommandType)" }
    $checks.Add([ordered]@{ name = "exports $name"; pass = $true })
}

$apiType = 'CombinedVmFirmwareVariableApi' -as [type]
if ($null -eq $apiType) { throw 'Firmware variable interop type did not compile.' }
foreach ($methodName in @('Read', 'Write', 'Delete')) {
    $methods = @($apiType.GetMethods([Reflection.BindingFlags]'Public,Static') | Where-Object Name -CEQ $methodName)
    if ($methods.Count -ne 1) { throw "Expected one public static $methodName entry point; actual=$($methods.Count)" }
    $checks.Add([ordered]@{ name = "compiles $methodName operation"; pass = $true })
}

$deleteArguments = $apiType::CreateDeleteArguments([uint32]7)
if ($null -ne $deleteArguments.Buffer -or [uint32]$deleteArguments.Size -ne 0 -or [uint32]$deleteArguments.Attributes -ne 7) {
    throw 'Firmware deletion arguments must use a null data pointer, zero size, and the exact previously read attributes.'
}
$deleteMethod = @($apiType.GetMethods([Reflection.BindingFlags]'Public,Static') | Where-Object Name -CEQ 'Delete')[0]
$deleteParameters = @($deleteMethod.GetParameters())
if ($deleteParameters.Count -ne 3 -or $deleteParameters[2].ParameterType -ne [uint32]) {
    throw 'Firmware deletion must accept the previously read attributes explicitly.'
}
$checks.Add([ordered]@{ name = 'zero-size deletion preserves the exact existing attributes'; pass = $true; attributes = 7 })

$source = [IO.File]::ReadAllText($modulePath)
foreach ($entryPoint in @('GetFirmwareEnvironmentVariableExW', 'SetFirmwareEnvironmentVariableExW', 'SeSystemEnvironmentPrivilege')) {
    if (-not $source.Contains($entryPoint)) { throw "Missing required firmware interop declaration: $entryPoint" }
    $checks.Add([ordered]@{ name = "declares $entryPoint"; pass = $true })
}
if (-not $source.Contains('scope.Dispose();') -or -not $source.Contains('privilegeRestored')) {
    throw 'Firmware variable operations must restore the prior privilege state and expose the result.'
}
$deleteCall = 'Set(name, guid, arguments.Buffer, arguments.Size, arguments.Attributes)'
if (-not $source.Contains($deleteCall) -or -not $source.Contains('Delete($Name, $Guid, $Attributes)')) {
    throw 'Firmware deletion must pass the verified existing attributes through to the native API.'
}
$checks.Add([ordered]@{ name = 'native delete forwards verified original attributes'; pass = $true })
$checks.Add([ordered]@{ name = 'privilege restoration is observable'; pass = $true })
if ([regex]::Matches($source, 'result\["supported"\]\s*=\s*success').Count -ne 2) {
    throw 'Successful write and delete operations must report supported=true; failed calls must not.'
}
$checks.Add([ordered]@{ name = 'write and delete support follows the native result'; pass = $true })
if (-not $source.Contains('result["success"] = false;') -or -not $source.Contains('result["win32Error"] = scope.RestoreError;')) {
    throw 'Privilege restoration failure must fail the operation with its exact Win32 error.'
}
$checks.Add([ordered]@{ name = 'privilege restore failure fails the operation'; pass = $true })

[ordered]@{
    schema = 'combined-vm-firmware-variable-module-tests.v1'
    status = 'PASS'
    checkCount = $checks.Count
    firmwareApiInvoked = $false
    checks = @($checks.ToArray())
} | ConvertTo-Json -Depth 8
