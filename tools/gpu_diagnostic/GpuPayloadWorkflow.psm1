Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:PayloadStateModulePath = Join-Path $PSScriptRoot 'GpuHostState.psm1'
$script:PayloadPlatformModulePath = Join-Path $PSScriptRoot 'GpuHostPlatform.psm1'
$script:GpuPayloadPreparationFiles = @(
    'tools\gpu_diagnostic\New-GpuPayloadVm.ps1',
    'tools\gpu_diagnostic\GpuHostState.psm1',
    'tools\host-runner\HostRunner.Core.psm1',
    'tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'
)
if (-not (Test-Path -LiteralPath $script:PayloadStateModulePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $script:PayloadPlatformModulePath -PathType Leaf)) {
    throw 'GPU payload workflow dependencies are missing.'
}
Import-Module -Name $script:PayloadStateModulePath -ErrorAction Stop

function Get-GpuPayloadWorkspaceRoot {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$SourceRoot)

    $source = [IO.Path]::GetFullPath($SourceRoot)
    $sourceParent = Split-Path -Parent $source
    $codexBuild = Split-Path -Parent $sourceParent
    if ((Split-Path -Leaf $sourceParent) -ieq 'worktrees' -and
        (Split-Path -Leaf $codexBuild) -ieq '.codex-build') {
        return Split-Path -Parent $codexBuild
    }
    return $source
}

function Get-GpuPayloadPaths {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [Parameter(Mandatory = $true)][string]$RunDirectory
    )

    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $run = [IO.Path]::GetFullPath($RunDirectory)
    $runId = Split-Path -Leaf $run
    if ($runId -cnotmatch '^gpu-payload-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{32}$') {
        throw 'RunDirectory must have a generated gpu-payload run identity.'
    }
    $shortId = $runId.Substring($runId.Length - 16)
    $stateRoot = Join-Path $workspace '.analysis\gpu-payload-diagnostic'
    $buildRoot = Join-Path (Join-Path $workspace '.codex-build\gb') $shortId
    $signedRoot = Join-Path $run 'signed'
    $controllerRuns = Join-Path (Join-Path $workspace '.codex-build') 'gpu-payload-vm'
    $controllerRun = Join-Path $controllerRuns $runId
    return [pscustomobject]@{
        WorkspaceRoot = $workspace
        StateRoot = $stateRoot
        RunsRoot = Join-Path $stateRoot 'runs'
        SelectorPath = Join-Path $stateRoot 'payload-prepared.json'
        ArchiveRoot = Join-Path $stateRoot 'archive'
        RunDirectory = $run
        RunId = $runId
        JournalPath = Join-Path $run 'journal.json'
        ActionsRoot = Join-Path $run 'actions'
        BuildRoot = $buildRoot
        HypervisorBuild = Join-Path $buildRoot 'h'
        LoaderBuild = Join-Path $buildRoot 'l'
        SignedDirectory = $signedRoot
        SignedPayload = Join-Path $signedRoot 'payload.efi'
        SignedLoader = Join-Path $signedRoot 'loader.efi'
        ControllerRunsRoot = $controllerRuns
        ControllerRunDirectory = $controllerRun
    }
}

function Test-GpuPayloadTransition {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $false)][AllowNull()][string]$Phase,
        [Parameter(Mandatory = $false)][AllowNull()][object]$InProgress
    )

    if ($Action -in @('Status', 'Report')) { return $true }
    if ($Action -ceq 'Archive' -and $null -ne $InProgress) {
        $interruptedAction = if ($InProgress -is [string]) { $InProgress } else { [string]$InProgress.Action }
        if ($interruptedAction -in @('Prepare', 'Build', 'Sign')) { return $Phase -in @('Prepared', 'Built', 'Signed') }
        if ($interruptedAction -ceq 'ValidateVm') {
            # This is only a necessary state-machine condition. The dispatcher
            # separately validates the saved controller result and cleanup proof.
            return -not [string]::IsNullOrWhiteSpace([string]$InProgress.ControllerRunDirectory)
        }
        if ($interruptedAction -ceq 'Archive') { return $true }
        return $false
    }
    if ($null -ne $InProgress -and -not (($InProgress -is [string]) -and $InProgress -ceq '')) { return $false }
    switch ($Action) {
        'Prepare' { return [string]::IsNullOrEmpty($Phase) -or $Phase -ceq 'New' }
        'Build' { return $Phase -ceq 'Prepared' }
        'Sign' { return $Phase -ceq 'Built' }
        'ValidateVm' { return $Phase -ceq 'Signed' }
        'Archive' { return $Phase -in @('Prepared', 'Built', 'Signed', 'Complete', 'Archived') }
        default { return $false }
    }
}

function Get-GpuPayloadNextPhase {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Action)

    $next = @{
        Prepare = 'Prepared'
        Build = 'Built'
        Sign = 'Signed'
        ValidateVm = 'Complete'
        Archive = 'Archived'
    }
    if (-not $next.ContainsKey($Action)) { throw "Unknown workflow action: $Action" }
    return [string]$next[$Action]
}

function Get-GpuPayloadGitSnapshot {
    param([Parameter(Mandatory = $true)][string]$SourceRoot)

    $source = [IO.Path]::GetFullPath($SourceRoot)
    $top = @(& git -C $source rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0) { throw 'SourceRoot is not inside a readable Git worktree.' }
    $topPath = [IO.Path]::GetFullPath(($top -join '').Trim())
    if (-not [string]::Equals($topPath, $source, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'SourceRoot must be the root of the diagnostic feature worktree.'
    }
    $branch = (@(& git -C $source branch --show-current 2>$null) -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or $branch -cne 'feat/smbios-boot-serial-probe') {
        throw 'Run the reviewed feat/smbios-boot-serial-probe worktree.'
    }
    $commit = (@(& git -C $source rev-parse HEAD 2>$null) -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -cnotmatch '^[0-9a-f]{40}$') {
        throw 'Cannot identify the diagnostic source revision.'
    }
    $changes = @(& git -C $source status --porcelain --untracked-files=all 2>$null)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect diagnostic source worktree status.' }
    if ($changes.Count -ne 0) { throw 'Source worktree must be clean before Prepare, Build, Sign, or ValidateVm.' }
    return [pscustomobject]@{ SourceRoot = $source; Branch = $branch; Commit = $commit }
}

function Get-GpuPayloadSourcePins {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$SourceRoot)

    $snapshot = Get-GpuPayloadGitSnapshot -SourceRoot $SourceRoot
    $relativeFiles = [ordered]@{
        WorkflowScript = 'tools\gpu_diagnostic\gpu-payload-test.ps1'
        WorkflowModule = 'tools\gpu_diagnostic\GpuPayloadWorkflow.psm1'
        StateModule = 'tools\gpu_diagnostic\GpuHostState.psm1'
        PlatformModule = 'tools\gpu_diagnostic\GpuHostPlatform.psm1'
        HypervisorCmake = 'components\hypervisor\CMakeLists.txt'
        HypervisorEfiCmake = 'components\hypervisor\src\efi\CMakeLists.txt'
        LoaderCmake = 'components\uefi-loader\CMakeLists.txt'
        HvscanScript = 'components\hypervisor\src\efi\tests\Test-EfiRuntimeScannerImage.ps1'
        VmController = 'components\hypervisor\setup\Test-GpuHalWindowsVm.ps1'
        VmGuestStartup = 'components\hypervisor\setup\guest\Invoke-GpuHalWindowsStartup.ps1'
        VmGuestAction = 'components\hypervisor\setup\guest\Invoke-GpuHalWindowsGuest.ps1'
        VmPreparationTool = 'tools\gpu_diagnostic\New-GpuPayloadVm.ps1'
        InstallerMain = 'components\uefi-loader\src\install\main.c'
        InstallerOneShot = 'components\uefi-loader\src\install\oneshot.c'
        InstallerBootNext = 'components\uefi-loader\src\install\bootnext.c'
        Certificate = 'components\hypervisor\hypersinv.cer'
        Shim = 'components\uefi-loader\src\vendor\shimx64.efi'
    }
    $pins = [ordered]@{}
    foreach ($name in $relativeFiles.Keys) {
        $path = Join-Path $snapshot.SourceRoot $relativeFiles[$name]
        $pins[$name] = Get-GpuFilePin -Path $path
    }
    return [pscustomobject]@{
        Commit = $snapshot.Commit
        Branch = $snapshot.Branch
        Files = [pscustomobject]$pins
    }
}

function Test-GpuPayloadPreparationCompatibility {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$PreparationCommit,
        [Parameter(Mandatory = $true)][string]$CurrentCommit
    )

    $source = [IO.Path]::GetFullPath($SourceRoot)
    if ($PreparationCommit -cnotmatch '^[0-9a-f]{40}$' -or $CurrentCommit -cnotmatch '^[0-9a-f]{40}$') {
        throw 'VM preparation compatibility requires full SHA-1 Git commit IDs.'
    }

    $null = & git -C $source merge-base --is-ancestor $PreparationCommit $CurrentCommit 2>$null
    $ancestorExit = $LASTEXITCODE
    if ($ancestorExit -eq 1) { throw 'VM preparation commit is not an ancestor of the current source commit.' }
    if ($ancestorExit -ne 0) { throw 'Git could not prove the VM preparation commit lineage.' }

    $fileProofs = [Collections.Generic.List[object]]::new()
    foreach ($relativePath in $script:GpuPayloadPreparationFiles) {
        $gitPath = $relativePath.Replace('\', '/')
        $preparationSpec = '{0}:{1}' -f $PreparationCommit, $gitPath
        $currentSpec = '{0}:{1}' -f $CurrentCommit, $gitPath
        $preparationBlob = (@(& git -C $source rev-parse $preparationSpec 2>$null) -join '').Trim()
        $preparationExit = $LASTEXITCODE
        $currentBlob = (@(& git -C $source rev-parse $currentSpec 2>$null) -join '').Trim()
        $currentExit = $LASTEXITCODE
        if ($preparationExit -ne 0 -or $currentExit -ne 0 -or
            $preparationBlob -cnotmatch '^[0-9a-f]{40}$' -or $currentBlob -cnotmatch '^[0-9a-f]{40}$') {
            throw "Git could not resolve preparation compatibility blob IDs for $relativePath."
        }
        if ($preparationBlob -cne $currentBlob) {
            throw "VM preparation code changed since the VM copy was created: $relativePath."
        }
        $fileProofs.Add([pscustomobject]@{
            Path = $relativePath
            PreparationBlob = $preparationBlob
            CurrentBlob = $currentBlob
            Identical = $true
        })
    }
    return [pscustomobject]@{
        Status = 'PREPARATION_CODE_COMPATIBLE'
        PreparationCommit = $PreparationCommit
        CurrentCommit = $CurrentCommit
        PreparationIsAncestor = $true
        Files = @($fileProofs)
    }
}

