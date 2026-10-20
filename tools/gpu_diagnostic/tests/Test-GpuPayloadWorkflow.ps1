[CmdletBinding()]
param([string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
}
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$WorkflowModulePath = Join-Path $ToolRoot 'GpuPayloadWorkflow.psm1'
$WorkflowScriptPath = Join-Path $ToolRoot 'gpu-payload-test.ps1'
$CloneScriptPath = Join-Path $ToolRoot 'New-GpuPayloadVm.ps1'
foreach ($RequiredPath in @($WorkflowModulePath, $WorkflowScriptPath, $CloneScriptPath)) {
    if (-not (Test-Path -LiteralPath $RequiredPath -PathType Leaf)) {
        throw "GPU payload workflow test input is missing: $RequiredPath"
    }
}
Import-Module (Join-Path $ToolRoot 'GpuHostState.psm1') -Force -ErrorAction Stop
$WorkflowModule = Import-Module $WorkflowModulePath -Force -PassThru -ErrorAction Stop | Select-Object -First 1

$script:Assertions = 0
$script:PassedCases = 0
$script:FailedCases = [Collections.Generic.List[string]]::new()
function Assert-True {
    param([bool]$Condition, [string]$Message)
    $script:Assertions++
    if (-not $Condition) { throw $Message }
}
function Assert-Throws {
    param([scriptblock]$Operation, [string]$MessagePattern, [string]$Description)
    $failure = $null
    try { $null = & $Operation }
    catch { $failure = [string]$_.Exception.Message }
    Assert-True ($null -ne $failure -and $failure -match $MessagePattern) `
        "$Description (observed: $failure)"
}
function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body
        $script:PassedCases++
        Write-Output "PASS: $Name"
    }
    catch {
        $script:FailedCases.Add("$Name`: $($_.Exception.Message)")
        Write-Output "FAIL: $Name`: $($_.Exception.Message)"
    }
}
function Get-TestFilePin {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force
    return [pscustomobject]@{
        Path = [IO.Path]::GetFullPath($Path)
        Sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
        Bytes = [long]$item.Length
    }
}

$WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
$FixtureRoot = Join-Path $WorkspaceRoot ('.codex-build\gpu-payload-workflow-contract-' +
    [Guid]::NewGuid().ToString('N'))
$FixtureRoot = [IO.Path]::GetFullPath($FixtureRoot)
$ExpectedBuildRoot = [IO.Path]::GetFullPath((Join-Path $WorkspaceRoot '.codex-build'))
$OldGitFunction = Get-Command -Name git -CommandType Function -ErrorAction SilentlyContinue
$script:FixtureGitFunctionInstalled = $false

