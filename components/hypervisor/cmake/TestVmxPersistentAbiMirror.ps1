[CmdletBinding()]
param([string]$Ml64Path)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$headerPath = Join-Path $root 'src/driver/vmx_persistent_launch_abi.h'
$includePath = Join-Path $root 'src/driver/vmx_persistent_launch.inc'
$definitions = [ordered]@{}
$duplicates = [System.Collections.Generic.List[string]]::new()

function Add-Definition([string]$name, [string]$expression) {
    if ($definitions.Contains($name)) {
        $duplicates.Add($name)
    } else {
        $definitions[$name] = $expression.Trim()
    }
}

foreach ($line in Get-Content -LiteralPath $headerPath) {
    if ($line -match '^\s*#define\s+(VMX_V3D_[A-Za-z0-9_]+)\s*(.*)$') {
        $name = $Matches[1]
        if ($line -notmatch ('^\s*#define\s+' + [regex]::Escape($name) + '\(')) {
            Add-Definition $name $Matches[2]
        }
    }
    if ($line -match '^\s*(VMX_V3D_[A-Za-z0-9_]+)\s*=\s*([^,}]+)') {
        Add-Definition $Matches[1] $Matches[2]
    }
}
if ($duplicates.Count -ne 0) {
    throw ("duplicate header definitions: {0}" -f ($duplicates -join ', '))
}

function Resolve-Definition([string]$name, [hashtable]$stack) {
    if ($stack[$name]) { throw ("cyclic header definition: {0}" -f $name) }
    $next = @{}
    foreach ($key in $stack.Keys) { $next[$key] = $true }
    $next[$name] = $true
    $expression = $definitions[$name] -replace '/\*.*?\*/', ''
    $expression = $expression -replace '(?i)UINT(?:32|64)_C\(([^)]*)\)', '$1'
    $expression = [regex]::Replace(
        $expression,
        '\bVMX_V3D_[A-Za-z0-9_]+\b',
        { param($match)
          if ($definitions.Contains($match.Value)) {
              Resolve-Definition $match.Value $next
          } else {
              $match.Value
          }
        })
    $expression = $expression -replace '(?i)(0x[0-9a-f]+|[0-9]+)(ULL|UL|LL|U|L)\b', '$1'
    $expression = $expression -replace '<<', '-shl'
    $expression = $expression -replace '>>', '-shr'
    try {
        return [uint64](Invoke-Expression $expression)
    } catch {
        throw ("non-numeric header definition {0} = {1}" -f $name, $expression)
    }
}

$headerValues = [ordered]@{}
foreach ($name in $definitions.Keys) {
    $headerValues[$name] = Resolve-Definition $name @{}
}

$includeDefinitions = [ordered]@{}
$includeDuplicates = [System.Collections.Generic.List[string]]::new()
foreach ($line in Get-Content -LiteralPath $includePath) {
    if ($line -match '^\s*(VMX_V3D_[A-Za-z0-9_]+)\s+EQU\s+(.+?)\s*$') {
        $name = $Matches[1]
        if ($name -eq 'VMX_V3D_PERSISTENT_LAUNCH_INC') { continue }
        if ($includeDefinitions.Contains($name)) {
            $includeDuplicates.Add($name)
        } else {
            $includeDefinitions[$name] = $Matches[2].Trim()
        }
    }
}
if ($includeDuplicates.Count -ne 0) {
    throw ("duplicate mirror definitions: {0}" -f ($includeDuplicates -join ', '))
}
if ($includeDefinitions.Count -ne $headerValues.Count) {
    throw ("mirror count mismatch: include={0}, header={1}" -f
        $includeDefinitions.Count, $headerValues.Count)
}
$missing = @($headerValues.Keys | Where-Object { -not $includeDefinitions.Contains($_) })
$extra = @($includeDefinitions.Keys | Where-Object { -not $headerValues.Contains($_) })
if ($missing.Count -or $extra.Count) {
    throw ("mirror name mismatch: missing=[{0}] extra=[{1}]" -f
        ($missing -join ', '), ($extra -join ', '))
}

function Resolve-IncludeDefinition([string]$name, [hashtable]$stack) {
    if ($stack[$name]) { throw ("cyclic mirror definition: {0}" -f $name) }
    $next = @{}
    foreach ($key in $stack.Keys) { $next[$key] = $true }
    $next[$name] = $true
    $literal = $includeDefinitions[$name]
    if ($literal -match '^VMX_V3D_[A-Za-z0-9_]+$') {
        if (-not $includeDefinitions.Contains($literal)) {
            throw ("unknown mirror alias for {0}: {1}" -f $name, $literal)
        }
        return Resolve-IncludeDefinition $literal $next
    }
    if ($literal -match '^(?:0x)?[0-9A-Fa-f]+h$') {
        $digits = $literal -replace '^(?:0x)', '' -replace 'h$', ''
        return [Convert]::ToUInt64($digits, 16)
    }
    if ($literal -match '^[0-9]+$') {
        return [uint64]$literal
    }
    throw ("unsupported MASM literal for {0}: {1}" -f $name, $literal)
}

foreach ($name in $headerValues.Keys) {
    $mirrorValue = Resolve-IncludeDefinition $name @{}
    if ($mirrorValue -ne $headerValues[$name]) {
        throw ("value mismatch for {0}: mirror={1} header={2}" -f
            $name, $mirrorValue, $headerValues[$name])
    }
}

$tempDir = Join-Path $env:TEMP ('vmx-v3d-mirror-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null
try {
    $probePath = Join-Path $tempDir 'mirror_probe.asm'
    $objectPath = Join-Path $tempDir 'mirror_probe.obj'
    $includeDir = Split-Path -Parent $includePath
    $includeName = Split-Path -Leaf $includePath
    $probe = @"
OPTION CASEMAP:NONE
INCLUDE $includeName
.code
PUBLIC VmxV3dMirrorProbe
VmxV3dMirrorProbe PROC
    mov eax, VMX_V3D_CONTEXT_SIZE
    mov rax, VMX_V3D_PERSISTENT_LAUNCH_MAGIC
    ret
VmxV3dMirrorProbe ENDP
END
"@
    if ($probe -match '(?im)\b(vmlaunch|vmresume|vmclear|vmxoff|vmcall)\b') {
        throw 'probe unexpectedly contains a live VMX instruction'
    }
    [IO.File]::WriteAllText($probePath, $probe)
    $ml64 = Get-Command ml64.exe -ErrorAction SilentlyContinue
    if ($Ml64Path) {
        if (-not (Test-Path -LiteralPath $Ml64Path -PathType Leaf)) { throw ('ml64 path not found: {0}' -f $Ml64Path) }
        $ml64Path = (Resolve-Path -LiteralPath $Ml64Path).Path
    } elseif ($null -ne $ml64) {
        $ml64Path = $ml64.Source
    } else {
        throw 'ml64.exe not found; initialize VS DevCmd or pass -Ml64Path'
    }
    & $ml64Path /nologo /c "/I$includeDir" "/Fo$objectPath" $probePath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $objectPath)) {
        throw ("ml64 mirror probe failed with exit code {0}" -f $LASTEXITCODE)
    }
    Write-Output ("V3D ABI mirror OK: {0} constants; ml64 probe: {1}" -f
        $headerValues.Count, $objectPath)
} finally {
    Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}
