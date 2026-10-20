[CmdletBinding()]
param(
    [string]$ToolRoot
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = Split-Path -Parent $PSScriptRoot
}
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$modulePath = Join-Path $ToolRoot 'GpuHostState.psm1'
if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
    throw "Required module was not found: $modulePath"
}
Import-Module -Name $modulePath -Force -ErrorAction Stop

$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = New-Object 'System.Collections.Generic.List[string]'
$script:SkippedCases = New-Object 'System.Collections.Generic.List[string]'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}

function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    $script:Assertions++
    if ($Expected -is [string] -or $Actual -is [string]) {
        if ([string]$Expected -cne [string]$Actual) { throw "$Message Expected '$Expected', got '$Actual'." }
    } elseif ($Expected -ne $Actual) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
}

function Assert-Throws {
    param([scriptblock]$Action, [string]$Message)
    $threw = $false
    try { & $Action } catch { $threw = $true }
    Assert-True $threw $Message
}

function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body
        $script:PassedCases++
    } catch {
        $script:FailedCases.Add("$Name`: $($_.Exception.Message)")
    }
}

function Get-ObjectPropertyName {
    param($Object, [string[]]$Candidates, [string]$Description)
    foreach ($candidate in $Candidates) {
        if ($null -ne $Object.PSObject.Properties[$candidate]) { return $candidate }
    }
    throw "The $Description field was absent. Available fields: $($Object.PSObject.Properties.Name -join ', ')"
}

function Copy-ObjectForTest {
    param($Object)
    return ($Object | ConvertTo-Json -Depth 12 | ConvertFrom-Json)
}

function Get-PowerShellFileAst {
    param([string]$Path)
    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$parseErrors)
    $errorList = @()
    if ($null -ne $parseErrors) { $errorList = @($parseErrors) }
    return [pscustomobject]@{ Ast = $ast; Tokens = $tokens; Errors = $errorList }
}

function Invoke-SelectorFixtureAst {
    param(
        [System.Management.Automation.Language.IfStatementAst]$SelectorAst,
        [string]$Path,
        [string]$ExpectedSourceRoot
    )
    $SelectorPath = $Path
    $RunDirectory = $null
    $RunsRoot = Join-Path (Split-Path -Parent $Path) 'runs'
    $SourceRoot = $ExpectedSourceRoot
    function Assert-GpuSamePath([string]$Actual, [string]$Expected) {
        if ([string]::IsNullOrWhiteSpace($Actual) -or
            -not [string]::Equals([IO.Path]::GetFullPath($Actual),
                [IO.Path]::GetFullPath($Expected), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Recorded path differs from its derived location: $Expected"
        }
    }
    Invoke-Expression $SelectorAst.Extent.Text
    return $RunDirectory
}

function Invoke-ExpectedPathsFixtureAst {
    param(
        [System.Management.Automation.Language.FunctionDefinitionAst]$FunctionAst,
        [string]$WorkspaceRoot,
        [string]$RunDirectory
    )
    $scriptText = $FunctionAst.Extent.Text + "`nGet-GpuExpectedPaths -Directory `$RunDirectory"
    return (Invoke-Expression $scriptText)
}

function ConvertTo-QuotedProcessArgument {
    param([string]$Value)
    if ($Value -notmatch '[\s"]') { return $Value }
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Invoke-PreviewChild {
    param(
        [string]$ScriptPath,
        [string[]]$ScriptArguments,
        [hashtable]$Environment = @{}
    )
    $powerShellPath = Join-Path $PSHOME 'powershell.exe'
    $allArguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $ScriptPath) + $ScriptArguments
    $argumentLine = (($allArguments | ForEach-Object { ConvertTo-QuotedProcessArgument ([string]$_) }) -join ' ')
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $powerShellPath
    $startInfo.Arguments = $argumentLine
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true
    foreach ($name in $Environment.Keys) {
        $startInfo.EnvironmentVariables[$name] = [string]$Environment[$name]
    }
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw "Preview child timed out: $ScriptPath"
    }
    return [pscustomobject]@{ ExitCode = $process.ExitCode; StdOut = $stdout; StdErr = $stderr }
}

function Get-PowerShellCommandNames {
    param([string]$Path)
    $parsed = Get-PowerShellFileAst -Path $Path
    $names = @($parsed.Ast.FindAll({
        param($node)
        return ($node -is [System.Management.Automation.Language.CommandAst])
    }, $true) | ForEach-Object { $_.GetCommandName() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    return $names
}

function New-GpuDispatcherFixture {
    param([string]$Parent, [string]$Name)

    $fixtureRoot = Join-Path $Parent ('dispatcher-' + $Name + '-' + [Guid]::NewGuid().ToString('N'))
    $workspaceRoot = Join-Path $fixtureRoot 'source workspace'
    $fixtureToolRoot = Join-Path $workspaceRoot 'tools\gpu_diagnostic'
    New-Item -ItemType Directory -Path $fixtureToolRoot -Force | Out-Null
    foreach ($fileName in @('gpu-host-test.ps1', 'gpu-host-rollback.ps1', 'GpuHostState.psm1')) {
        Copy-Item -LiteralPath (Join-Path $ToolRoot $fileName) -Destination (Join-Path $fixtureToolRoot $fileName)
    }

    $fakePlatform = @'
function Invoke-GpuHostPlatformAction {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$Action,
        [Parameter(Mandatory=$true)][object]$State,
        [Parameter(Mandatory=$true)][string]$ActionDirectory,
        [switch]$PhysicalOutputConfirmed,
        [switch]$RecoveryReady,
        [switch]$ReviewedFirmwareDifference
    )
    $journalPath = Join-Path $State.RunDirectory 'journal.json'
    if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf)) {
        throw 'Fixture platform could not find the persisted journal.'
    }
    $diskState = [IO.File]::ReadAllText($journalPath) | ConvertFrom-Json
    if ($null -eq $diskState.InProgress -or
        $diskState.InProgress.Action -cne $State.InProgress.Action -or
        $diskState.InProgress.Directory -cne $ActionDirectory -or
        $diskState.Phase -cne $State.Phase) {
        throw 'Fixture platform observed missing or mismatched persisted intent.'
    }
    if ($Action -eq 'Arm' -and ($diskState.EverArmIntent -ne $true -or $State.EverArmIntent -ne $true)) {
        throw 'Fixture platform observed Arm without the persisted EverArmIntent latch.'
    }
    if ($Action -eq 'Prepare') {
        $selectorPath = Join-Path $State.WorkspaceRoot '.analysis\gpu-baremetal\host-prepared.json'
        if (-not (Test-Path -LiteralPath $selectorPath -PathType Leaf)) {
            throw 'Fixture Prepare reached platform before publishing the selector.'
        }
        $selector = [IO.File]::ReadAllText($selectorPath) | ConvertFrom-Json
        if ($selector.RunId -cne $State.RunId -or $selector.RunDirectory -cne $State.RunDirectory) {
            throw 'Fixture Prepare published a selector for a different run.'
        }
    }
    if ($Action -eq 'Disarm' -and $env:GPU_FIXTURE_DISARMED_BOOT_ID) {
        $State.DisarmedBootId = $env:GPU_FIXTURE_DISARMED_BOOT_ID
    }
    $line = '{0}|{1}|{2}' -f $Action, $State.InProgress.Action, $State.Phase
    [IO.File]::AppendAllText($env:GPU_FIXTURE_PLATFORM_LOG, $line + [Environment]::NewLine)
    if ($env:GPU_FIXTURE_FAIL_ACTION -and $env:GPU_FIXTURE_FAIL_ACTION -ceq $Action) {
        throw "FIXTURE_INJECTED_FAILURE:$Action"
    }
    if ($Action -eq 'FinalVerify') {
        $State.FinalVerification = [pscustomobject]@{
            Succeeded = $true
            FixtureOnly = $true
            VerifiedUtc = [DateTime]::UtcNow.ToString('o')
        }
    }
    if ($Action -eq 'Disarm') {
        $result = [ordered]@{
            Succeeded = $true
            Status = if ($env:GPU_FIXTURE_RESET_MARKER -ceq 'true') { 'DISARMED_RESET_OBSERVED' } else { 'DISARMED_RESET_REQUIRED' }
            FixtureOnly = $true
            PersistedIntent = $diskState.InProgress.Action
            ArmIntentLatched = [bool]$diskState.EverArmIntent
        }
        if ($env:GPU_FIXTURE_RESET_MARKER -ceq 'true') { $result.ResetBoundaryPassed = $true }
        elseif ($env:GPU_FIXTURE_RESET_MARKER -ceq 'false') { $result.ResetBoundaryPassed = $false }
        return [pscustomobject]$result
    }
    return [pscustomobject]@{
        Succeeded = $true
        Status = ('FIXTURE_ONLY_' + $Action)
        FixtureOnly = $true
        PersistedIntent = $diskState.InProgress.Action
        ArmIntentLatched = [bool]$diskState.EverArmIntent
    }
}

Export-ModuleMember -Function 'Invoke-GpuHostPlatformAction'
'@
    [IO.File]::WriteAllText((Join-Path $fixtureToolRoot 'GpuHostPlatform.psm1'), $fakePlatform)

    $harness = @'
param(
    [Parameter(Mandatory=$true)][string]$ToolRoot,
    [Parameter(Mandatory=$true)][string]$Action,
    [Parameter(Mandatory=$true)][string]$WorkspaceRoot,
    [string]$RecoveryDirectory,
    [string]$RunDirectory,
    [switch]$PhysicalOutputConfirmed,
    [switch]$RecoveryReady
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$global:LASTEXITCODE = 0
function global:git {
    $gitArguments = @($args | ForEach-Object { [string]$_ })
    $joined = [string]::Join(' ', $gitArguments)
    [IO.File]::AppendAllText($env:GPU_FIXTURE_GIT_LOG, $joined + [Environment]::NewLine)
    $global:LASTEXITCODE = 0
    if ($gitArguments -contains 'branch' -and $gitArguments -contains '--show-current') {
        return 'feat/smbios-boot-serial-probe'
    }
    if ($gitArguments -contains 'status' -and $gitArguments -contains '--porcelain') {
        return
    }
    if ($gitArguments -contains 'rev-parse' -and $gitArguments -contains 'HEAD') {
        return 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
    }
    throw "Git call is outside the fixture allowlist: $joined"
}
$dispatcherArguments = @{ WorkspaceRoot = $WorkspaceRoot }
if ($RunDirectory) { $dispatcherArguments.RunDirectory = $RunDirectory }
if ($Action -eq 'Prepare') {
    if (-not $RecoveryDirectory) { throw 'Fixture Prepare requires a test-owned RecoveryDirectory.' }
    $dispatcherArguments.RecoveryDirectory = $RecoveryDirectory
    $dispatcherArguments.PhysicalOutputConfirmed = [bool]$PhysicalOutputConfirmed
    $dispatcherArguments.RecoveryReady = [bool]$RecoveryReady
}
try {
    & (Join-Path $ToolRoot 'gpu-host-test.ps1') $Action @dispatcherArguments
    exit 0
} catch {
    [Console]::Error.WriteLine(('FIXTURE_DISPATCHER_ERROR: ' + $_.Exception.Message))
    exit 17
}
'@
    $harnessPath = Join-Path $fixtureRoot 'Invoke-GpuDispatcherFixture.ps1'
    [IO.File]::WriteAllText($harnessPath, $harness)

    return [pscustomobject]@{
        Root = $fixtureRoot
        WorkspaceRoot = $workspaceRoot
        ToolRoot = $fixtureToolRoot
        HarnessPath = $harnessPath
        RecoveryDirectory = (Join-Path $fixtureRoot 'fixture-recovery')
        StateRoot = (Join-Path $workspaceRoot '.analysis\gpu-baremetal')
        SelectorPath = (Join-Path $workspaceRoot '.analysis\gpu-baremetal\host-prepared.json')
        GitLog = (Join-Path $fixtureRoot 'fixture-git.log')
        PlatformLog = (Join-Path $fixtureRoot 'fixture-platform.log')
    }
}