function Assert-GpuPayloadSourcePins {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][object]$ExpectedPins
    )

    $current = Get-GpuPayloadSourcePins -SourceRoot $SourceRoot
    if (-not [string]::Equals([string]$ExpectedPins.Commit, [string]$current.Commit, [StringComparison]::Ordinal)) {
        throw 'Source commit changed since Prepare.'
    }
    foreach ($property in $ExpectedPins.Files.PSObject.Properties) {
        $currentPin = $current.Files.PSObject.Properties[[string]$property.Name]
        if ($null -eq $currentPin) { throw "Source pin is no longer present: $($property.Name)" }
        Assert-GpuFilePin -Pin $property.Value
        if (-not [string]::Equals([string]$property.Value.Sha256, [string]$currentPin.Value.Sha256, [StringComparison]::OrdinalIgnoreCase) -or
            [long]$property.Value.Bytes -ne [long]$currentPin.Value.Bytes -or
            -not [string]::Equals([string]$property.Value.Path, [string]$currentPin.Value.Path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Source pin changed since Prepare: $($property.Name)"
        }
    }
    return $current
}

function Test-GpuPayloadVmxPath {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    try { return [IO.Path]::GetExtension($Path) -ieq '.vmx' }
    catch { return $false }
}

function Assert-GpuPayloadFreshOutput {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root
    )

    $rootPath = [IO.Path]::GetFullPath($Root)
    $output = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) { throw "Output root does not exist: $rootPath" }
    $parent = Split-Path -Parent $output
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { throw "Output parent does not exist: $parent" }
    $null = Assert-GpuPathWithin -Path $parent -Root $rootPath -AllowRoot -MustExist
    if (Test-Path -LiteralPath $output) { throw "Refusing to overwrite an existing output: $output" }
    return $output
}

function Get-GpuPayloadObjectProperty {
    param([AllowNull()][object]$Object, [Parameter(Mandatory = $true)][string]$Name)
    if ($null -eq $Object) { return $null }
    if ($Object -is [System.Collections.IDictionary]) {
        if ($Object.Contains($Name)) { return $Object[$Name] }
        return $null
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Assert-GpuPayloadArtifactPins {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][object]$Pins,
        [Parameter(Mandatory = $true)][object]$ExpectedPaths
    )

    $expectedProperties = if ($ExpectedPaths -is [System.Collections.IDictionary]) { $ExpectedPaths.Keys } else { @($ExpectedPaths.PSObject.Properties | ForEach-Object { $_.Name }) }
    foreach ($name in $expectedProperties) {
        $pin = Get-GpuPayloadObjectProperty -Object $Pins -Name ([string]$name)
        $expectedPath = [string](Get-GpuPayloadObjectProperty -Object $ExpectedPaths -Name ([string]$name))
        if ($null -eq $pin -or [string]::IsNullOrWhiteSpace($expectedPath)) { throw "Artifact pin is missing: $name" }
        Assert-GpuFilePin -Pin $pin
        if (-not [string]::Equals([IO.Path]::GetFullPath([string]$pin.Path), [IO.Path]::GetFullPath($expectedPath), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Artifact pin path differs from the expected output: $name"
        }
    }
}

function New-GpuPayloadRunId {
    return 'gpu-payload-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N')
}

