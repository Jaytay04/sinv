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
$entry = @($definitions | Where-Object Name -CEQ 'Save-Gpu062106ReviewEntryJournal')
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
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $tempParent ('GpuFirmwareEntry062106-' + [guid]::NewGuid().ToString('N'))))
try {
    $runId = 'gpu-20261021T062106Z-d7c2add0ddc44d0fb3b06badc7018942'
    $runRoot = Join-Path $fixtureRoot ('.analysis\gpu-baremetal\runs\' + $runId)
    $actionDirectory = Join-Path $runRoot 'actions\20261021T070900000Z-FinalVerify-00000000000000000000000000000001'
    [void][IO.Directory]::CreateDirectory($actionDirectory)
    $journalPath = Join-Path $runRoot 'journal.json'
    [IO.File]::WriteAllText($journalPath, '{"Phase":"Disarmed","Intent":"failed-FinalVerify"}', [Text.UTF8Encoding]::new($false))
    $original = Get-GpuFilePin $journalPath

    $sourcePath = Join-Path $fixtureRoot 'source-pin.txt'
    [IO.File]::WriteAllText($sourcePath, 'existing source pin remains available for recovery', [Text.UTF8Encoding]::new($false))
    $sourcePin = Get-GpuFilePin $sourcePath
    $state = [pscustomobject]@{
        WorkspaceRoot = $fixtureRoot
        RunDirectory = $runRoot
        RunId = $runId
        SourcePins = @($sourcePin)
    }
    $sourcePinsBefore = ConvertTo-Json -InputObject @($state.SourcePins) -Depth 5 -Compress

    # Exercise the production copy/check algorithm against a private small file.
    # Only its closed evidence constants are substituted in this isolated function.
    $definition = $entry[0].Extent.Text.Replace('26B4C9D657AE65DFD007073C4E5F99A1E620720774BEFDDBB398DE7E3F8E9A80', [string]$original.Sha256).Replace('44133L', ([string]$original.Bytes + 'L'))
    . ([scriptblock]::Create($definition))
    $copy = Save-Gpu062106ReviewEntryJournal -State $state -ActionDirectory $actionDirectory
    Assert-True ($copy.Sha256 -ceq $original.Sha256 -and $copy.Bytes -eq $original.Bytes) 'Entry copy differs from its original pin.'
    Assert-True ([IO.File]::ReadAllText($copy.Path) -ceq [IO.File]::ReadAllText($journalPath)) 'Entry copy is not byte-equivalent.'
    Assert-True ($copy.Path -ceq (Join-Path $actionDirectory 'reviewed-firmware-entry-journal.json')) 'Entry copy was written outside the reviewed action directory.'
    Assert-GpuFilePin -Pin $original
    Assert-Throws { Save-Gpu062106ReviewEntryJournal -State $state -ActionDirectory $actionDirectory } 'already exists'

    $nextAction = Join-Path $runRoot 'actions\20261021T070900001Z-FinalVerify-00000000000000000000000000000002'
    [void][IO.Directory]::CreateDirectory($nextAction)
    $state.RunId = 'another-run'
    Assert-Throws { Save-Gpu062106ReviewEntryJournal -State $state -ActionDirectory $nextAction } 'fixed 062106 run'
    $state.RunId = $runId
    $state.RunDirectory = Join-Path $fixtureRoot 'different-run-directory'
    Assert-Throws { Save-Gpu062106ReviewEntryJournal -State $state -ActionDirectory $nextAction } 'path|match|differ|equal'
    $state.RunDirectory = $runRoot
    Assert-Throws { Save-Gpu062106ReviewEntryJournal -State $state -ActionDirectory $fixtureRoot } 'outside|within'
    $badAction = Join-Path $runRoot 'actions\not-a-FinalVerify'
    [void][IO.Directory]::CreateDirectory($badAction)
    Assert-Throws { Save-Gpu062106ReviewEntryJournal -State $state -ActionDirectory $badAction } 'FinalVerify action directory'
    Assert-True (-not [IO.File]::Exists((Join-Path $nextAction 'reviewed-firmware-entry-journal.json'))) 'A rejected run, path, or action created a journal copy.'

    [IO.File]::AppendAllText($journalPath, 'changed')
    $changedPin = Get-GpuFilePin $journalPath
    Assert-Throws { Save-Gpu062106ReviewEntryJournal -State $state -ActionDirectory $nextAction } 'exact pre-retry inspection pin'
    Assert-True (-not [IO.File]::Exists((Join-Path $nextAction 'reviewed-firmware-entry-journal.json'))) 'A changed journal was copied.'
    Assert-GpuFilePin -Pin $changedPin
    Assert-True ((Get-GpuFilePin $copy.Path).Sha256 -ceq $original.Sha256) 'Original entry evidence was overwritten.'
    Assert-True ((ConvertTo-Json -InputObject @($state.SourcePins) -Depth 5 -Compress) -ceq $sourcePinsBefore) 'The checkpoint changed existing SourcePins metadata.'
    Assert-GpuFilePin -Pin $sourcePin

    $source = [IO.File]::ReadAllText($scriptPath)
    $entryCalls = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Save-Gpu062106ReviewEntryJournal' }, $true))
    Assert-True ($entryCalls.Count -eq 1) 'Dispatcher must call the 062106 entry checkpoint exactly once.'
    $checkpointIf = $entryCalls[0].Parent
    while ($null -ne $checkpointIf -and $checkpointIf -isnot [Management.Automation.Language.IfStatementAst]) { $checkpointIf = $checkpointIf.Parent }
    Assert-True ($null -ne $checkpointIf) 'The 062106 checkpoint call is not guarded by a dispatcher condition.'
    $condition = $checkpointIf.Clauses[0].Item1.Extent.Text
    Assert-True ($condition.Contains("`$Action -eq 'FinalVerify'") -and $condition.Contains('$ReviewedFirmwareDifference') -and
        $condition.Contains($runId)) 'The 062106 entry checkpoint is not restricted to its reviewed FinalVerify retry.'

    $callPosition = $source.IndexOf('Save-Gpu062106ReviewEntryJournal -State $State', [StringComparison]::Ordinal)
    $ifPosition = $checkpointIf.Extent.StartOffset
    $preCopy = $source.Substring($ifPosition, $callPosition - $ifPosition)
    Assert-True ($callPosition -gt $ifPosition -and
        $preCopy -notmatch '\$State\.(InProgress|RecoveryHistory)\s*=' -and
        $preCopy -notmatch 'Save-GpuJournal\s+\$State') 'The inspected journal is mutated before its entry copy is verified.'
    $newIntent = $source.IndexOf('$State.InProgress = [pscustomobject]', $callPosition, [StringComparison]::Ordinal)
    $historyChange = $source.IndexOf('$State.RecoveryHistory = @($State.RecoveryHistory)', $callPosition, [StringComparison]::Ordinal)
    $journalSave = $source.IndexOf('Save-GpuJournal $State', $callPosition, [StringComparison]::Ordinal)
    Assert-True ($newIntent -gt $callPosition -and $historyChange -gt $callPosition -and $journalSave -gt $callPosition) 'The entry copy is not verified before retry intent, recovery history, and journal mutation.'
    Assert-True ($source.Contains('if (-not $EntryJournalCheckpointPending)')) 'A failed entry checkpoint can overwrite the inspected journal in error handling.'
    $pendingBefore = $source.LastIndexOf('$EntryJournalCheckpointPending = $true', $callPosition, [StringComparison]::Ordinal)
    $pendingAfter = $source.IndexOf('$EntryJournalCheckpointPending = $false', $callPosition, [StringComparison]::Ordinal)
    Assert-True ($pendingBefore -ge $ifPosition -and $pendingAfter -gt $callPosition) 'Entry checkpoint failure state is not tracked around the copy.'
    $sourcePinAssignments = @($entry[0].FindAll({ param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -match '\$State\.SourcePins' }, $true))
    Assert-True ($sourcePinAssignments.Count -eq 0) 'The 062106 entry helper must preserve existing SourcePins.'

    [pscustomobject]@{ Status='PASS'; Assertions=$script:Assertions; NativeCommandsExecuted=$false; HostStateChanged=$false } | ConvertTo-Json
}
finally {
    if ([IO.Directory]::Exists($fixtureRoot)) {
        $resolved = [IO.Path]::GetFullPath($fixtureRoot)
        if (-not $resolved.StartsWith($tempParent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
            -not [IO.Path]::GetFileName($resolved).StartsWith('GpuFirmwareEntry062106-', [StringComparison]::Ordinal)) {
            throw 'Refusing cleanup outside the private fixture directory.'
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