function Invoke-GpuFixtureAction {
    param(
        [object]$Fixture,
        [string]$Action,
        [string]$FailPlatformAction,
        [string]$RunDirectory,
        [hashtable]$AdditionalEnvironment = @{}
    )
    $arguments = @('-ToolRoot', $Fixture.ToolRoot, '-Action', $Action, '-WorkspaceRoot', $Fixture.WorkspaceRoot)
    if ($RunDirectory) { $arguments += @('-RunDirectory', $RunDirectory) }
    if ($Action -eq 'Prepare') {
        $arguments += @('-RecoveryDirectory', $Fixture.RecoveryDirectory, '-PhysicalOutputConfirmed', '-RecoveryReady')
    }
    $environment = @{
        GPU_FIXTURE_GIT_LOG = $Fixture.GitLog
        GPU_FIXTURE_PLATFORM_LOG = $Fixture.PlatformLog
        GPU_FIXTURE_FAIL_ACTION = $FailPlatformAction
    }
    foreach ($name in $AdditionalEnvironment.Keys) {
        $environment[$name] = $AdditionalEnvironment[$name]
    }
    return Invoke-PreviewChild -ScriptPath $Fixture.HarnessPath -ScriptArguments $arguments -Environment $environment
}

function Get-GpuFixtureJournal {
    param([object]$Fixture, [string]$RunDirectory)
    if (-not $RunDirectory) {
        $selector = Read-GpuJson -Path $Fixture.SelectorPath
        $RunDirectory = $selector.RunDirectory
    }
    return Read-GpuJson -Path (Join-Path $RunDirectory 'journal.json')
}

function Get-GpuFixturePlatformCalls {
    param([object]$Fixture)
    if (-not (Test-Path -LiteralPath $Fixture.PlatformLog -PathType Leaf)) { return @() }
    return @(Get-Content -LiteralPath $Fixture.PlatformLog)
}

function Assert-GpuFixtureActionSucceeded {
    param([object]$Fixture, [string]$Action)
    $result = Invoke-GpuFixtureAction -Fixture $Fixture -Action $Action
    Assert-Equal 0 $result.ExitCode "$Action fixture action failed: $($result.StdErr)"
    $journal = Get-GpuFixtureJournal -Fixture $Fixture
    Assert-True ($null -eq $journal.InProgress) "$Action returned with an in-progress action still set."
    return $journal
}

