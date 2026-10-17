$ErrorActionPreference = 'Stop'

function Assert-WorkflowCondition([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-WorkflowThrows([scriptblock]$Action, [string]$Message) {
    $threw = $false
    try {
        & $Action
    } catch {
        $threw = $true
    }
    if (-not $threw) {
        throw $Message
    }
}

function Reset-WorkflowMocks {
    $global:WorkflowEvents = @()
    $global:WorkflowSavedStates = @()
    $global:WorkflowChainOperations = @()
    $global:WorkflowBaselineCount = 0
    $global:WorkflowMountCount = 0
    $global:WorkflowUnmountCount = 0
    $global:WorkflowAdminCount = 0
    $global:WorkflowMockBootNext = $false
    $global:WorkflowCoreArmFailure = $false
    $global:WorkflowArmedSaveFailure = $false
    $global:WorkflowArmIntentProof = $false
    $global:WorkflowArmBootNextBefore = $null
    $global:LASTEXITCODE = 0
}

function Assert-Admin {
    $global:WorkflowAdminCount++
}

function Assert-VariantBaseline($Snapshot) {
    if ($null -eq $Snapshot -or $Snapshot.id -cne 'workflow-snapshot') {
        throw 'Unexpected mock baseline snapshot.'
    }
    $global:WorkflowBaselineCount++
    $global:WorkflowEvents += 'baseline'
}

function Mount-Esp {
    $global:WorkflowMountCount++
    return [pscustomobject]@{ EspRoot = $global:WorkflowFakeEspRoot }
}

function Dismount-Esp($Mount) {
    if ($null -eq $Mount -or $Mount.EspRoot -cne $global:WorkflowFakeEspRoot) {
        throw 'Unexpected mock ESP mount.'
    }
    $global:WorkflowUnmountCount++
    $global:WorkflowEvents += 'unmount'
}

function Get-VariantBootIdentity {
    return 'mock-boot-identity'
}

function Save-VariantState($State) {
    $saved = [pscustomobject]@{
        armed = [bool]$State.armed
        phase = [string]$State.phase
        armedFromBoot = [string]$State.armedFromBoot
    }
    $global:WorkflowSavedStates += $saved
    $global:WorkflowEvents += ('save:' + $saved.phase)
    if ($global:WorkflowArmedSaveFailure -and
        $saved.phase -ceq 'armed-by-user-awaiting-new-boot') {
        $global:WorkflowArmedSaveFailure = $false
        throw 'Injected post-arm state-save failure.'
    }
}

function Invoke-MockChain {
    param(
        [Parameter(Position = 0)][string]$Operation,
        [string]$InventoryBuildDir,
        [string]$LoaderBuildDir,
        [string]$SignedDirectory
    )

    $global:WorkflowChainOperations += $Operation
    $global:WorkflowEvents += ('chain:' + $Operation)
    if ($Operation -ceq 'Arm') {
        $last = if ($global:WorkflowSavedStates.Count -gt 0) {
            $global:WorkflowSavedStates[-1]
        } else {
            $null
        }
        $global:WorkflowArmIntentProof =
            ($null -ne $last -and $last.phase -ceq 'arm-pending' -and
             -not $last.armed -and
             $last.armedFromBoot -ceq 'mock-boot-identity')
        $global:WorkflowArmBootNextBefore = [bool]$global:WorkflowMockBootNext
        if ($global:WorkflowCoreArmFailure) {
            $global:LASTEXITCODE = 1
            return
        }
        $global:WorkflowMockBootNext = $true
        $global:WorkflowEvents += 'bootnext:on'
        $global:LASTEXITCODE = 0
        return
    }
    if ($Operation -ceq 'Disarm') {
        $global:WorkflowMockBootNext = $false
        $global:WorkflowEvents += 'bootnext:off'
        $global:LASTEXITCODE = 0
        return
    }
    throw ('Unexpected mock chain operation: ' + $Operation)
}

function New-WorkflowState([bool]$ReadyToArm = $true) {
    return [pscustomobject]@{
        readyToArm = $ReadyToArm
        snapshot = [pscustomobject]@{ id = 'workflow-snapshot' }
        armed = $false
        phase = 'seven-fields-awaiting-user-boot'
        armedFromBoot = $null
    }
}

function New-WorkflowReportLines {
    param(
        [int]$EntryCount,
        [string]$BuildId,
        [switch]$DriftNonselectedField
    )

    $rows = Import-Csv -LiteralPath (Join-Path $global:variantPackage 'reports\client-originals.tsv') `
        -Delimiter "`t"
    $names = @(
        'system_serial', 'system_vendor', 'system_product', 'board_serial',
        'board_vendor', 'board_product', 'system_uuid'
    )
    $members = @(
        'FakeSMBIOSSerial', 'FakeSMBIOSManufacturer', 'FakeSMBIOSProduct',
        'FakeMotherboardSerial', 'FakeMotherboardManufacturer',
        'FakeMotherboardProduct', 'FakeSMBIOSUUID'
    )
    $lines = @(
        'HSMBIOS001',
        'mask=127',
        ('build_id=' + $BuildId),
        ('entries=' + $EntryCount)
    )
    for ($entry = 0; $entry -lt $EntryCount; $entry++) {
        $major = if ($entry -eq 0) { '3' } else { '2' }
        $lines += ('entry' + "`t" + $entry + "`t" + $major + "`tEfiRuntimeServicesData")
        for ($field = 0; $field -lt $names.Count; $field++) {
            $pair = @($rows | Where-Object { $_.member -CEQ $members[$field] })
            if ($pair.Count -ne 1) {
                throw ('Expected exactly one authoritative TSV row for ' + $members[$field])
            }
            $expected = $pair[0].replacement
            if ($DriftNonselectedField -and $entry -eq 0 -and $field -eq 1) {
                $expected = 'UNEXPECTED-FIELD-VALUE'
            }
            $lines += (@('field', [string]$entry, $names[$field],
                         $pair[0].original, $expected) -join "`t")
        }
    }
    $lines += 'result=applied'
    $lines += 'END'
    return ,([string[]]$lines)
}

function New-WorkflowWindowsReadback {
    param(
        [string]$BaselinePath,
        [ValidateSet('original','seven')][string]$Mode,
        [string]$BootUtc
    )

    $rows = Import-Csv -LiteralPath $BaselinePath -Delimiter "`t"
    $names = @(
        'system_serial', 'system_vendor', 'system_product', 'board_serial',
        'board_vendor', 'board_product', 'system_uuid'
    )
    $members = @(
        'FakeSMBIOSSerial', 'FakeSMBIOSManufacturer', 'FakeSMBIOSProduct',
        'FakeMotherboardSerial', 'FakeMotherboardManufacturer',
        'FakeMotherboardProduct', 'FakeSMBIOSUUID'
    )
    $fields = [ordered]@{}
    for ($field = 0; $field -lt $names.Count; $field++) {
        $pair = @($rows | Where-Object { $_.member -CEQ $members[$field] })
        if ($pair.Count -ne 1) {
            throw ('Expected exactly one authoritative TSV row for ' + $members[$field])
        }
        $expected = if ($Mode -ceq 'seven') { $pair[0].replacement } else { $pair[0].original }
        $fields[$names[$field]] = [ordered]@{
            originalKnown = $pair[0].original
            expected = $expected
            observedNative = $expected
            observedWmi = $expected
            pass = $true
            nativeMatches = $true
            wmiMatches = $true
        }
    }
    return [ordered]@{
        hostFixture = $true
        fixtureKind = 'workflow-test-only; no host read was performed'
        schema = 'smbios-windows-readback.v1'
        result = 'PASS'
        mode = $Mode
        bootIdentity = [ordered]@{ approximateBootUtc = $BootUtc }
        fields = $fields
    }
}

function Write-WorkflowReport([string]$Path, [string[]]$Lines) {
    [IO.File]::WriteAllLines($Path, $Lines, [Text.Encoding]::ASCII)
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function New-VariantReportPath([string]$Kind, [string]$Extension = 'json') {
    $name = $Kind + '.' + [Guid]::NewGuid().ToString('N') + '.' + $Extension
    $global:WorkflowProofPathCalls++
    $global:WorkflowProofOutputPath = Join-Path $global:WorkflowReportRoot $name
    return $global:WorkflowProofOutputPath
}

function Invoke-WorkflowReportRejection([string]$Path, [string]$BuildId) {
    Assert-WorkflowThrows {
        Assert-VariantBootReport -Path $Path -BuildId $BuildId
    } ('Expected report rejection for ' + [IO.Path]::GetFileName($Path))
}

$worktreePackage = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $worktreePackage '..\..\..\..'))
$mainPackage = Join-Path $repositoryRoot 'spoof-handoff'
$runProbePath = Join-Path $worktreePackage 'tools\smbios_boot\run-probe.ps1'
$authoritativeTsv = Join-Path $mainPackage 'reports\client-originals.tsv'
$unitBase = [IO.Path]::GetFullPath((Join-Path $worktreePackage '.build\smbios-seven\workflow-unit'))
$unitRoot = Join-Path $unitBase ([Guid]::NewGuid().ToString('N'))
$unitRootFull = [IO.Path]::GetFullPath($unitRoot)
$pathPrefix = $unitBase.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$passed = 0
$total = 30

Assert-WorkflowCondition (Test-Path -LiteralPath $runProbePath -PathType Leaf) 'run-probe.ps1 was not found.'
Assert-WorkflowCondition (Test-Path -LiteralPath $authoritativeTsv -PathType Leaf) 'Main-package client-originals.tsv was not found.'
Assert-WorkflowCondition ($unitRootFull.StartsWith($pathPrefix, [StringComparison]::OrdinalIgnoreCase)) 'Workflow fixture path escaped its .build directory.'

try {
    [void][IO.Directory]::CreateDirectory($unitRootFull)
    $global:WorkflowFakeEspRoot = Join-Path $unitRootFull 'esp-empty'
    [void][IO.Directory]::CreateDirectory($global:WorkflowFakeEspRoot)
    $reportRoot = Join-Path $unitRootFull 'reports'
    [void][IO.Directory]::CreateDirectory($reportRoot)
    $global:WorkflowReportRoot = $reportRoot

    # Parse the parent script, then evaluate only selected functions and path expressions.
    # Its top-level mode dispatcher and all device-facing operations remain unexecuted.
    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile(
        $runProbePath, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) {
        throw ('run-probe.ps1 parse failed: ' + $parseErrors[0].Message)
    }
    $signedAssignmentNodes = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
            $node.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
            $node.Left.VariablePath.UserPath -CEQ 'variantSigned'
    }, $true))
    if ($signedAssignmentNodes.Count -ne 1) {
        throw 'Expected exactly one variantSigned assignment in run-probe.ps1.'
    }

    # Evaluate only the actual signed-path assignment with its actual worktree root.
    # No parent-script body, signer, firmware helper, or device operation is run.
    $variantWorktree = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $runProbePath) '..\..\..'))
    $variantPackage = $mainPackage
    foreach ($assignmentName in @('variantArtifacts', 'variantReports', 'variantInventory')) {
        $assignmentNodes = @($ast.FindAll({
            param($node)
            $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                $node.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                $node.Left.VariablePath.UserPath -CEQ $assignmentName
        }, $true))
        if ($assignmentNodes.Count -ne 1) {
            throw ('Expected exactly one ' + $assignmentName + ' assignment in run-probe.ps1.')
        }
        Invoke-Expression ('$' + $assignmentName + ' = ' + $assignmentNodes[0].Right.Extent.Text)
    }
    Invoke-Expression ('$variantSigned = ' + $signedAssignmentNodes[0].Right.Extent.Text)
    $sevenFieldsParameters = @($ast.ParamBlock.Parameters | Where-Object {
        $_.Name.VariablePath.UserPath -CEQ 'SevenFields'
    })
    Assert-WorkflowCondition ($sevenFieldsParameters.Count -eq 1 -and
        $sevenFieldsParameters[0].StaticType -eq [System.Management.Automation.SwitchParameter]) 'run-probe.ps1 must expose the SevenFields switch for the new launcher.'
    Assert-WorkflowCondition ([IO.Path]::GetFullPath($variantArtifacts) -ceq
        [IO.Path]::GetFullPath((Join-Path $mainPackage 'artifacts\smbios-seven-probe'))) 'The seven-field artifact root does not match the pinned path.'
    Assert-WorkflowCondition ([IO.Path]::GetFullPath($variantReports) -ceq
        [IO.Path]::GetFullPath((Join-Path $mainPackage 'reports\smbios-boot-20261013\seven-fields'))) 'The seven-field report root does not match the pinned path.'
    Assert-WorkflowCondition ([IO.Path]::GetFullPath($variantInventory) -ceq
        [IO.Path]::GetFullPath((Join-Path $variantArtifacts 'inventory'))) 'The inventory path does not remain under the seven-field artifact root.'
    Assert-WorkflowCondition ([IO.Path]::GetFullPath($variantSigned) -ceq
        [IO.Path]::GetFullPath((Join-Path $variantWorktree 'spoof-handoff\artifacts\smbios-seven-probe\signed'))) 'The signed path does not remain in the seven-field worktree output.'
    $sevenFieldGate = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.IfStatementAst] -and
            @($node.Clauses | Where-Object {
                $_.Item1.Extent.Text -ceq '-not $SevenFields'
            }).Count -eq 1
    }, $true))
    $reportDirectoryCreates = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.CommandAst] -and
            $node.GetCommandName() -CEQ 'New-Item'
    }, $true))
    if ($sevenFieldGate.Count -ne 1 -or $reportDirectoryCreates.Count -lt 1 -or
        $sevenFieldGate[0].Extent.StartOffset -ge $reportDirectoryCreates[0].Extent.StartOffset) {
        throw 'The required SevenFields gate must precede report-directory creation.'
    }
    $gateHarness = [scriptblock]::Create(
        ('$SevenFields=$false' + "`r`n" + $sevenFieldGate[0].Extent.Text + "`r`n" + "throw 'Report directory creation was reached.'"))
    $oldLauncherFailure = $null
    try {
        & $gateHarness
    } catch {
        $oldLauncherFailure = $_.Exception.Message
    }
    Assert-WorkflowCondition ($oldLauncherFailure -like '*previous serial profile is closed*') 'An invocation missing SevenFields did not fail at the early profile gate.'
    $sevenReadbackCalls = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.CommandAst] -and
            $node.GetCommandName() -CEQ 'Invoke-VariantReadback' -and
            $node.Extent.Text -match "'seven'"
    }, $true))
    $sevenCompletionPhases = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
            $node.Right.Extent.Text -ceq "'seven-fields-proven-normal-boot-restore-required'"
    }, $true))
    Assert-WorkflowCondition ($sevenReadbackCalls.Count -eq 1 -and
        $sevenCompletionPhases.Count -eq 1) 'Collect must perform the seven-field Windows read-back and record its seven-field completion phase.'
    $collectorPath = Join-Path $repositoryRoot 'tools\efi_inventory\shim-chain-collector.ps1'
    $collectorTokens = $null
    $collectorErrors = $null
    $collectorAst = [System.Management.Automation.Language.Parser]::ParseFile(
        $collectorPath, [ref]$collectorTokens, [ref]$collectorErrors)
    if ($collectorErrors.Count -ne 0) {
        throw ('Collector parser failed: ' + $collectorErrors[0].Message)
    }
    $signedGuardDefinitions = @($collectorAst.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -CEQ 'Assert-SignedDirectoryTarget'
    }, $true))
    if ($signedGuardDefinitions.Count -ne 1) {
        throw 'Expected exactly one authoritative Assert-SignedDirectoryTarget definition.'
    }
    Invoke-Expression $signedGuardDefinitions[0].Extent.Text
    function Assert-NoReparseAncestors([string]$Path, [string]$Boundary) {
        $global:WorkflowNoReparseCalls += [pscustomobject]@{ Path = $Path; Boundary = $Boundary }
    }
    $script:RepoRoot = $repositoryRoot
    $script:SignedDirectory = $variantSigned
    $global:WorkflowNoReparseCalls = @()
    Assert-SignedDirectoryTarget
    Assert-WorkflowCondition ([IO.Path]::GetFullPath($script:SignedDirectory).StartsWith(
        [IO.Path]::GetFullPath((Join-Path $repositoryRoot '.codex-build')) + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) 'The actual variantSigned assignment is outside the collector .codex-build boundary.'
    Assert-WorkflowCondition ($global:WorkflowNoReparseCalls.Count -eq 1 -and
        $global:WorkflowNoReparseCalls[0].Path -ceq $script:SignedDirectory) 'The collector signed-path guard did not validate the assigned path ancestry.'
    $acceptedGuardCalls = $global:WorkflowNoReparseCalls.Count
    $script:SignedDirectory = Join-Path $mainPackage 'artifacts\smbios-seven-probe\signed'
    Assert-WorkflowThrows { Assert-SignedDirectoryTarget } 'The collector guard accepted the old main-package signing target.'
    Assert-WorkflowCondition ($global:WorkflowNoReparseCalls.Count -eq $acceptedGuardCalls) 'The rejected old path reached the ancestry check.'
    $passed++

    $prepareClauses = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.IfStatementAst] -and
            @($node.Clauses | Where-Object {
                $_.Item1.Extent.Text -match '^\s*\$variantMode\s*-eq\s*''Prepare''\s*$'
            }).Count -gt 0
    }, $true))
    if ($prepareClauses.Count -ne 1) {
        throw 'Expected one Prepare mode block in run-probe.ps1.'
    }
    $prepareClause = @($prepareClauses[0].Clauses | Where-Object {
        $_.Item1.Extent.Text -match '^\s*\$variantMode\s*-eq\s*''Prepare''\s*$'
    })[0]
    $prepareBlock = $prepareClause.Item2
    $preflightCalls = @($prepareBlock.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.CommandAst] -and
            $node.GetCommandName() -CEQ 'Assert-VariantPreparationInputs'
    }, $true))
    $snapshotCalls = @($prepareBlock.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.CommandAst] -and
            $node.GetCommandName() -CEQ 'Invoke-VariantChild' -and
            $node.Extent.Text.Contains('snapshot-reference.ps1')
    }, $true))
    $signCalls = @($prepareBlock.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.CommandAst] -and
            $node.GetCommandName() -CEQ 'Invoke-VariantChild' -and
            $node.Extent.Text -match "@\(\s*'Sign'"
    }, $true))
    $readbackCalls = @($prepareBlock.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.CommandAst] -and
            $node.GetCommandName() -CEQ 'Invoke-VariantReadback'
    }, $true))
    if ($preflightCalls.Count -ne 1 -or $snapshotCalls.Count -ne 1 -or
        $signCalls.Count -ne 1 -or $readbackCalls.Count -lt 1) {
        throw 'Prepare must contain one preparation preflight, snapshot, Sign, and original read-back call.'
    }
    $preflightOffset = $preflightCalls[0].Extent.StartOffset
    foreach ($laterCall in @($readbackCalls[0], $snapshotCalls[0], $signCalls[0])) {
        if ($preflightOffset -ge $laterCall.Extent.StartOffset) {
            throw 'Preparation input checks must run before read-back, snapshot, and Sign.'
        }
    }
    $preflightDefinitions = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -CEQ 'Assert-VariantPreparationInputs'
    }, $true))
    if ($preflightDefinitions.Count -ne 1) {
        throw 'Expected one read-only Assert-VariantPreparationInputs definition.'
    }
    $passed++

    $saveDefinitions = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -CEQ 'Save-VariantState'
    }, $true))
    if ($saveDefinitions.Count -ne 1) {
        throw 'Expected exactly one actual Save-VariantState function in run-probe.ps1.'
    }
    $savedSaveMock = (Get-Command Save-VariantState -CommandType Function).ScriptBlock
    $actualStatePath = Join-Path $reportRoot 'actual-state-save.json'
    $script:variantStatePath = $actualStatePath
    try {
        Invoke-Expression $saveDefinitions[0].Extent.Text
        $stagingState = [pscustomobject]@{
            phase = 'staging-recovery-journal'
            readyToArm = $false
            marker = 'staging'
        }
        $readyState = [pscustomobject]@{
        phase = 'seven-fields-awaiting-user-boot'
            readyToArm = $true
            marker = 'ready'
        }
        $notReadyState = [pscustomobject]@{
            phase = 'seven-fields-awaiting-user-boot'
            readyToArm = $false
            marker = 'not-ready'
        }
        Save-VariantState $stagingState
        Save-VariantState $readyState
        Save-VariantState $notReadyState

        $latestState = Get-Content -LiteralPath $actualStatePath -Raw | ConvertFrom-Json
        Assert-WorkflowCondition ($latestState.phase -ceq 'seven-fields-awaiting-user-boot' -and
            -not $latestState.readyToArm -and $latestState.marker -ceq 'not-ready') 'Actual state save did not leave the latest state in the destination.'
        $backupFiles = @(Get-ChildItem -LiteralPath $reportRoot -File | Where-Object {
            $_.Name -match '^actual-state-save\.json\.[0-9a-f]{32}\.bak$'
        })
        Assert-WorkflowCondition ($backupFiles.Count -eq 2 -and
            @($backupFiles.Name | Select-Object -Unique).Count -eq 2) 'Repeated state saves did not preserve two uniquely named prior-state backups.'
        $backupMarkers = @($backupFiles | ForEach-Object {
            (Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json).marker
        } | Sort-Object)
        Assert-WorkflowCondition (($backupMarkers -join ',') -ceq 'ready,staging') 'Prior state backups did not preserve the staging and ready records.'
        Assert-WorkflowCondition (@(Get-ChildItem -LiteralPath $reportRoot -File | Where-Object {
            $_.Name -like 'actual-state-save.json.*.tmp'
        }).Count -eq 0) 'Actual state save left a temporary file behind.'
    } finally {
        Set-Item -LiteralPath Function:\Save-VariantState -Value $savedSaveMock
    }
    $passed++

    $completionDefinitions = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -CEQ 'Complete-VariantPreparation'
    }, $true))
    if ($completionDefinitions.Count -ne 1) {
        throw 'Expected exactly one actual Complete-VariantPreparation function in run-probe.ps1.'
    }
    function Invoke-CompletionWorkflowFixture {
        param(
            [string]$FunctionText,
            [psobject]$State,
            [bool]$FailStatus
        )

        $fixtureLines = @(
            'param([string]$FunctionText,[psobject]$State,[bool]$FailStatus)',
            '$context=[pscustomobject]@{events=@();childOperations=@();adminCount=0;baselineCount=0;mountCount=0;unmountCount=0;saveCount=0;readbackCount=0;savedPhase=$null;savedReady=$false;statusFailure=$FailStatus}',
            '$variantChain=''mock-chain'';$variantInventory=''mock-inventory'';$variantLoader=''mock-loader'';$variantSigned=''mock-signed'';$variantReportLeaf=''HypersinvSmbiosPatch.txt''',
            'function Assert-Admin {$context.adminCount++;$context.events+= ''admin''}',
            'function Assert-VariantBaseline($Snapshot) {$context.baselineCount++;$context.events+= ''baseline''}',
            'function New-VariantReportPath([string]$Kind,[string]$Extension=''json'') {return (Join-Path $global:WorkflowFakeEspRoot ($Kind+''.''+$Extension))}',
            'function Invoke-VariantChild([string]$Script,[string[]]$Arguments,[string]$Log) {$operation=$Arguments[0];$context.childOperations+= $operation;$context.events+= (''child:''+$operation);if($context.statusFailure -and $operation -ceq ''Status''){throw ''Injected Status child failure.''}}',
            'function Mount-Esp {$context.mountCount++;$context.events+= ''mount'';return [pscustomobject]@{EspRoot=$global:WorkflowFakeEspRoot}}',
            'function Dismount-Esp($Mount) {$context.unmountCount++;$context.events+= ''dismount''}',
            'function Invoke-VariantReadback([string]$Expected) {$context.readbackCount++;$context.events+= (''readback:''+$Expected);return ''mock-original-report''}',
            'function Save-VariantState($SavedState) {$context.saveCount++;$context.savedPhase=$SavedState.phase;$context.savedReady=[bool]$SavedState.readyToArm;$context.events+= ''save''}',
            '$ErrorActionPreference=''Stop''',
            '$null=Invoke-Expression $FunctionText',
            '$output=@();$errorText=$null;try{$output=@(Complete-VariantPreparation $State)}catch{$errorText=$_.Exception.Message}',
            '[pscustomobject]@{context=$context;state=$State;output=$output;errorText=$errorText}'
        )
        $fixture = [scriptblock]::Create(($fixtureLines -join "`r`n"))
        return & $fixture $FunctionText $State $FailStatus
    }

    $pendingPreparation = [pscustomobject]@{
        snapshot = [pscustomobject]@{ id = 'workflow-snapshot' }
        phase = 'staging-recovery-journal'
        armed = $false
        rebooted = $false
        readyToArm = $false
    }
    $completedPreparation = Invoke-CompletionWorkflowFixture `
        $completionDefinitions[0].Extent.Text $pendingPreparation $false
    Assert-WorkflowCondition ($null -eq $completedPreparation.errorText) ('Valid pending preparation did not finalize: ' + $completedPreparation.errorText)
    Assert-WorkflowCondition ($completedPreparation.state.phase -ceq 'seven-fields-awaiting-user-boot' -and
        $completedPreparation.state.readyToArm -and -not $completedPreparation.state.armed) 'Successful finalization did not mark the state ready and unarmed.'
    Assert-WorkflowCondition (($completedPreparation.context.childOperations -join ',') -ceq 'Status') 'Finalization invoked something besides Status.'
    Assert-WorkflowCondition ($completedPreparation.context.baselineCount -eq 2 -and
        $completedPreparation.context.mountCount -eq 1 -and $completedPreparation.context.unmountCount -eq 1 -and
        $completedPreparation.context.readbackCount -eq 1 -and $completedPreparation.context.saveCount -eq 1) 'Finalization omitted a baseline, empty-output check, original read-back, or state save.'
    Assert-WorkflowCondition (($completedPreparation.context.events -join ',') -ceq 'admin,baseline,child:Status,mount,dismount,readback:original,baseline,save') 'Finalization operations ran in an unexpected order.'
    Assert-WorkflowCondition ($completedPreparation.output -join '' -like '*SEVEN-FIELD PROBE STAGED*') 'Successful finalization did not report the staged seven-field state.'
    $passed++

    foreach ($outputLeaf in @(
        'HypersinvSmbiosPatch.txt',
        'HypersinvEfiInventory.txt',
        'EFI\HypersinvShimInventory\chain-status.txt',
        'EFI\HypersinvShimInventory\chain-diagnostic.txt'
    )) {
        $ownedOutputPath = Join-Path $global:WorkflowFakeEspRoot $outputLeaf
        $ownedOutputParent = Split-Path -Parent $ownedOutputPath
        [void][IO.Directory]::CreateDirectory($ownedOutputParent)
        [IO.File]::WriteAllText($ownedOutputPath, 'fixture-owned output')
        $outputBlockedState = [pscustomobject]@{
            snapshot = [pscustomobject]@{ id = 'workflow-snapshot' }
            phase = 'staging-recovery-journal'
            armed = $false
            rebooted = $false
            readyToArm = $false
        }
        $outputBlocked = Invoke-CompletionWorkflowFixture `
            $completionDefinitions[0].Extent.Text $outputBlockedState $false
        Assert-WorkflowCondition ($outputBlocked.errorText -like ('*Uncollected EFI output exists*' + $outputLeaf + '*')) ('Finalization failed to reject pre-existing output: ' + $outputLeaf)
        Assert-WorkflowCondition ($outputBlocked.state.phase -ceq 'staging-recovery-journal' -and
            -not $outputBlocked.state.readyToArm -and $outputBlocked.context.saveCount -eq 0) ('Pre-existing output finalized state for: ' + $outputLeaf)
        Assert-WorkflowCondition ($outputBlocked.context.childOperations -join ',' -ceq 'Status' -and
            $outputBlocked.context.mountCount -eq 1 -and $outputBlocked.context.unmountCount -eq 1 -and
            $outputBlocked.context.readbackCount -eq 0) ('Output refusal did not stop and dismount before original read-back: ' + $outputLeaf)
        Remove-Item -LiteralPath $ownedOutputPath -Force
    }
    $passed++

    $statusFailurePreparation = [pscustomobject]@{
        snapshot = [pscustomobject]@{ id = 'workflow-snapshot' }
        phase = 'staging-recovery-journal'
        armed = $false
        rebooted = $false
        readyToArm = $false
    }
    $statusFailure = Invoke-CompletionWorkflowFixture `
        $completionDefinitions[0].Extent.Text $statusFailurePreparation $true
    Assert-WorkflowCondition ($statusFailure.errorText -ceq 'Injected Status child failure.') 'Status failure was not propagated.'
    Assert-WorkflowCondition ($statusFailure.state.phase -ceq 'staging-recovery-journal' -and
        -not $statusFailure.state.readyToArm -and $statusFailure.context.saveCount -eq 0) 'Status failure incorrectly finalized or saved the state.'
    Assert-WorkflowCondition (($statusFailure.context.childOperations -join ',') -ceq 'Status' -and
        $statusFailure.context.mountCount -eq 0 -and $statusFailure.context.readbackCount -eq 0) 'Status failure proceeded into later finalization steps.'
    $passed++

    foreach ($blockedState in @(
        [pscustomobject]@{ snapshot = [pscustomobject]@{ id = 'workflow-snapshot' }; phase = 'seven-fields-awaiting-user-boot'; armed = $false; rebooted = $false; readyToArm = $true },
        [pscustomobject]@{ snapshot = [pscustomobject]@{ id = 'workflow-snapshot' }; phase = 'staging-recovery-journal'; armed = $true; rebooted = $false; readyToArm = $false }
    )) {
        $blockedPreparation = Invoke-CompletionWorkflowFixture `
            $completionDefinitions[0].Extent.Text $blockedState $false
        Assert-WorkflowCondition ($blockedPreparation.errorText -like '*Only an unarmed, incomplete preparation journal can be finalized.*') 'Already-ready or armed state was not rejected.'
        Assert-WorkflowCondition (($blockedPreparation.context.events -join ',') -ceq 'admin' -and
            $blockedPreparation.context.baselineCount -eq 0 -and $blockedPreparation.context.childOperations.Count -eq 0 -and
            $blockedPreparation.context.mountCount -eq 0 -and $blockedPreparation.context.readbackCount -eq 0 -and
            $blockedPreparation.context.saveCount -eq 0) 'Invalid journal reached verification, ESP, or save operations.'
    }
    $passed++

    $rollbackClearDefinitions = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -CEQ 'Clear-VariantRollbackBootState'
    }, $true))
    if ($rollbackClearDefinitions.Count -ne 1) {
        throw 'Expected exactly one actual Clear-VariantRollbackBootState function in run-probe.ps1.'
    }
    function Invoke-RollbackBootFixture {
        param(
            [string]$FunctionText,
            [psobject]$Options
        )

        $fixtureLines = @(
            'param([string]$FunctionText,[psobject]$Options)',
            '$context=[pscustomobject]@{events=@();hashCount=0;baselineCount=0;mountCount=0;dismountCount=0;installerArgs=@();exitCode=[int]$Options.exitCode;options=$Options;logPath=$null}',
            '$ErrorActionPreference=''Stop''',
            'function New-VariantReportPath([string]$Kind,[string]$Extension=''json'') {$context.logPath=Join-Path $global:WorkflowReportRoot ($Kind+''.''+$Extension);$context.events+= ''report-path'';return $context.logPath}',
            'function Mount-Esp {$context.mountCount++;$context.events+= ''mount'';return [pscustomobject]@{EspRoot=$global:WorkflowFakeEspRoot;VolumeGuid=''fixture-volume-guid''}}',
            'function Dismount-Esp($Mount) {$context.dismountCount++;$context.events+= ''dismount''}',
            'function Get-BootManagerHash([string]$Path) {$context.hashCount++;$context.events+= ''boot-hash'';if($context.options.hashDrift -and $context.hashCount -gt 1){return ''changed-hash''};return ''original-hash''}',
            'function Get-InstallTool {$context.events+= ''get-installer'';return ''Invoke-MockRollbackInstaller''}',
            'function Invoke-MockRollbackInstaller {$context.events+= ''installer-call'';$context.installerArgs=@($args|ForEach-Object{[string]$_});foreach($line in $context.options.stdout){Write-Output $line};foreach($line in $context.options.stderr){Write-Error -Message $line -ErrorAction Continue};$global:LASTEXITCODE=$context.exitCode}',
            'function Assert-VariantBaseline([string]$Snapshot) {$context.baselineCount++;$context.events+= ''baseline'';if($context.options.baselineDrift){throw ''Injected baseline drift.''}}',
            '$null=Invoke-Expression $FunctionText',
            '$global:LASTEXITCODE=0;$output=@();$errorText=$null;try{$output=@(Clear-VariantRollbackBootState ''fixture-snapshot'')}catch{$errorText=$_.Exception.Message}',
            '[pscustomobject]@{context=$context;output=$output;errorText=$errorText}'
        )
        $fixture = [scriptblock]::Create(($fixtureLines -join "`r`n"))
        return & $fixture $FunctionText $Options
    }

    $rollbackSuccessOptions = [pscustomobject]@{
        exitCode = 0
        stdout = @('installer cleanup completed')
        stderr = @('native-like installer stderr')
        hashDrift = $false
        baselineDrift = $false
    }
    $rollbackSuccess = Invoke-RollbackBootFixture `
        $rollbackClearDefinitions[0].Extent.Text $rollbackSuccessOptions
    Assert-WorkflowCondition ($null -eq $rollbackSuccess.errorText -and
        ($rollbackSuccess.output -join '') -like '*Owned one-time boot state cleared*') 'Successful owned BootNext cleanup did not pass.'
    Assert-WorkflowCondition (($rollbackSuccess.context.installerArgs -join '|') -ceq 'oneshot|--mode|off|--profile|shim-inventory|--esp-volume|fixture-volume-guid') 'Rollback cleanup called ib-install with unexpected arguments.'
    Assert-WorkflowCondition ($rollbackSuccess.context.hashCount -eq 2 -and
        $rollbackSuccess.context.dismountCount -eq 1 -and $rollbackSuccess.context.baselineCount -eq 1) 'Successful cleanup skipped before/after Boot Manager verification or baseline verification.'
    Assert-WorkflowCondition (($rollbackSuccess.context.events -join ',') -ceq 'report-path,mount,boot-hash,get-installer,installer-call,boot-hash,dismount,baseline') 'Rollback cleanup actions ran in an unexpected order.'
    $successLog = [IO.File]::ReadAllText($rollbackSuccess.context.logPath)
    Assert-WorkflowCondition ($successLog.Contains('installer cleanup completed') -and
        $successLog.Contains('native-like installer stderr') -and $successLog.Contains('EXIT=0')) 'Rollback log omitted installer stdout, ErrorRecord stderr, or exit status.'
    $passed++

    $alreadyAbsentOptions = [pscustomobject]@{
        exitCode = 1
        stdout = @('BootNext cleanup failed: owned Boot#### entry was not found')
        stderr = @()
        hashDrift = $false
        baselineDrift = $false
    }
    $alreadyAbsent = Invoke-RollbackBootFixture `
        $rollbackClearDefinitions[0].Extent.Text $alreadyAbsentOptions
    Assert-WorkflowCondition ($null -eq $alreadyAbsent.errorText -and
        ($alreadyAbsent.output -join '') -like '*Owned one-time boot state already absent*') 'Exact already-absent cleanup result was not accepted.'
    Assert-WorkflowCondition ($alreadyAbsent.context.hashCount -eq 2 -and
        $alreadyAbsent.context.baselineCount -eq 1 -and $alreadyAbsent.context.dismountCount -eq 1) 'Already-absent result skipped Boot Manager or baseline verification.'
    $passed++

    foreach ($unsafeOutput in @(
        'BootNext read failed with access denied',
        'Owned Boot#### attributes do not match expected variable attributes',
        'Multiple owned Boot#### entries require manual review',
        'BootNext targets an option that was not found'
    )) {
        $unsafeOptions = [pscustomobject]@{
            exitCode = 1
            stdout = @($unsafeOutput)
            stderr = @()
            hashDrift = $false
            baselineDrift = $false
        }
        $unsafeResult = Invoke-RollbackBootFixture `
            $rollbackClearDefinitions[0].Extent.Text $unsafeOptions
        Assert-WorkflowCondition ($unsafeResult.errorText -like '*Owned one-time boot cleanup failed*') ('Unsafe cleanup result was accepted: ' + $unsafeOutput)
        Assert-WorkflowCondition ($unsafeResult.context.hashCount -eq 1 -and
            $unsafeResult.context.baselineCount -eq 0 -and $unsafeResult.context.dismountCount -eq 1) ('Unsafe cleanup result proceeded beyond fail-closed cleanup: ' + $unsafeOutput)
    }
    $passed++

    $wrongExitOptions = [pscustomobject]@{
        exitCode = 2
        stdout = @('BootNext cleanup failed: owned Boot#### entry was not found')
        stderr = @()
        hashDrift = $false
        baselineDrift = $false
    }
    $wrongExit = Invoke-RollbackBootFixture `
        $rollbackClearDefinitions[0].Extent.Text $wrongExitOptions
    Assert-WorkflowCondition ($wrongExit.errorText -like '*Owned one-time boot cleanup failed*' -and
        $wrongExit.context.hashCount -eq 1 -and $wrongExit.context.baselineCount -eq 0) 'Exact absent text with a non-absent exit code was accepted.'
    $passed++

    $extraOutputOptions = [pscustomobject]@{
        exitCode = 1
        stdout = @('BootNext cleanup failed: owned Boot#### entry was not found','additional installer output')
        stderr = @()
        hashDrift = $false
        baselineDrift = $false
    }
    $extraOutput = Invoke-RollbackBootFixture `
        $rollbackClearDefinitions[0].Extent.Text $extraOutputOptions
    Assert-WorkflowCondition ($extraOutput.errorText -like '*Owned one-time boot cleanup failed*' -and
        $extraOutput.context.hashCount -eq 1 -and $extraOutput.context.baselineCount -eq 0) 'Extra output alongside the absent line was accepted.'
    $passed++

    $bootManagerDriftOptions = [pscustomobject]@{
        exitCode = 0
        stdout = @()
        stderr = @()
        hashDrift = $true
        baselineDrift = $false
    }
    $bootManagerDrift = Invoke-RollbackBootFixture `
        $rollbackClearDefinitions[0].Extent.Text $bootManagerDriftOptions
    Assert-WorkflowCondition ($bootManagerDrift.errorText -like '*Windows Boot Manager changed during rollback cleanup*' -and
        $bootManagerDrift.context.hashCount -eq 2 -and $bootManagerDrift.context.baselineCount -eq 0 -and
        $bootManagerDrift.context.dismountCount -eq 1) 'Windows Boot Manager drift did not stop rollback before baseline verification.'
    $passed++

    $baselineDriftOptions = [pscustomobject]@{
        exitCode = 0
        stdout = @()
        stderr = @()
        hashDrift = $false
        baselineDrift = $true
    }
    $baselineDrift = Invoke-RollbackBootFixture `
        $rollbackClearDefinitions[0].Extent.Text $baselineDriftOptions
    Assert-WorkflowCondition ($baselineDrift.errorText -ceq 'Injected baseline drift.' -and
        $baselineDrift.context.hashCount -eq 2 -and $baselineDrift.context.baselineCount -eq 1 -and
        $baselineDrift.context.dismountCount -eq 1) 'Firmware baseline drift after cleanup was not propagated.'
    $passed++

    foreach ($name in @('Invoke-VariantArm', 'Assert-VariantBootReport', 'Save-VariantRoundTripProof')) {
        $definitions = @($ast.FindAll({
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -CEQ $name
        }, $true))
        if ($definitions.Count -ne 1) {
            throw ('Expected one AST definition for ' + $name)
        }
        Invoke-Expression $definitions[0].Extent.Text
    }
    $childDefinitions = @($ast.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -CEQ 'Invoke-VariantChild'
    }, $true))
    $trapDefinitions = @($ast.FindAll({
        param($node)
        $node.GetType().Name -CEQ 'TrapStatementAst'
    }, $true))
    if ($childDefinitions.Count -ne 1 -or $trapDefinitions.Count -ne 1) {
        throw 'Expected exactly one child invocation function and one trap AST.'
    }
    $childFunctionText = $childDefinitions[0].Extent.Text
    $trapText = $trapDefinitions[0].Extent.Text

    $global:variantPackage = $mainPackage
    $global:variantReportLeaf = 'collector-report.txt'
    $global:variantChain = 'Invoke-MockChain'
    $global:variantInventory = 'mock-inventory-build'
    $global:variantLoader = 'mock-loader-build'
    $global:variantSigned = 'mock-signed-directory'

    Reset-WorkflowMocks
    $normalState = New-WorkflowState
    Invoke-VariantArm $normalState
    Assert-WorkflowCondition $global:WorkflowArmIntentProof 'Intent state was not saved before the mock core arm.'
    Assert-WorkflowCondition (-not $global:WorkflowArmBootNextBefore) 'Mock BootNext was already set before core arm.'
    Assert-WorkflowCondition $global:WorkflowMockBootNext 'Successful arm did not set mock BootNext.'
    Assert-WorkflowCondition ($normalState.armed -and $normalState.phase -ceq 'armed-by-user-awaiting-new-boot') 'Successful arm state was not saved as armed.'
    Assert-WorkflowCondition ($global:WorkflowSavedStates.Count -eq 2 -and -not $global:WorkflowSavedStates[0].armed -and $global:WorkflowSavedStates[0].phase -ceq 'arm-pending' -and $global:WorkflowSavedStates[1].armed) 'Intent and armed state saves were not both recorded.'
    Assert-WorkflowCondition ($global:WorkflowEvents.IndexOf('save:arm-pending') -lt $global:WorkflowEvents.IndexOf('chain:Arm')) 'Core arm ran before the intent journal save.'
    Assert-WorkflowCondition ($global:WorkflowUnmountCount -eq 1 -and $global:WorkflowBaselineCount -eq 1) 'Normal arm did not complete the expected mock preflight.'
    $passed++

    Reset-WorkflowMocks
    $global:WorkflowArmedSaveFailure = $true
    $saveFailureState = New-WorkflowState
    $saveFailureText = $null
    try {
        Invoke-VariantArm $saveFailureState
    } catch {
        $saveFailureText = $_.Exception.Message
    }
    Assert-WorkflowCondition ($saveFailureText -like '*Injected post-arm state-save failure*') 'Post-arm journal failure was not propagated.'
    Assert-WorkflowCondition (($global:WorkflowChainOperations -join ',') -ceq 'Arm,Disarm') 'Post-arm state-save failure did not invoke owned disarm.'
    Assert-WorkflowCondition (-not $global:WorkflowMockBootNext -and -not $saveFailureState.armed) 'Post-arm save failure left the mock chain armed.'
    Assert-WorkflowCondition ($saveFailureState.phase -ceq 'seven-fields-awaiting-user-boot') 'Post-arm save failure did not restore the unarmed phase.'
    Assert-WorkflowCondition ($global:WorkflowBaselineCount -eq 2) 'Post-arm save failure did not verify the baseline after disarm.'
    Assert-WorkflowCondition (-not $global:WorkflowSavedStates[-1].armed -and $global:WorkflowSavedStates[-1].phase -ceq 'seven-fields-awaiting-user-boot') 'Recovery journal did not record the unarmed state.'
    $passed++

    Reset-WorkflowMocks
    $global:WorkflowCoreArmFailure = $true
    $coreFailureState = New-WorkflowState
    $coreFailureText = $null
    try {
        Invoke-VariantArm $coreFailureState
    } catch {
        $coreFailureText = $_.Exception.Message
    }
    Assert-WorkflowCondition ($coreFailureText -ceq 'Arm failed.') 'Core arm failure was not propagated.'
    Assert-WorkflowCondition (($global:WorkflowChainOperations -join ',') -ceq 'Arm,Disarm') 'Core arm failure did not invoke owned disarm.'
    Assert-WorkflowCondition (-not $global:WorkflowMockBootNext -and -not $coreFailureState.armed) 'Core arm failure left the mock chain armed.'
    Assert-WorkflowCondition ($coreFailureState.phase -ceq 'seven-fields-awaiting-user-boot' -and $global:WorkflowBaselineCount -eq 2) 'Core arm failure did not return to the checked unarmed baseline.'
    $passed++

    Reset-WorkflowMocks
    $unreadyState = New-WorkflowState -ReadyToArm $false
    Assert-WorkflowThrows { Invoke-VariantArm $unreadyState } 'Unready state unexpectedly reached the arm flow.'
    Assert-WorkflowCondition ($global:WorkflowAdminCount -eq 1 -and $global:WorkflowChainOperations.Count -eq 0) 'Unready state did not block before the core arm.'
    Assert-WorkflowCondition ($global:WorkflowBaselineCount -eq 0 -and $global:WorkflowMountCount -eq 0) 'Unready state reached preflight side effects.'
    $passed++

    $buildId = '0123456789ABCDEF0123456789ABCDEF'
    $onePath = Join-Path $reportRoot 'valid-one-entry.txt'
    $twoPath = Join-Path $reportRoot 'valid-two-entry.txt'
    $oneLines = New-WorkflowReportLines -EntryCount 1 -BuildId $buildId
    Write-WorkflowReport $onePath $oneLines
    Assert-VariantBootReport -Path $onePath -BuildId $buildId
    $passed++

    $twoLines = New-WorkflowReportLines -EntryCount 2 -BuildId $buildId
    Write-WorkflowReport $twoPath $twoLines
    Assert-VariantBootReport -Path $twoPath -BuildId $buildId
    $passed++

    $wrongNoncePath = Join-Path $reportRoot 'wrong-build-id.txt'
    Write-WorkflowReport $wrongNoncePath $oneLines
    Invoke-WorkflowReportRejection $wrongNoncePath 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF'
    $passed++

    $truncatedPath = Join-Path $reportRoot 'truncated.txt'
    Write-WorkflowReport $truncatedPath ([string[]]$oneLines[0..($oneLines.Count - 2)])
    Invoke-WorkflowReportRejection $truncatedPath $buildId
    $passed++

    $malformedPath = Join-Path $reportRoot 'malformed-provenance.txt'
    $malformedLines = [string[]]$oneLines.Clone()
    $malformedLines[4] = "entry`t0`t4`tEfiRuntimeServicesData"
    Write-WorkflowReport $malformedPath $malformedLines
    Invoke-WorkflowReportRejection $malformedPath $buildId
    $passed++

    $driftPath = Join-Path $reportRoot 'nonselected-field-drift.txt'
    $driftLines = New-WorkflowReportLines -EntryCount 1 -BuildId $buildId -DriftNonselectedField
    Write-WorkflowReport $driftPath $driftLines
    Invoke-WorkflowReportRejection $driftPath $buildId
    $passed++

    $global:WorkflowProofPathCalls = 0
    $bootStart = [DateTime]::UtcNow.AddMinutes(-10)
    $originalReadbackPath = Join-Path $reportRoot 'fixture-original-readback.json'
    $spoofReadbackPath = Join-Path $reportRoot 'fixture-seven-readback.json'
    $restoredReadbackPath = Join-Path $reportRoot 'fixture-restored-readback.json'
    $originalFixture = New-WorkflowWindowsReadback -BaselinePath $authoritativeTsv `
        -Mode original -BootUtc $bootStart.ToString('o')
    $spoofFixture = New-WorkflowWindowsReadback -BaselinePath $authoritativeTsv `
        -Mode seven -BootUtc $bootStart.AddSeconds(10).ToString('o')
    $restoredFixture = New-WorkflowWindowsReadback -BaselinePath $authoritativeTsv `
        -Mode original -BootUtc $bootStart.AddSeconds(20).ToString('o')
    foreach ($fixture in @(
        @{ Path = $originalReadbackPath; Value = $originalFixture },
        @{ Path = $spoofReadbackPath; Value = $spoofFixture },
        @{ Path = $restoredReadbackPath; Value = $restoredFixture }
    )) {
        [IO.File]::WriteAllText($fixture.Path, ($fixture.Value | ConvertTo-Json -Depth 8),
            [Text.UTF8Encoding]::new($false))
    }
    $proofState = [pscustomobject]@{
        mask = 127
        allSevenLiveProven = $true
        originalReadback = $originalReadbackPath
        spoofReadback = $spoofReadbackPath
        bootReadback = $onePath
        buildId = $buildId
    }
    $proofPath = Save-VariantRoundTripProof $proofState $restoredReadbackPath
    $proof = Get-Content -LiteralPath $proofPath -Raw | ConvertFrom-Json
    Assert-WorkflowCondition ($proof.result -ceq 'PASS' -and $proof.mask -eq 127 -and
        @($proof.fields).Count -eq 7 -and @($proof.readbacks).Count -eq 3 -and
        $global:WorkflowProofPathCalls -eq 1) 'Valid seven-field round-trip fixtures did not produce a complete proof.'
    foreach ($fixturePath in @($originalReadbackPath, $spoofReadbackPath, $restoredReadbackPath)) {
        $fixtureValue = Get-Content -LiteralPath $fixturePath -Raw | ConvertFrom-Json
        Assert-WorkflowCondition ($fixtureValue.hostFixture -eq $true -and
            $fixtureValue.fixtureKind -ceq 'workflow-test-only; no host read was performed') 'Synthetic Windows read-back fixture was not clearly marked.'
    }
    for ($index = 0; $index -lt 3; $index++) {
        $fixturePath = @($originalReadbackPath, $spoofReadbackPath, $restoredReadbackPath)[$index]
        Assert-WorkflowCondition ($proof.readbacks[$index].sha256 -ceq (Get-Sha256 $fixturePath)) 'Round-trip proof did not record the real fixture SHA-256.'
    }
    $passed++

    $wrongFieldFixture = New-WorkflowWindowsReadback -BaselinePath $authoritativeTsv `
        -Mode seven -BootUtc $bootStart.AddSeconds(10).ToString('o')
    $wrongFieldFixture.fields.system_product.observedWmi = 'workflow-fixture-drift'
    [IO.File]::WriteAllText($spoofReadbackPath, ($wrongFieldFixture | ConvertTo-Json -Depth 8),
        [Text.UTF8Encoding]::new($false))
    $pathCallsBeforeReject = $global:WorkflowProofPathCalls
    Assert-WorkflowThrows { Save-VariantRoundTripProof $proofState $restoredReadbackPath } `
        'Round-trip proof accepted a mismatched field observation.'
    Assert-WorkflowCondition ($global:WorkflowProofPathCalls -eq $pathCallsBeforeReject) 'Rejected field evidence attempted to create a proof file.'
    $passed++

    $wrongModeFixture = New-WorkflowWindowsReadback -BaselinePath $authoritativeTsv `
        -Mode original -BootUtc $bootStart.AddSeconds(10).ToString('o')
    [IO.File]::WriteAllText($spoofReadbackPath, ($wrongModeFixture | ConvertTo-Json -Depth 8),
        [Text.UTF8Encoding]::new($false))
    $pathCallsBeforeReject = $global:WorkflowProofPathCalls
    Assert-WorkflowThrows { Save-VariantRoundTripProof $proofState $restoredReadbackPath } `
        'Round-trip proof accepted an incorrect middle read-back mode.'
    Assert-WorkflowCondition ($global:WorkflowProofPathCalls -eq $pathCallsBeforeReject) 'Rejected mode evidence attempted to create a proof file.'
    $passed++

    $sameBootFixture = New-WorkflowWindowsReadback -BaselinePath $authoritativeTsv `
        -Mode seven -BootUtc $bootStart.ToString('o')
    [IO.File]::WriteAllText($spoofReadbackPath, ($sameBootFixture | ConvertTo-Json -Depth 8),
        [Text.UTF8Encoding]::new($false))
    $pathCallsBeforeReject = $global:WorkflowProofPathCalls
    Assert-WorkflowThrows { Save-VariantRoundTripProof $proofState $restoredReadbackPath } `
        'Round-trip proof accepted read-backs without three sufficiently separated boots.'
    Assert-WorkflowCondition ($global:WorkflowProofPathCalls -eq $pathCallsBeforeReject) 'Rejected boot identity evidence attempted to create a proof file.'
    $passed++

    $childScriptPath = Join-Path $reportRoot 'silent-child-failure.ps1'
    $plainChildLog = Join-Path $reportRoot 'silent-child-plain.log'
    $trappedChildLog = $plainChildLog
    [IO.File]::WriteAllText($childScriptPath, "exit 37`r`n", [Text.Encoding]::ASCII)
    $global:variantPowerShell = (Get-Process -Id $PID).Path
    $harnessPrefix = @(
        'param([string]$PowerShellPath,[string]$ChildScript,[string]$LogPath)',
        '$ErrorActionPreference = ''Stop''',
        '$variantPowerShell = $PowerShellPath',
        '$variantExecutionLog = $LogPath'
    )
    $invokeLine = 'Invoke-VariantChild -Script $ChildScript -Arguments @() -Log $LogPath'
    $plainHarness = [scriptblock]::Create(
        (($harnessPrefix + @($childFunctionText) + @($invokeLine)) -join "`r`n"))
    $trappedHarness = [scriptblock]::Create(
        (($harnessPrefix + @($trapText, $childFunctionText) + @($invokeLine)) -join "`r`n"))

    $plainFailureMessage = $null
    $plainOutput = @()
    try {
        $plainOutput = @(& $plainHarness $global:variantPowerShell $childScriptPath $plainChildLog *>&1)
    } catch {
        $plainFailureMessage = $_.Exception.Message
    }
    Assert-WorkflowCondition (-not [string]::IsNullOrWhiteSpace($plainFailureMessage)) 'The silent child failure did not produce an error without the trap.'
    Assert-WorkflowCondition ($plainFailureMessage -match '37' -and $plainFailureMessage.Contains($childScriptPath)) 'The child exit code or script path was missing from the original error.'
    Assert-WorkflowCondition ($global:LASTEXITCODE -eq 37) 'The child nonzero exit code was not preserved.'
    Assert-WorkflowCondition (Test-Path -LiteralPath $plainChildLog -PathType Leaf) 'Invoke-VariantChild did not initialize a log for silent child failure.'
    Remove-Item -LiteralPath $plainChildLog -Force
    Assert-WorkflowCondition (-not (Test-Path -LiteralPath $plainChildLog)) 'Could not reset the isolated child log before trap verification.'

    $trappedFailureMessage = $null
    $trappedOutput = @()
    try {
        $trappedOutput = @(& $trappedHarness $global:variantPowerShell $childScriptPath $trappedChildLog *>&1)
    } catch {
        $trappedFailureMessage = $_.Exception.Message
    }
    Assert-WorkflowCondition ($trappedFailureMessage -ceq $plainFailureMessage) 'The actual run-probe trap changed or hid the original child failure message.'
    Assert-WorkflowCondition ($global:LASTEXITCODE -eq 37) 'The trapped child failure lost its nonzero exit code.'
    Assert-WorkflowCondition (Test-Path -LiteralPath $trappedChildLog -PathType Leaf) 'The actual trap/child function did not leave a log for silent failure.'
    $passed++

    $stderrChildPath = Join-Path $reportRoot 'stderr-child-failure.ps1'
    $stderrChildLog = Join-Path $reportRoot 'stderr-child-failure.log'
    [IO.File]::WriteAllText(
        $stderrChildPath,
        "[Console]::Error.WriteLine('injected snapshot failure Win32 5')`r`nexit 38`r`n",
        [Text.Encoding]::ASCII)
    $plainStderrMessage = $null
    try {
        $null = @(& $plainHarness $global:variantPowerShell $stderrChildPath $stderrChildLog *>&1)
    } catch {
        $plainStderrMessage = $_.Exception.Message
    }
    Assert-WorkflowCondition ($plainStderrMessage -like 'Step failed (38):*') 'Native stderr prematurely replaced the child exit failure.'
    Assert-WorkflowCondition (-not $plainStderrMessage.Contains('NativeCommandError')) 'Native stderr escaped as NativeCommandError before exit handling.'
    Assert-WorkflowCondition ($global:LASTEXITCODE -eq 38) 'Native stderr failure lost exit code 38.'
    Assert-WorkflowCondition (Test-Path -LiteralPath $stderrChildLog -PathType Leaf) 'Native stderr failure did not create its log.'
    $plainStderrLogText = [IO.File]::ReadAllText($stderrChildLog)
    Assert-WorkflowCondition ($plainStderrLogText.Contains('injected snapshot failure Win32 5') -and $plainStderrLogText.Contains('EXIT=38')) 'Native stderr text or exit marker was missing from the log.'

    Remove-Item -LiteralPath $stderrChildLog -Force
    $trappedStderrMessage = $null
    try {
        $null = @(& $trappedHarness $global:variantPowerShell $stderrChildPath $stderrChildLog *>&1)
    } catch {
        $trappedStderrMessage = $_.Exception.Message
    }
    Assert-WorkflowCondition ($trappedStderrMessage -ceq $plainStderrMessage) 'The trap changed the native-stderr child failure message.'
    Assert-WorkflowCondition ($global:LASTEXITCODE -eq 38) 'The trapped native-stderr failure lost exit code 38.'
    $trappedStderrLogText = [IO.File]::ReadAllText($stderrChildLog)
    Assert-WorkflowCondition ($trappedStderrLogText.Contains('injected snapshot failure Win32 5') -and $trappedStderrLogText.Contains('EXIT=38')) 'The trapped log omitted stderr or the child exit marker.'
    $passed++

    if ($passed -ne $total) {
        throw ('Workflow fixture count mismatch: ' + $passed + '/' + $total)
    }
    Write-Output ('workflow fixtures: ' + $passed + '/' + $total + ' passed')
} finally {
    if (Test-Path -LiteralPath $unitRootFull) {
        $resolvedUnit = [IO.Path]::GetFullPath($unitRootFull)
        if (-not $resolvedUnit.StartsWith($pathPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to clean a workflow fixture path outside .build.'
        }
        Remove-Item -LiteralPath $resolvedUnit -Recurse -Force
    }
}
