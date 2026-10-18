$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$worktreeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testOutputRoot = Join-Path $worktreeRoot ('spoof-handoff\reports\activation-repair-20261018\pathb-build-tests\' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($testOutputRoot)
$script:checks = New-Object System.Collections.Generic.List[string]
$failure = $null

function Check([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:checks.Add($Message)
}

function Get-FunctionSource([string]$Path,[string]$Name) {
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    if ($errors.Count -gt 0) { throw "Could not parse $Path`: $($errors[0].Message)" }
    $found = @($ast.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name},$true))
    if ($found.Count -ne 1) { throw "Expected one $Name function; found $($found.Count)." }
    return [scriptblock]::Create($found[0].Extent.Text)
}

function Write-Utf8([string]$Path,[string]$Value) {
    $parent = Split-Path -Parent $Path
    [void][IO.Directory]::CreateDirectory($parent)
    [IO.File]::WriteAllText($Path,$Value,[Text.UTF8Encoding]::new($false))
}

function Assert-Throws([scriptblock]$Action,[string]$ExpectedText,[string]$Message) {
    $actual = $null
    try { & $Action | Out-Null } catch { $actual = $_.Exception.Message }
    Check ($null -ne $actual -and $actual.Contains($ExpectedText)) $Message
}

try {
    $runnerPath = Join-Path $PSScriptRoot 'run.ps1'
    $runnerAst = $null
    $tokens = $null
    $parseErrors = $null
    $runnerAst = [System.Management.Automation.Language.Parser]::ParseFile($runnerPath,[ref]$tokens,[ref]$parseErrors)
    if ($parseErrors.Count -gt 0) { throw "Combined runner does not parse: $($parseErrors[0].Message)" }
    Import-Module (Join-Path $PSScriptRoot 'model.psm1') -Force
    . (Get-FunctionSource $runnerPath 'Get-CombinedHostSha256')
    . (Get-FunctionSource $runnerPath 'Get-CombinedPathBBuildLayout')
    . (Get-FunctionSource $runnerPath 'Assert-CombinedPathBBuildOutputs')
    . (Get-FunctionSource $runnerPath 'Assert-CombinedPathBPreparedBuild')
    . (Get-FunctionSource $runnerPath 'Invoke-CombinedPathB')
    . (Get-FunctionSource $runnerPath 'Install-CombinedVmPinnedPathBArtifacts')

    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $worktreeRoot '..\..\..'))
    $script:RepositoryRoot = $repositoryRoot
    $script:WorktreeRoot = $worktreeRoot
    $fakeRun = Join-Path $testOutputRoot 'combined-run'
    [void][IO.Directory]::CreateDirectory($fakeRun)
    $script:mockBuildSummaryMode = 'pass'
    $script:capturedBuildArguments = $null
    function Assert-CombinedExecutionSourcePins { return $true }
    function Invoke-CombinedPowerShellChild {
        param([string]$ScriptPath,[string[]]$Arguments,[string]$LogDirectory,[string]$Name)
        $script:capturedBuildArguments = @($Arguments)
        $script:capturedBuildLogDirectory = $LogDirectory
        $loaderCount = if ($script:mockBuildSummaryMode -ceq 'pass') { 28 } else { 27 }
        $inventoryCount = 2
        $stdout = "100% tests passed, 0 tests failed out of $loaderCount`r`n100% tests passed, 0 tests failed out of $inventoryCount`r`nBuild and host CTest completed. No ESP was mounted and no firmware state was changed.`r`n"
        $stdoutPath = Join-Path $LogDirectory ($Name + '.stdout.txt')
        $stderrPath = Join-Path $LogDirectory ($Name + '.stderr.txt')
        Write-Utf8 $stdoutPath $stdout
        Write-Utf8 $stderrPath ''
        return [pscustomobject]@{name=$Name;exitCode=0;stdout=$stdout;stderr='';stdoutPath=$stdoutPath;stderrPath=$stderrPath}
    }

    $layout = Get-CombinedPathBBuildLayout -Worktree $worktreeRoot -RunDirectory $fakeRun
    $layoutAgain = Get-CombinedPathBBuildLayout -Worktree $worktreeRoot -RunDirectory $fakeRun
    $otherRun = Join-Path $testOutputRoot 'combined-run-other'
    [void][IO.Directory]::CreateDirectory($otherRun)
    $otherLayout = Get-CombinedPathBBuildLayout -Worktree $worktreeRoot -RunDirectory $otherRun
    Check ($layout.buildRoot -ceq $layoutAgain.buildRoot -and $layout.identity -match '\A[0-9a-f]{16}\z') 'Path-B build layout is deterministic for its run and uses a 16-hex identity'
    Check ($layout.buildRoot -cne $otherLayout.buildRoot) 'separate combined runs receive isolated build roots'
    $artifactBoundary = [IO.Path]::GetFullPath((Join-Path $worktreeRoot 'spoof-handoff\artifacts')).TrimEnd('\') + '\'
    Check ($layout.buildRoot.StartsWith($artifactBoundary,[StringComparison]::OrdinalIgnoreCase) -and $layout.buildRoot.EndsWith('\cb\' + $layout.identity,[StringComparison]::OrdinalIgnoreCase)) 'short build root stays under worktree spoof-handoff artifacts'
    Check ($layout.loaderBuild.EndsWith('\l',[StringComparison]::OrdinalIgnoreCase) -and $layout.inventoryBuild.EndsWith('\i',[StringComparison]::OrdinalIgnoreCase) -and $layout.temporaryRoot.EndsWith('\t',[StringComparison]::OrdinalIgnoreCase)) 'loader, inventory, and TEMP use the measured short path leaves'
    Check ($layout.maximumProbePathLength -le 240 -and $layout.maximumProbePathBudget -eq 240 -and $layout.temporaryRoot.Length -le 200) 'predicted CMake probe and temporary paths stay below explicit limits'

    $previousTemp = [Environment]::GetEnvironmentVariable('TEMP','Process')
    $previousTmp = [Environment]::GetEnvironmentVariable('TMP','Process')
    try {
        $pathBuild = Invoke-CombinedPathB -Journal ([pscustomobject]@{runDirectory=$fakeRun}) -Mode 'Build'
    } finally {
        [Environment]::SetEnvironmentVariable('TEMP',$previousTemp,'Process')
        [Environment]::SetEnvironmentVariable('TMP',$previousTmp,'Process')
    }
    $expectedArguments = @('-Mode','Build','-LoaderBuildDir',$layout.loaderBuild,'-InventoryBuildDir',$layout.inventoryBuild,'-MountLetter','S')
    Check (($script:capturedBuildArguments -join "`n") -ceq ($expectedArguments -join "`n")) 'stock Path-B Build receives the exact short loader and inventory paths'
    Check ($script:capturedBuildLogDirectory -ceq (Join-Path $fakeRun 'actions')) 'Path-B build evidence is logged under its combined run'
    Check ([Environment]::GetEnvironmentVariable('TEMP','Process') -ceq $previousTemp -and [Environment]::GetEnvironmentVariable('TMP','Process') -ceq $previousTmp) 'short TEMP and TMP overrides are restored after Build'

    foreach ($artifact in @(
        [pscustomobject]@{name='inventory_loader.efi';path=(Join-Path $layout.loaderBuild 'inventory_loader.efi')},
        [pscustomobject]@{name='ib-install.exe';path=(Join-Path $layout.loaderBuild 'ib-install.exe')},
        [pscustomobject]@{name='inventory.efi';path=(Join-Path $layout.inventoryBuild 'efi\inventory.efi')},
        [pscustomobject]@{name='efi_inventory.exe';path=(Join-Path $layout.inventoryBuild 'efi_inventory.exe')}
    )) { Write-Utf8 $artifact.path ('owned build output:' + $artifact.name) }
    $prepared = Assert-CombinedPathBBuildOutputs -Journal ([pscustomobject]@{runDirectory=$fakeRun}) -Build $pathBuild
    Check ($prepared.status -ceq 'BUILD_AND_CTEST_PASS' -and $prepared.ctestPassed -eq 30 -and $prepared.artifacts.Count -eq 4) 'Prepare records complete stock outputs only after exact 28+2 CTest passes'
    $preparedJournal = [pscustomobject]@{runDirectory=$fakeRun;pathBPreparedBuild=$prepared}
    Check ((Assert-CombinedPathBPreparedBuild -Journal $preparedJournal -Repository $repositoryRoot -Worktree $worktreeRoot).ctestPassed -eq 30) 'prepared build and evidence hashes are revalidated from their owned paths'
    Write-Utf8 (Join-Path $layout.loaderBuild 'inventory_loader.efi') 'modified after prepare'
    Assert-Throws { Assert-CombinedPathBPreparedBuild -Journal $preparedJournal -Repository $repositoryRoot -Worktree $worktreeRoot } 'artifact changed after Prepare' 'changed prepared output is rejected before a lifecycle action'

    $script:mockBuildSummaryMode = 'mismatch'
    $mismatchRun = Join-Path $testOutputRoot 'mismatch-run'
    [void][IO.Directory]::CreateDirectory($mismatchRun)
    $mismatchBuild = Invoke-CombinedPathB -Journal ([pscustomobject]@{runDirectory=$mismatchRun}) -Mode 'Build'
    Assert-Throws { Assert-CombinedPathBBuildOutputs -Journal ([pscustomobject]@{runDirectory=$mismatchRun}) -Build $mismatchBuild } 'CTest summaries mismatch' 'a CTest count mismatch fails Prepare before outputs can be trusted'
    Assert-Throws { Invoke-CombinedPathB -Journal ([pscustomobject]@{runDirectory=$mismatchRun}) -Mode 'Build' } 'Refusing to reuse' 'a repeated Build cannot reuse the same run workspace'

    # Exercise the production copy routine with a valid owned source/destination
    # pair, then prove failures leave the destination bytes intact.
    $installRun = Join-Path $testOutputRoot 'install-run'
    [void][IO.Directory]::CreateDirectory($installRun)
    $installLayout = Get-CombinedPathBBuildLayout -Worktree $worktreeRoot -RunDirectory $installRun
    $script:mockBuildSummaryMode = 'pass'
    $installPathBuild = Invoke-CombinedPathB -Journal ([pscustomobject]@{runDirectory=$installRun}) -Mode 'Build'
    [void][IO.Directory]::CreateDirectory($installLayout.loaderBuild)
    [void][IO.Directory]::CreateDirectory($installLayout.inventoryBuild)
    foreach ($artifact in @(
        [pscustomobject]@{name='inventory_loader.efi';path=(Join-Path $installLayout.loaderBuild 'inventory_loader.efi')},
        [pscustomobject]@{name='ib-install.exe';path=(Join-Path $installLayout.loaderBuild 'ib-install.exe')},
        [pscustomobject]@{name='inventory.efi';path=(Join-Path $installLayout.inventoryBuild 'efi\inventory.efi')},
        [pscustomobject]@{name='efi_inventory.exe';path=(Join-Path $installLayout.inventoryBuild 'efi_inventory.exe')}
    )) { Write-Utf8 $artifact.path ('prepared:' + $artifact.name) }
    $installPrepared = Assert-CombinedPathBBuildOutputs -Journal ([pscustomobject]@{runDirectory=$installRun}) -Build $installPathBuild
    $preparedInstallJournal = [pscustomobject]@{runDirectory=$installRun;pathBPreparedBuild=$installPrepared;pathBBuild=$null}
    Check ((Assert-CombinedPathBPreparedBuild -Journal $preparedInstallJournal -Repository $repositoryRoot -Worktree $worktreeRoot).status -ceq 'BUILD_AND_CTEST_PASS') 'original prepared artifacts are accepted before Arm'
    $vmSourceRoot = Join-Path $worktreeRoot ('spoof-handoff\artifacts\pathb-build-tests\' + [Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($vmSourceRoot)
    $sourceRecords = [Collections.Generic.List[object]]::new()
    foreach ($name in @('inventory_loader.efi','ib-install.exe')) {
        $sourcePath = Join-Path $vmSourceRoot $name
        $destinationPath = Join-Path $installLayout.loaderBuild $name
        Write-Utf8 $sourcePath ('tested:' + $name)
        Write-Utf8 $destinationPath ('native:' + $name)
        $sourceRecords.Add([pscustomobject]@{name=$name;path=$sourcePath;sha256=(Get-CombinedHostSha256 $sourcePath)})
    }
    $installJournal = [pscustomobject]@{runDirectory=$installRun;vmGate=[pscustomobject]@{pathBArtifacts=@($sourceRecords.ToArray())}}
    $installed = @(Install-CombinedVmPinnedPathBArtifacts -Journal $installJournal -Repository $repositoryRoot -Worktree $worktreeRoot -BuildRoot $installLayout.buildRoot)
    $copyMatches = $installed.Count -eq 2
    foreach ($record in $installed) {
        $source = @($sourceRecords.ToArray() | Where-Object { $_.name -ceq $record.name })[0]
        if ((Get-CombinedHostSha256 $record.destination) -cne $source.sha256 -or -not $record.nativeBuildBackup -or (Get-CombinedHostSha256 $record.nativeBuildBackup) -cne $record.nativeBuildSha256) { $copyMatches = $false }
    }
    Check $copyMatches 'VM-pinned files copy only into the prepared short loader directory with native bytes backed up and verified'
    $collectorPath = Join-Path $installLayout.inventoryBuild 'efi\inventory.efi'
    Write-Utf8 $collectorPath 'prepared host payload'
    $activePathBuild = [pscustomobject]@{
        loaderBuild=$installLayout.loaderBuild
        inventoryBuild=$installLayout.inventoryBuild
        pinnedVehicleArtifacts=$installed
        loaderSha256=(Get-CombinedHostSha256 (Join-Path $installLayout.loaderBuild 'inventory_loader.efi'))
        collectorSha256=(Get-CombinedHostSha256 $collectorPath)
        payloadSha256=(Get-CombinedHostSha256 $collectorPath)
    }
    $activeJournal = [pscustomobject]@{runDirectory=$installRun;pathBPreparedBuild=$installPrepared;pathBBuild=$activePathBuild;vmGate=[pscustomobject]@{pathBArtifacts=@($sourceRecords.ToArray())};hostPayload=[pscustomobject]@{payloadSha256=$activePathBuild.payloadSha256}}
    Check ((Assert-CombinedPathBPreparedBuild -Journal $activeJournal -Repository $repositoryRoot -Worktree $worktreeRoot).status -ceq 'BUILD_AND_CTEST_PASS') 'legitimate post-Arm VM-pinned loaders and host-payload collector pass against their lifecycle hashes'
    $savedInstalledHash = [string]$installed[0].installedSha256
    $installed[0].installedSha256 = '0' * 64
    Assert-Throws { Assert-CombinedPathBPreparedBuild -Journal $activeJournal -Repository $repositoryRoot -Worktree $worktreeRoot } 'does not match its validated VM gate pin' 'post-Arm vehicle hash must match the validated VM gate pin'
    $installed[0].installedSha256 = $savedInstalledHash
    $activeJournal.hostPayload.payloadSha256 = '0' * 64
    Assert-Throws { Assert-CombinedPathBPreparedBuild -Journal $activeJournal -Repository $repositoryRoot -Worktree $worktreeRoot } 'validated prepared host payload hash' 'post-Arm collector hash must match the validated host payload pin'
    $activeJournal.hostPayload.payloadSha256 = $activePathBuild.payloadSha256
    Write-Utf8 (Join-Path $installLayout.loaderBuild 'inventory_loader.efi') 'post-Arm unexpected loader tamper'
    Assert-Throws { Assert-CombinedPathBPreparedBuild -Journal $activeJournal -Repository $repositoryRoot -Worktree $worktreeRoot } 'Path-B artifact changed after Prepare' 'post-Arm artifact tampering remains rejected'
    $tamperedJournal = [pscustomobject]@{runDirectory=$installRun;vmGate=[pscustomobject]@{pathBArtifacts=@([pscustomobject]@{name='inventory_loader.efi';path=$sourceRecords[0].path;sha256=('0' * 64)},$sourceRecords[1])}}
    Assert-Throws { Install-CombinedVmPinnedPathBArtifacts -Journal $tamperedJournal -Repository $repositoryRoot -Worktree $worktreeRoot -BuildRoot $installLayout.buildRoot } 'changed' 'tampered VM-pinned artifact is rejected'
    Assert-Throws { Install-CombinedVmPinnedPathBArtifacts -Journal $installJournal -Repository $repositoryRoot -Worktree $worktreeRoot -BuildRoot (Join-Path $worktreeRoot 'spoof-handoff\artifacts\other') } 'Path-B build output is outside its owned worktree run directory' 'artifact copier rejects a path outside the deterministic run root'

    $prepareFunction = Get-FunctionSource $runnerPath 'Invoke-CombinedOriginalPrepare'
    $prepareText = $prepareFunction.ToString()
    $buildPosition = $prepareText.IndexOf("Invoke-CombinedPathB -Journal `$pathBuildJournal -Mode 'Build'",[StringComparison]::Ordinal)
    $storagePosition = $prepareText.IndexOf("'-Action','Prepare'",[StringComparison]::Ordinal)
    $journalWritePosition = $prepareText.IndexOf('Write-CombinedHostAtomicJson $journalPath $journal',[StringComparison]::Ordinal)
    Check ($buildPosition -ge 0 -and $storagePosition -gt $buildPosition -and $journalWritePosition -gt $storagePosition) 'Prepare runs the stock Path-B Build before external storage Prepare and before it publishes an activation journal'
    $mutationMarker = $false
    $script:mockBuildSummaryMode = 'mismatch'
    $failureRun = Join-Path $testOutputRoot 'failure-run'
    [void][IO.Directory]::CreateDirectory($failureRun)
    try {
        $failedBuild = Invoke-CombinedPathB -Journal ([pscustomobject]@{runDirectory=$failureRun}) -Mode 'Build'
        $null = Assert-CombinedPathBBuildOutputs -Journal ([pscustomobject]@{runDirectory=$failureRun}) -Build $failedBuild
        $mutationMarker = $true
    } catch { }
    Check (-not $mutationMarker) 'a failed or mismatched stock Build aborts the Prepare continuation before later host lifecycle work'
} catch {
    $failure = $_.Exception.ToString()
}

$result = [ordered]@{
    schema = 'combined-host-pathb-build-tests.v1'
    status = if ($null -eq $failure) { 'PASS' } else { 'FAIL' }
    checkCount = $script:checks.Count
    checks = @($script:checks.ToArray())
    failure = $failure
    outputRoot = $testOutputRoot
    scope = 'Pure and mocked path/build orchestration checks; no stock build, reader, elevation, policy, driver, ESP, firmware, boot, or VM action.'
}
$resultPath = Join-Path $testOutputRoot 'result.json'
[IO.File]::WriteAllText($resultPath,($result | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$result | ConvertTo-Json -Depth 8
if ($null -ne $failure) { throw $failure }