$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
$scratchParent = Join-Path $workspaceRoot '.codex-build\gpu-host-tests'
if (-not (Test-Path -LiteralPath $scratchParent -PathType Container)) {
    New-Item -ItemType Directory -Path $scratchParent -Force | Out-Null
}
$scratchParentFull = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $scratchParent).Path)
$expectedScratchPrefix = [IO.Path]::GetFullPath((Join-Path $workspaceRoot '.codex-build'))
if (-not $scratchParentFull.StartsWith($expectedScratchPrefix.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Scratch parent escaped the workspace .codex-build directory: $scratchParentFull"
}
if ((Get-Item -LiteralPath $scratchParentFull -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "Refusing to use a reparse-point scratch parent: $scratchParentFull"
}
$runRoot = Join-Path $scratchParentFull ('run-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$runRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $runRoot).Path)
$restrictedRoot = Join-Path $runRoot 'restricted'
$outsideSibling = Join-Path $runRoot 'restricted-sibling'
$junctionPath = Join-Path $restrictedRoot 'escape-link'
New-Item -ItemType Directory -Path $restrictedRoot | Out-Null
New-Item -ItemType Directory -Path $outsideSibling | Out-Null
$fixtureFile = Join-Path $restrictedRoot 'payload.bin'
[IO.File]::WriteAllBytes($fixtureFile, [byte[]](1, 2, 3, 4, 5, 6, 7, 8))
$fixtureJson = Join-Path $restrictedRoot 'state.json'
$fixtureWorkspace = Join-Path $runRoot 'synthetic workspace'
$fixtureStateRoot = Join-Path $fixtureWorkspace '.analysis\gpu-baremetal'
$fixtureRunsRoot = Join-Path $fixtureStateRoot 'runs'
$fixtureRunId = 'gpu-20261019T120000Z-0123456789abcdef0123456789abcdef'
$fixtureRunDirectory = Join-Path $fixtureRunsRoot $fixtureRunId
$fixtureJournalPath = Join-Path $fixtureRunDirectory 'journal.json'
$fixtureSelectorPath = Join-Path $fixtureStateRoot 'host-prepared.json'
$dispatcherPath = Join-Path $ToolRoot 'gpu-host-test.ps1'
$rollbackPath = Join-Path $ToolRoot 'gpu-host-rollback.ps1'

try {
    Invoke-TestCase 'Path containment accepts an existing child' {
        $null = Assert-GpuPathWithin -Path $fixtureFile -Root $restrictedRoot -MustExist
        Assert-True (Test-Path -LiteralPath $fixtureFile -PathType Leaf) 'Accepted child disappeared.'
    }

    Invoke-TestCase 'Path containment rejects a sibling that shares the root prefix' {
        $sibling = Join-Path $outsideSibling 'payload.bin'
        [IO.File]::WriteAllText($sibling, 'fixture')
        Assert-Throws { Assert-GpuPathWithin -Path $sibling -Root $restrictedRoot } 'A root-prefix sibling was accepted.'
    }

    Invoke-TestCase 'Path containment resolves parent traversal before accepting' {
        $traversal = Join-Path $restrictedRoot '..\restricted-sibling\payload.bin'
        Assert-Throws { Assert-GpuPathWithin -Path $traversal -Root $restrictedRoot } 'Parent traversal escaped the root.'
    }

    Invoke-TestCase 'Path containment requires explicit permission for the root itself' {
        Assert-Throws { Assert-GpuPathWithin -Path $restrictedRoot -Root $restrictedRoot } 'The root itself was accepted without AllowRoot.'
        $null = Assert-GpuPathWithin -Path $restrictedRoot -Root $restrictedRoot -AllowRoot -MustExist
    }

    Invoke-TestCase 'Path containment rejects missing paths when MustExist is set' {
        $missing = Join-Path $restrictedRoot 'missing.bin'
        Assert-Throws { Assert-GpuPathWithin -Path $missing -Root $restrictedRoot -MustExist } 'A missing path passed MustExist.'
    }

    $junctionCreated = $false
    try {
        New-Item -ItemType Junction -Path $junctionPath -Target $outsideSibling -ErrorAction Stop | Out-Null
        $junctionCreated = $true
    } catch {
        $script:SkippedCases.Add("Symlink/junction escape: junction creation unavailable ($($_.Exception.Message))")
    }
    if ($junctionCreated) {
        Invoke-TestCase 'Path containment rejects a junction escape' {
            $junctionFile = Join-Path $junctionPath 'payload.bin'
            [IO.File]::WriteAllText($junctionFile, 'fixture')
            Assert-Throws { Assert-GpuPathWithin -Path $junctionFile -Root $restrictedRoot -MustExist } 'A junction escape was accepted.'
        }
    }

    Invoke-TestCase 'JSON write/read round trip returns the complete object' {
        $value = [ordered]@{ Version = 1; Name = 'fixture'; Items = @('a', 'b', 'c') }
        Write-GpuJson -Path $fixtureJson -Value $value -NoOverwrite
        $read = Read-GpuJson -Path $fixtureJson
        Assert-Equal 1 $read.Version 'JSON scalar did not round trip.'
        Assert-Equal 'fixture' $read.Name 'JSON string did not round trip.'
        Assert-Equal 3 @($read.Items).Count 'JSON array did not round trip.'
    }

    Invoke-TestCase 'JSON NoOverwrite preserves the original file' {
        $before = [IO.File]::ReadAllText($fixtureJson)
        Assert-Throws { Write-GpuJson -Path $fixtureJson -Value @{ Version = 2 } -NoOverwrite } 'NoOverwrite allowed a replacement.'
        Assert-Equal $before ([IO.File]::ReadAllText($fixtureJson)) 'NoOverwrite changed the original file.'
    }

    Invoke-TestCase 'JSON replacement is readable as one complete update' {
        $largeText = 'z' * 65536
        Write-GpuJson -Path $fixtureJson -Value @{ Version = 2; Text = $largeText }
        $read = Read-GpuJson -Path $fixtureJson
        Assert-Equal 2 $read.Version 'Replacement version is wrong.'
        Assert-Equal 65536 ([string]$read.Text).Length 'Replacement was incomplete.'
        Assert-Equal $largeText ([string]$read.Text) 'Replacement content differs.'
    }

    Invoke-TestCase 'Malformed JSON is rejected' {
        $badJson = Join-Path $restrictedRoot 'malformed.json'
        [IO.File]::WriteAllText($badJson, '{"partial":')
        Assert-Throws { Read-GpuJson -Path $badJson } 'Malformed JSON was accepted.'
    }

    Invoke-TestCase 'Failed JSON update leaves old JSON intact and cleans temporary siblings' {
        $lockedJson = Join-Path $restrictedRoot 'locked.json'
        Write-GpuJson -Path $lockedJson -Value @{ Version = 1 } -NoOverwrite
        $before = [IO.File]::ReadAllText($lockedJson)
        $beforeNames = @(Get-ChildItem -LiteralPath $restrictedRoot -Force | ForEach-Object Name | Sort-Object)
        $lock = [IO.File]::Open($lockedJson, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        try {
            Assert-Throws { Write-GpuJson -Path $lockedJson -Value @{ Version = 2 } } 'Write to an exclusively locked target unexpectedly succeeded.'
        } finally {
            $lock.Dispose()
        }
        Assert-Equal $before ([IO.File]::ReadAllText($lockedJson)) 'Failed update damaged the previous JSON.'
        $afterNames = @(Get-ChildItem -LiteralPath $restrictedRoot -Force | ForEach-Object Name | Sort-Object)
        Assert-Equal ($beforeNames -join '|') ($afterNames -join '|') 'Failed update left temporary siblings behind.'
    }

    $pin = Get-GpuFilePin -Path $fixtureFile
    Invoke-TestCase 'File pin accepts the unchanged file' {
        Assert-GpuFilePin -Pin $pin
    }

    Invoke-TestCase 'File pin detects same-size content drift' {
        $original = [IO.File]::ReadAllBytes($fixtureFile)
        try {
            $changed = [byte[]]$original.Clone()
            $changed[0] = [byte]($changed[0] -bxor 255)
            [IO.File]::WriteAllBytes($fixtureFile, $changed)
            Assert-Throws { Assert-GpuFilePin -Pin $pin } 'A same-size hash drift was accepted.'
        } finally {
            [IO.File]::WriteAllBytes($fixtureFile, $original)
        }
        Assert-GpuFilePin -Pin $pin
    }

    Invoke-TestCase 'File pin detects size drift' {
        $original = [IO.File]::ReadAllBytes($fixtureFile)
        try {
            [IO.File]::WriteAllBytes($fixtureFile, [byte[]]($original + [byte[]](9)))
            Assert-Throws { Assert-GpuFilePin -Pin $pin } 'A size drift was accepted.'
        } finally {
            [IO.File]::WriteAllBytes($fixtureFile, $original)
        }
    }

    Invoke-TestCase 'File pin rejects an invalid stored SHA-256' {
        $hashField = Get-ObjectPropertyName -Object $pin -Candidates @('Sha256', 'SHA256', 'Sha256Hex', 'Hash') -Description 'SHA-256 pin'
        $invalidPin = Copy-ObjectForTest $pin
        $invalidPin.$hashField = 'xyz'
        Assert-Throws { Assert-GpuFilePin -Pin $invalidPin } 'An invalid SHA-256 pin was accepted.'
    }

    Invoke-TestCase 'ESP identity accepts matching volume GUIDs' {
        $expectedIdentity = '\\?\Volume{01234567-89ab-cdef-0123-456789abcdef}\'
        Assert-GpuEspIdentity -Expected $expectedIdentity -Actual $expectedIdentity
    }

    Invoke-TestCase 'ESP identity rejects a different volume GUID' {
        $expectedIdentity = '\\?\Volume{01234567-89ab-cdef-0123-456789abcdef}\'
        $otherIdentity = '\\?\Volume{fedcba98-7654-3210-fedc-ba9876543210}\'
        Assert-Throws { Assert-GpuEspIdentity -Expected $expectedIdentity -Actual $otherIdentity } 'A different ESP identity was accepted.'
    }

    Invoke-TestCase 'Health verdict accepts the complete expected CPU set and aggregate PASS' {
        $lines = @(
            'UEFI_HYPERVISOR_WINDOWS_CPU:0:0:PASS',
            'UEFI_HYPERVISOR_WINDOWS_CPU:0:1:PASS',
            'UEFI_HYPERVISOR_WINDOWS_CPU:0:2:PASS',
            'UEFI_HYPERVISOR_WINDOWS_CPU:0:3:PASS',
            'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS'
        )
        $verdict = Get-GpuHealthVerdict -Lines $lines -ExpectedCpus 4 -ExpectedResult PASS
        Assert-True ([bool]$verdict.Passed) 'A complete PASS report was rejected.'
        Assert-Equal 4 $verdict.CpuCount 'Wrong number of CPUs was counted.'
        Assert-Equal 4 $verdict.CpuPassCount 'Wrong number of passing CPUs was counted.'
        Assert-Equal 'PASS' $verdict.Aggregate 'Wrong aggregate was reported.'
    }

    Invoke-TestCase 'Health verdict accepts the explicit ABSENT result only when requested' {
        $lines = @(
            'UEFI_HYPERVISOR_WINDOWS_CPU:0:0:ABSENT',
            'UEFI_HYPERVISOR_WINDOWS_CPU:0:1:ABSENT',
            'UEFI_HYPERVISOR_WINDOWS_CPUID:ABSENT'
        )
        $verdict = Get-GpuHealthVerdict -Lines $lines -ExpectedCpus 2 -ExpectedResult ABSENT
        Assert-True ([bool]$verdict.Passed) 'An expected ABSENT report was rejected.'
        Assert-Equal 2 $verdict.CpuCount 'ABSENT report counted the wrong CPUs.'
        Assert-Equal 'ABSENT' $verdict.Aggregate 'ABSENT aggregate was lost.'
        $unexpected = Get-GpuHealthVerdict -Lines $lines -ExpectedCpus 2 -ExpectedResult PASS
        Assert-True (-not [bool]$unexpected.Passed) 'ABSENT was accepted when PASS was required.'
    }

    Invoke-TestCase 'Health verdict rejects missing, failed, and mixed CPU reports' {
        $missing = Get-GpuHealthVerdict -Lines @('UEFI_HYPERVISOR_WINDOWS_CPU:0:0:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS') -ExpectedCpus 2 -ExpectedResult PASS
        Assert-True (-not [bool]$missing.Passed) 'A missing CPU was accepted.'
        $failed = Get-GpuHealthVerdict -Lines @('UEFI_HYPERVISOR_WINDOWS_CPU:0:0:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPU:0:1:FAIL', 'UEFI_HYPERVISOR_WINDOWS_CPUID:FAIL') -ExpectedCpus 2 -ExpectedResult PASS
        Assert-True (-not [bool]$failed.Passed) 'A failed CPU was accepted.'
        $mixed = Get-GpuHealthVerdict -Lines @('UEFI_HYPERVISOR_WINDOWS_CPU:0:0:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPU:0:1:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPUID:FAIL') -ExpectedCpus 2 -ExpectedResult PASS
        Assert-True (-not [bool]$mixed.Passed) 'A PASS CPU set with a failing aggregate was accepted.'
    }

    Invoke-TestCase 'Health verdict rejects duplicate and malformed CPU records' {
        $duplicate = Get-GpuHealthVerdict -Lines @('UEFI_HYPERVISOR_WINDOWS_CPU:0:0:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPU:0:0:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS') -ExpectedCpus 1 -ExpectedResult PASS
        Assert-True (-not [bool]$duplicate.Passed) 'A duplicate CPU record was accepted.'
        $malformed = Get-GpuHealthVerdict -Lines @('UEFI_HYPERVISOR_WINDOWS_CPU:x:0:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPU:0:1:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS') -ExpectedCpus 2 -ExpectedResult PASS
        Assert-True (-not [bool]$malformed.Passed) 'A malformed CPU record was accepted.'
    }

    Invoke-TestCase 'Health verdict rejects a missing or duplicate aggregate' {
        $missing = Get-GpuHealthVerdict -Lines @('UEFI_HYPERVISOR_WINDOWS_CPU:0:0:PASS') -ExpectedCpus 1 -ExpectedResult PASS
        Assert-True (-not [bool]$missing.Passed) 'A report without aggregate was accepted.'
        $duplicate = Get-GpuHealthVerdict -Lines @('UEFI_HYPERVISOR_WINDOWS_CPU:0:0:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS', 'UEFI_HYPERVISOR_WINDOWS_CPUID:PASS') -ExpectedCpus 1 -ExpectedResult PASS
        Assert-True (-not [bool]$duplicate.Passed) 'A duplicate aggregate was accepted.'
    }

    $actions = @('Build', 'Sign', 'Backup', 'Stage', 'Arm', 'Collect', 'FinalVerify')
    $phases = @('Preparing', 'Prepared', 'Built', 'Signed', 'BackedUp', 'Staged', 'Armed', 'Collected', 'Disarmed', 'Complete')
    $expectedPrerequisites = @{
        Build = 'Prepared'; Sign = 'Built'; Backup = 'Signed'; Stage = 'BackedUp'; Arm = 'Staged'; Collect = 'Armed'; FinalVerify = 'Disarmed'
    }
    Invoke-TestCase 'Action state machine accepts each forward action at its prerequisite phase' {
        foreach ($action in $actions) {
            Assert-GpuActionAllowed -Action $action -Phase $expectedPrerequisites[$action]
        }
    }

    Invoke-TestCase 'Action state machine rejects each forward action at other known phases' {
        foreach ($action in $actions) {
            foreach ($phase in $phases) {
                if ($phase -ne $expectedPrerequisites[$action]) {
                    Assert-Throws { Assert-GpuActionAllowed -Action $action -Phase $phase } "$action was allowed from $phase."
                }
            }
        }
    }

    Invoke-TestCase 'Action state machine allows recovery/status actions from any known phase' {
        foreach ($action in @('Status', 'Disarm', 'Rollback')) {
            foreach ($phase in $phases) {
                Assert-GpuActionAllowed -Action $action -Phase $phase
            }
        }
    }

    Invoke-TestCase 'Action state machine blocks forward actions while an operation is in progress' {
        foreach ($action in $actions) {
            Assert-Throws { Assert-GpuActionAllowed -Action $action -Phase $expectedPrerequisites[$action] -InProgress 'Build' } "$action was allowed while in progress."
        }
        foreach ($action in @('Status', 'Disarm', 'Rollback')) {
            Assert-GpuActionAllowed -Action $action -Phase Armed -InProgress 'Build'
        }
    }

    Invoke-TestCase 'FinalVerify retry helper admits only a disarmed interrupted verification or cleanup' {
        foreach ($interruptedAction in @('FinalVerify', 'Disarm', 'Rollback')) {
            Assert-True (Test-GpuFinalVerifyRetry -Action FinalVerify -Phase Disarmed -InProgress $interruptedAction) "String in-progress action '$interruptedAction' was not eligible for the read-only retry."
            Assert-True (Test-GpuFinalVerifyRetry -Action FinalVerify -Phase Disarmed -InProgress ([pscustomobject]@{ Action=$interruptedAction })) "Object in-progress action '$interruptedAction' was not eligible for the read-only retry."
            Assert-GpuActionAllowed -Action FinalVerify -Phase Disarmed -InProgress ([pscustomobject]@{ Action=$interruptedAction })
        }
        foreach ($interruptedAction in @($null, '', 'Build', 'Arm', 'Stage', 'Collect', 'Unknown')) {
            Assert-True (-not (Test-GpuFinalVerifyRetry -Action FinalVerify -Phase Disarmed -InProgress $interruptedAction)) "In-progress action '$interruptedAction' was incorrectly eligible for retry."
        }
        foreach ($action in @('Build', 'Sign', 'Backup', 'Stage', 'Arm', 'Collect', 'Rollback', 'Archive')) {
            Assert-True (-not (Test-GpuFinalVerifyRetry -Action $action -Phase Disarmed -InProgress 'Rollback')) "Action '$action' was incorrectly eligible for FinalVerify retry."
        }
        foreach ($phase in @('Preparing', 'Prepared', 'Built', 'Signed', 'BackedUp', 'Staged', 'Armed', 'Collected', 'Complete')) {
            Assert-True (-not (Test-GpuFinalVerifyRetry -Action FinalVerify -Phase $phase -InProgress 'Rollback')) "Phase '$phase' was incorrectly eligible for FinalVerify retry."
            Assert-Throws { Assert-GpuActionAllowed -Action FinalVerify -Phase $phase -InProgress 'Rollback' } "FinalVerify retry was allowed from $phase."
        }
        Assert-True (-not (Test-GpuFinalVerifyRetry -Action FinalVerify -Phase Disarmed -InProgress $null)) 'A retry without interrupted evidence was incorrectly identified as a retry.'
    }

    Invoke-TestCase 'Action state machine rejects unknown phase and action values' {
        Assert-Throws { Assert-GpuActionAllowed -Action Build -Phase Unknown } 'An unknown phase was accepted.'
        Assert-Throws { Assert-GpuActionAllowed -Action Unknown -Phase Prepared } 'An unknown action was accepted.'
    }

    Invoke-TestCase 'Dispatcher and rollback wrapper exist and parse without PowerShell errors' {
        foreach ($path in @($dispatcherPath, $rollbackPath)) {
            Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Required host script is missing: $path"
            $parsed = Get-PowerShellFileAst -Path $path
            Assert-Equal 0 $parsed.Errors.Count "PowerShell parser reported errors for $path."
        }
    }

    Invoke-TestCase 'Dispatcher exposes positional action and supports WhatIf' {
        $dispatcherText = [IO.File]::ReadAllText($dispatcherPath)
        Assert-True ($dispatcherText -match '(?is)^\s*\[CmdletBinding\([^\r\n]*SupportsShouldProcess\s*=\s*\$true') 'Dispatcher does not enable ShouldProcess/WhatIf.'
        Assert-True ($dispatcherText -match '(?is)\[Parameter\(Position\s*=\s*0\s*,\s*Mandatory\s*=\s*\$true\)\]\s*\[ValidateSet\(') 'Action is not the first positional mandatory parameter.'
    }

    Invoke-TestCase 'Rollback wrapper delegates Rollback and forwards WhatIf' {
        $rollbackText = [IO.File]::ReadAllText($rollbackPath)
        Assert-True ($rollbackText -match '(?is)SupportsShouldProcess\s*=\s*\$true') 'Rollback wrapper does not enable WhatIf.'
        Assert-True ($rollbackText -match '(?is)Action\s*=\s*[\x27\x22]Rollback[\x27\x22]') 'Rollback wrapper does not select the Rollback action.'
        Assert-True ($rollbackText -match '(?is)WhatIf\s*=\s*\[bool\]\s*\$WhatIfPreference') 'Rollback wrapper does not forward WhatIf.'
        Assert-True ($rollbackText -match '(?is)&\s*\(Join-Path\s+\$PSScriptRoot\s+[\x27\x22]gpu-host-test\.ps1') 'Rollback wrapper does not invoke the dispatcher.'
    }

    Invoke-TestCase 'Dispatcher WhatIf exits before workspace or host preflight' {
        $dispatcherText = [IO.File]::ReadAllText($dispatcherPath)
        $previewAt = $dispatcherText.IndexOf('if ($WhatIfPreference)', [StringComparison]::OrdinalIgnoreCase)
        $sourceRootAt = $dispatcherText.IndexOf('$SourceRoot =', [StringComparison]::OrdinalIgnoreCase)
        $moduleAt = $dispatcherText.IndexOf('Import-Module', [StringComparison]::OrdinalIgnoreCase)
        $shouldProcessAt = $dispatcherText.IndexOf('$PSCmdlet.ShouldProcess', [StringComparison]::OrdinalIgnoreCase)
        Assert-True ($previewAt -ge 0 -and $sourceRootAt -gt $previewAt -and $moduleAt -gt $previewAt -and $shouldProcessAt -gt $previewAt) 'WhatIf guard is not before root resolution, module import, and ShouldProcess.'
        $previewTail = $dispatcherText.Substring($previewAt, $sourceRootAt - $previewAt)
        Assert-True ($previewTail -match '(?is)ConvertTo-Json.{0,1000}return') 'Early WhatIf block does not return after emitting its preview.'
        Assert-True ($previewTail -notmatch '(?i)(Import-Module|Get-Gpu|New-Item|Invoke-GpuHostPlatformAction|Get-CimInstance|Get-Disk|git\s+-C|Mount-Volume)') 'Early WhatIf block contains a host probe, write, or platform call.'
    }

    Invoke-TestCase 'Dispatcher resolves the exact saved selector, never a timestamp-selected run' {
        $parsed = Get-PowerShellFileAst -Path $dispatcherPath
        $dispatcherText = [IO.File]::ReadAllText($dispatcherPath)
        $selectorNodes = @($parsed.Ast.FindAll({
            param($node)
            if ($node -isnot [System.Management.Automation.Language.IfStatementAst] -or $node.Clauses.Count -eq 0) { return $false }
            return ($node.Clauses[0].Item1.Extent.Text -match '(?is)not\s+\$RunDirectory' -and
                $node.Extent.Text -match '(?is)Read-GpuJson\s+-Path\s+\$SelectorPath')
        }, $true))
        Assert-Equal 1 $selectorNodes.Count 'Expected one explicit no-RunDirectory selector block.'
        $selectorText = $selectorNodes[0].Extent.Text
        Assert-True ($selectorText -match '(?is)Read-GpuJson\s+-Path\s+\$SelectorPath') 'Saved selector is not read from its exact path.'
        Assert-True ($selectorText -match '(?is)\$RunDirectory\s*=\s*\$Selector\.RunDirectory') 'Saved selector path is not used as the exact run location.'
        Assert-True ($selectorText -match '(?i)never the newest directory') 'Missing-selector failure does not explicitly reject newest-directory guessing.'
        Assert-True ($dispatcherText -notmatch '(?i)(LastWriteTime|CreationTime|Sort-Object.{0,100}(Newest|LastWriteTime)|Select-Object.{0,100}-First\s+1)') 'Dispatcher contains timestamp/newest-run selection logic.'

        New-Item -ItemType Directory -Path $fixtureRunDirectory -Force | Out-Null
        $syntheticJournal = [ordered]@{
            SchemaVersion = 1; RunId = $fixtureRunId; WorkspaceRoot = $fixtureWorkspace
            SourceRoot = $fixtureWorkspace; SourceCommit = ('a' * 40); RunDirectory = $fixtureRunDirectory
            RecoveryBase = (Join-Path $runRoot 'recovery'); RecoveryRunDirectory = (Join-Path (Join-Path $runRoot 'recovery') $fixtureRunId)
            Phase = 'Armed'; InProgress = $null; SourcePins = @(); Host = $null; Paths = @{}
            Tools = $null; BuildArtifacts = $null; SignedArtifacts = $null; Backup = $null
            Stage = $null; ArmBootId = $null; EverArmIntent = $true; DisarmedBootId = $null
            Collection = $null; FinalVerification = $null; LastFailure = $null
        }
        Write-GpuJson -Path $fixtureJournalPath -Value $syntheticJournal -NoOverwrite
        Write-GpuJson -Path $fixtureSelectorPath -NoOverwrite -Value ([ordered]@{
            SchemaVersion = 1; RunId = $fixtureRunId; RunDirectory = $fixtureRunDirectory
            JournalPath = $fixtureJournalPath; SourceRoot = $fixtureWorkspace
        })
        $selectedRun = Invoke-SelectorFixtureAst -SelectorAst $selectorNodes[0] -Path $fixtureSelectorPath -ExpectedSourceRoot $fixtureWorkspace
        Assert-Equal $fixtureRunDirectory $selectedRun 'Dispatcher did not select the saved run ID.'
        Assert-Throws { Invoke-SelectorFixtureAst -SelectorAst $selectorNodes[0] -Path (Join-Path $fixtureStateRoot 'missing-selector.json') -ExpectedSourceRoot $fixtureWorkspace } 'Dispatcher guessed a run when the selector was absent.'
    }

    Invoke-TestCase 'Build paths use the bounded short root, outside the long run directory' {
        $parsed = Get-PowerShellFileAst -Path $dispatcherPath
        $pathFunction = $parsed.Ast.Find({
            param($node)
            return ($node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-GpuExpectedPaths')
        }, $true)
        Assert-True ($null -ne $pathFunction) 'Pure expected-path function was not found for fixture testing.'
        New-Item -ItemType Directory -Path $fixtureRunDirectory -Force | Out-Null
        $fixtureBuildRoot = Join-Path $fixtureWorkspace '.codex-build\gb'
        New-Item -ItemType Directory -Path $fixtureBuildRoot -Force | Out-Null

        $derived = Invoke-ExpectedPathsFixtureAst -FunctionAst $pathFunction -WorkspaceRoot $fixtureWorkspace -RunDirectory $fixtureRunDirectory
        $shortRunId = $fixtureRunId.Substring($fixtureRunId.Length - 16)
        $shortBuildRoot = Join-Path $fixtureBuildRoot $shortRunId
        Assert-Equal (Join-Path $shortBuildRoot 'h') $derived.HypervisorBuild 'Hypervisor build path did not use the bounded short root.'
        Assert-Equal (Join-Path $shortBuildRoot 'l') $derived.LoaderBuild 'Loader build path did not use the bounded short root.'
        Assert-Equal (Join-Path $fixtureRunDirectory 'signed') $derived.SignedDirectory 'Signed artifacts no longer remain in the run directory.'
        $null = Assert-GpuPathWithin -Path $derived.HypervisorBuild -Root $fixtureBuildRoot
        $null = Assert-GpuPathWithin -Path $derived.LoaderBuild -Root $fixtureBuildRoot
        $null = Assert-GpuPathWithin -Path $derived.SignedDirectory -Root $fixtureRunDirectory
        Assert-Throws { Assert-GpuPathWithin -Path $derived.HypervisorBuild -Root $fixtureRunDirectory } 'Hypervisor build path remained nested under the long run directory.'
        Assert-Throws { Assert-GpuPathWithin -Path $derived.LoaderBuild -Root $fixtureRunDirectory } 'Loader build path remained nested under the long run directory.'
    }

    Invoke-TestCase 'Recorded status reports journal facts without live ownership claims' {
        $parsed = Get-PowerShellFileAst -Path $dispatcherPath
        $statusNode = $parsed.Ast.Find({
            param($node)
            return ($node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-GpuStatus')
        }, $true)
        Assert-True ($null -ne $statusNode) 'Pure Get-GpuStatus function was not found for fixture testing.'
        $journal = Read-GpuJson -Path $fixtureJournalPath
        $invokeStatus = [scriptblock]::Create($statusNode.Extent.Text + "`nGet-GpuStatus `$args[0]")
        $status = & $invokeStatus $journal
        Assert-Equal 'RECORDED_STATE_ONLY' $status.Status 'Recorded status was labeled as live attestation.'
        Assert-Equal $fixtureRunDirectory $status.RunDirectory 'Status returned the wrong fixture run.'
        Assert-True (-not [bool]$status.BootNextLiveAttested) 'Status falsely attested live BootNext.'
        Assert-True (-not [bool]$status.NativeGpuIsolationAttested) 'Status falsely attested native GPU isolation.'
    }

    Invoke-TestCase 'Intent and Arm latch persist before platform call; phases gate dispatch' {
        $dispatcherText = [IO.File]::ReadAllText($dispatcherPath)
        $phaseGate = $dispatcherText.IndexOf('Assert-GpuActionAllowed -Action $Action -Phase $State.Phase', [StringComparison]::Ordinal)
        $actionDirectoryAt = $dispatcherText.LastIndexOf('$ActionDirectory = Join-Path', [StringComparison]::Ordinal)
        $intentAt = $dispatcherText.IndexOf('$State.InProgress =', [StringComparison]::Ordinal)
        $armLatchAt = $dispatcherText.IndexOf('$State.EverArmIntent = $true', [StringComparison]::Ordinal)
        $saveAfterIntentAt = $dispatcherText.IndexOf('Save-GpuJournal $State', $intentAt, [StringComparison]::Ordinal)
        $platformAt = $dispatcherText.IndexOf('Invoke-GpuHostPlatformAction -Action $PlatformAction', [StringComparison]::Ordinal)
        Assert-True ($phaseGate -ge 0 -and $actionDirectoryAt -gt $phaseGate) 'Action/phase gate is missing or occurs after action-directory creation.'
        Assert-True ($intentAt -gt $actionDirectoryAt -and $saveAfterIntentAt -gt $intentAt -and $platformAt -gt $saveAfterIntentAt) 'In-progress intent is not saved before the platform call.'
        Assert-True ($armLatchAt -gt $intentAt -and $armLatchAt -lt $saveAfterIntentAt) 'Arm ownership latch is not persisted with the initial intent.'
        Assert-True ($dispatcherText -match '(?is)\$State\.InProgress\s*-and\s*\$Action\s*-notin\s*@\(\x27Status\x27,\x27Disarm\x27,\x27Rollback\x27\)') 'Interrupted-action guard does not allow recovery/status actions.'
        Assert-True ($dispatcherText -match '(?is)Test-GpuFinalVerifyRetry\s+-Action\s+\$Action\s+-Phase\s+\$State\.Phase\s+-InProgress\s+\$State\.InProgress') 'Interrupted-action guard does not use the narrow FinalVerify retry predicate.'
    }

    Invoke-TestCase 'Failure handling preserves journal evidence and keeps recovery actions reachable' {
        $dispatcherText = [IO.File]::ReadAllText($dispatcherPath)
        Assert-True ($dispatcherText -match '(?is)catch\s*\{.{0,500}\$State\.LastFailure\s*=\s*\$Failure.{0,500}Save-GpuJournal\s+\$State') 'Failure path does not persist the failure to the journal.'
        Assert-True ($dispatcherText -match '(?is)failure\.json.{0,300}InProgress\s*=\s*\$State\.InProgress') 'Failure path does not preserve in-progress evidence.'
        Assert-True ($dispatcherText -match '(?is)Status\s+or\s+rollback') 'Failure guidance does not direct the operator to recovery.'
        Assert-True ($dispatcherText -match '(?is)if\s*\(\$Action\s*-eq\s*\x27Rollback\x27\)\s*\{\s*\$PlatformAction\s*=\s*\x27Disarm\x27') 'Rollback does not delegate to the disarm recovery operation.'
        Assert-True ($dispatcherText -match '(?is)finally\s*\{\s*\$Lock\.Dispose\(\)') 'Action lock is not released on failure.'
    }

    Invoke-TestCase 'Dispatcher and rollback wrapper contain no automatic reboot command' {
        foreach ($path in @($dispatcherPath, $rollbackPath)) {
            $sourceText = [IO.File]::ReadAllText($path)
            Assert-True ($sourceText -notmatch '(?im)^\s*(?:&\s*)?(?:Restart-Computer|shutdown(?:\.exe)?|bcdedit(?:\.exe)?\s+[^\r\n]*(?:/reboot|/bootsequence))\b') "Automatic reboot command found in $path."
        }
    }

    Invoke-TestCase 'Dispatcher and rollback WhatIf children exit cleanly without creating isolated roots' {
        $whatIfWorkspace = Join-Path $runRoot 'WhatIf workspace must stay absent'
        $whatIfRunDirectory = Join-Path $whatIfWorkspace 'run-must-not-be-created'
        $whatIfRecovery = Join-Path $runRoot 'WhatIf recovery must stay absent'
        Assert-True (-not (Test-Path -LiteralPath $whatIfWorkspace)) 'WhatIf workspace fixture unexpectedly exists before child test.'
        $dispatcherResult = Invoke-PreviewChild -ScriptPath $dispatcherPath -ScriptArguments @(
            'Arm', '-WorkspaceRoot', $whatIfWorkspace, '-RunDirectory', $whatIfRunDirectory,
            '-RecoveryDirectory', $whatIfRecovery, '-WhatIf'
        )
        Assert-Equal 0 $dispatcherResult.ExitCode "Dispatcher WhatIf child failed: $($dispatcherResult.StdErr)"
        Assert-True ($dispatcherResult.StdOut -match '"Preview"\s*:\s*true') 'Dispatcher WhatIf did not emit a preview.'
        Assert-True (-not (Test-Path -LiteralPath $whatIfWorkspace)) 'Dispatcher WhatIf created its nonexistent workspace.'
        Assert-True (-not (Test-Path -LiteralPath $whatIfRecovery)) 'Dispatcher WhatIf created its recovery directory.'

        $rollbackResult = Invoke-PreviewChild -ScriptPath $rollbackPath -ScriptArguments @(
            '-WorkspaceRoot', $whatIfWorkspace, '-RunDirectory', $whatIfRunDirectory, '-WhatIf'
        )
        Assert-Equal 0 $rollbackResult.ExitCode "Rollback WhatIf child failed: $($rollbackResult.StdErr)"
        Assert-True ($rollbackResult.StdOut -match '"Action"\s*:\s*"Rollback"') 'Rollback WhatIf did not forward the Rollback action.'
        Assert-True ($rollbackResult.StdOut -match '"Preview"\s*:\s*true') 'Rollback WhatIf did not reach the dispatcher preview.'
        Assert-True (-not (Test-Path -LiteralPath $whatIfWorkspace)) 'Rollback WhatIf created its nonexistent workspace.'
    }

    Invoke-TestCase 'Reviewed firmware flag previews FinalVerify and rejects other actions without effects' {
        $absentRoot = Join-Path $runRoot 'review-preview-root-must-stay-absent'
        $preview = Invoke-PreviewChild -ScriptPath $dispatcherPath -ScriptArguments @(
            'FinalVerify', '-ReviewedFirmwareDifference', '-WorkspaceRoot', $absentRoot, '-WhatIf'
        )
        Assert-Equal 0 $preview.ExitCode "Reviewed FinalVerify preview failed: $($preview.StdErr)"
        Assert-True ($preview.StdOut -match '"ReviewedFirmwareDifferenceRequested"\s*:\s*true') 'Preview omitted the explicit review request.'
        Assert-True (-not (Test-Path -LiteralPath $absentRoot)) 'Reviewed preview created its workspace.'
        $rejected = Invoke-PreviewChild -ScriptPath $dispatcherPath -ScriptArguments @(
            'Arm', '-ReviewedFirmwareDifference', '-WorkspaceRoot', $absentRoot, '-WhatIf'
        )
        Assert-True ($rejected.ExitCode -ne 0) 'Reviewed firmware flag was accepted for Arm.'
        Assert-True (($rejected.StdOut + $rejected.StdErr) -match 'supported only with FinalVerify') 'Other-action rejection had an unexpected reason.'
        Assert-True (-not (Test-Path -LiteralPath $absentRoot)) 'Rejected review request created its workspace.'
    }

    Invoke-TestCase 'Fixture dispatcher command allowlist contains only local logic and intercepted git' {
        $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name 'allowlist'
        $allowedCommands = @(
            'Assert-GpuActionAllowed', 'Assert-GpuFilePin', 'Assert-GpuNoReparsePath',
            'Assert-GpuPathWithin', 'Assert-GpuSafeFilePath',
            'Assert-GpuRecordedState', 'Assert-GpuSamePath', 'ConvertFrom-Json',
            'ConvertTo-Json', 'Copy-GpuRecoveryJournal', 'Export-ModuleMember', 'Add-Member',
            'ForEach-Object', 'Get-Content', 'Get-GpuExpectedPaths', 'Get-GpuFilePin',
            'Get-GpuCanonicalPath', 'Get-GpuPathComparisonValue', 'Get-GpuSourceCommit', 'Get-GpuStatus',
            'Get-Item', 'Import-Module', 'Invoke-GpuHostPlatformAction', 'Join-Path',
            'New-Item', 'New-Object', 'Read-GpuJson', 'Remove-Item', 'Save-GpuJournal', 'Set-StrictMode',
            'Split-Path', 'Test-GpuFinalVerifyRetry', 'Test-GpuReparsePoint', 'Test-Path', 'Write-GpuJson',
            'Write-Warning', 'git'
        )
        foreach ($path in @(
            (Join-Path $fixture.ToolRoot 'gpu-host-test.ps1'),
            (Join-Path $fixture.ToolRoot 'gpu-host-rollback.ps1'),
            (Join-Path $fixture.ToolRoot 'GpuHostState.psm1'),
            (Join-Path $fixture.ToolRoot 'GpuHostPlatform.psm1'),
            $fixture.HarnessPath
        )) {
            $unknown = @(Get-PowerShellCommandNames -Path $path | Where-Object { $allowedCommands -notcontains $_ })
            Assert-Equal 0 $unknown.Count "Unexpected command(s) in fixture execution path ${path}: $($unknown -join ', ')."
        }
        Assert-Equal 0 @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'A command allowlist check invoked the fake platform.'
    }

    Invoke-TestCase 'Hermetic dispatcher runs the complete phase sequence and leaves Complete closed' {
        $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name 'complete'

        $emptyStatus = Invoke-GpuFixtureAction -Fixture $fixture -Action Status
        Assert-Equal 0 $emptyStatus.ExitCode "Status without a selector failed: $($emptyStatus.StdErr)"
        Assert-True ($emptyStatus.StdOut -match 'NOT_PREPARED') 'Empty Status did not report NOT_PREPARED.'
        Assert-True (-not (Test-Path -LiteralPath $fixture.StateRoot)) 'Empty Status created the state root.'
        Assert-Equal 0 @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Empty Status reached the fake platform.'

        $prepared = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action Prepare
        Assert-Equal 'Prepared' $prepared.Phase 'Prepare did not advance to Prepared.'
        Assert-True (Test-Path -LiteralPath $fixture.SelectorPath -PathType Leaf) 'Prepare did not publish the exact selector.'
        $savedSelector = Read-GpuJson -Path $fixture.SelectorPath
        Assert-Equal $prepared.RunId $savedSelector.RunId 'Selector run ID differs from the journal.'
        Assert-Equal $prepared.RunDirectory $savedSelector.RunDirectory 'Selector run path differs from the journal.'
        Assert-Equal (Join-Path $prepared.RunDirectory 'journal.json') $savedSelector.JournalPath 'Selector journal path is not exact.'
        $selectorBytes = [IO.File]::ReadAllBytes($fixture.SelectorPath)
        $callsAfterPrepare = @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count

        $earlyArchive = Invoke-GpuFixtureAction -Fixture $fixture -Action Archive
        Assert-True ($earlyArchive.ExitCode -ne 0) 'Archive accepted a run before successful FinalVerify.'
        Assert-True ($earlyArchive.StdErr -match 'requires a successful FinalVerify') 'Incomplete Archive failed for an unrelated reason.'
        Assert-True (Test-Path -LiteralPath $fixture.SelectorPath -PathType Leaf) 'Rejected Archive removed the active selector.'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $fixture.StateRoot ('archive\' + $prepared.RunId + '.json')))) 'Rejected Archive created an archive record.'
        Assert-Equal $callsAfterPrepare @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Rejected Archive reached the platform.'

        $repeatPrepare = Invoke-GpuFixtureAction -Fixture $fixture -Action Prepare
        Assert-True ($repeatPrepare.ExitCode -ne 0) 'A second Prepare overwrote the saved selector.'
        Assert-True ($repeatPrepare.StdErr -match 'already exists') 'Second Prepare did not explain the saved selector refusal.'
        Assert-Equal $callsAfterPrepare @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Second Prepare reached the platform.'
        Assert-Equal ([Convert]::ToBase64String($selectorBytes)) ([Convert]::ToBase64String([IO.File]::ReadAllBytes($fixture.SelectorPath))) 'Second Prepare changed the selector.'

        $outOfOrder = Invoke-GpuFixtureAction -Fixture $fixture -Action Arm
        Assert-True ($outOfOrder.ExitCode -ne 0) 'Arm was allowed before Build, Sign, Backup, and Stage.'
        Assert-True ($outOfOrder.StdErr -match 'not allowed during phase Prepared') 'Out-of-order Arm did not fail at the phase gate.'
        Assert-Equal $callsAfterPrepare @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Out-of-order Arm reached the platform.'

        foreach ($transition in @(
            @{ Action='Build'; Phase='Built' },
            @{ Action='Sign'; Phase='Signed' },
            @{ Action='Backup'; Phase='BackedUp' },
            @{ Action='Stage'; Phase='Staged' },
            @{ Action='Arm'; Phase='Armed' },
            @{ Action='Collect'; Phase='Collected' },
            @{ Action='Disarm'; Phase='Disarmed' },
            @{ Action='FinalVerify'; Phase='Complete' }
        )) {
            $journal = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action $transition.Action
            Assert-Equal $transition.Phase $journal.Phase "$($transition.Action) reached the wrong phase."
        }
        $completeCalls = @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count
        foreach ($action in @('Disarm', 'Rollback')) {
            $cleanup = Invoke-GpuFixtureAction -Fixture $fixture -Action $action
            Assert-Equal 0 $cleanup.ExitCode "Complete-state $action did not return a clean closed status."
            Assert-True ($cleanup.StdOut -match 'ALREADY_COMPLETE') "Complete-state $action did not report ALREADY_COMPLETE."
            Assert-Equal $completeCalls @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count "Complete-state $action reopened the platform path."
            Assert-Equal 'Complete' (Get-GpuFixtureJournal -Fixture $fixture).Phase "Complete-state $action changed the final phase."
        }

        $selected = Read-GpuJson -Path $fixture.SelectorPath
        $completedJournal = Get-GpuFixtureJournal -Fixture $fixture
        Assert-True ($null -ne $completedJournal.FinalVerification) 'Successful FinalVerify did not persist its verification record.'
        $archiveRoot = Join-Path $fixture.StateRoot 'archive'
        $archivePath = Join-Path $archiveRoot ($selected.RunId + '.json')
        $runsRoot = Join-Path $fixture.StateRoot 'runs'
        $otherRunId = 'gpu-20261019T130000Z-11111111111111111111111111111111'
        $otherRunDirectory = Join-Path $runsRoot $otherRunId
        New-Item -ItemType Directory -Path $otherRunDirectory -Force | Out-Null
        $otherState = Copy-ObjectForTest $completedJournal
        $otherState.RunId = $otherRunId
        $otherState.RunDirectory = $otherRunDirectory
        $otherState.RecoveryRunDirectory = Join-Path $otherState.RecoveryBase $otherRunId
        $otherState.Paths = [pscustomobject]@{
            HypervisorBuild = Join-Path $otherRunDirectory 'h'
            LoaderBuild = Join-Path $otherRunDirectory 'l'
            SignedDirectory = Join-Path $otherRunDirectory 'signed'
        }
        Write-GpuJson -Path (Join-Path $otherRunDirectory 'journal.json') -Value $otherState -NoOverwrite

        $wrongRunArchive = Invoke-GpuFixtureAction -Fixture $fixture -Action Archive -RunDirectory $otherRunDirectory
        Assert-True ($wrongRunArchive.ExitCode -ne 0) 'Archive accepted an explicit run that differs from the saved selector.'
        Assert-True ($wrongRunArchive.StdErr -match 'Recorded path differs|Selector belongs to another run') 'Wrong-run Archive failed for an unrelated reason.'
        Assert-True (Test-Path -LiteralPath $fixture.SelectorPath -PathType Leaf) 'Wrong-run Archive removed the selected run selector.'
        Assert-True (-not (Test-Path -LiteralPath $archivePath)) 'Wrong-run Archive created an archive record.'
        Assert-Equal $completeCalls @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Wrong-run Archive reached the platform.'

        $selectorHash = (Get-FileHash -LiteralPath $fixture.SelectorPath -Algorithm SHA256).Hash
        $archive = Invoke-GpuFixtureAction -Fixture $fixture -Action Archive
        Assert-Equal 0 $archive.ExitCode "Archive after successful FinalVerify failed: $($archive.StdErr)"
        Assert-True ($archive.StdOut -match 'SELECTOR_ARCHIVED') 'Archive did not report successful selector archival.'
        Assert-True (-not (Test-Path -LiteralPath $fixture.SelectorPath)) 'Successful Archive kept the active selector.'
        Assert-True (Test-Path -LiteralPath $archivePath -PathType Leaf) 'Successful Archive did not save the selector record.'
        Assert-Equal $selectorHash (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash 'Archived selector bytes differ from the active selector.'
        Assert-Equal 'Complete' (Get-GpuFixtureJournal -Fixture $fixture -RunDirectory $selected.RunDirectory).Phase 'Archive removed or altered the completed journal.'
        Assert-Equal $completeCalls @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Archive reached the platform.'

        $nextPrepared = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action Prepare
        Assert-True ($nextPrepared.RunId -cne $selected.RunId) 'Prepare reused the archived run identity.'
        Assert-True (Test-Path -LiteralPath (Join-Path $selected.RunDirectory 'journal.json') -PathType Leaf) 'Preparing a new run removed the prior journal.'
    }

    Invoke-TestCase 'Hermetic dispatcher preserves failed Stage intent and permits Status plus Disarm recovery' {
        $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name 'failed-stage-disarm'
        foreach ($action in @('Prepare', 'Build', 'Sign', 'Backup')) {
            $null = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action $action
        }
        $beforeStageCalls = @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count
        $failedStage = Invoke-GpuFixtureAction -Fixture $fixture -Action Stage -FailPlatformAction Stage
        Assert-True ($failedStage.ExitCode -ne 0) 'Injected Stage failure unexpectedly succeeded.'
        Assert-True ($failedStage.StdErr -match 'FIXTURE_INJECTED_FAILURE:Stage') 'Injected Stage failure reason was lost.'
        $failedJournal = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'BackedUp' $failedJournal.Phase 'Failed Stage advanced the phase.'
        Assert-Equal 'Stage' $failedJournal.InProgress.Action 'Failed Stage intent was cleared.'
        Assert-True ($failedJournal.LastFailure -match 'FIXTURE_INJECTED_FAILURE:Stage') 'Failed Stage was not persisted in LastFailure.'
        $stageActionDirectory = $failedJournal.InProgress.Directory
        $stageFailurePath = Join-Path $stageActionDirectory 'failure.json'
        Assert-True (Test-Path -LiteralPath $stageFailurePath -PathType Leaf) 'Failed Stage receipt was not persisted.'
        Assert-Equal ($beforeStageCalls + 1) @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Injected Stage did not make exactly one fake platform call.'

        $status = Invoke-GpuFixtureAction -Fixture $fixture -Action Status
        Assert-Equal 0 $status.ExitCode "Status could not read an interrupted action: $($status.StdErr)"
        Assert-True ($status.StdOut -match 'RECORDED_STATE_ONLY') 'Interrupted Status claimed a live check or returned no status.'
        Assert-True ($status.StdOut -match '"Action"\s*:\s*"Stage"') 'Interrupted Status omitted the saved Stage intent.'
        $blockedArm = Invoke-GpuFixtureAction -Fixture $fixture -Action Arm
        Assert-True ($blockedArm.ExitCode -ne 0) 'Arm was allowed while Stage remained in progress.'
        Assert-True ($blockedArm.StdErr -match 'earlier action is incomplete') 'Blocked Arm did not identify the interrupted Stage.'
        Assert-Equal ($beforeStageCalls + 1) @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Blocked Arm reached the fake platform.'

        $disarm = Invoke-GpuFixtureAction -Fixture $fixture -Action Disarm
        Assert-Equal 0 $disarm.ExitCode "Disarm could not recover an interrupted Stage: $($disarm.StdErr)"
        $afterDisarm = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Disarmed' $afterDisarm.Phase 'Disarm recovery did not reach Disarmed.'
        Assert-True ($null -eq $afterDisarm.InProgress) 'Disarm recovery left an in-progress action.'
        $recoveredStageIntents = @($afterDisarm.RecoveryHistory | Where-Object {
            $_.Action -ceq 'Stage' -and $_.Directory -ceq $stageActionDirectory
        })
        Assert-Equal 1 $recoveredStageIntents.Count 'Disarm recovery discarded or duplicated the interrupted Stage intent.'
        Assert-True (Test-Path -LiteralPath $stageFailurePath -PathType Leaf) 'Disarm recovery erased the failed Stage receipt.'
        Assert-Equal 'Stage' (Read-GpuJson -Path $stageFailurePath).InProgress.Action 'Failed Stage evidence changed during Disarm.'
    }

    Invoke-TestCase 'Hermetic dispatcher permits Rollback directly after an interrupted Stage' {
        $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name 'failed-stage-rollback'
        foreach ($action in @('Prepare', 'Build', 'Sign', 'Backup')) {
            $null = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action $action
        }
        $failedStage = Invoke-GpuFixtureAction -Fixture $fixture -Action Stage -FailPlatformAction Stage
        Assert-True ($failedStage.ExitCode -ne 0) 'Injected Stage failure unexpectedly succeeded.'
        $failedJournal = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Stage' $failedJournal.InProgress.Action 'Failure fixture lacks its interrupted Stage marker.'

        $rollback = Invoke-GpuFixtureAction -Fixture $fixture -Action Rollback
        Assert-Equal 0 $rollback.ExitCode "Rollback could not recover an interrupted Stage: $($rollback.StdErr)"
        $recovered = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Disarmed' $recovered.Phase 'Rollback did not reach the disarmed phase.'
        Assert-True ($null -eq $recovered.InProgress) 'Rollback recovery left an in-progress action.'
        $calls = @(Get-GpuFixturePlatformCalls -Fixture $fixture)
        Assert-True ($calls[-1] -match '^Disarm\|Rollback\|BackedUp$') 'Rollback was not mapped to fixture Disarm after persisting Rollback intent.'
    }

    Invoke-TestCase 'Rollback preserves an observed reset boundary and permits FinalVerify' {
        $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name 'rollback-reset-observed'
        foreach ($action in @('Prepare', 'Build', 'Sign', 'Backup', 'Stage', 'Arm')) {
            $null = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action $action
        }

        $firstDisarm = Invoke-GpuFixtureAction -Fixture $fixture -Action Disarm -AdditionalEnvironment @{
            GPU_FIXTURE_RESET_MARKER = 'false'
            GPU_FIXTURE_DISARMED_BOOT_ID = 'boot-first-disarm'
        }
        Assert-Equal 0 $firstDisarm.ExitCode "Initial fixture Disarm failed: $($firstDisarm.StdErr)"
        $beforeRollback = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Disarmed' $beforeRollback.Phase 'Initial fixture Disarm did not reach Disarmed.'
        Assert-Equal 'boot-first-disarm' $beforeRollback.DisarmedBootId 'Initial fixture disarm boundary was not recorded.'

        $rollback = Invoke-GpuFixtureAction -Fixture $fixture -Action Rollback -AdditionalEnvironment @{
            GPU_FIXTURE_RESET_MARKER = 'true'
        }
        Assert-Equal 0 $rollback.ExitCode "Rollback failed after the reset boundary was observed: $($rollback.StdErr)"
        $receipt = $rollback.StdOut | ConvertFrom-Json
        Assert-Equal 'DISARMED_RESET_OBSERVED' $receipt.Status 'Rollback replaced the successful reset-observed status with a power-cycle-pending status.'
        Assert-True ([bool]$receipt.Details.ResetBoundaryPassed) 'Rollback receipt omitted the reset-boundary evidence.'
        $afterRollback = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Disarmed' $afterRollback.Phase 'Successful Rollback did not remain in Disarmed.'
        Assert-Equal 'boot-first-disarm' $afterRollback.DisarmedBootId 'Rollback overwrote the original disarm boot boundary.'

        $finalVerify = Invoke-GpuFixtureAction -Fixture $fixture -Action FinalVerify
        Assert-Equal 0 $finalVerify.ExitCode "FinalVerify was blocked after reset-observed Rollback: $($finalVerify.StdErr)"
        $completed = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Complete' $completed.Phase 'FinalVerify did not complete after reset-observed Rollback.'
        Assert-Equal 'boot-first-disarm' $completed.DisarmedBootId 'FinalVerify changed the saved disarm reset boundary.'
    }

    Invoke-TestCase 'Rollback keeps the power-cycle pending status when reset evidence is false or absent' {
        foreach ($marker in @('false', 'absent')) {
            $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name ('rollback-reset-' + $marker)
            foreach ($action in @('Prepare', 'Build', 'Sign', 'Backup', 'Stage', 'Arm')) {
                $null = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action $action
            }
            $rollback = Invoke-GpuFixtureAction -Fixture $fixture -Action Rollback -AdditionalEnvironment @{
                GPU_FIXTURE_RESET_MARKER = $marker
            }
            Assert-Equal 0 $rollback.ExitCode "Rollback with '$marker' reset evidence failed: $($rollback.StdErr)"
            $receipt = $rollback.StdOut | ConvertFrom-Json
            Assert-Equal 'RECOVERY_PENDING_USER_POWER_CYCLE' $receipt.Status "Rollback did not preserve the pending status for '$marker' reset evidence."
            if ($marker -eq 'false') {
                Assert-Equal $false ([bool]$receipt.Details.ResetBoundaryPassed) 'Explicit false reset evidence was not retained.'
            }
            else {
                Assert-True ($null -eq $receipt.Details.PSObject.Properties['ResetBoundaryPassed']) 'Absent reset evidence was synthesized as present.'
            }
        }
    }

    Invoke-TestCase 'Hermetic dispatcher retries failed FinalVerify directly and preserves its intent' {
        $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name 'final-verify-retry'
        foreach ($action in @('Prepare', 'Build', 'Sign', 'Backup', 'Stage', 'Arm')) {
            $null = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action $action
        }
        $disarm = Invoke-GpuFixtureAction -Fixture $fixture -Action Disarm -AdditionalEnvironment @{
            GPU_FIXTURE_RESET_MARKER = 'true'
        }
        Assert-Equal 0 $disarm.ExitCode "Fixture Disarm failed before FinalVerify retry coverage: $($disarm.StdErr)"

        $failed = Invoke-GpuFixtureAction -Fixture $fixture -Action FinalVerify -FailPlatformAction FinalVerify
        Assert-True ($failed.ExitCode -ne 0) 'Injected FinalVerify failure unexpectedly succeeded.'
        Assert-True ($failed.StdErr -match 'FIXTURE_INJECTED_FAILURE:FinalVerify') 'Initial FinalVerify failure reason was lost.'
        $failedJournal = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Disarmed' $failedJournal.Phase 'Failed FinalVerify changed the Disarmed phase.'
        Assert-Equal 'FinalVerify' $failedJournal.InProgress.Action 'Failed FinalVerify intent was not retained.'
        $failedIntent = $failedJournal.InProgress
        $failedReceiptPath = Join-Path $failedIntent.Directory 'failure.json'
        Assert-True (Test-Path -LiteralPath $failedReceiptPath -PathType Leaf) 'Failed FinalVerify receipt was not saved.'
        Assert-Equal 0 @($failedJournal.RecoveryHistory).Count 'Fresh failed FinalVerify unexpectedly changed existing recovery history.'
        $callsBeforeRetry = @(Get-GpuFixturePlatformCalls -Fixture $fixture)

        $retry = Invoke-GpuFixtureAction -Fixture $fixture -Action FinalVerify
        Assert-Equal 0 $retry.ExitCode "FinalVerify retry failed: $($retry.StdErr)"
        $retryJournal = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Complete' $retryJournal.Phase 'Successful FinalVerify retry did not complete the run.'
        Assert-True ($null -eq $retryJournal.InProgress) 'Successful FinalVerify retry left an intent in progress.'
        Assert-True ($null -ne $retryJournal.FinalVerification) 'Successful FinalVerify retry did not persist verification evidence.'
        $retriedIntent = @($retryJournal.RecoveryHistory | Where-Object {
            $_.Action -ceq 'FinalVerify' -and $_.Directory -ceq $failedIntent.Directory
        })
        Assert-Equal 1 $retriedIntent.Count 'Successful retry discarded or duplicated the original failed FinalVerify intent.'
        Assert-True (Test-Path -LiteralPath $failedReceiptPath -PathType Leaf) 'Successful retry erased the original failure receipt.'
        $callsAfterRetry = @(Get-GpuFixturePlatformCalls -Fixture $fixture)
        Assert-Equal ($callsBeforeRetry.Count + 1) $callsAfterRetry.Count 'Successful retry did not make exactly one new platform call.'
        Assert-True ($callsAfterRetry[-1] -match '^FinalVerify\|FinalVerify\|Disarmed$') 'Retry did not call FinalVerify directly from the disarmed state.'
        Assert-Equal 1 @($callsAfterRetry | Where-Object { $_ -match '^Disarm\|' }).Count 'FinalVerify retry invoked Disarm again.'
    }

    Invoke-TestCase 'Hermetic dispatcher retries FinalVerify after failed Disarm or Rollback without cleanup' {
        foreach ($interruptedAction in @('Disarm', 'Rollback')) {
            $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name ('final-verify-after-' + $interruptedAction.ToLowerInvariant())
            foreach ($action in @('Prepare', 'Build', 'Sign', 'Backup', 'Stage', 'Arm')) {
                $null = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action $action
            }
            $disarm = Invoke-GpuFixtureAction -Fixture $fixture -Action Disarm -AdditionalEnvironment @{
                GPU_FIXTURE_RESET_MARKER = 'true'
            }
            Assert-Equal 0 $disarm.ExitCode "Initial fixture Disarm failed before interrupted $interruptedAction coverage: $($disarm.StdErr)"

            $failedCleanup = Invoke-GpuFixtureAction -Fixture $fixture -Action $interruptedAction -FailPlatformAction Disarm
            Assert-True ($failedCleanup.ExitCode -ne 0) "Injected $interruptedAction failure unexpectedly succeeded."
            Assert-True ($failedCleanup.StdErr -match 'FIXTURE_INJECTED_FAILURE:Disarm') "$interruptedAction failure reason was lost."
            $failedJournal = Get-GpuFixtureJournal -Fixture $fixture
            Assert-Equal 'Disarmed' $failedJournal.Phase "$interruptedAction failure changed the existing Disarmed phase."
            Assert-Equal $interruptedAction $failedJournal.InProgress.Action "$interruptedAction failure intent was not retained."
            $failedIntent = $failedJournal.InProgress
            $failedReceiptPath = Join-Path $failedIntent.Directory 'failure.json'
            Assert-True (Test-Path -LiteralPath $failedReceiptPath -PathType Leaf) "$interruptedAction failure receipt was not saved."
            $callsBeforeRetry = @(Get-GpuFixturePlatformCalls -Fixture $fixture)
            $disarmCallsBeforeRetry = @($callsBeforeRetry | Where-Object { $_ -match '^Disarm\|' }).Count

            $retry = Invoke-GpuFixtureAction -Fixture $fixture -Action FinalVerify
            Assert-Equal 0 $retry.ExitCode "FinalVerify after interrupted $interruptedAction failed: $($retry.StdErr)"
            $retriedJournal = Get-GpuFixtureJournal -Fixture $fixture
            Assert-Equal 'Complete' $retriedJournal.Phase "FinalVerify after $interruptedAction did not complete the run."
            Assert-True ($null -eq $retriedJournal.InProgress) "FinalVerify after $interruptedAction left an intent in progress."
            $preservedIntent = @($retriedJournal.RecoveryHistory | Where-Object {
                $_.Action -ceq $interruptedAction -and $_.Directory -ceq $failedIntent.Directory
            })
            Assert-Equal 1 $preservedIntent.Count "FinalVerify discarded or duplicated interrupted $interruptedAction evidence."
            Assert-True (Test-Path -LiteralPath $failedReceiptPath -PathType Leaf) "FinalVerify erased the failed $interruptedAction receipt."
            $callsAfterRetry = @(Get-GpuFixturePlatformCalls -Fixture $fixture)
            Assert-Equal ($callsBeforeRetry.Count + 1) $callsAfterRetry.Count "FinalVerify after $interruptedAction made unexpected platform calls."
            Assert-True ($callsAfterRetry[-1] -match '^FinalVerify\|FinalVerify\|Disarmed$') "FinalVerify after $interruptedAction did not invoke verification directly."
            Assert-Equal $disarmCallsBeforeRetry @($callsAfterRetry | Where-Object { $_ -match '^Disarm\|' }).Count "FinalVerify after $interruptedAction retried cleanup."
        }
    }

    Invoke-TestCase 'Failed FinalVerify retry stays disarmed and blocks Archive Arm and Stage' {
        $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name 'failed-final-verify-retry'
        foreach ($action in @('Prepare', 'Build', 'Sign', 'Backup', 'Stage', 'Arm')) {
            $null = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action $action
        }
        $disarm = Invoke-GpuFixtureAction -Fixture $fixture -Action Disarm -AdditionalEnvironment @{
            GPU_FIXTURE_RESET_MARKER = 'true'
        }
        Assert-Equal 0 $disarm.ExitCode "Fixture Disarm failed before failed-retry coverage: $($disarm.StdErr)"
        $firstFailure = Invoke-GpuFixtureAction -Fixture $fixture -Action FinalVerify -FailPlatformAction FinalVerify
        Assert-True ($firstFailure.ExitCode -ne 0) 'Initial injected FinalVerify failure unexpectedly succeeded.'
        $firstFailedJournal = Get-GpuFixtureJournal -Fixture $fixture
        $firstIntent = $firstFailedJournal.InProgress
        $secondFailure = Invoke-GpuFixtureAction -Fixture $fixture -Action FinalVerify -FailPlatformAction FinalVerify
        Assert-True ($secondFailure.ExitCode -ne 0) 'Injected FinalVerify retry unexpectedly succeeded.'
        Assert-True ($secondFailure.StdErr -match 'FIXTURE_INJECTED_FAILURE:FinalVerify') 'Retry failure reason was lost.'
        $failedJournal = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Disarmed' $failedJournal.Phase 'Failed FinalVerify retry changed the Disarmed phase.'
        Assert-Equal 'FinalVerify' $failedJournal.InProgress.Action 'Failed retry evidence was not retained.'
        $latestFailurePath = Join-Path $failedJournal.InProgress.Directory 'failure.json'
        Assert-True (Test-Path -LiteralPath $latestFailurePath -PathType Leaf) 'Failed retry receipt was not saved.'
        $firstIntentHistory = @($failedJournal.RecoveryHistory | Where-Object {
            $_.Action -ceq 'FinalVerify' -and $_.Directory -ceq $firstIntent.Directory
        })
        Assert-Equal 1 $firstIntentHistory.Count 'Failed retry discarded the first FinalVerify failure evidence.'
        $platformCalls = @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count

        foreach ($blockedAction in @('Archive', 'Arm', 'Stage')) {
            $blocked = Invoke-GpuFixtureAction -Fixture $fixture -Action $blockedAction
            Assert-True ($blocked.ExitCode -ne 0) "$blockedAction was allowed after a failed FinalVerify retry."
            Assert-Equal $platformCalls @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count "$blockedAction reached the platform after retry failure."
            $current = Get-GpuFixtureJournal -Fixture $fixture
            Assert-Equal 'Disarmed' $current.Phase "$blockedAction changed the failed retry phase."
            Assert-Equal 'FinalVerify' $current.InProgress.Action "$blockedAction changed the failed retry intent."
        }
        Assert-True (Test-Path -LiteralPath $latestFailurePath -PathType Leaf) 'Blocked actions erased retry failure evidence.'
        Assert-True (Test-Path -LiteralPath $fixture.SelectorPath -PathType Leaf) 'Blocked actions removed the active selector.'
    }

    Invoke-TestCase 'Hermetic dispatcher rejects source-pin drift before platform execution' {
        $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name 'pin-drift'
        $prepared = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action Prepare
        $pinnedFile = Join-Path $fixture.ToolRoot 'gpu-host-rollback.ps1'
        [IO.File]::AppendAllText($pinnedFile, "`r`n# fixture-only pin drift`r`n")
        $build = Invoke-GpuFixtureAction -Fixture $fixture -Action Build
        Assert-True ($build.ExitCode -ne 0) 'Build accepted a changed source pin.'
        Assert-True ($build.StdErr -match 'Pinned file has drifted') 'Pin drift failed for an unrelated reason.'
        $journal = Get-GpuFixtureJournal -Fixture $fixture
        Assert-Equal 'Prepared' $journal.Phase 'Pin drift advanced the phase.'
        Assert-True ($null -eq $journal.InProgress) 'Pin drift persisted a platform intent.'
        Assert-Equal 1 @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Pin drift reached the fake Build platform.'
    }

    Invoke-TestCase 'Hermetic dispatcher rejects malformed, escaped, and mismatched selectors' {
        $fixture = New-GpuDispatcherFixture -Parent $runRoot -Name 'bad-selector'
        $prepared = Assert-GpuFixtureActionSucceeded -Fixture $fixture -Action Prepare
        $originalSelector = Read-GpuJson -Path $fixture.SelectorPath
        $platformCalls = @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count

        [IO.File]::WriteAllText($fixture.SelectorPath, '{malformed-json')
        $malformed = Invoke-GpuFixtureAction -Fixture $fixture -Action Status
        Assert-True ($malformed.ExitCode -ne 0) 'Status accepted a malformed selector.'
        Assert-True ($malformed.StdErr -match 'malformed') 'Malformed selector failure was not reported as JSON parsing.'
        Assert-Equal $platformCalls @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Malformed selector reached the platform.'

        $escapedSelector = Copy-ObjectForTest $originalSelector
        $escapedSelector.RunDirectory = Join-Path $fixture.Root 'escaped run outside runs root'
        $escapedSelector.JournalPath = Join-Path $escapedSelector.RunDirectory 'journal.json'
        Write-GpuJson -Path $fixture.SelectorPath -Value $escapedSelector
        $escaped = Invoke-GpuFixtureAction -Fixture $fixture -Action Status
        Assert-True ($escaped.ExitCode -ne 0) 'Status accepted a selector that escaped the runs root.'
        Assert-True ($escaped.StdErr -match 'Recorded path differs|outside the permitted root') 'Escaped selector failed for an unrelated reason.'
        Assert-Equal $platformCalls @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Escaped selector reached the platform.'

        $wrongSource = Copy-ObjectForTest $originalSelector
        $wrongSource.SourceRoot = Join-Path $fixture.Root 'other-source'
        Write-GpuJson -Path $fixture.SelectorPath -Value $wrongSource
        $sourceMismatch = Invoke-GpuFixtureAction -Fixture $fixture -Action Status
        Assert-True ($sourceMismatch.ExitCode -ne 0) 'Status accepted a selector for a different source root.'
        Assert-True ($sourceMismatch.StdErr -match 'Recorded path differs') 'Source-root mismatch failed for an unrelated reason.'
        Assert-Equal $platformCalls @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Mismatched selector reached the platform.'

        $wrongJournal = Copy-ObjectForTest $originalSelector
        $wrongJournal.JournalPath = Join-Path $fixture.Root 'other-journal.json'
        Write-GpuJson -Path $fixture.SelectorPath -Value $wrongJournal
        $journalMismatch = Invoke-GpuFixtureAction -Fixture $fixture -Action Status
        Assert-True ($journalMismatch.ExitCode -ne 0) 'Status accepted a selector for a different journal path.'
        Assert-True ($journalMismatch.StdErr -match 'Recorded path differs') 'Journal-path mismatch failed for an unrelated reason.'
        Assert-Equal $platformCalls @(Get-GpuFixturePlatformCalls -Fixture $fixture).Count 'Mismatched journal selector reached the platform.'
    }

    foreach ($skip in $script:SkippedCases) { Write-Output ("GPU_HOST_STATE_TESTS:SKIP {0}" -f $skip) }
    if ($script:FailedCases.Count -gt 0) {
        Write-Output ("GPU_HOST_STATE_TESTS:FAIL cases={0}" -f $script:FailedCases.Count)
        foreach ($failure in $script:FailedCases) { Write-Output ("GPU_HOST_STATE_TESTS:FAIL {0}" -f $failure) }
        exit 1
    }
    Write-Output ("GPU_HOST_STATE_TESTS:PASS cases={0} assertions={1} skipped={2}" -f $script:PassedCases, $script:Assertions, $script:SkippedCases.Count)
} finally {
    # Remove any junction before recursively cleaning the fixture tree. The
    # junction target is itself a fixture, but never let recursive cleanup
    # follow a reparse point.
    if (Test-Path -LiteralPath $junctionPath) {
        $junctionPayload = Join-Path $junctionPath 'payload.bin'
        if (Test-Path -LiteralPath $junctionPayload -PathType Leaf) {
            Remove-Item -LiteralPath $junctionPayload -Force
        }
        Remove-Item -LiteralPath $junctionPath -Force
    }
    # Remove only this run's fixture directory after proving its resolved path is
    # a direct child of the private ignored .codex-build test area.
    $resolvedRunRoot = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $runRoot).Path)
    $runPrefix = $scratchParentFull.TrimEnd('\') + '\run-'
    if (-not $resolvedRunRoot.StartsWith($runPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a fixture path outside this test run: $resolvedRunRoot"
    }
    if ($resolvedRunRoot -eq $scratchParentFull) { throw 'Refusing to remove the shared scratch parent.' }
    Remove-Item -LiteralPath $resolvedRunRoot -Recurse -Force
}