try {
    [void](New-Item -ItemType Directory -Path $FixtureRoot -ErrorAction Stop)

    Invoke-TestCase 'offline copy requires the unique reviewed snapshot to be current' {
        $tokens = $null
        $errors = $null
        $cloneAst = [Management.Automation.Language.Parser]::ParseFile($CloneScriptPath, [ref]$tokens, [ref]$errors)
        if ($errors.Count -ne 0) { throw 'VM preparation script did not parse.' }
        $snapshotFunction = @($cloneAst.FindAll({ param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -ceq 'Get-GpuVmSnapshotSelection'
        }, $true))
        if ($snapshotFunction.Count -ne 1) { throw 'Snapshot parser function was not found exactly once.' }
        . ([scriptblock]::Create($snapshotFunction[0].Extent.Text))
        $snapshotPath = Join-Path $FixtureRoot 'reviewed.vmsd'
        $reviewedName = 'hsinv-default-25h2-26200.9457'
        $validSnapshot = @('snapshot.current = "39"', 'snapshot.numSnapshots = "39"',
            'snapshot38.uid = "39"', ('snapshot38.displayName = "' + $reviewedName + '"'))
        [IO.File]::WriteAllLines($snapshotPath, $validSnapshot)
        $proof = Get-GpuVmSnapshotSelection -VmsdPath $snapshotPath -ExpectedName $reviewedName
        Assert-True ($proof.CurrentUid -ceq '39' -and $proof.SnapshotIndex -ceq '38' -and
            $proof.UniqueCurrentMatch -eq $true) 'valid VMware snapshotN metadata did not select the reviewed state'
        [IO.File]::WriteAllLines($snapshotPath, @($validSnapshot -replace 'snapshot.current = "39"', 'snapshot.current = "38"'))
        Assert-Throws { Get-GpuVmSnapshotSelection -VmsdPath $snapshotPath -ExpectedName $reviewedName } `
            'not already at the uniquely identified' 'a different current snapshot must be rejected'
        [IO.File]::WriteAllLines($snapshotPath, @($validSnapshot + 'snapshot39.uid = "40"' +
            ('snapshot39.displayName = "' + $reviewedName + '"')))
        Assert-Throws { Get-GpuVmSnapshotSelection -VmsdPath $snapshotPath -ExpectedName $reviewedName } `
            'not already at the uniquely identified' 'ambiguous reviewed snapshot names must be rejected'
    }

    Invoke-TestCase 'generated paths are deterministic and confined to their run identity' {
        $runId = 'gpu-payload-20261019T120000Z-0123456789abcdef0123456789abcdef'
        $runDirectory = Join-Path $FixtureRoot $runId
        $paths = Get-GpuPayloadPaths -WorkspaceRoot $FixtureRoot -RunDirectory $runDirectory
        Assert-True ($paths.BuildRoot -ceq (Join-Path $FixtureRoot '.codex-build\gb\0123456789abcdef')) `
            'build directory was not derived from the exact run ID suffix'
        Assert-True ($paths.SignedPayload -ceq (Join-Path $runDirectory 'signed\payload.efi') -and
            $paths.SignedLoader -ceq (Join-Path $runDirectory 'signed\loader.efi')) `
            'signed image paths escaped the selected run'
        Assert-Throws {
            Get-GpuPayloadPaths -WorkspaceRoot $FixtureRoot `
                -RunDirectory (Join-Path $FixtureRoot 'gpu-payload-not-a-generated-id')
        } 'generated gpu-payload run identity' 'malformed run identities are rejected'
    }

    Invoke-TestCase 'phase transition guard admits only the required workflow order' {
        $valid = @(
            @('Prepare', 'New'), @('Build', 'Prepared'), @('Sign', 'Built'),
            @('ValidateVm', 'Signed'), @('Archive', 'Prepared'),
            @('Archive', 'Built'), @('Archive', 'Signed'), @('Archive', 'Complete'),
            @('Status', 'New'), @('Report', 'Archived')
        )
        foreach ($entry in $valid) {
            Assert-True (Test-GpuPayloadTransition -Action $entry[0] -Phase $entry[1] -InProgress $null) `
                "expected transition $($entry[0]) from $($entry[1])"
        }
        $invalid = @(
            @('Build', 'New'), @('Build', 'Signed'), @('Sign', 'Prepared'),
            @('ValidateVm', 'Built'), @('Prepare', 'Signed'), @('Archive', 'New'),
            @('Unknown', 'Prepared')
        )
        foreach ($entry in $invalid) {
            Assert-True (-not (Test-GpuPayloadTransition -Action $entry[0] -Phase $entry[1] -InProgress $null)) `
                "unexpected transition $($entry[0]) from $($entry[1])"
        }
        foreach ($action in @('Prepare', 'Build', 'Sign', 'ValidateVm', 'Archive')) {
            Assert-True (-not (Test-GpuPayloadTransition -Action $action -Phase 'Signed' `
                    -InProgress ([pscustomobject]@{ Action = 'Interrupted' }))) `
                "$action was allowed with an interrupted mutation recorded"
        }
        Assert-True (-not (Test-GpuPayloadTransition -Action 'ValidateVm' -Phase 'Signed' `
                -InProgress 'ValidateVm')) 'ValidateVm was allowed while its prior attempt remained in progress'
        Assert-True ((Get-GpuPayloadNextPhase -Action 'ValidateVm') -ceq 'Complete') `
            'successful SignedNative VM validation must be the only transition to Complete'
        Assert-True ((Get-GpuPayloadNextPhase -Action 'Archive') -ceq 'Archived') `
            'Archive must be the terminal transition'
        Assert-True (Test-GpuPayloadVmxPath -Path 'C:\vm\diagnostic.vmx') `
            'a normal VMX configuration path should pass validation'
        Assert-True (-not (Test-GpuPayloadVmxPath -Path 'C:\vm\diagnostic.vmx.bak')) `
            'a backup file must not pass as a VMX configuration'
    }

    Invoke-TestCase 'WhatIf previews perform no filesystem or native actions' {
        $missingWorkspace = Join-Path $FixtureRoot 'never-created-workspace'
        $missingCredentialRoot = Join-Path $FixtureRoot 'never-created-credentials'
        $runDirectory = Join-Path $missingWorkspace 'gpu-payload-20261019T120000Z-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
        $workflowPreview = @(& $WorkflowScriptPath Prepare -WorkspaceRoot $missingWorkspace `
            -RunDirectory $runDirectory -WhatIf) -join "`n"
        $workflowResult = $workflowPreview | ConvertFrom-Json
        Assert-True ($workflowResult.Preview -eq $true -and
            $workflowResult.FilesystemChangesPerformed -eq $false -and
            $workflowResult.NativeCommandsExecuted -eq $false -and
            $workflowResult.VmStarted -eq $false) `
            'workflow preview did not report zero side effects'
        Assert-True (-not (Test-Path -LiteralPath $missingWorkspace) -and
            -not (Test-Path -LiteralPath $runDirectory)) `
            'workflow preview created a workspace, run, selector, or journal'

        $cloneRun = Join-Path $FixtureRoot 'never-created-clone'
        $clonePreview = @(& $CloneScriptPath -WorkspaceRoot $missingWorkspace `
            -RunDirectory $cloneRun -CredentialStateRoot $missingCredentialRoot `
            -VmrunPath (Join-Path $FixtureRoot 'never-run-vmrun.exe') -WhatIf) -join "`n"
        $cloneResult = $clonePreview | ConvertFrom-Json
        Assert-True ($cloneResult.Preview -eq $true -and
            $cloneResult.HostBootChanges -eq $false -and $cloneResult.VmChanges -eq $false) `
            'clone preview did not report zero host and VM changes'
        Assert-True (-not (Test-Path -LiteralPath $missingWorkspace) -and
            -not (Test-Path -LiteralPath $cloneRun) -and
            -not (Test-Path -LiteralPath $missingCredentialRoot)) `
            'clone preview touched the workspace, VM, or credential state'
    }

    Invoke-TestCase 'fresh outputs reject existing and out-of-root targets' {
        $safeRoot = Join-Path $FixtureRoot 'output-root'
        $safeSubdirectory = Join-Path $safeRoot 'build'
        $outside = Join-Path $FixtureRoot 'outside-output.bin'
        [void](New-Item -ItemType Directory -Path $safeSubdirectory -Force)
        $fresh = Assert-GpuPayloadFreshOutput -Path (Join-Path $safeSubdirectory 'new.efi') -Root $safeRoot
        Assert-True ($fresh -ceq [IO.Path]::GetFullPath((Join-Path $safeSubdirectory 'new.efi'))) `
            'safe fresh output was not accepted'
        [IO.File]::WriteAllBytes($outside, [byte[]]@(1, 2, 3))
        Assert-Throws {
            Assert-GpuPayloadFreshOutput -Path $outside -Root $safeRoot
        } 'outside|path boundary|remain within' 'outputs outside the designated root are rejected'
        $existing = Join-Path $safeSubdirectory 'existing.bin'
        [IO.File]::WriteAllBytes($existing, [byte[]]@(4, 5))
        Assert-Throws {
            Assert-GpuPayloadFreshOutput -Path $existing -Root $safeRoot
        } 'existing output|overwrite' 'existing output files are never overwritten'
    }

    Invoke-TestCase 'artifact pins bind exact path, length, and content hash' {
        $artifactRoot = Join-Path $FixtureRoot 'artifact-pins'
        [void](New-Item -ItemType Directory -Path $artifactRoot)
        $efiPath = Join-Path $artifactRoot 'uefi_hypervisor.efi'
        $loaderPath = Join-Path $artifactRoot 'loader.efi'
        [IO.File]::WriteAllBytes($efiPath, [byte[]]@(0x4d, 0x5a, 1, 2, 3))
        [IO.File]::WriteAllBytes($loaderPath, [byte[]]@(0x4d, 0x5a, 6, 7))
        $redirectedPath = Join-Path $FixtureRoot 'outside-artifact.efi'
        [IO.File]::WriteAllBytes($redirectedPath, [byte[]]@(0x4d, 0x5a, 9, 8))
        $pins = [pscustomobject]@{
            HypervisorEfi = Get-TestFilePin $efiPath
            Loader = Get-TestFilePin $loaderPath
        }
        $expected = [pscustomobject]@{ HypervisorEfi = $efiPath; Loader = $loaderPath }
        Assert-GpuPayloadArtifactPins -Pins $pins -ExpectedPaths $expected
        Assert-True $true 'unchanged exact artifact pins pass'
        [IO.File]::WriteAllBytes($loaderPath, [byte[]]@(0x4d, 0x5a, 9, 8))
        Assert-Throws {
            Assert-GpuPayloadArtifactPins -Pins $pins -ExpectedPaths $expected
        } 'changed|pin|hash' 'artifact content changes after pin capture are rejected'
        Assert-Throws {
            Assert-GpuPayloadArtifactPins -Pins $pins `
                -ExpectedPaths ([pscustomobject]@{ HypervisorEfi = $efiPath; Loader = $redirectedPath })
        } 'path differs|pin' 'artifact pins cannot be redirected to a different output path'
    }

    Invoke-TestCase 'source pin assertion detects any pinned source byte change' {
        $sourceFixture = Join-Path $FixtureRoot 'synthetic-source'
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
        foreach ($relative in $relativeFiles.Values) {
            $path = Join-Path $sourceFixture $relative
            [void](New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force)
            [IO.File]::WriteAllBytes($path, [byte[]]@(0x01, 0x02, 0x03, 0x04))
        }

        function global:git {
            param([Parameter(ValueFromRemainingArguments = $true)][string[]]$GitArguments)
            if ($GitArguments.Count -lt 4 -or $GitArguments[0] -cne '-C') {
                throw 'Unexpected synthetic Git invocation.'
            }
            $argumentsText = $GitArguments[1..($GitArguments.Count - 1)] -join ' '
            $global:LASTEXITCODE = 0
            if ($GitArguments[2] -ceq 'merge-base' -and $GitArguments[3] -ceq '--is-ancestor') {
                if (-not $script:MockPreparationIsAncestor) { $global:LASTEXITCODE = 1 }
                return
            }
            if ($GitArguments[2] -ceq 'rev-parse' -and $GitArguments.Count -eq 4 -and
                [string]$GitArguments[3] -match '^[0-9a-f]{40}:.+$') {
                $spec = [string]$GitArguments[3]
                if ($script:MockPreparationBlobs.ContainsKey($spec)) { return [string]$script:MockPreparationBlobs[$spec] }
                $global:LASTEXITCODE = 1
                return
            }
            if ($argumentsText -match 'rev-parse --show-toplevel$') { return [IO.Path]::GetFullPath($GitArguments[1]) }
            if ($argumentsText -match 'branch --show-current$') { return 'feat/smbios-boot-serial-probe' }
            if ($argumentsText -match 'rev-parse HEAD$') { return '0123456789abcdef0123456789abcdef01234567' }
            if ($argumentsText -match 'status --porcelain --untracked-files=all$') {
                if ($script:MockGitDirty) { return ' M synthetic-file' }
                return
            }
            $global:LASTEXITCODE = 1
            throw "Unexpected synthetic Git arguments: $argumentsText"
        }
        $script:FixtureGitFunctionInstalled = $true
        $script:MockGitDirty = $false
        $sourcePins = Get-GpuPayloadSourcePins -SourceRoot $sourceFixture
        $null = Assert-GpuPayloadSourcePins -SourceRoot $sourceFixture -ExpectedPins $sourcePins
        Assert-True $true 'unchanged synthetic source pins pass'
        $tamperedSourcePath = [string]$sourcePins.Files.WorkflowModule.Path
        [IO.File]::WriteAllBytes($tamperedSourcePath, [byte[]]@(0x01, 0x02, 0xff, 0x04))
        Assert-Throws {
            Assert-GpuPayloadSourcePins -SourceRoot $sourceFixture -ExpectedPins $sourcePins
        } 'changed|pin|hash' 'tampered pinned source files are rejected'
        [IO.File]::WriteAllBytes($tamperedSourcePath, [byte[]]@(0x01, 0x02, 0x03, 0x04))
        $script:MockGitDirty = $true
        Assert-Throws {
            Assert-GpuPayloadSourcePins -SourceRoot $sourceFixture -ExpectedPins $sourcePins
        } 'clean|porcelain|changed' 'a dirty source tree is rejected even when pinned file bytes match'
    }

    Invoke-TestCase 'VM preparation is reusable only across an ancestor with identical preparation blobs' {
        $compatibilityGitCommand = Get-Command -Name git -CommandType Function -ErrorAction SilentlyContinue
        $compatibilityGit = if ($null -ne $compatibilityGitCommand) { [string]$compatibilityGitCommand.Definition } else { $null }
        function global:git {
            param([Parameter(ValueFromRemainingArguments = $true)][string[]]$GitArguments)
            if ($GitArguments.Count -lt 4 -or $GitArguments[0] -cne '-C') {
                throw 'Unexpected VM preparation compatibility Git invocation.'
            }
            $global:LASTEXITCODE = 0
            if ($GitArguments[2] -ceq 'merge-base' -and $GitArguments[3] -ceq '--is-ancestor') {
                if (-not $script:MockPreparationIsAncestor) { $global:LASTEXITCODE = 1 }
                return
            }
            if ($GitArguments[2] -ceq 'rev-parse' -and $GitArguments.Count -eq 4) {
                $spec = [string]$GitArguments[3]
                if ($script:MockPreparationBlobs.ContainsKey($spec)) { return [string]$script:MockPreparationBlobs[$spec] }
                $global:LASTEXITCODE = 1
                return
            }
            $global:LASTEXITCODE = 1
            throw 'Unexpected VM preparation compatibility Git arguments.'
        }
        try {
            $sourceFixture = Join-Path $FixtureRoot 'compatibility-source'
            [void](New-Item -ItemType Directory -Path $sourceFixture -Force)
            $preparationCommit = '1' * 40
            $currentCommit = '2' * 40
            $script:MockPreparationIsAncestor = $true
            $script:MockPreparationBlobs = @{}
            $blobIndex = 1
            foreach ($relativePath in @(
                'tools\gpu_diagnostic\New-GpuPayloadVm.ps1',
                'tools\gpu_diagnostic\GpuHostState.psm1',
                'tools\host-runner\HostRunner.Core.psm1',
                'tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'
            )) {
                $blob = ('{0:x40}' -f $blobIndex)
                $gitPath = $relativePath.Replace('\', '/')
                $script:MockPreparationBlobs[('{0}:{1}' -f $preparationCommit, $gitPath)] = $blob
                $script:MockPreparationBlobs[('{0}:{1}' -f $currentCommit, $gitPath)] = $blob
                $blobIndex++
            }
            $proof = Test-GpuPayloadPreparationCompatibility -SourceRoot $sourceFixture `
                -PreparationCommit $preparationCommit -CurrentCommit $currentCommit
            Assert-True ($proof.Status -ceq 'PREPARATION_CODE_COMPATIBLE' -and
                $proof.PreparationIsAncestor -eq $true -and $proof.Files.Count -eq 4 -and
                @($proof.Files | Where-Object { -not $_.Identical }).Count -eq 0) `
                'compatible preparation proof did not accept the unchanged blob set across a newer commit'

            $changedSpec = '{0}:{1}' -f $currentCommit, 'tools/host-runner/HostRunner.Core.psm1'
            $script:MockPreparationBlobs[$changedSpec] = 'f' * 40
            Assert-Throws {
                Test-GpuPayloadPreparationCompatibility -SourceRoot $sourceFixture `
                    -PreparationCommit $preparationCommit -CurrentCommit $currentCommit
            } 'preparation code changed' 'changed preparation code must block copy reuse'

            $script:MockPreparationBlobs.Remove($changedSpec)
            Assert-Throws {
                Test-GpuPayloadPreparationCompatibility -SourceRoot $sourceFixture `
                    -PreparationCommit $preparationCommit -CurrentCommit $currentCommit
            } 'could not resolve.*blob IDs' 'missing preparation blob must block copy reuse'

            $script:MockPreparationIsAncestor = $false
            Assert-Throws {
                Test-GpuPayloadPreparationCompatibility -SourceRoot $sourceFixture `
                    -PreparationCommit $preparationCommit -CurrentCommit $currentCommit
            } 'not an ancestor' 'unrelated source history must block copy reuse'
        }
        finally {
            if ($null -ne $compatibilityGit) {
                Set-Item -Path Function:\global:git -Value ([scriptblock]::Create($compatibilityGit))
            } else {
                Remove-Item -Path Function:\global:git -Force -ErrorAction SilentlyContinue
            }
            Remove-Variable MockPreparationIsAncestor, MockPreparationBlobs -Scope Script -ErrorAction SilentlyContinue
        }
    }

    Invoke-TestCase 'ValidateVm mutation intent persists all writable evidence fields before execution' {
        $runId = 'gpu-payload-20261019T120002Z-1234567890abcdef1234567890abcdef'
        $runDirectory = Join-Path (Join-Path $FixtureRoot 'mutation-workspace') $runId
        $paths = Get-GpuPayloadPaths -WorkspaceRoot (Split-Path -Parent $runDirectory) -RunDirectory $runDirectory
        [void](New-Item -ItemType Directory -Path $paths.ActionsRoot -Force)
        $state = [pscustomobject]@{ Phase = 'Signed'; InProgress = $null; LastFailure = $null; UpdatedUtc = $null }
        $actionDirectory = & $WorkflowModule {
            param($testState, $testPaths)
            Start-GpuPayloadMutation -State $testState -Paths $testPaths -Action 'ValidateVm'
        } $state $paths
        $intentNames = @($state.InProgress.PSObject.Properties | ForEach-Object { $_.Name })
        foreach ($name in @('ControllerRunDirectory', 'VmxPath', 'VmxSha256', 'VmPreparationCompatibility')) {
            Assert-True ($intentNames -contains $name -and $null -eq $state.InProgress.$name) `
                "ValidateVm intent did not initialize writable evidence property $name"
        }
        Assert-True ($intentNames -contains 'ControllerInvocationStarted' -and
            $state.InProgress.ControllerInvocationStarted -is [bool] -and
            -not $state.InProgress.ControllerInvocationStarted) `
            'ValidateVm intent did not persist the pre-launch state before any controller invocation'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $actionDirectory 'result.json') -PathType Leaf)) `
            'mutation setup must not fabricate a completed result receipt'
    }

    Invoke-TestCase 'pre-controller failure archival requires pinned VMX and absent NVRAM proof' {
        $workspace = Join-Path $FixtureRoot 'pre-controller-workspace'
        $runId = 'gpu-payload-20261019T120003Z-abcdefabcdefabcdefabcdefabcdefab'
        $runDirectory = Join-Path $workspace ('.analysis\gpu-payload-diagnostic\runs\' + $runId)
        $paths = Get-GpuPayloadPaths -WorkspaceRoot $workspace -RunDirectory $runDirectory
        $vmxDirectory = Join-Path $workspace '.codex-build\gpu_payload_vm\vm-20261019T120000Z-1234abcd\clone'
        $sourceDirectory = Join-Path $workspace '.codex-build\retained-vm'
        [void](New-Item -ItemType Directory -Path $paths.StateRoot -Force)
        [void](New-Item -ItemType Directory -Path $vmxDirectory -Force)
        [void](New-Item -ItemType Directory -Path $sourceDirectory -Force)
        [void](New-Item -ItemType Directory -Path $paths.ControllerRunsRoot -Force)
        $vmxPath = Join-Path $vmxDirectory 'hsinv-Windows-25H2.vmx'
        $sourceVmxPath = Join-Path $sourceDirectory 'hsinv-Windows-25H2.vmx'
        [IO.File]::WriteAllText($vmxPath, 'diagnostic-vmx-fixture')
        [IO.File]::WriteAllText($sourceVmxPath, 'retained-vmx-fixture')
        $vmxPin = Get-TestFilePin $vmxPath
        $sourceVmxPin = Get-TestFilePin $sourceVmxPath
        $commit = '3' * 40
        $receiptPath = Join-Path $paths.StateRoot 'vm-prepared.json'
        [IO.File]::WriteAllText($receiptPath, (ConvertTo-Json -InputObject ([pscustomobject]@{
            SchemaVersion = 1; Status = 'CLONE_PREPARED_TRUST_UNVERIFIED'; SourceCommit = $commit
            VmxPath = $vmxPath; VmxSha256 = $vmxPin.Sha256
            SourceVmx = $sourceVmxPath; SourceVmxSha256 = $sourceVmxPin.Sha256
            HostBootChanges = $false; RetainedVmMoved = $false; TrustValidated = $false
            TrustMechanism = 'VM firmware db append, not MOK enrollment'
        }) -Depth 5), [Text.UTF8Encoding]::new($false))
        $state = [pscustomobject]@{
            RunId = $runId; WorkspaceRoot = $workspace; SourceCommit = $commit; Phase = 'Signed'
            LastFailure = [pscustomobject]@{
                Action = 'ValidateVm'
                Failure = "The property 'ControllerRunDirectory' cannot be found on this object. Verify that the property exists and can be set."
                Intent = [pscustomobject]@{ Action = 'ValidateVm' }
            }
        }
        $archiveProof = Get-GpuPayloadArchiveDisposition -State $state -Paths $paths
        Assert-True ($archiveProof.Status -ceq 'ABORTED_ARCHIVED' -and
            $archiveProof.PreControllerFailureProof.Status -ceq 'PRE_CONTROLLER_FAILURE_NO_VM_STARTED' -and
            $archiveProof.PreControllerFailureProof.LegacyRecovery -eq $true -and
            $archiveProof.PreControllerFailureProof.FreshNvramAbsent -eq $true) `
            'the exact legacy pre-controller failure should archive only with matching VMX pins and no controller/NVRAM evidence'

        $controllerRunPath = Join-Path $paths.ControllerRunsRoot ($runId + '-fd170b1a1994422f809162e89b2dbfeb')
        $knownVmPreflightFailure = [pscustomobject]@{
            RunId = $runId; WorkspaceRoot = $workspace; SourceCommit = $commit; Phase = 'Signed'
            LastFailure = [pscustomobject]@{
                Action = 'ValidateVm';
                Failure = 'Signed loader Authenticode signature is invalid: UnknownError'
                Intent = [pscustomobject]@{
                    Action = 'ValidateVm'; ControllerInvocationStarted = $true
                    ControllerRunDirectory = $controllerRunPath; VmxPath = $vmxPath
                }
            }
        }
        $knownFailureProof = Get-GpuPayloadArchiveDisposition -State $knownVmPreflightFailure -Paths $paths
        Assert-True ($knownFailureProof.Status -ceq 'ABORTED_ARCHIVED' -and
            $knownFailureProof.PreControllerFailureProof.Status -ceq 'PRE_VM_START_FAILURE_NO_VM_BOOT' -and
            $knownFailureProof.PreControllerFailureProof.ControllerRunDirectoryAbsent -and
            $knownFailureProof.PreControllerFailureProof.FreshNvramAbsent -and
            $knownFailureProof.PreControllerFailureProof.VmLockAbsent) `
            'the exact known Authenticode preflight failure can archive only with no controller run, unchanged VMX pins, and no NVRAM/lock'

        $unknownFailure = [pscustomobject]@{
            RunId = $runId; WorkspaceRoot = $workspace; SourceCommit = $commit; Phase = 'Signed'
            LastFailure = [pscustomobject]@{
                Action = 'ValidateVm'; Failure = 'Unexpected controller failure before result';
                Intent = [pscustomobject]@{
                    Action = 'ValidateVm'; ControllerInvocationStarted = $true
                    ControllerRunDirectory = $controllerRunPath; VmxPath = $vmxPath
                }
            }
        }
        Assert-Throws { Get-GpuPayloadArchiveDisposition -State $unknownFailure -Paths $paths } `
            'Path does not exist|controller result|result.json' 'an unknown controller failure without a run result cannot be archived'

        [IO.File]::WriteAllText($vmxPath, 'tampered-diagnostic-vmx-fixture')
        Assert-Throws { Get-GpuPayloadArchiveDisposition -State $knownVmPreflightFailure -Paths $paths } `
            'changed|pins are unchanged' 'modified prepared VMX must block the known preflight archive recovery'
        [IO.File]::WriteAllText($vmxPath, 'diagnostic-vmx-fixture')

        $nvramPath = Join-Path $vmxDirectory 'gpu-payload-test.nvram'
        [IO.File]::WriteAllBytes($nvramPath, [byte[]]@(1))
        Assert-Throws { Get-GpuPayloadArchiveDisposition -State $state -Paths $paths } `
            'NVRAM exists|may have started' 'diagnostic NVRAM existence must block the pre-controller archive proof'
        Assert-Throws { Get-GpuPayloadArchiveDisposition -State $knownVmPreflightFailure -Paths $paths } `
            'NVRAM exists|may have started' 'diagnostic NVRAM existence must block the known VM preflight recovery'
        Remove-Item -LiteralPath $nvramPath -Force

        $lockPath = $vmxPath + '.lck'
        [void](New-Item -ItemType Directory -Path $lockPath)
        Assert-Throws { Get-GpuPayloadArchiveDisposition -State $knownVmPreflightFailure -Paths $paths } `
            'lock artifact exists|may have started' 'diagnostic VMX lock must block the known VM preflight recovery'
        Remove-Item -LiteralPath $lockPath -Recurse -Force

        [void](New-Item -ItemType Directory -Path $controllerRunPath -Force)
        Assert-Throws { Get-GpuPayloadArchiveDisposition -State $knownVmPreflightFailure -Paths $paths } `
            'Path does not exist|result.json' `
            'an existing controller run directory without result cannot use no-result recovery'
        Remove-Item -LiteralPath $controllerRunPath -Recurse -Force

        [void](New-Item -ItemType Directory -Path $controllerRunPath -Force)
        $earlyResultPath = Join-Path $controllerRunPath 'result.json'
        $earlyResult = [pscustomobject]@{
            SchemaVersion = 1; Result = 'FAIL'; ValidationProfile = 'SignedNative'; CleanupFailures = @()
            Vm = [pscustomobject]@{ VmStartAttempted = $false; VmWriteAttempted = $false }
            Snapshots = [pscustomobject]@{ BeforeCreated = $false; AfterCreated = $false; Before = $null; After = $null }
        }
        [IO.File]::WriteAllText($earlyResultPath, (ConvertTo-Json -InputObject $earlyResult -Depth 6), [Text.UTF8Encoding]::new($false))
        $earlyResultState = [pscustomobject]@{
            RunId = $runId; WorkspaceRoot = $workspace; SourceCommit = $commit; Phase = 'Signed'
            LastFailure = [pscustomobject]@{
                Action = 'ValidateVm'; Failure = 'Early SignedNative preflight failed';
                Intent = [pscustomobject]@{
                    Action = 'ValidateVm'; ControllerInvocationStarted = $true
                    ControllerRunDirectory = $controllerRunPath; VmxPath = $vmxPath
                }
            }
        }
        $earlyResultProof = Get-GpuPayloadArchiveDisposition -State $earlyResultState -Paths $paths
        Assert-True ($earlyResultProof.Status -ceq 'ABORTED_ARCHIVED' -and
            $earlyResultProof.PreControllerFailureProof.Status -ceq 'PRE_VM_START_FAILURE_NO_VM_BOOT' -and
            $null -ne $earlyResultProof.PreControllerFailureProof.ControllerResultPin -and
            $null -eq $earlyResultProof.VmCleanupResultPin) `
            'an integrity-pinned controller result may prove failure before any VM start/write without weakening normal cleanup checks'
        $earlyResult.Vm.VmStartAttempted = $true
        [IO.File]::WriteAllText($earlyResultPath, (ConvertTo-Json -InputObject $earlyResult -Depth 6), [Text.UTF8Encoding]::new($false))
        Assert-Throws { Get-GpuPayloadArchiveDisposition -State $earlyResultState -Paths $paths } `
            'snapshot/stop gate|cleanup evidence' 'a controller result claiming a VM start must satisfy normal VM cleanup evidence'
        Remove-Item -LiteralPath $controllerRunPath -Recurse -Force
    }

    Invoke-TestCase 'VM admission requires authenticated SignedNative evidence and cleanup gates' {
        function New-TestSignedNativeVmResult {
            $handoff = @(
                'schema=1'
                'state=READY_BOOT_MANAGER'
                'restore=PASS'
                'payload=PASS'
            ) -join "`r`n"
            $diagnostic = @(
                'schema=1'
                'payload_launch=ok'
                'payload_status=0x0000000000000000'
                'shim_load_phase=firmware_load'
                'shim_load_status=0x0000000000000000'
            ) -join "`r`n"
            $byteEncoding = [Text.Encoding]::GetEncoding(28591)
            return [pscustomobject]@{
                SchemaVersion = 1
                Result = 'PASS'
                ValidationProfile = 'SignedNative'
                SignedNativePassed = $true
                CleanupFailures = @()
                Snapshots = [pscustomobject]@{
                    BeforeCreated = $true; AfterCreated = $true
                    RevertedBefore = $true; VmOffConfirmed = $true
                    Before = 'before-fixture'; After = 'after-fixture'
                }
                Vm = [pscustomobject]@{
                    NestedVirtualization = $true; Network = 'disabled'
                    OriginalSecureBoot = $true; Passthrough = 'none'
                    VmxPath = (Join-Path $FixtureRoot 'diagnostic.vmx')
                }
                Phases = [pscustomobject]@{
                    Inspect = 'PASS'; Stage = 'PASS'
                    SignedNativeEfi = 'PASS'; WindowsStartup = 'PASS'
                }
                HalResults = [pscustomobject]@{
                    SignedNativeEfiAdmissionMarkers = 'PASS'
                    DmaIsolationAdmission = 'EXTERNAL_VMM_ADMISSION'
                    VmwareDbAppendMarker = 'PASS_LOG_MARKER'
                    NativeGpuBackend = 'NOT_EXERCISED_EXTERNAL_VMM'
                    ActualVmxRootWcRenderDrainAndUnmap = 'NOT_EXERCISED_EXTERNAL_VMM'
                    IntelPhysicalPlaneAndDmaEnforcement = 'NOT_TESTED_BY_THIS_VM'
                    HostDualGpuTopology = 'NOT_TESTED_BY_THIS_VM'
                }
                WindowsStartupEvidence = [pscustomobject]@{
                    ValidationProfile = 'SignedNative'; Status = 'PASS'
                    SecureBoot = $true; ExpectedSecureBoot = $true
                    ExpectedCertificateDerPresent = $true
                    ExpectedCertificateSha256 = 'A' * 64
                    CertificateDbInspection = 'BOUNDED_RAW_DER_SUBSEQUENCE_ONLY'
                    HandoffStatusBytes = $byteEncoding.GetByteCount($handoff)
                    HandoffStatusCapturedBytes = $byteEncoding.GetByteCount($handoff)
                    HandoffStatusTooLarge = $false
                    PayloadDiagnosticBytes = $byteEncoding.GetByteCount($diagnostic)
                    PayloadDiagnosticCapturedBytes = $byteEncoding.GetByteCount($diagnostic)
                    PayloadDiagnosticTooLarge = $false
                    DiagnosticTextEncoding = 'ISO-8859-1 byte-preserving'
                    EfiMarkers = @(
                        'uefi hypervisor: DMA isolation admission: PASS'
                        'uefi hypervisor: BSP firmware return: PASS'
                    )
                    HandoffStatus = $handoff
                    PayloadDiagnostic = $diagnostic
                }
                VmwareDbAppendMarker = 'PASS_LOG_MARKER'
            }
        }
        function Copy-TestVmResult([object]$Result) {
            return (ConvertFrom-Json -InputObject (ConvertTo-Json -InputObject $Result -Depth 12))
        }

        $validResult = New-TestSignedNativeVmResult
        Assert-GpuPayloadVmResult -Result $validResult `
            -ExpectedVmxPath $validResult.Vm.VmxPath -ExpectedCertificateSha256 ('A' * 64)
        Assert-True $true 'strict SignedNative evidence with stopped/reverted VM is admitted'

        $invalidCases = @()
        $case = Copy-TestVmResult $validResult
        $case.ValidationProfile = 'Model'
        $invalidCases += [pscustomobject]@{ Name = 'Model profile with a generic PASS'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.WindowsStartupEvidence.SecureBoot = $false
        $invalidCases += [pscustomobject]@{ Name = 'Secure Boot disabled'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.WindowsStartupEvidence.ExpectedCertificateDerPresent = $false
        $invalidCases += [pscustomobject]@{ Name = 'expected certificate absent'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.WindowsStartupEvidence.HandoffStatus = $null
        $invalidCases += [pscustomobject]@{ Name = 'raw handoff diagnostics missing'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.WindowsStartupEvidence.PayloadDiagnostic = 'GPU_HAL_VM:ROOT_WC_RENDER PASS'
        $invalidCases += [pscustomobject]@{ Name = 'model self-test marker substituted for payload diagnostic'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.WindowsStartupEvidence.PayloadDiagnostic = $case.WindowsStartupEvidence.PayloadDiagnostic.Replace(
            'payload_status=0x0000000000000000', 'payload_status=0x8000000000000001')
        $invalidCases += [pscustomobject]@{ Name = 'failed payload EFI status'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.WindowsStartupEvidence.PayloadDiagnosticCapturedBytes++
        $invalidCases += [pscustomobject]@{ Name = 'diagnostic byte count mismatch'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.WindowsStartupEvidence.HandoffStatusTooLarge = $true
        $invalidCases += [pscustomobject]@{ Name = 'oversized handoff status'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.WindowsStartupEvidence.DiagnosticTextEncoding = 'UTF-8 replacement decoding'
        $invalidCases += [pscustomobject]@{ Name = 'lossy diagnostic encoding'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.VmwareDbAppendMarker = 'NOT_CONFIRMED'
        $invalidCases += [pscustomobject]@{ Name = 'VMware Secure Boot db append marker absent'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.HalResults.VmwareDbAppendMarker = 'NOT_CONFIRMED'
        $invalidCases += [pscustomobject]@{ Name = 'HAL VMware db append marker absent'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.Snapshots.RevertedBefore = $false
        $invalidCases += [pscustomobject]@{ Name = 'baseline snapshot not restored'; Result = $case }
        $case = Copy-TestVmResult $validResult
        $case.CleanupFailures = @('VM process remains running')
        $invalidCases += [pscustomobject]@{ Name = 'cleanup failure'; Result = $case }

        foreach ($invalid in $invalidCases) {
            Assert-Throws {
                Assert-GpuPayloadVmResult -Result $invalid.Result `
                    -ExpectedVmxPath $validResult.Vm.VmxPath -ExpectedCertificateSha256 ('A' * 64)
            } 'not|did not|missing|failed|cleanup|Secure Boot|certificate|SignedNative' `
                "admission rejects $($invalid.Name)"
        }
    }

    Invoke-TestCase 'Status and Report never promote incomplete or forged runs to PASS' {
        $workspace = Join-Path $FixtureRoot 'status-workspace'
        $sourceRoot = $WorkspaceRoot
        $runId = 'gpu-payload-20261019T120001Z-abcdef0123456789abcdef0123456789'
        $runDirectory = Join-Path $workspace ('.analysis\gpu-payload-diagnostic\runs\' + $runId)
        $paths = Get-GpuPayloadPaths -WorkspaceRoot $workspace -RunDirectory $runDirectory
        [void](New-Item -ItemType Directory -Path $paths.ActionsRoot -Force)

        function Write-TestJournal {
            param([string]$Phase, [AllowNull()][object]$LastFailure, [AllowNull()][object]$Validation)
            $state = [ordered]@{
                SchemaVersion = 1; Workflow = 'gpu-payload-diagnostic'; RunId = $runId
                WorkspaceRoot = $workspace; SourceRoot = $sourceRoot
                RunDirectory = $runDirectory; JournalPath = $paths.JournalPath
                Phase = $Phase; InProgress = $null; SourcePins = [pscustomobject]@{ Commit = ('0' * 40); Files = [pscustomobject]@{} }
                Paths = [pscustomobject]@{}; Actions = @(); LastFailure = $LastFailure
                BuildArtifacts = $null; SignedArtifacts = $null; Validation = $Validation
            }
            [IO.File]::WriteAllText($paths.JournalPath,
                (ConvertTo-Json -InputObject $state -Depth 10), [Text.UTF8Encoding]::new($false))
        }

        Write-TestJournal -Phase 'Signed' -LastFailure $null -Validation $null
        foreach ($action in @('Status', 'Report')) {
            $summary = Invoke-GpuPayloadAction -Action $action -SourceRoot $sourceRoot `
                -WorkspaceRoot $workspace -RunDirectory $runDirectory
            Assert-True ($summary.Status -ceq 'READY' -and $summary.Phase -ceq 'Signed' -and
                $summary.Validation -eq $null -and $summary.Succeeded -eq $false) `
                "$action labeled an incomplete Signed run as success"
        }

        $failure = [pscustomobject]@{ Action = 'Build'; Status = 'FAILED'; Succeeded = $false }
        Write-TestJournal -Phase 'Signed' -LastFailure $failure -Validation $null
        $failedSummary = Invoke-GpuPayloadAction -Action 'Report' -SourceRoot $sourceRoot `
            -WorkspaceRoot $workspace -RunDirectory $runDirectory
        Assert-True ($failedSummary.Status -ceq 'FAILED' -and $failedSummary.Succeeded -eq $false) `
            'Report hid the failed action or exposed workflow success after failure'

        Write-TestJournal -Phase 'Complete' -LastFailure $null -Validation $null
        $forgedSummary = Invoke-GpuPayloadAction -Action 'Status' -SourceRoot $sourceRoot `
            -WorkspaceRoot $workspace -RunDirectory $runDirectory
        Assert-True ($forgedSummary.Status -cne 'COMPLETE' -and $forgedSummary.Succeeded -eq $false) `
            'Status accepted a forged Complete phase without signed VM validation evidence'
    }

    Write-Output "RESULT: $script:PassedCases cases, $script:Assertions assertions, $($script:FailedCases.Count) failed."
    if ($script:FailedCases.Count -gt 0) { throw ($script:FailedCases -join "`n") }
}
finally {
    if ($script:FixtureGitFunctionInstalled) {
        if ($null -ne $OldGitFunction) { Set-Item -Path Function:\global:git -Value $OldGitFunction.ScriptBlock }
        else { Remove-Item -Path Function:\global:git -Force -ErrorAction SilentlyContinue }
    }
    Remove-Variable MockGitDirty -Scope Script -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $FixtureRoot) {
        $actual = [IO.Path]::GetFullPath($FixtureRoot)
        $parent = [IO.Path]::GetFullPath((Split-Path -Parent $FixtureRoot))
        if (-not $actual.Equals([IO.Path]::GetFullPath((Join-Path $ExpectedBuildRoot ([IO.Path]::GetFileName($FixtureRoot)))), [StringComparison]::OrdinalIgnoreCase) -or
            -not $parent.Equals($ExpectedBuildRoot, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($FixtureRoot) -notmatch '^gpu-payload-workflow-contract-[0-9a-f]{32}$') {
            throw 'Fixture cleanup refused a path outside its exact owned test directory.'
        }
        $reparse = @(Get-ChildItem -LiteralPath $FixtureRoot -Force -Recurse |
            Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 })
        if ($reparse.Count -ne 0) { throw "Fixture cleanup refused reparse point $($reparse[0].FullName)." }
        Remove-Item -LiteralPath $FixtureRoot -Recurse -Force
    }
}