function Assert-GpuPayloadState {
    param(
        [Parameter(Mandatory = $true)][object]$State,
        [Parameter(Mandatory = $true)][object]$Paths,
        [Parameter(Mandatory = $true)][string]$SourceRoot
    )

    if ($State.SchemaVersion -ne 1 -or $State.Workflow -cne 'gpu-payload-diagnostic') { throw 'Unsupported payload workflow journal schema.' }
    foreach ($name in @('RunId', 'WorkspaceRoot', 'SourceRoot', 'RunDirectory', 'JournalPath', 'Phase', 'InProgress', 'SourcePins', 'Paths', 'Actions', 'LastFailure')) {
        if ($null -eq $State.PSObject.Properties[$name]) { throw "Payload workflow journal lacks $name." }
    }
    if ($State.RunId -cne $Paths.RunId -or
        -not [string]::Equals([IO.Path]::GetFullPath([string]$State.RunDirectory), [string]$Paths.RunDirectory, [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals([IO.Path]::GetFullPath([string]$State.WorkspaceRoot), [string]$Paths.WorkspaceRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals([IO.Path]::GetFullPath([string]$State.SourceRoot), [IO.Path]::GetFullPath($SourceRoot), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Payload workflow journal identity does not match the selected run.'
    }
    $null = Assert-GpuPathWithin -Path $Paths.RunDirectory -Root $Paths.RunsRoot -MustExist
}

function Read-GpuPayloadSelectedRun {
    param(
        [Parameter(Mandatory = $true)][object]$Paths,
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [AllowNull()][string]$RunDirectory
    )

    $selectedDirectory = $RunDirectory
    if ([string]::IsNullOrWhiteSpace($selectedDirectory)) {
        if (-not (Test-Path -LiteralPath $Paths.SelectorPath -PathType Leaf)) { return $null }
        $selector = Read-GpuJson -Path $Paths.SelectorPath
        if ($selector.SchemaVersion -ne 1 -or [string]::IsNullOrWhiteSpace([string]$selector.RunDirectory)) { throw 'Payload run selector is malformed.' }
        $selectedDirectory = [string]$selector.RunDirectory
    }
    $selectedDirectory = [IO.Path]::GetFullPath($selectedDirectory)
    $selectedPaths = Get-GpuPayloadPaths -WorkspaceRoot $Paths.WorkspaceRoot -RunDirectory $selectedDirectory
    $null = Assert-GpuPathWithin -Path $selectedPaths.JournalPath -Root $selectedPaths.RunDirectory -MustExist
    $state = Read-GpuJson -Path $selectedPaths.JournalPath
    Assert-GpuPayloadState -State $state -Paths $selectedPaths -SourceRoot $SourceRoot
    return [pscustomobject]@{ State = $state; Paths = $selectedPaths }
}

function Get-GpuPayloadRunStatus {
    param([AllowNull()][object]$State, [AllowNull()][object]$Paths)
    if ($null -eq $State) { return 'NO_ACTIVE_RUN' }
    if ($null -ne $State.InProgress) { return 'IN_PROGRESS' }
    if ($State.Phase -ceq 'Archived') {
        if ($State.ArchiveStatus -in @('VALIDATED_ARCHIVED', 'ABORTED_ARCHIVED') -and
            $null -ne $Paths -and (Test-Path -LiteralPath (Join-Path $Paths.ArchiveRoot ($State.RunId + '.json')) -PathType Leaf)) {
            return [string]$State.ArchiveStatus
        }
        return 'BLOCKED'
    }
    if ($State.Phase -ceq 'Complete') {
        if (Test-GpuPayloadCompletionProof -State $State) { return 'COMPLETE' }
        return 'BLOCKED'
    }
    if ($null -ne $State.LastFailure) { return 'FAILED' }
    return 'READY'
}

function Test-GpuPayloadCompletionProof {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][object]$State)

    return ([string]$State.Phase -ceq 'Complete' -and (Test-GpuPayloadValidationProof -State $State))
}

function Test-GpuPayloadValidationProof {
    param([Parameter(Mandatory = $true)][object]$State)

    $validation = $State.Validation
    if ($null -eq $validation -or
        [string]$validation.Status -cne 'PASS' -or
        [string]$validation.Profile -cne 'SignedNative' -or
        $validation.VmStopped -isnot [bool] -or -not [bool]$validation.VmStopped -or
        $validation.RevertedToBefore -isnot [bool] -or -not [bool]$validation.RevertedToBefore -or
        [string]::IsNullOrWhiteSpace([string]$validation.SnapshotBefore) -or
        [string]::IsNullOrWhiteSpace([string]$validation.SnapshotAfter) -or
        @($validation.CleanupFailures).Count -ne 0 -or
        [string]::IsNullOrWhiteSpace([string]$validation.ControllerRunDirectory) -or
        [string]::IsNullOrWhiteSpace([string]$validation.ResultPath) -or
        $null -eq $validation.ResultPin) { return $false }
    try {
        $workspaceBuildRoot = Join-Path ([string]$State.WorkspaceRoot) '.codex-build\gpu-payload-vm'
        $controllerRun = [IO.Path]::GetFullPath([string]$validation.ControllerRunDirectory)
        $resultPath = [IO.Path]::GetFullPath([string]$validation.ResultPath)
        $null = Assert-GpuPathWithin -Path $controllerRun -Root $workspaceBuildRoot -MustExist
        $null = Assert-GpuPathWithin -Path $resultPath -Root $controllerRun -MustExist
        if ([IO.Path]::GetFileName($resultPath) -cne 'result.json') { return $false }
        Assert-GpuFilePin -Pin $validation.ResultPin
        $actualPin = Get-GpuFilePin -Path $resultPath
        if ([string]$actualPin.Sha256 -cne [string]$validation.ResultPin.Sha256 -or
            [long]$actualPin.Bytes -ne [long]$validation.ResultPin.Bytes) { return $false }
        $result = Read-GpuJson -Path $resultPath
        Assert-GpuPayloadVmResult -Result $result -ExpectedVmxPath ([string]$validation.VmxPath) `
            -ExpectedCertificateSha256 ([string]$State.Tools.Certificate.Sha256)
        if ([string]$result.RunId -cne [string]$validation.ControllerRunId -or
            [string]$result.Snapshots.Before -cne [string]$validation.SnapshotBefore -or
            [string]$result.Snapshots.After -cne [string]$validation.SnapshotAfter) { return $false }
    }
    catch { return $false }
    return $true
}

function New-GpuPayloadSummary {
    param(
        [Parameter(Mandatory = $true)][string]$Action,
        [AllowNull()][object]$State,
        [AllowNull()][object]$Paths
    )

    if ($null -eq $State) {
        return [pscustomobject]@{
            Action = $Action; Status = 'NO_ACTIVE_RUN'; QuerySucceeded = $true; Succeeded = $false; Phase = $null
            RunId = $null; RunDirectory = $null; InProgress = $null; LastFailure = $null
            Actions = @(); BuildArtifacts = $null; SignedArtifacts = $null; Validation = $null
        }
    }
    return [pscustomobject]@{
        Action = $Action
        Status = Get-GpuPayloadRunStatus -State $State -Paths $Paths
        QuerySucceeded = $true
        Succeeded = (Test-GpuPayloadCompletionProof -State $State)
        Phase = [string]$State.Phase
        RunId = [string]$State.RunId
        RunDirectory = [string]$State.RunDirectory
        JournalPath = [string]$Paths.JournalPath
        InProgress = $State.InProgress
        LastFailure = $State.LastFailure
        Actions = @($State.Actions)
        BuildArtifacts = $State.BuildArtifacts
        SignedArtifacts = $State.SignedArtifacts
        Validation = $State.Validation
    }
}

function New-GpuPayloadActionDirectory {
    param([object]$Paths, [string]$Action)
    return Join-Path $Paths.ActionsRoot ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + $Action + '-' + [Guid]::NewGuid().ToString('N'))
}

function Save-GpuPayloadState {
    param([object]$State, [object]$Paths)
    $State.UpdatedUtc = [DateTime]::UtcNow.ToString('o')
    Write-GpuJson -Path $Paths.JournalPath -Value $State
}

function Assert-GpuPayloadNativeSuccess {
    param([object]$Result, [string]$Description)
    if ($null -eq $Result -or -not [bool]$Result.Succeeded) {
        $detail = if ($null -ne $Result -and $Result.StderrPath -and (Test-Path -LiteralPath $Result.StderrPath -PathType Leaf)) {
            [IO.File]::ReadAllText([string]$Result.StderrPath)
        } else { 'No native command receipt was produced.' }
        throw "$Description failed. Evidence: $($Result.ReceiptPath). $detail"
    }
    return $Result
}

function Invoke-GpuPayloadPlatformPrivate {
    param(
        [Parameter(Mandatory = $true)][string]$FunctionName,
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$BoundArguments
    )

    $module = Import-Module -Name $script:PayloadPlatformModulePath -PassThru -ErrorAction Stop | Select-Object -Last 1
    return & $module {
        param($Name, $Root, $FunctionArguments)
        $script:CurrentSourceRoot = [IO.Path]::GetFullPath([string]$Root)
        $privateCommand = Get-Command -Name $Name -CommandType Function -ErrorAction Stop
        & $privateCommand @FunctionArguments
    } $FunctionName $SourceRoot $BoundArguments
}

function Invoke-GpuPayloadNativeCommand {
    param(
        [string]$Program, [string[]]$Arguments, [string]$ActionDirectory, [string]$Label
    )
    return Invoke-GpuPayloadPlatformPrivate -FunctionName 'Invoke-GpuNativeCommand' `
        -SourceRoot $script:CurrentSourceRoot -BoundArguments @{
            Program = $Program; Arguments = [string[]]$Arguments
            ActionDirectory = $ActionDirectory; Label = $Label
        }
}

function Invoke-GpuPayloadNative {
    param(
        [string]$Program, [string[]]$Arguments, [string]$ActionDirectory, [string]$Label
    )
    $result = Invoke-GpuPayloadPlatformPrivate -FunctionName 'Invoke-GpuNativeCommand' `
        -SourceRoot $script:CurrentSourceRoot -BoundArguments @{
            Program = $Program; Arguments = [string[]]$Arguments
            ActionDirectory = $ActionDirectory; Label = $Label
        }
    return Assert-GpuPayloadNativeSuccess -Result $result -Description $Label
}

function Invoke-GpuPayloadHvscan {
    param([string]$ImagePath, [string]$DumpbinPath, [string]$ActionDirectory, [string]$Label)

    $audit = Join-Path $script:CurrentSourceRoot 'components\hypervisor\src\efi\tests\Test-EfiRuntimeScannerImage.ps1'
    $powershell = (Get-Command -Name 'powershell.exe' -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $result = Invoke-GpuPayloadNative -Program $powershell -Arguments @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $audit,
        '-ImagePath', $ImagePath, '-DumpbinPath', $DumpbinPath, '-MaximumPages', '16'
    ) -ActionDirectory $ActionDirectory -Label $Label
    $output = [IO.File]::ReadAllText([string]$result.StdoutPath)
    if ($output -notmatch 'PASS: \.hvscan is self-contained') {
        throw "$Label did not report the required .hvscan self-containment PASS. Evidence: $($result.StdoutPath)"
    }
    return Get-GpuFilePin -Path $result.StdoutPath
}

function Get-GpuPayloadToolPaths {
    param([object]$Paths, [string]$SourceRoot)
    return [pscustomobject]@{
        Installer = Join-Path $Paths.LoaderBuild 'ib-install.exe'
        Probe = Join-Path $Paths.LoaderBuild 'ib-tcg-dump.exe'
        Verifier = Join-Path $Paths.LoaderBuild 'install_authenticode_test.exe'
        Shim = Join-Path $SourceRoot 'components\uefi-loader\src\vendor\shimx64.efi'
        Certificate = Join-Path $SourceRoot 'components\hypervisor\hypersinv.cer'
    }
}

function Get-GpuPayloadBuildArtifactPaths {
    param([object]$Paths)
    return [pscustomobject]@{
        HypervisorEfi = Join-Path $Paths.HypervisorBuild 'src\efi\uefi_hypervisor.efi'
        Loader = Join-Path $Paths.LoaderBuild 'loader.efi'
    }
}

function Assert-GpuPayloadPinsCurrent {
    param([object]$State, [object]$Paths)
    $null = Assert-GpuPayloadSourcePins -SourceRoot $State.SourceRoot -ExpectedPins $State.SourcePins
    if ($null -ne $State.BuildArtifacts) {
        $expected = Get-GpuPayloadBuildArtifactPaths -Paths $Paths
        Assert-GpuPayloadArtifactPins -Pins $State.BuildArtifacts -ExpectedPaths $expected
    }
    if ($null -ne $State.Tools) {
        $expectedTools = Get-GpuPayloadToolPaths -Paths $Paths -SourceRoot $State.SourceRoot
        Assert-GpuPayloadArtifactPins -Pins $State.Tools -ExpectedPaths $expectedTools
    }
    if ($null -ne $State.SignedArtifacts) {
        $expectedSigned = [pscustomobject]@{ Payload = $Paths.SignedPayload; Loader = $Paths.SignedLoader }
        Assert-GpuPayloadArtifactPins -Pins $State.SignedArtifacts -ExpectedPaths $expectedSigned
    }
}

function Invoke-GpuPayloadBuild {
    param([object]$State, [object]$Paths, [string]$ActionDirectory)

    $sourceRoot = [string]$State.SourceRoot
    $toolchain = Invoke-GpuPayloadPlatformPrivate -FunctionName 'Get-GpuFreshVsToolchain' `
        -SourceRoot $sourceRoot -BoundArguments @{ ActionDirectory = $ActionDirectory }
    $cmake = [string]$toolchain.Tools.'cmake.exe'
    $ctest = [string]$toolchain.Tools.'ctest.exe'
    $workspaceBuildRoot = Join-Path $Paths.WorkspaceRoot '.codex-build'
    if (-not (Test-Path -LiteralPath $workspaceBuildRoot -PathType Container)) {
        [void](New-Item -ItemType Directory -Path $workspaceBuildRoot -Force -ErrorAction Stop)
    }
    $null = Assert-GpuPathWithin -Path $workspaceBuildRoot -Root $Paths.WorkspaceRoot -MustExist
    $buildContainer = Join-Path $workspaceBuildRoot 'gb'
    if (-not (Test-Path -LiteralPath $buildContainer -PathType Container)) {
        [void](New-Item -ItemType Directory -Path $buildContainer -ErrorAction Stop)
    }
    $null = Assert-GpuPathWithin -Path $buildContainer -Root $workspaceBuildRoot -MustExist
    $null = Assert-GpuPayloadFreshOutput -Path $Paths.BuildRoot -Root $buildContainer
    [void](New-Item -ItemType Directory -Path $Paths.BuildRoot -ErrorAction Stop)
    foreach ($buildPath in @($Paths.HypervisorBuild, $Paths.LoaderBuild)) {
        Assert-GpuPayloadFreshOutput -Path $buildPath -Root $Paths.BuildRoot | Out-Null
    }
    $hypervisorSource = Join-Path $sourceRoot 'components\hypervisor'
    $loaderSource = Join-Path $sourceRoot 'components\uefi-loader'
    $configureHypervisor = Invoke-GpuPayloadNative -Program $cmake -Arguments @(
        '-S', $hypervisorSource, '-B', $Paths.HypervisorBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OBJECT_PATH_MAX=220',
        '-DHYPERSINV_BUILD_CORE=OFF', '-DHYPERSINV_BUILD_DRIVER=OFF',
        '-DHYPERSINV_BUILD_LOADER=OFF', '-DHYPERSINV_BUILD_UEFI=ON',
        '-DHYPERSINV_GPU_VM_SELFTEST=OFF', '-DHYPERSINV_GPU_DIAGNOSTIC_NATIVE=ON',
        '-DHYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED=ON'
    ) -ActionDirectory $ActionDirectory -Label 'cmake-configure-native-hypervisor'
    $buildHypervisor = Invoke-GpuPayloadNative -Program $cmake -Arguments @('--build', $Paths.HypervisorBuild, '--parallel', '4') `
        -ActionDirectory $ActionDirectory -Label 'cmake-build-native-hypervisor'
    $hypervisorLog = Join-Path $ActionDirectory 'hypervisor-ctest.log'
    $hypervisorCtest = Invoke-GpuPayloadNative -Program $ctest -Arguments @(
        '--test-dir', $Paths.HypervisorBuild, '--output-on-failure', '--no-tests=error', '--output-log', $hypervisorLog
    ) -ActionDirectory $ActionDirectory -Label 'ctest-native-hypervisor'
    $hypervisorArtifacts = Get-GpuPayloadBuildArtifactPaths -Paths $Paths
    $unsignedAudit = Invoke-GpuPayloadHvscan -ImagePath $hypervisorArtifacts.HypervisorEfi `
        -DumpbinPath ([string]$toolchain.Tools.'dumpbin.exe') -ActionDirectory $ActionDirectory -Label 'hvscan-native-unsigned'

    $configureLoader = Invoke-GpuPayloadNative -Program $cmake -Arguments @(
        '-S', $loaderSource, '-B', $Paths.LoaderBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OBJECT_PATH_MAX=220', '-DIB_STATIC_RUNTIME=ON',
        '-DIB_QEMU_DEBUG_PORTS=ON', '-DIB_PAYLOAD_REPLAY_INDEPENDENT=ON'
    ) -ActionDirectory $ActionDirectory -Label 'cmake-configure-loader-signed-vm'
    $buildLoader = Invoke-GpuPayloadNative -Program $cmake -Arguments @('--build', $Paths.LoaderBuild, '--parallel', '4') `
        -ActionDirectory $ActionDirectory -Label 'cmake-build-loader-signed-vm'
    $loaderLog = Join-Path $ActionDirectory 'loader-ctest.log'
    $loaderCtest = Invoke-GpuPayloadNative -Program $ctest -Arguments @(
        '--test-dir', $Paths.LoaderBuild, '--output-on-failure', '--no-tests=error', '--output-log', $loaderLog
    ) -ActionDirectory $ActionDirectory -Label 'ctest-loader'

    $artifactPaths = Get-GpuPayloadBuildArtifactPaths -Paths $Paths
    $toolPaths = Get-GpuPayloadToolPaths -Paths $Paths -SourceRoot $sourceRoot
    $buildPins = [pscustomobject]@{
        HypervisorEfi = Get-GpuFilePin -Path $artifactPaths.HypervisorEfi
        Loader = Get-GpuFilePin -Path $artifactPaths.Loader
    }
    $toolPins = [ordered]@{}
    foreach ($name in @('Installer', 'Probe', 'Verifier', 'Shim', 'Certificate')) {
        $toolPins[$name] = Get-GpuFilePin -Path $toolPaths.$name
    }
    $State.BuildArtifacts = $buildPins
    $State.Tools = [pscustomobject]$toolPins
    $State.BuildEvidence = [pscustomobject]@{
        HypervisorConfigureReceipt = $configureHypervisor.ReceiptPath
        HypervisorBuildReceipt = $buildHypervisor.ReceiptPath
        HypervisorCtestReceipt = $hypervisorCtest.ReceiptPath
        HypervisorCtestLog = Get-GpuFilePin -Path $hypervisorLog
        HvscanUnsigned = $unsignedAudit
        LoaderConfigureReceipt = $configureLoader.ReceiptPath
        LoaderBuildReceipt = $buildLoader.ReceiptPath
        LoaderCtestReceipt = $loaderCtest.ReceiptPath
        LoaderCtestLog = Get-GpuFilePin -Path $loaderLog
        Toolchain = $toolchain
        BuildOptions = [pscustomobject]@{
            NativeDiagnostic = $true
            OutputConfirmed = $true
            VmSelftest = $false
            StaticRuntime = $true
            LoaderReplayIndependent = $true
            LoaderDebugPorts = $true
            LoaderDebugPortsNote = 'VM diagnostic artifact only; this differs from the previous physical loader build, which used debug ports off.'
        }
    }
    return [pscustomobject]@{ Status = 'BUILD_AND_TESTS_PASS'; BuildArtifacts = $buildPins; Tools = $State.Tools; BuildEvidence = $State.BuildEvidence }
}

function Invoke-GpuPayloadSign {
    param([object]$State, [object]$Paths, [string]$ActionDirectory)

    Assert-GpuPayloadPinsCurrent -State $State -Paths $Paths
    $sourceRoot = [string]$State.SourceRoot
    $toolchain = Invoke-GpuPayloadPlatformPrivate -FunctionName 'Get-GpuFreshVsToolchain' `
        -SourceRoot $sourceRoot -BoundArguments @{ ActionDirectory = $ActionDirectory }
    $null = Assert-GpuPayloadFreshOutput -Path $Paths.SignedDirectory -Root $Paths.RunDirectory
    [void](New-Item -ItemType Directory -Path $Paths.SignedDirectory -ErrorAction Stop)
    $buildPaths = Get-GpuPayloadBuildArtifactPaths -Paths $Paths
    [IO.File]::Copy($buildPaths.Loader, $Paths.SignedLoader, $false)
    [IO.File]::Copy($buildPaths.HypervisorEfi, $Paths.SignedPayload, $false)

    $certificatePath = [string]$State.Tools.Certificate.Path
    $thumbprint = Invoke-GpuPayloadPlatformPrivate -FunctionName 'Assert-GpuThumbprintAndPrivateKey' `
        -SourceRoot $sourceRoot -BoundArguments @{ CertificatePath = $certificatePath }
    if (-not [string]::Equals([string]$thumbprint, '59DBD6780CFE60C020B3F4511211FEC22C397B85', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'CurrentUser signer thumbprint does not match the pinned diagnostic certificate.'
    }
    $signTool = Invoke-GpuPayloadPlatformPrivate -FunctionName 'Get-GpuSignToolPath' -SourceRoot $sourceRoot -BoundArguments @{}
    $verifier = [string]$State.Tools.Verifier.Path
    foreach ($image in @($Paths.SignedLoader, $Paths.SignedPayload)) {
        $sign = Invoke-GpuPayloadNative -Program $signTool -Arguments @(
            'sign', '/s', 'My', '/sha1', $thumbprint, '/fd', 'SHA256', $image
        ) -ActionDirectory $ActionDirectory -Label ('signtool-' + [IO.Path]::GetFileName($image))
        $verify = Invoke-GpuPayloadNative -Program $verifier -Arguments @($image, $certificatePath) `
            -ActionDirectory $ActionDirectory -Label ('verify-authenticode-' + [IO.Path]::GetFileName($image))
    }
    $audit = Invoke-GpuPayloadHvscan -ImagePath $Paths.SignedPayload `
        -DumpbinPath ([string]$toolchain.Tools.'dumpbin.exe') -ActionDirectory $ActionDirectory -Label 'hvscan-signed-payload'
    $State.SignedArtifacts = [pscustomobject]@{
        Payload = Get-GpuFilePin -Path $Paths.SignedPayload
        Loader = Get-GpuFilePin -Path $Paths.SignedLoader
    }
    $State.SignEvidence = [pscustomobject]@{
        SignerThumbprint = $thumbprint
        SignToolPath = $signTool
        PayloadAudit = $audit
        SignedImages = @($Paths.SignedLoader, $Paths.SignedPayload)
        ExistingCurrentUserCertificateUsed = $true
        PrivateKeyExported = $false
    }
    return [pscustomobject]@{ Status = 'SIGNATURE_AND_AUDIT_PASS'; SignedArtifacts = $State.SignedArtifacts; SignEvidence = $State.SignEvidence }
}

function Assert-GpuPayloadVmCleanupEvidence {
    param([object]$Result, [AllowNull()][string]$ExpectedVmxPath)

    if ($null -eq $Result -or [int]$Result.SchemaVersion -ne 1) { throw 'VM controller result schema is unsupported.' }
    if (@($Result.CleanupFailures).Count -ne 0) { throw 'VM controller reported cleanup failures.' }
    foreach ($name in @('BeforeCreated', 'AfterCreated', 'RevertedBefore', 'VmOffConfirmed')) {
        $value = $Result.Snapshots.$name
        if ($value -isnot [bool] -or -not $value) { throw "VM controller evidence is missing a successful snapshot/stop gate: $name" }
    }
    foreach ($name in @('Before', 'After')) {
        if ([string]::IsNullOrWhiteSpace([string]$Result.Snapshots.$name)) { throw "VM controller did not record the $name snapshot identity." }
    }
    if ($Result.Vm.NestedVirtualization -isnot [bool] -or -not [bool]$Result.Vm.NestedVirtualization -or
        [string]$Result.Vm.Network -cne 'disabled') {
        throw 'VM evidence does not confirm the required nested, network-disabled test environment.'
    }
    if (@($Result.CleanupFailures).Count -ne 0 -and [string]$Result.Result -ceq 'PASS') {
        throw 'VM controller reported PASS alongside cleanup failures.'
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedVmxPath) -and
        -not [string]::Equals([IO.Path]::GetFullPath([string]$Result.Vm.VmxPath),
            [IO.Path]::GetFullPath($ExpectedVmxPath), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'VM result path does not match the VMX pinned for this workflow.'
    }
}

function Get-GpuPayloadArchiveDisposition {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][object]$State, [Parameter(Mandatory = $true)][object]$Paths)

    $intent = $null
    $inProgress = Get-GpuPayloadObjectProperty -Object $State -Name 'InProgress'
    $interrupted = Get-GpuPayloadObjectProperty -Object $State -Name 'ArchivedInterruptedAction'
    $lastFailure = Get-GpuPayloadObjectProperty -Object $State -Name 'LastFailure'
    $validation = Get-GpuPayloadObjectProperty -Object $State -Name 'Validation'
    $lastAction = Get-GpuPayloadObjectProperty -Object $State -Name 'LastAction'
    if ($null -ne $inProgress -and [string](Get-GpuPayloadObjectProperty -Object $inProgress -Name 'Action') -ceq 'ValidateVm') {
        $intent = $inProgress
    }
    elseif ($null -ne $interrupted -and [string](Get-GpuPayloadObjectProperty -Object $interrupted -Name 'Action') -ceq 'ValidateVm') {
        $intent = $interrupted
    }
    elseif ($null -ne $lastFailure -and [string](Get-GpuPayloadObjectProperty -Object $lastFailure -Name 'Action') -ceq 'ValidateVm') {
        $intent = Get-GpuPayloadObjectProperty -Object $lastFailure -Name 'Intent'
    }
    elseif ($null -ne $lastFailure -and $null -ne (Get-GpuPayloadObjectProperty -Object $lastFailure -Name 'Intent') -and
        [string](Get-GpuPayloadObjectProperty -Object (Get-GpuPayloadObjectProperty -Object $lastFailure -Name 'Intent') -Name 'Action') -ceq 'ValidateVm') {
        $intent = Get-GpuPayloadObjectProperty -Object $lastFailure -Name 'Intent'
    }
    elseif ($null -ne $lastFailure -and $null -ne (Get-GpuPayloadObjectProperty -Object (Get-GpuPayloadObjectProperty -Object $lastFailure -Name 'PriorFailure') -Name 'Intent') -and
        [string](Get-GpuPayloadObjectProperty -Object (Get-GpuPayloadObjectProperty -Object (Get-GpuPayloadObjectProperty -Object $lastFailure -Name 'PriorFailure') -Name 'Intent') -Name 'Action') -ceq 'ValidateVm') {
        $intent = Get-GpuPayloadObjectProperty -Object (Get-GpuPayloadObjectProperty -Object $lastFailure -Name 'PriorFailure') -Name 'Intent'
    }
    elseif ($null -ne $validation) {
        $intent = $validation
    }
    elseif ($null -ne $lastAction -and [string](Get-GpuPayloadObjectProperty -Object $lastAction -Name 'Action') -ceq 'ValidateVm') {
        $intent = $lastAction
    }

    if ($null -eq $intent) {
        $recordedVmAttempt = $false
        foreach ($candidate in @($inProgress, $interrupted, $lastFailure, $validation, $lastAction)) {
            if ($null -ne $candidate -and [string](Get-GpuPayloadObjectProperty -Object $candidate -Name 'Action') -ceq 'ValidateVm') {
                $recordedVmAttempt = $true
            }
            $nestedIntent = Get-GpuPayloadObjectProperty -Object $candidate -Name 'Intent'
            if ($null -ne $nestedIntent -and [string](Get-GpuPayloadObjectProperty -Object $nestedIntent -Name 'Action') -ceq 'ValidateVm') {
                $recordedVmAttempt = $true
            }
        }
        if ($recordedVmAttempt -or [string]$State.Phase -ceq 'Complete') {
            throw 'Cannot archive a VM validation attempt without a saved controller result proving stop and snapshot cleanup.'
        }
        return [pscustomobject]@{ Allowed = $true; Status = 'ABORTED_ARCHIVED'; VmCleanupResultPin = $null; Reason = 'No VM controller invocation is recorded.' }
    }
    $controllerRunDirectory = [string](Get-GpuPayloadObjectProperty -Object $intent -Name 'ControllerRunDirectory')
    $invocationStarted = Get-GpuPayloadObjectProperty -Object $intent -Name 'ControllerInvocationStarted'
    $lastFailureMessage = [string](Get-GpuPayloadObjectProperty -Object $lastFailure -Name 'Failure')
    $legacyMissingPropertyFailure = $null -eq $invocationStarted -and
        $lastFailureMessage -like '*property ''ControllerRunDirectory'' cannot be found*'
    $knownPreflightSignatureFailure = $invocationStarted -is [bool] -and [bool]$invocationStarted -and
        $lastFailureMessage -like '*Signed loader Authenticode signature is invalid: UnknownError*'
    $neverInvokedController = $invocationStarted -is [bool] -and -not [bool]$invocationStarted
    if ($neverInvokedController -or $legacyMissingPropertyFailure) {
        $preControllerProof = Get-GpuPayloadPreControllerProof -State $State -Paths $Paths `
            -ExpectedControllerRunDirectory $controllerRunDirectory `
            -RequirePreparationCommitMatch $legacyMissingPropertyFailure
        return [pscustomobject]@{
            Allowed = $true
            Status = 'ABORTED_ARCHIVED'
            VmCleanupResultPin = $null
            PreControllerFailureProof = $preControllerProof
            Reason = 'No VM controller invocation began; VMX pins are unchanged and diagnostic NVRAM and lock artifacts are absent.'
        }
    }
    if ($knownPreflightSignatureFailure -and -not [string]::IsNullOrWhiteSpace($controllerRunDirectory) -and
        -not (Test-Path -LiteralPath ([IO.Path]::GetFullPath($controllerRunDirectory)))) {
        $preVmProof = Get-GpuPayloadPreControllerProof -State $State -Paths $Paths `
            -ExpectedControllerRunDirectory $controllerRunDirectory -ProofStatus 'PRE_VM_START_FAILURE_NO_VM_BOOT' `
            -RequireNoVmLocks
        return [pscustomobject]@{
            Allowed = $true
            Status = 'ABORTED_ARCHIVED'
            VmCleanupResultPin = $null
            PreControllerFailureProof = $preVmProof
            Reason = 'The known SignedNative Authenticode preflight failure happened before VM start; VMX pins are unchanged and NVRAM/lock evidence is absent.'
        }
    }
    if ([string]::IsNullOrWhiteSpace($controllerRunDirectory)) {
        throw 'Cannot archive a VM validation attempt without a saved controller result or recognized no-start proof.'
    }
    $controllerRunDirectory = [IO.Path]::GetFullPath($controllerRunDirectory)
    $null = Assert-GpuPathWithin -Path $controllerRunDirectory -Root $Paths.ControllerRunsRoot -MustExist
    $resultPath = Join-Path $controllerRunDirectory 'result.json'
    $null = Assert-GpuPathWithin -Path $resultPath -Root $controllerRunDirectory -MustExist
    $beforePin = Get-GpuFilePin -Path $resultPath
    $result = Read-GpuJson -Path $resultPath
    $afterPin = Get-GpuFilePin -Path $resultPath
    if ([string]$beforePin.Sha256 -cne [string]$afterPin.Sha256 -or [long]$beforePin.Bytes -ne [long]$afterPin.Bytes) {
        throw 'VM controller result changed while checking its stop and snapshot cleanup proof.'
    }
    if (Test-GpuPayloadNoVmStartResult -Result $result) {
        $noStartProof = Get-GpuPayloadPreControllerProof -State $State -Paths $Paths `
            -ExpectedControllerRunDirectory $controllerRunDirectory -ProofStatus 'PRE_VM_START_FAILURE_NO_VM_BOOT' `
            -RequireNoVmLocks -AllowExpectedControllerRunDirectory
        $noStartProof | Add-Member -MemberType NoteProperty -Name 'ControllerResultPin' -Value $afterPin
        return [pscustomobject]@{
            Allowed = $true
            Status = 'ABORTED_ARCHIVED'
            VmCleanupResultPin = $null
            PreControllerFailureProof = $noStartProof
            ControllerRunDirectory = $controllerRunDirectory
            Reason = 'Controller evidence confirms it failed before VM start or VM writes; prepared VMX pins and absent NVRAM/lock evidence agree.'
        }
    }
    Assert-GpuPayloadVmCleanupEvidence -Result $result -ExpectedVmxPath ([string]$intent.VmxPath)
    return [pscustomobject]@{
        Allowed = $true
        Status = if (([string]$State.Phase -ceq 'Complete' -or
                ([string]$State.Phase -ceq 'Archived' -and [string]$State.ArchiveFromPhase -ceq 'Complete')) -and
                (Test-GpuPayloadValidationProof -State $State)) { 'VALIDATED_ARCHIVED' } else { 'ABORTED_ARCHIVED' }
        VmCleanupResultPin = $afterPin
        ControllerRunDirectory = $controllerRunDirectory
        Reason = 'The recorded controller result confirms VM-off, before/after snapshots, baseline revert, and no cleanup failures.'
    }
}

function Get-GpuPayloadPreControllerProof {
    param(
        [Parameter(Mandatory = $true)][object]$State,
        [Parameter(Mandatory = $true)][object]$Paths,
        [AllowNull()][string]$ExpectedControllerRunDirectory,
        [string]$ProofStatus = 'PRE_CONTROLLER_FAILURE_NO_VM_STARTED',
        [bool]$RequirePreparationCommitMatch = $false,
        [switch]$RequireNoVmLocks,
        [switch]$AllowExpectedControllerRunDirectory
    )

    $controllerRoot = [IO.Path]::GetFullPath([string]$Paths.ControllerRunsRoot)
    $matchingControllerRuns = @()
    if (Test-Path -LiteralPath $controllerRoot -PathType Container) {
        $matchingControllerRuns = @(Get-ChildItem -LiteralPath $controllerRoot -Force -Directory |
            Where-Object { $_.Name.StartsWith(([string]$State.RunId + '-'), [StringComparison]::OrdinalIgnoreCase) })
    }
    $expectedControllerRun = $null
    if (-not [string]::IsNullOrWhiteSpace($ExpectedControllerRunDirectory)) {
        $expectedControllerRun = [IO.Path]::GetFullPath($ExpectedControllerRunDirectory)
        $null = Assert-GpuPathWithin -Path $expectedControllerRun -Root $controllerRoot
        if ($AllowExpectedControllerRunDirectory) {
            $matchingControllerRuns = @($matchingControllerRuns | Where-Object {
                -not [string]::Equals([IO.Path]::GetFullPath($_.FullName), $expectedControllerRun, [StringComparison]::OrdinalIgnoreCase)
            })
            if (-not (Test-Path -LiteralPath $expectedControllerRun -PathType Container)) {
                throw 'The expected controller run directory is missing despite a result-based VM no-start claim.'
            }
        } elseif (Test-Path -LiteralPath $expectedControllerRun) {
            throw 'The expected controller run directory exists; the no-result preflight failure cannot be proven.'
        }
    }
    if ($matchingControllerRuns.Count -ne 0) {
        throw 'A controller run directory exists for this payload workflow; no pre-controller archive proof is possible.'
    }

    $receiptPath = Join-Path $Paths.StateRoot 'vm-prepared.json'
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) { throw 'VM preparation receipt is missing; cannot prove that the VM was not started.' }
    $receipt = Read-GpuJson -Path $receiptPath
    if ($receipt.SchemaVersion -ne 1 -or [string]$receipt.Status -cne 'CLONE_PREPARED_TRUST_UNVERIFIED' -or
        [bool]$receipt.HostBootChanges -or [bool]$receipt.RetainedVmMoved -or [bool]$receipt.TrustValidated -or
        [string]$receipt.TrustMechanism -cne 'VM firmware db append, not MOK enrollment' -or
        [string]$receipt.VmxSha256 -notmatch '^[0-9A-Fa-f]{64}$' -or
        [string]$receipt.SourceVmxSha256 -notmatch '^[0-9A-Fa-f]{64}$' -or
        -not (Test-GpuPayloadVmxPath -Path ([string]$receipt.VmxPath)) -or
        -not (Test-GpuPayloadVmxPath -Path ([string]$receipt.SourceVmx))) {
        throw 'VM preparation receipt cannot support a pre-controller no-start proof.'
    }
    if ($RequirePreparationCommitMatch -and
        [string]$receipt.SourceCommit -cne [string]$State.SourceCommit) {
        throw 'Legacy pre-controller recovery requires the prepared VM and workflow to share the same source commit.'
    }

    $workspaceBuildRoot = Join-Path $Paths.WorkspaceRoot '.codex-build'
    $vmx = [IO.Path]::GetFullPath([string]$receipt.VmxPath)
    $sourceVmx = [IO.Path]::GetFullPath([string]$receipt.SourceVmx)
    $null = Assert-GpuPathWithin -Path $vmx -Root $workspaceBuildRoot -MustExist
    $null = Assert-GpuPathWithin -Path $sourceVmx -Root $workspaceBuildRoot -MustExist
    $vmxPin = Get-GpuFilePin -Path $vmx
    $sourceVmxPin = Get-GpuFilePin -Path $sourceVmx
    if ([string]$vmxPin.Sha256 -cne [string]$receipt.VmxSha256 -or
        [string]$sourceVmxPin.Sha256 -cne [string]$receipt.SourceVmxSha256) {
        throw 'Prepared VMX or retained source VMX changed; pre-controller no-start proof is unavailable.'
    }
    $nvramPath = Join-Path (Split-Path -Parent $vmx) 'gpu-payload-test.nvram'
    if (Test-Path -LiteralPath $nvramPath) {
        throw 'Diagnostic VM NVRAM exists; the VM may have started, so pre-controller archival is blocked.'
    }
    $vmLockPath = $vmx + '.lck'
    if ($RequireNoVmLocks -and (Test-Path -LiteralPath $vmLockPath)) {
        throw 'Diagnostic VMX lock artifact exists; the VM may have started, so archival is blocked.'
    }
    return [pscustomobject]@{
        Status = $ProofStatus
        Failure = [string](Get-GpuPayloadObjectProperty -Object $State.LastFailure -Name 'Failure')
        LegacyRecovery = [bool]$RequirePreparationCommitMatch
        WorkflowCommit = [string]$State.SourceCommit
        PreparationCommit = [string]$receipt.SourceCommit
        ControllerRunDirectory = $expectedControllerRun
        ControllerRunDirectoryAbsent = ($null -eq $expectedControllerRun -or -not (Test-Path -LiteralPath $expectedControllerRun))
        ControllerRunDirectoryCount = $matchingControllerRuns.Count
        PreparedVmxPin = $vmxPin
        RetainedVmxPin = $sourceVmxPin
        FreshNvramPath = $nvramPath
        FreshNvramAbsent = $true
        VmLockPath = if ($RequireNoVmLocks) { $vmLockPath } else { $null }
        VmLockAbsent = if ($RequireNoVmLocks) { -not (Test-Path -LiteralPath $vmLockPath) } else { $null }
    }
}

function Test-GpuPayloadNoVmStartResult {
    param([AllowNull()][object]$Result)

    if ($null -eq $Result -or [int]$Result.SchemaVersion -ne 1 -or [string]$Result.Result -ceq 'PASS' -or
        [string]$Result.ValidationProfile -cne 'SignedNative' -or @($Result.CleanupFailures).Count -ne 0) { return $false }
    $vmStartAttempted = Get-GpuPayloadObjectProperty -Object $Result -Name 'VmStartAttempted'
    if ($null -eq $vmStartAttempted) { $vmStartAttempted = Get-GpuPayloadObjectProperty -Object $Result.Vm -Name 'VmStartAttempted' }
    $vmWriteAttempted = Get-GpuPayloadObjectProperty -Object $Result -Name 'VmWriteAttempted'
    if ($null -eq $vmWriteAttempted) { $vmWriteAttempted = Get-GpuPayloadObjectProperty -Object $Result.Vm -Name 'VmWriteAttempted' }
    $beforeCreated = Get-GpuPayloadObjectProperty -Object $Result.Snapshots -Name 'BeforeCreated'
    $afterCreated = Get-GpuPayloadObjectProperty -Object $Result.Snapshots -Name 'AfterCreated'
    $beforeSnapshot = Get-GpuPayloadObjectProperty -Object $Result.Snapshots -Name 'Before'
    $afterSnapshot = Get-GpuPayloadObjectProperty -Object $Result.Snapshots -Name 'After'
    return ($vmStartAttempted -is [bool] -and -not [bool]$vmStartAttempted -and
        $vmWriteAttempted -is [bool] -and -not [bool]$vmWriteAttempted -and
        $beforeCreated -is [bool] -and -not [bool]$beforeCreated -and
        $afterCreated -is [bool] -and -not [bool]$afterCreated -and
        [string]::IsNullOrWhiteSpace([string]$beforeSnapshot) -and
        [string]::IsNullOrWhiteSpace([string]$afterSnapshot))
}

function Test-GpuPayloadSignedNativeRawEvidence {
    param([object]$Startup)

    if ($null -eq $Startup) { return $false }
    if ($Startup.HandoffStatusBytes -isnot [ValueType] -or $Startup.HandoffStatusBytes -is [bool] -or
        $Startup.PayloadDiagnosticBytes -isnot [ValueType] -or $Startup.PayloadDiagnosticBytes -is [bool] -or
        $Startup.HandoffStatusCapturedBytes -isnot [ValueType] -or $Startup.HandoffStatusCapturedBytes -is [bool] -or
        $Startup.PayloadDiagnosticCapturedBytes -isnot [ValueType] -or $Startup.PayloadDiagnosticCapturedBytes -is [bool] -or
        $Startup.HandoffStatusTooLarge -isnot [bool] -or [bool]$Startup.HandoffStatusTooLarge -or
        $Startup.PayloadDiagnosticTooLarge -isnot [bool] -or [bool]$Startup.PayloadDiagnosticTooLarge -or
        [string]$Startup.DiagnosticTextEncoding -cne 'ISO-8859-1 byte-preserving') { return $false }
    $handoffBytes = [long]$Startup.HandoffStatusBytes
    $payloadBytes = [long]$Startup.PayloadDiagnosticBytes
    $handoffCaptured = [long]$Startup.HandoffStatusCapturedBytes
    $payloadCaptured = [long]$Startup.PayloadDiagnosticCapturedBytes
    if ($handoffBytes -lt 0 -or $handoffBytes -gt 4096 -or $payloadBytes -lt 0 -or $payloadBytes -gt 4096 -or
        $handoffCaptured -ne $handoffBytes -or $payloadCaptured -ne $payloadBytes) { return $false }
    try {
        $encoding = [Text.Encoding]::GetEncoding(28591)
        if ($encoding.GetByteCount([string]$Startup.HandoffStatus) -ne $handoffCaptured -or
            $encoding.GetByteCount([string]$Startup.PayloadDiagnostic) -ne $payloadCaptured) { return $false }
    }
    catch { return $false }
    $handoff = @{}
    foreach ($line in ([string]$Startup.HandoffStatus -split "`r?`n")) {
        if ($line -match '^([a-z_]+)=([A-Za-z0-9_]+)$') {
            if ($handoff.ContainsKey($Matches[1])) { return $false }
            $handoff[$Matches[1]] = $Matches[2]
        }
        elseif (-not [string]::IsNullOrWhiteSpace($line)) { return $false }
    }
    $diagnostic = @{}
    foreach ($line in ([string]$Startup.PayloadDiagnostic -split "`r?`n")) {
        if ($line -match '^([a-z_]+)=([A-Za-z0-9_]+|0x[0-9A-F]{16})$') {
            if ($diagnostic.ContainsKey($Matches[1])) { return $false }
            $diagnostic[$Matches[1]] = $Matches[2]
        }
        elseif (-not [string]::IsNullOrWhiteSpace($line)) { return $false }
    }
    return $handoff['schema'] -ceq '1' -and
        $handoff['state'] -ceq 'READY_BOOT_MANAGER' -and
        $handoff['restore'] -ceq 'PASS' -and
        $handoff['payload'] -ceq 'PASS' -and
        $diagnostic['schema'] -ceq '1' -and
        $diagnostic['payload_launch'] -ceq 'ok' -and
        $diagnostic['payload_status'] -ceq '0x0000000000000000' -and
        $diagnostic['shim_load_phase'] -ceq 'firmware_load' -and
        $diagnostic['shim_load_status'] -ceq '0x0000000000000000'
}

function Assert-GpuPayloadVmResult {
    [CmdletBinding()]
    param(
        [object]$Result,
        [AllowNull()][string]$ExpectedVmxPath,
        [AllowNull()][string]$ExpectedCertificateSha256
    )

    Assert-GpuPayloadVmCleanupEvidence -Result $Result -ExpectedVmxPath $ExpectedVmxPath
    if ([string]$Result.Result -cne 'PASS' -or
        [string]$Result.ValidationProfile -cne 'SignedNative' -or
        $Result.SignedNativePassed -isnot [bool] -or -not [bool]$Result.SignedNativePassed) {
        throw 'VM controller did not report a successful SignedNative result.'
    }
    if ([string]$Result.VmwareDbAppendMarker -cne 'PASS_LOG_MARKER' -or
        [string]$Result.HalResults.VmwareDbAppendMarker -cne 'PASS_LOG_MARKER') {
        throw 'VM controller did not confirm the VMware Secure Boot db append log marker.'
    }
    foreach ($name in @('Inspect', 'Stage', 'SignedNativeEfi', 'WindowsStartup')) {
        if ([string]$Result.Phases.$name -cne 'PASS') { throw "VM controller phase did not pass: $name" }
    }
    $startup = $Result.WindowsStartupEvidence
    if ($null -eq $startup -or [string]$startup.ValidationProfile -cne 'SignedNative' -or
        [string]$startup.Status -cne 'PASS' -or $startup.SecureBoot -isnot [bool] -or
        -not [bool]$startup.SecureBoot -or $startup.ExpectedSecureBoot -isnot [bool] -or
        -not [bool]$startup.ExpectedSecureBoot -or $startup.ExpectedCertificateDerPresent -isnot [bool] -or
        -not [bool]$startup.ExpectedCertificateDerPresent -or
        [string]$startup.CertificateDbInspection -cne 'BOUNDED_RAW_DER_SUBSEQUENCE_ONLY' -or
        [string]$startup.ExpectedCertificateSha256 -notmatch '^[0-9A-Fa-f]{64}$' -or
        -not [string]::Equals([string]$startup.ExpectedCertificateSha256, $ExpectedCertificateSha256, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-GpuPayloadSignedNativeRawEvidence -Startup $startup)) {
        throw 'Authenticated Windows startup evidence did not confirm SignedNative Secure Boot and certificate admission.'
    }
    $expectedMarkers = @('uefi hypervisor: DMA isolation admission: PASS', 'uefi hypervisor: BSP firmware return: PASS')
    foreach ($marker in $expectedMarkers) {
        if (@($Result.WindowsStartupEvidence.EfiMarkers) -cnotcontains $marker) { throw "SignedNative EFI marker is missing: $marker" }
    }
    if ([string]$Result.HalResults.SignedNativeEfiAdmissionMarkers -cne 'PASS' -or
        [string]$Result.HalResults.DmaIsolationAdmission -cne 'EXTERNAL_VMM_ADMISSION' -or
        [string]$Result.HalResults.NativeGpuBackend -cne 'NOT_EXERCISED_EXTERNAL_VMM' -or
        [string]$Result.HalResults.ActualVmxRootWcRenderDrainAndUnmap -cne 'NOT_EXERCISED_EXTERNAL_VMM' -or
        [string]$Result.HalResults.IntelPhysicalPlaneAndDmaEnforcement -cne 'NOT_TESTED_BY_THIS_VM' -or
        [string]$Result.HalResults.HostDualGpuTopology -cne 'NOT_TESTED_BY_THIS_VM') {
        throw 'SignedNative HAL evidence has an unexpected VM-only or host-only status.'
    }
    if ($Result.Vm.OriginalSecureBoot -isnot [bool] -or -not [bool]$Result.Vm.OriginalSecureBoot -or
        [string]$Result.Vm.Passthrough -cne 'none') {
        throw 'SignedNative test did not preserve Secure Boot or used GPU passthrough.'
    }
}

function Invoke-GpuPayloadValidateVm {
    param(
        [object]$State, [object]$Paths, [string]$ActionDirectory,
        [AllowNull()][string]$VmxPath,
        [Parameter(Mandatory = $true)][string]$CredentialStateRoot
    )

    Assert-GpuPayloadPinsCurrent -State $State -Paths $Paths
    $null = Assert-GpuPayloadSourcePins -SourceRoot $State.SourceRoot -ExpectedPins $State.SourcePins
    $workspaceBuildRoot = Join-Path $Paths.WorkspaceRoot '.codex-build'
    $vmReceiptPath = Join-Path $Paths.StateRoot 'vm-prepared.json'
    if (-not (Test-Path -LiteralPath $vmReceiptPath -PathType Leaf)) { throw 'Run New-GpuPayloadVm first; vm-prepared.json is missing.' }
    $vmReceipt = Read-GpuJson -Path $vmReceiptPath
    if ($vmReceipt.SchemaVersion -ne 1 -or
        [string]$vmReceipt.Status -cne 'CLONE_PREPARED_TRUST_UNVERIFIED' -or
        [bool]$vmReceipt.HostBootChanges -or [bool]$vmReceipt.RetainedVmMoved -or
        [bool]$vmReceipt.TrustValidated -or
        [string]$vmReceipt.TrustMechanism -cne 'VM firmware db append, not MOK enrollment' -or
        [string]$vmReceipt.PublicCertificateSha256 -cne [string]$State.Tools.Certificate.Sha256 -or
        -not (Test-GpuPayloadVmxPath -Path ([string]$vmReceipt.VmxPath)) -or
        [string]$vmReceipt.VmxSha256 -notmatch '^[0-9A-Fa-f]{64}$' -or
        [string]$vmReceipt.SourceVmxSha256 -notmatch '^[0-9A-Fa-f]{64}$' -or
        [string]::IsNullOrWhiteSpace([string]$vmReceipt.Snapshot) -or
        [string]::IsNullOrWhiteSpace([string]$vmReceipt.SourceVmx)) {
        throw 'VM preparation receipt is malformed or does not match this signed workflow.'
    }
    $preparationCompatibility = Test-GpuPayloadPreparationCompatibility -SourceRoot $State.SourceRoot `
        -PreparationCommit ([string]$vmReceipt.SourceCommit) -CurrentCommit ([string]$State.SourceCommit)
    $vmx = [IO.Path]::GetFullPath([string]$vmReceipt.VmxPath)
    $sourceVmx = [IO.Path]::GetFullPath([string]$vmReceipt.SourceVmx)
    $null = Assert-GpuPathWithin -Path $vmx -Root $workspaceBuildRoot -MustExist
    $null = Assert-GpuPathWithin -Path $sourceVmx -Root $workspaceBuildRoot -MustExist
    if ([IO.Path]::GetExtension($vmx) -ine '.vmx' -or [IO.Path]::GetExtension($sourceVmx) -ine '.vmx') {
        throw 'Prepared and retained source VM paths must identify VMware .vmx files.'
    }
    $vmxPin = Get-GpuFilePin -Path $vmx
    $sourceVmxPin = Get-GpuFilePin -Path $sourceVmx
    if (-not [string]::Equals([string]$vmxPin.Sha256, [string]$vmReceipt.VmxSha256, [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals([string]$sourceVmxPin.Sha256, [string]$vmReceipt.SourceVmxSha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Prepared VMX or retained source VMX changed after the preparation receipt was written.'
    }
    if (-not [string]::IsNullOrWhiteSpace($VmxPath) -and
        -not [string]::Equals([IO.Path]::GetFullPath($VmxPath), $vmx, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Explicit -VmxPath does not match the pinned New-GpuPayloadVm receipt.'
    }
    $credentialRoot = [IO.Path]::GetFullPath($CredentialStateRoot)
    if (-not (Test-Path -LiteralPath $credentialRoot -PathType Container)) { throw 'CredentialStateRoot must already exist.' }
    $null = Assert-GpuPathWithin -Path $credentialRoot -Root $Paths.WorkspaceRoot -AllowRoot -MustExist

    $vmRunsRoot = $Paths.ControllerRunsRoot
    if (-not (Test-Path -LiteralPath $vmRunsRoot -PathType Container)) {
        [void](New-Item -ItemType Directory -Path $vmRunsRoot -Force -ErrorAction Stop)
    }
    $controllerRun = Join-Path $vmRunsRoot ($Paths.RunId + '-' + [Guid]::NewGuid().ToString('N'))
    $null = Assert-GpuPayloadFreshOutput -Path $controllerRun -Root $vmRunsRoot
    $State.InProgress.ControllerRunDirectory = $controllerRun
    $State.InProgress.VmxPath = $vmx
    $State.InProgress.VmxSha256 = [string]$vmxPin.Sha256
    $State.InProgress.VmPreparationCompatibility = $preparationCompatibility
    Save-GpuPayloadState -State $State -Paths $Paths
    $controller = Join-Path $State.SourceRoot 'components\hypervisor\setup\Test-GpuHalWindowsVm.ps1'
    $null = Assert-GpuPathWithin -Path $controller -Root $State.SourceRoot -MustExist
    $powershell = (Get-Command -Name 'powershell.exe' -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $controller,
        '-WorkspaceRoot', $Paths.WorkspaceRoot,
        '-VmxPath', $vmx,
        '-RunDirectory', $controllerRun,
        '-EfiPath', $Paths.SignedPayload,
        '-LoaderPath', $Paths.SignedLoader,
        '-InstallerPath', $State.Tools.Installer.Path,
        '-ProbePath', $State.Tools.Probe.Path,
        '-CredentialStateRoot', $credentialRoot,
        '-ValidationProfile', 'SignedNative',
        '-ShimPath', $State.Tools.Shim.Path,
        '-CertificatePath', $State.Tools.Certificate.Path,
        '-VerifierPath', $State.Tools.Verifier.Path,
        '-VerifierSha256', $State.Tools.Verifier.Sha256
    )
    $State.InProgress.ControllerInvocationStarted = $true
    Save-GpuPayloadState -State $State -Paths $Paths
    $controllerReceipt = Invoke-GpuPayloadNative -Program $powershell -Arguments $arguments `
        -ActionDirectory $ActionDirectory -Label 'signed-native-disposable-vm-controller'
    $resultPath = Join-Path $controllerRun 'result.json'
    $null = Assert-GpuPathWithin -Path $resultPath -Root $controllerRun -MustExist
    $vmResult = Read-GpuJson -Path $resultPath
    Assert-GpuPayloadVmResult -Result $vmResult -ExpectedVmxPath $vmx `
        -ExpectedCertificateSha256 ([string]$State.Tools.Certificate.Sha256)
    $vmReceiptPin = Get-GpuFilePin -Path $vmReceiptPath
    $State.Validation = [pscustomobject]@{
        Status = 'PASS'
        Profile = 'SignedNative'
        VmxPath = $vmx
        ControllerRunId = [string]$vmResult.RunId
        ControllerRunDirectory = $controllerRun
        ControllerReceipt = $controllerReceipt.ReceiptPath
        ResultPath = $resultPath
        ResultPin = Get-GpuFilePin -Path $resultPath
        VmPreparationReceiptPin = $vmReceiptPin
        VmPreparationCompatibility = $preparationCompatibility
        SnapshotBefore = [string]$vmResult.Snapshots.Before
        SnapshotAfter = [string]$vmResult.Snapshots.After
        VmStopped = [bool]$vmResult.Snapshots.VmOffConfirmed
        RevertedToBefore = [bool]$vmResult.Snapshots.RevertedBefore
        CleanupFailures = @($vmResult.CleanupFailures)
        IntelPhysicalPlaneAndDmaEnforcement = 'NOT_TESTED_BY_THIS_VM'
        HostDualGpuTopology = 'NOT_TESTED_BY_THIS_VM'
    }
    return [pscustomobject]@{ Status = 'SIGNED_NATIVE_VM_VALIDATION_PASS'; Validation = $State.Validation }
}

function Start-GpuPayloadMutation {
    param([object]$State, [object]$Paths, [string]$Action)

    $actionDirectory = New-GpuPayloadActionDirectory -Paths $Paths -Action $Action
    $null = Assert-GpuPayloadFreshOutput -Path $actionDirectory -Root $Paths.ActionsRoot
    $State.InProgress = [pscustomobject]@{
        Action = $Action
        StartedUtc = [DateTime]::UtcNow.ToString('o')
        ActionDirectory = $actionDirectory
        PhaseBefore = [string]$State.Phase
        ControllerRunDirectory = $null
        VmxPath = $null
        VmxSha256 = $null
        VmPreparationCompatibility = $null
        ControllerInvocationStarted = $false
    }
    $State.LastFailure = $null
    Save-GpuPayloadState -State $State -Paths $Paths
    [void](New-Item -ItemType Directory -Path $actionDirectory -ErrorAction Stop)
    return $actionDirectory
}

function Complete-GpuPayloadMutation {
    param([object]$State, [object]$Paths, [string]$Action, [string]$ActionDirectory, [object]$Data)

    $State.Phase = Get-GpuPayloadNextPhase -Action $Action
    $State.LastAction = [pscustomobject]@{
        Action = $Action; Status = [string]$Data.Status; Succeeded = $true
        StartedUtc = [string]$State.InProgress.StartedUtc
        CompletedUtc = [DateTime]::UtcNow.ToString('o'); ActionDirectory = $ActionDirectory
    }
    $State.Actions = @($State.Actions) + @($State.LastAction)
    $State.InProgress = $null
    Save-GpuPayloadState -State $State -Paths $Paths
    $receipt = [pscustomobject]@{
        Action = $Action; Status = [string]$Data.Status; Succeeded = $true
        Phase = [string]$State.Phase; RunId = [string]$State.RunId
        RunDirectory = [string]$State.RunDirectory; ActionDirectory = $ActionDirectory
        JournalPath = $Paths.JournalPath; Details = $Data
    }
    Write-GpuJson -Path (Join-Path $ActionDirectory 'result.json') -Value $receipt -NoOverwrite
    return $receipt
}

function Fail-GpuPayloadMutation {
    param([object]$State, [object]$Paths, [string]$Action, [string]$ActionDirectory, [string]$Message)

    $entry = [pscustomobject]@{
        Action = $Action; Status = 'FAILED'; Succeeded = $false
        StartedUtc = if ($null -ne $State.InProgress) { [string]$State.InProgress.StartedUtc } else { $null }
        FailedUtc = [DateTime]::UtcNow.ToString('o'); ActionDirectory = $ActionDirectory
        Intent = $State.InProgress
        Failure = $Message
    }
    $State.Actions = @($State.Actions) + @($entry)
    $State.LastAction = $entry
    $State.LastFailure = $entry
    $State.InProgress = $null
    try { Save-GpuPayloadState -State $State -Paths $Paths } catch { }
    if ($ActionDirectory -and (Test-Path -LiteralPath $ActionDirectory -PathType Container)) {
        try { Write-GpuJson -Path (Join-Path $ActionDirectory 'failure.json') -Value $entry -NoOverwrite } catch { }
    }
}

function Invoke-GpuPayloadPrepare {
    param([object]$Paths, [string]$SourceRoot)

    if (Test-Path -LiteralPath $Paths.SelectorPath -PathType Leaf) {
        throw 'An active payload workflow selector exists. Report and archive that exact run first.'
    }
    $snapshot = Get-GpuPayloadGitSnapshot -SourceRoot $SourceRoot
    $sourcePins = Get-GpuPayloadSourcePins -SourceRoot $SourceRoot
    if (-not [string]::Equals($snapshot.Commit, $sourcePins.Commit, [StringComparison]::Ordinal)) { throw 'Source changed while Prepare captured pins.' }
    $null = Assert-GpuPayloadFreshOutput -Path $Paths.RunDirectory -Root $Paths.RunsRoot
    [void](New-Item -ItemType Directory -Path $Paths.RunDirectory -ErrorAction Stop)
    [void](New-Item -ItemType Directory -Path $Paths.ActionsRoot -ErrorAction Stop)
    $state = [pscustomobject]@{
        SchemaVersion = 1
        Workflow = 'gpu-payload-diagnostic'
        RunId = $Paths.RunId
        WorkspaceRoot = $Paths.WorkspaceRoot
        SourceRoot = $SourceRoot
        SourceCommit = $snapshot.Commit
        SourcePins = $sourcePins
        RunDirectory = $Paths.RunDirectory
        JournalPath = $Paths.JournalPath
        Paths = [pscustomobject]@{
            HypervisorBuild = $Paths.HypervisorBuild
            LoaderBuild = $Paths.LoaderBuild
            SignedPayload = $Paths.SignedPayload
            SignedLoader = $Paths.SignedLoader
        }
        Phase = 'Prepared'
        Status = 'READY'
        InProgress = $null
        Actions = @()
        LastAction = $null
        LastFailure = $null
        Tools = $null
        BuildArtifacts = $null
        SignedArtifacts = $null
        BuildEvidence = $null
        SignEvidence = $null
        Validation = $null
        ArchiveStatus = $null
        ArchiveFromPhase = $null
        ArchiveVmCleanupResultPin = $null
        ArchiveReason = $null
        ArchivePreControllerFailureProof = $null
        ArchivedInterruptedAction = $null
        CreatedUtc = [DateTime]::UtcNow.ToString('o')
        UpdatedUtc = [DateTime]::UtcNow.ToString('o')
    }
    Write-GpuJson -Path $Paths.JournalPath -Value $state -NoOverwrite
    $selector = [pscustomobject]@{
        SchemaVersion = 1
        RunId = $Paths.RunId
        RunDirectory = $Paths.RunDirectory
        JournalPath = $Paths.JournalPath
        SourceCommit = $snapshot.Commit
        CreatedUtc = [DateTime]::UtcNow.ToString('o')
    }
    Write-GpuJson -Path $Paths.SelectorPath -Value $selector -NoOverwrite
    return [pscustomobject]@{ Status = 'PREPARED'; RunId = $Paths.RunId; RunDirectory = $Paths.RunDirectory; JournalPath = $Paths.JournalPath; SourceCommit = $snapshot.Commit; SourcePins = $sourcePins }
}

function Invoke-GpuPayloadArchive {
    param([object]$State, [object]$Paths)

    if (-not (Test-Path -LiteralPath $Paths.ArchiveRoot -PathType Container)) {
        [void](New-Item -ItemType Directory -Path $Paths.ArchiveRoot -Force -ErrorAction Stop)
    }
    $null = Assert-GpuPathWithin -Path $Paths.ArchiveRoot -Root $Paths.StateRoot -MustExist
    $archivePath = Join-Path $Paths.ArchiveRoot ($State.RunId + '.json')
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        if ([string]$State.Phase -cne 'Archived') {
            $State.ArchiveFromPhase = [string]$State.Phase
            $State.Phase = 'Archived'
            $State.Status = [string]$State.ArchiveStatus
            $State.LastAction = [pscustomobject]@{
                Action = 'Archive'; Status = [string]$State.ArchiveStatus; Succeeded = $true
                CompletedUtc = [DateTime]::UtcNow.ToString('o')
            }
            $State.Actions = @($State.Actions) + @($State.LastAction)
        }
        $State.InProgress = $null
        Save-GpuPayloadState -State $State -Paths $Paths
        Write-GpuJson -Path $archivePath -Value $State -NoOverwrite
    }
    else {
        $archive = Read-GpuJson -Path $archivePath
        if ($archive.RunId -cne $State.RunId -or $archive.SourceCommit -cne $State.SourceCommit -or
            $archive.ArchiveStatus -cne $State.ArchiveStatus) { throw 'Existing archive identity or disposition does not match this run.' }
    }
    if (Test-Path -LiteralPath $Paths.SelectorPath -PathType Leaf) {
        $null = Assert-GpuPathWithin -Path $Paths.SelectorPath -Root $Paths.StateRoot -MustExist
        $selector = Read-GpuJson -Path $Paths.SelectorPath
        if ($selector.RunId -ceq $State.RunId -and [string]::Equals([IO.Path]::GetFullPath([string]$selector.RunDirectory), $Paths.RunDirectory, [StringComparison]::OrdinalIgnoreCase)) {
            [IO.File]::Delete($Paths.SelectorPath)
        }
    }
    return [pscustomobject]@{
        Action = 'Archive'; Status = [string]$State.ArchiveStatus; Succeeded = $true
        Phase = 'Archived'; RunId = $State.RunId; RunDirectory = $Paths.RunDirectory
        ArchivePath = $archivePath; ArchivePin = Get-GpuFilePin -Path $archivePath; EvidencePreserved = $true
        PreControllerFailureProof = $State.ArchivePreControllerFailureProof
    }
}

function Invoke-GpuPayloadAction {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][ValidateSet('Prepare', 'Build', 'Sign', 'ValidateVm', 'Status', 'Report', 'Archive')][string]$Action,
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$WorkspaceRoot,
        [AllowNull()][string]$RunDirectory,
        [AllowNull()][string]$VmxPath,
        [AllowNull()][string]$CredentialStateRoot
    )

    $source = [IO.Path]::GetFullPath($SourceRoot)
    $workspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    if (-not (Test-Path -LiteralPath $workspace -PathType Container)) { throw "WorkspaceRoot must exist: $workspace" }
    $stateRoot = Join-Path $workspace '.analysis\gpu-payload-diagnostic'
    if ($Action -eq 'Prepare') {
        $runId = if ([string]::IsNullOrWhiteSpace($RunDirectory)) { New-GpuPayloadRunId } else { Split-Path -Leaf ([IO.Path]::GetFullPath($RunDirectory)) }
        $candidate = if ([string]::IsNullOrWhiteSpace($RunDirectory)) { Join-Path (Join-Path $stateRoot 'runs') $runId } else { [IO.Path]::GetFullPath($RunDirectory) }
        $paths = Get-GpuPayloadPaths -WorkspaceRoot $workspace -RunDirectory $candidate
        $runsRoot = $paths.RunsRoot
        if (-not (Test-Path -LiteralPath $stateRoot -PathType Container)) { [void](New-Item -ItemType Directory -Path $stateRoot -Force -ErrorAction Stop) }
        if (-not (Test-Path -LiteralPath $runsRoot -PathType Container)) { [void](New-Item -ItemType Directory -Path $runsRoot -Force -ErrorAction Stop) }
        $null = Assert-GpuPathWithin -Path $runsRoot -Root $stateRoot -MustExist
        if (-not (Test-GpuPayloadTransition -Action 'Prepare' -Phase 'New' -InProgress $null)) { throw 'Prepare transition is invalid.' }
        return Invoke-GpuPayloadPrepare -Paths $paths -SourceRoot $source
    }

    if (-not (Test-Path -LiteralPath $stateRoot -PathType Container)) {
        if ($Action -in @('Status', 'Report')) { return New-GpuPayloadSummary -Action $Action -State $null -Paths $null }
        throw 'No payload diagnostic run exists. Run Prepare first.'
    }
    $rootPaths = [pscustomobject]@{ WorkspaceRoot = $workspace; StateRoot = $stateRoot; SelectorPath = Join-Path $stateRoot 'payload-prepared.json' }
    $selected = Read-GpuPayloadSelectedRun -Paths $rootPaths -SourceRoot $source -RunDirectory $RunDirectory
    if ($null -eq $selected) { return New-GpuPayloadSummary -Action $Action -State $null -Paths $null }
    $state = $selected.State
    $paths = $selected.Paths

    if ($Action -in @('Status', 'Report')) { return New-GpuPayloadSummary -Action $Action -State $state -Paths $paths }
    if ($null -ne $state.LastFailure -and [string]$state.LastFailure.Action -ceq 'Archive' -and $Action -cne 'Archive') {
        throw 'Archive was interrupted. Finish archiving this run before any new workflow action.'
    }
    if ($Action -in @('Build', 'Sign') -and $null -ne $state.LastFailure -and
        [string]$state.LastFailure.Action -ceq $Action) {
        throw "The prior $Action action failed; its output directory is retained. Archive this run and start a fresh Prepare before retrying."
    }
    if ($Action -ceq 'ValidateVm' -and $null -ne $state.LastFailure -and
        [string]$state.LastFailure.Action -ceq 'ValidateVm') {
        $failedIntent = Get-GpuPayloadObjectProperty -Object $state.LastFailure -Name 'Intent'
        $failedControllerRun = [string](Get-GpuPayloadObjectProperty -Object $failedIntent -Name 'ControllerRunDirectory')
        if (-not [string]::IsNullOrWhiteSpace($failedControllerRun)) {
            $null = Get-GpuPayloadArchiveDisposition -State $state -Paths $paths
        }
    }
    if (-not (Test-GpuPayloadTransition -Action $Action -Phase ([string]$state.Phase) -InProgress $state.InProgress)) {
        throw "Action $Action is blocked for phase $($state.Phase) or an interrupted action is still recorded. Use Report, then Archive this run before starting a fresh Prepare."
    }
    if ($Action -eq 'Archive') {
        try {
            $archiveDisposition = Get-GpuPayloadArchiveDisposition -State $state -Paths $paths
            $state.ArchiveStatus = [string]$archiveDisposition.Status
            $state.ArchiveVmCleanupResultPin = $archiveDisposition.VmCleanupResultPin
            $state.ArchiveReason = [string]$archiveDisposition.Reason
            $archiveProofProperty = $state.PSObject.Properties['ArchivePreControllerFailureProof']
            if ($null -eq $archiveProofProperty) {
                $state | Add-Member -MemberType NoteProperty -Name 'ArchivePreControllerFailureProof' `
                    -Value $archiveDisposition.PreControllerFailureProof
            } else {
                $state.ArchivePreControllerFailureProof = $archiveDisposition.PreControllerFailureProof
            }
            if ($null -ne $state.InProgress) { $state.ArchivedInterruptedAction = $state.InProgress }
            elseif ($null -ne $state.LastFailure -and $state.LastFailure.Intent) { $state.ArchivedInterruptedAction = $state.LastFailure.Intent }
            $state.InProgress = [pscustomobject]@{
                Action = 'Archive'; StartedUtc = [DateTime]::UtcNow.ToString('o')
                PhaseBefore = [string]$state.Phase
            }
            Save-GpuPayloadState -State $state -Paths $paths
            return Invoke-GpuPayloadArchive -State $state -Paths $paths
        }
        catch {
            $state.LastFailure = [pscustomobject]@{
                Action = 'Archive'; Failure = $_.Exception.Message; FailedUtc = [DateTime]::UtcNow.ToString('o')
                Intent = $state.InProgress; PriorFailure = $state.LastFailure
            }
            $state.InProgress = $null
            try { Save-GpuPayloadState -State $state -Paths $paths } catch { }
            throw
        }
    }
    if ($Action -eq 'ValidateVm') {
        if ([string]::IsNullOrWhiteSpace($CredentialStateRoot)) { $CredentialStateRoot = Join-Path $workspace '.analysis\host-runner' }
    }
    if ($Action -in @('Build', 'Sign', 'ValidateVm')) { Assert-GpuPayloadPinsCurrent -State $state -Paths $paths }

    $actionDirectory = $null
    try {
        $actionDirectory = Start-GpuPayloadMutation -State $state -Paths $paths -Action $Action
        $script:CurrentSourceRoot = $source
        switch ($Action) {
            'Build' { $data = Invoke-GpuPayloadBuild -State $state -Paths $paths -ActionDirectory $actionDirectory }
            'Sign' { $data = Invoke-GpuPayloadSign -State $state -Paths $paths -ActionDirectory $actionDirectory }
            'ValidateVm' { $data = Invoke-GpuPayloadValidateVm -State $state -Paths $paths -ActionDirectory $actionDirectory -VmxPath $VmxPath -CredentialStateRoot $CredentialStateRoot }
        }
        $null = Assert-GpuPayloadSourcePins -SourceRoot $source -ExpectedPins $state.SourcePins
        return Complete-GpuPayloadMutation -State $state -Paths $paths -Action $Action -ActionDirectory $actionDirectory -Data $data
    }
    catch {
        $message = $_.Exception.Message
        Fail-GpuPayloadMutation -State $state -Paths $paths -Action $Action -ActionDirectory $actionDirectory -Message $message
        return [pscustomobject]@{
            Action = $Action; Status = 'FAILED'; Succeeded = $false; Phase = [string]$state.Phase
            RunId = [string]$state.RunId; RunDirectory = [string]$state.RunDirectory
            JournalPath = [string]$paths.JournalPath; ActionDirectory = $actionDirectory
            Failure = $message; InProgress = $state.InProgress; LastFailure = $state.LastFailure
        }
    }
}

Export-ModuleMember -Function @(
    'Get-GpuPayloadWorkspaceRoot',
    'Get-GpuPayloadPaths',
    'Test-GpuPayloadTransition',
    'Get-GpuPayloadNextPhase',
    'Get-GpuPayloadSourcePins',
    'Assert-GpuPayloadSourcePins',
    'Test-GpuPayloadPreparationCompatibility',
    'Test-GpuPayloadVmxPath',
    'Assert-GpuPayloadFreshOutput',
    'Assert-GpuPayloadArtifactPins',
    'Test-GpuPayloadCompletionProof',
    'Assert-GpuPayloadVmResult',
    'Get-GpuPayloadArchiveDisposition',
    'Invoke-GpuPayloadAction'
)
