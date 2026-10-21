[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $ToolRoot) { $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
$scriptPath = Join-Path $ToolRoot 'gpu-host-test.ps1'
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw 'Dispatcher parse failed.' }
$definitions = $ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $true)
$entry = @($definitions | Where-Object Name -CEQ 'Save-Gpu042142ReviewEntryJournal')
$samePath = @($definitions | Where-Object Name -CEQ 'Assert-GpuSamePath')
if ($entry.Count -ne 1 -or $samePath.Count -ne 1) { throw 'Required dispatcher helpers were not found.' }
Import-Module (Join-Path $ToolRoot 'GpuHostState.psm1') -DisableNameChecking -ErrorAction Stop
. ([scriptblock]::Create($samePath[0].Extent.Text))
$script:Assertions = 0
function Assert-True([bool]$Condition, [string]$Message) {
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}
function Assert-Throws([scriptblock]$Operation, [string]$Pattern) {
    $message = $null
    try { $null = & $Operation } catch { $message = $_.Exception.Message }
    Assert-True ($null -ne $message -and $message -match $Pattern) "Expected failure '$Pattern'; observed '$message'."
}

$tempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $tempParent ('GpuFirmwareEntry042142-' + [guid]::NewGuid().ToString('N'))))
try {
    $runId = 'gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2'
    $runRoot = Join-Path $fixtureRoot ('.analysis\gpu-baremetal\runs\' + $runId)
    $actionDirectory = Join-Path $runRoot 'actions\20261021T050900000Z-FinalVerify-00000000000000000000000000000001'
    [void][IO.Directory]::CreateDirectory($actionDirectory)
    $journalPath = Join-Path $runRoot 'journal.json'
    [IO.File]::WriteAllText($journalPath, '{"Phase":"Disarmed","Intent":"failed-FinalVerify"}', [Text.UTF8Encoding]::new($false))
    $original = Get-GpuFilePin $journalPath
    # Exercise the production copy/check algorithm against a private small file.
    # Only its closed evidence constants are substituted in this isolated function.
    $definition = $entry[0].Extent.Text.Replace('3287B5E86D018DD374748D56E16CF7BBDE0D245ECF426779EAAB80E5A2B08583', [string]$original.Sha256).Replace('44133L', ([string]$original.Bytes + 'L'))
    . ([scriptblock]::Create($definition))
    $state = [pscustomobject]@{ WorkspaceRoot=$fixtureRoot; RunDirectory=$runRoot; RunId=$runId }
    $copy = Save-Gpu042142ReviewEntryJournal -State $state -ActionDirectory $actionDirectory
    Assert-True ($copy.Sha256 -ceq $original.Sha256 -and $copy.Bytes -eq $original.Bytes) 'Entry copy differs from its original pin.'
    Assert-True ([IO.File]::ReadAllText($copy.Path) -ceq [IO.File]::ReadAllText($journalPath)) 'Entry copy is not byte-equivalent.'
    Assert-GpuFilePin -Pin $original
    Assert-Throws { Save-Gpu042142ReviewEntryJournal -State $state -ActionDirectory $actionDirectory } 'already exists'
    [IO.File]::AppendAllText($journalPath, 'changed')
    $nextAction = Join-Path $runRoot 'actions\20261021T050900001Z-FinalVerify-00000000000000000000000000000002'
    [void][IO.Directory]::CreateDirectory($nextAction)
    Assert-Throws { Save-Gpu042142ReviewEntryJournal -State $state -ActionDirectory $nextAction } 'exact pre-retry inspection pin'
    Assert-True (-not [IO.File]::Exists((Join-Path $nextAction 'reviewed-firmware-entry-journal.json'))) 'A changed journal was copied.'
    Assert-True ((Get-GpuFilePin $copy.Path).Sha256 -ceq $original.Sha256) 'Original entry evidence was overwritten.'
    $state.RunId = 'another-run'
    Assert-Throws { Save-Gpu042142ReviewEntryJournal -State $state -ActionDirectory $nextAction } 'fixed 042142 run'
    $state.RunId = $runId
    Assert-Throws { Save-Gpu042142ReviewEntryJournal -State $state -ActionDirectory $fixtureRoot } 'outside|within'
    $badAction = Join-Path $runRoot 'actions\not-a-FinalVerify'
    [void][IO.Directory]::CreateDirectory($badAction)
    Assert-Throws { Save-Gpu042142ReviewEntryJournal -State $state -ActionDirectory $badAction } 'FinalVerify action directory'

    $source = [IO.File]::ReadAllText($scriptPath)
    $copyCall = $source.IndexOf('$null = Save-Gpu042142ReviewEntryJournal', [StringComparison]::Ordinal)
    $newIntent = $source.IndexOf('$State.InProgress = [pscustomobject]', [StringComparison]::Ordinal)
    $historyChange = $source.IndexOf('$State.RecoveryHistory = @($State.RecoveryHistory)', [StringComparison]::Ordinal)
    Assert-True ($copyCall -gt 0 -and $copyCall -lt $newIntent -and $copyCall -lt $historyChange) 'Entry copy is not verified before journal mutation.'
    Assert-True ($source.Contains('if (-not $EntryJournalCheckpointPending)')) 'A failed entry checkpoint can overwrite the inspected journal in error handling.'
    Assert-True ($source.Contains('$EntryJournalCheckpointPending = $true') -and $source.Contains('$EntryJournalCheckpointPending = $false')) 'Entry checkpoint state is not tracked.'
    [pscustomobject]@{ Status='PASS'; Assertions=$script:Assertions; NativeCommandsExecuted=$false; HostStateChanged=$false } | ConvertTo-Json
}
finally {
    if ([IO.Directory]::Exists($fixtureRoot)) {
        $resolved = [IO.Path]::GetFullPath($fixtureRoot)
        if (-not $resolved.StartsWith($tempParent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
            -not [IO.Path]::GetFileName($resolved).StartsWith('GpuFirmwareEntry042142-', [StringComparison]::Ordinal)) {
            throw 'Refusing cleanup outside the private fixture directory.'
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
