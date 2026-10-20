[CmdletBinding()]
param(
    [string]$ControllerPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
if ([string]::IsNullOrWhiteSpace($ControllerPath)) {
    $ControllerPath = Join-Path $PSScriptRoot '..\Test-GpuHalWindowsVm.ps1'
}
$ControllerPath = [IO.Path]::GetFullPath($ControllerPath)
if (-not (Test-Path -LiteralPath $ControllerPath -PathType Leaf)) {
    throw "GPU HAL Windows VM controller was not found: $ControllerPath"
}

$Tokens = $null
$ParseErrors = $null
$ControllerAst = [Management.Automation.Language.Parser]::ParseFile(
    $ControllerPath, [ref]$Tokens, [ref]$ParseErrors)
if ($ParseErrors.Count -gt 0) {
    throw "Controller parse failed: $($ParseErrors[0].Message)"
}

# Execute only the controller's guarded Add-Type literal. This exercises the
# production interop declaration used by Get-WindowsHardLinkCount without
# executing any other top-level controller statement.
$SafetyGuards = @($ControllerAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.IfStatementAst] -and
                $Node.Extent.Text.Contains('GpuHalWindowsVmSafety.NativeMethods') -and
                $Node.Extent.Text.Contains('Add-Type -TypeDefinition')
        }, $true))
if ($SafetyGuards.Count -ne 1 -or $SafetyGuards[0].Clauses.Count -ne 1 -or
    $null -ne $SafetyGuards[0].ElseClause) {
    throw 'Controller native interop guard changed shape; review the pure test before proceeding.'
}
$SafetyCommands = @($SafetyGuards[0].Clauses[0].Item2.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.CommandAst]
        }, $true))
if ($SafetyCommands.Count -ne 1 -or
    $SafetyCommands[0].GetCommandName() -cne 'Add-Type' -or
    $SafetyCommands[0].CommandElements.Count -ne 3 -or
    $SafetyCommands[0].CommandElements[1].ParameterName -cne 'TypeDefinition' -or
    $SafetyCommands[0].CommandElements[2] -isnot
        [Management.Automation.Language.StringConstantExpressionAst] -or
    -not ([string]$SafetyCommands[0].CommandElements[2].Value).Contains('GetFileInformationByHandle')) {
    throw 'Controller native interop declaration is not the expected literal Add-Type command.'
}
. ([scriptblock]::Create($SafetyGuards[0].Extent.Text))

# Load only pure function definitions. The controller's outer lifecycle, including
# vmrun/snapshot/start/stop actions, is never dot-sourced or invoked here.
$FunctionNames = @(
    'Get-FullPath', 'Test-PathWithin', 'Assert-NoReparsePoint',
    'Assert-NoReparsePathChain', 'Get-WindowsHardLinkCount',
    'Assert-SingleHardLink', 'Get-SafeRelativePath', 'Resolve-VmxRelativePath',
    'Assert-StandaloneDisk', 'Get-VmxSetting', 'Assert-PreparedClone',
    'Set-VmxSetting', 'Get-SafeVmxSummary', 'Test-ConstantHex',
    'Get-AuthenticatedWindowsResult', 'Write-CommandRecord',
    'Stop-OnlyTargetVm', 'Get-VerifiedLiteralAssignmentValue',
    'Import-VmwareGuestInfoConstants', 'Assert-GuestInspection',
    'Read-BoundedText', 'Get-Sha256', 'Test-SignedNativeSignatureEvidence',
    'Get-SignedNativeArtifactEvidence', 'Test-EfiMarkers',
    'Test-SignedNativeEfiMarkers', 'Test-SignedNativePayloadEvidence',
    'Render-GpuStartupTemplate',
    'Wait-WindowsVmEvidence'
)
$FunctionDefinitions = @{}
foreach ($Definition in $ControllerAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.FunctionDefinitionAst]
        }, $true)) {
    $FunctionDefinitions[[string]$Definition.Name] = $Definition
}
foreach ($Name in $FunctionNames) {
    if (-not $FunctionDefinitions.ContainsKey($Name)) {
        throw "Controller pure-function definition is missing: $Name"
    }
    . ([scriptblock]::Create($FunctionDefinitions[$Name].Extent.Text))
}

function Find-FunctionDefinitionAst {
    param(
        [Parameter(Mandatory = $true)][Management.Automation.Language.Ast]$Ast,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $Matches = @($Ast.FindAll({
                param($Node)
                $Node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                    $Node.Name -ceq $Name
            }, $true))
    if ($Matches.Count -ne 1) { throw "Expected one pure function definition: $Name" }
    return $Matches[0]
}

function Invoke-MockedSecureBootValue {
    param(
        [Parameter(Mandatory = $true)][string]$DefinitionText,
        [AllowNull()]$Value,
        [bool]$Missing,
        [bool]$AccessFailure
    )
    return & {
        param($DefinitionText, $Value, $Missing, $AccessFailure)
        $MockRegistryState = [pscustomobject]@{
            Value = $Value
            Missing = $Missing
        }
        Add-Member -InputObject $MockRegistryState -MemberType ScriptMethod -Name GetValue -Value {
            param($Name, $Default)
            if ($Name -cne 'UEFISecureBootEnabled') {
                throw "Unexpected synthetic registry value name: $Name"
            }
            if ($this.Missing) { return $Default }
            return $this.Value
        }
        function Get-Item {
            param([string]$LiteralPath, [string]$ErrorAction)
            if ($LiteralPath -cne 'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State') {
                throw "Unexpected synthetic registry path: $LiteralPath"
            }
            if ($AccessFailure) { throw 'Synthetic registry access denied.' }
            return $MockRegistryState
        }
        . ([scriptblock]::Create($DefinitionText))
        return Get-SecureBootValue
    } $DefinitionText $Value $Missing $AccessFailure
}

$HostRunnerPath = Join-Path $RepositoryRoot 'tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'
$HostRunnerTokens = $null
$HostRunnerErrors = $null
$HostRunnerAst = [Management.Automation.Language.Parser]::ParseFile(
    $HostRunnerPath, [ref]$HostRunnerTokens, [ref]$HostRunnerErrors)
if ($HostRunnerErrors.Count -gt 0) {
    throw "HostRunner parse failed: $($HostRunnerErrors[0].Message)"
}
$CorePath = Join-Path $RepositoryRoot 'tools\host-runner\HostRunner.Core.psm1'
$CoreTokens = $null
$CoreErrors = $null
$CoreAst = [Management.Automation.Language.Parser]::ParseFile(
    $CorePath, [ref]$CoreTokens, [ref]$CoreErrors)
if ($CoreErrors.Count -gt 0) {
    throw "HostRunner.Core parse failed: $($CoreErrors[0].Message)"
}
$GuestScriptPath = Join-Path $RepositoryRoot 'components\hypervisor\setup\guest\Invoke-GpuHalWindowsGuest.ps1'
$GuestTokens = $null
$GuestErrors = $null
$GuestAst = [Management.Automation.Language.Parser]::ParseFile(
    $GuestScriptPath, [ref]$GuestTokens, [ref]$GuestErrors)
if ($GuestErrors.Count -gt 0) {
    throw "Windows guest worker parse failed: $($GuestErrors[0].Message)"
}
$StartupScriptPath = Join-Path $RepositoryRoot 'components\hypervisor\setup\guest\Invoke-GpuHalWindowsStartup.ps1'
$StartupTokens = $null
$StartupErrors = $null
$StartupAst = [Management.Automation.Language.Parser]::ParseFile(
    $StartupScriptPath, [ref]$StartupTokens, [ref]$StartupErrors)
if ($StartupErrors.Count -gt 0) {
    throw "Windows startup worker parse failed: $($StartupErrors[0].Message)"
}
$GuestTextWriter = Find-FunctionDefinitionAst $GuestAst 'Write-Utf8NoBom'
$StartupByteWriter = Find-FunctionDefinitionAst $StartupAst 'Write-Bytes'
$GuestSecureBootGetter = Find-FunctionDefinitionAst $GuestAst 'Get-SecureBootValue'
$StartupSecureBootGetter = Find-FunctionDefinitionAst $StartupAst 'Get-SecureBootValue'
$GuestReparsePathGuard = Find-FunctionDefinitionAst $GuestAst 'Assert-GuestPathWithoutReparsePoints'
$GuestStageRequestCleanup = Find-FunctionDefinitionAst $GuestAst 'Clear-AuthenticatedStageRequestChunks'
$GuestByteSequence = Find-FunctionDefinitionAst $GuestAst 'Test-ByteSequence'
$GuestCertificateDbEvidence = Find-FunctionDefinitionAst $GuestAst 'Get-ExpectedCertificateDbEvidence'
$StartupTemplateRenderer = Find-FunctionDefinitionAst $ControllerAst 'Render-GpuStartupTemplate'
. ([scriptblock]::Create((Find-FunctionDefinitionAst $CoreAst 'Test-UhrRunId').Extent.Text))
. ([scriptblock]::Create((Find-FunctionDefinitionAst $HostRunnerAst 'Test-UhrGuestInfoRunId').Extent.Text))
. ([scriptblock]::Create((Find-FunctionDefinitionAst $HostRunnerAst 'Get-VmwareGuestInfoBase').Extent.Text))
[void](Import-VmwareGuestInfoConstants -HostRunnerEndBlock $HostRunnerAst.EndBlock)

$ExpectedGuestInfoConstants = [ordered]@{
    VmwareGuestInfoPrefix = 'uefiloader'
    VmwareGuestInfoRequestManifest = 'uefiloader.request.manifest'
    VmwareGuestInfoResponseManifest = 'uefiloader.response.manifest'
    VmwareGuestInfoWorkerStatus = 'uefiloader.worker.status'
    VmwareGuestInfoRequestChunkPrefix = 'uefiloader.request.chunk.'
    VmwareGuestInfoResponseChunkPrefix = 'uefiloader.response.chunk.'
    VmwareGuestInfoDirectStage2Prefix = 'uefiloader.direct.stage2.'
    VmwareGuestInfoExactStatus = 'uefiloader.exact.status'
    VmwareGuestInfoExactStatusGuestName = 'guestinfo.uefiloader.exact.status'
    VmwareGuestInfoChunkBytes = 9000
    VmwareGuestInfoChunkChars = 12000
    VmwareGuestInfoMaxPackageBytes = 4194304
    VmwareGuestInfoMaxRequestChunks = 256
    VmwareGuestInfoMaxEvidenceBytes = 33554432
    VmwareGuestInfoMaxManifestChars = 65536
    VmwareGuestInfoTombstone = 'MA=='
}
$GuestInfoConstantsMatch = $true
foreach ($Name in $ExpectedGuestInfoConstants.Keys) {
    $Actual = Get-Variable -Name $Name -Scope Script -ValueOnly
    if ($Actual -cne $ExpectedGuestInfoConstants[$Name]) {
        $GuestInfoConstantsMatch = $false
        break
    }
}
$ActualBase = Get-VmwareGuestInfoBase -RunId 'uefi-windows-probe-20261019-142200-0123456789'
if ($ActualBase -cne [string]$script:VmwareGuestInfoPrefix) {
    $GuestInfoConstantsMatch = $false
}
if (-not $GuestInfoConstantsMatch) {
    throw 'HostRunner GuestInfo constants were not imported from the actual AST values.'
}
$script:HostConstantsImported = $GuestInfoConstantsMatch

. ([scriptblock]::Create($GuestReparsePathGuard.Extent.Text))
. ([scriptblock]::Create($GuestStageRequestCleanup.Extent.Text))
$GuestLocalSystemCall = @($GuestAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.CommandAst] -and
                $Node.GetCommandName() -ceq 'Assert-VmLocalSystem'
        }, $true))
$GuestAuthenticationCall = @($GuestAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.CommandAst] -and
                $Node.GetCommandName() -ceq 'Assert-AuthenticatedGuestPackage'
        }, $true))
$GuestReleaseCalls = @($GuestAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.CommandAst] -and
                $Node.GetCommandName() -ceq 'Clear-AuthenticatedStageRequestChunks'
        }, $true))
$GuestEntryDiagnosticCalls = @($GuestAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.CommandAst] -and
                $Node.GetCommandName() -ceq 'Publish-SetupDiagnostic' -and
                $Node.Extent.Text -match '-Phase\s+Entry'
        }, $true))
$GuestReleaseGuard = $null
if ($GuestReleaseCalls.Count -eq 1) {
    $GuestReleaseGuard = $GuestReleaseCalls[0].Parent
    while ($null -ne $GuestReleaseGuard -and
        $GuestReleaseGuard -isnot [Management.Automation.Language.IfStatementAst]) {
        $GuestReleaseGuard = $GuestReleaseGuard.Parent
    }
}
$GuestAuthFlagSet = @($GuestAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.AssignmentStatementAst] -and
                $Node.Extent.Text.Contains('$script:GuestPackageAuthenticated = $true')
        }, $true))
$script:GuestStageReclamationContractPassed = ($GuestLocalSystemCall.Count -eq 1 -and
    $GuestAuthenticationCall.Count -eq 1 -and $GuestReleaseCalls.Count -eq 1 -and
    $GuestEntryDiagnosticCalls.Count -eq 1 -and $GuestAuthFlagSet.Count -eq 1 -and
    $null -ne $GuestReleaseGuard -and $GuestReleaseGuard.Clauses.Count -eq 1 -and
    $GuestReleaseGuard.Clauses[0].Item1.Extent.Text -match '\$GpuMode\s+-ceq\s+''Stage''' -and
    $GuestAst.Extent.Text.Contains('ConsumedRequestChunksReleased=$script:ConsumedRequestChunksReleased') -and
    $GuestLocalSystemCall[0].Extent.StartOffset -lt $GuestAuthenticationCall[0].Extent.StartOffset -and
    $GuestAuthenticationCall[0].Extent.StartOffset -lt $GuestAuthFlagSet[0].Extent.StartOffset -and
    $GuestAuthFlagSet[0].Extent.StartOffset -lt $GuestReleaseCalls[0].Extent.StartOffset -and
    $GuestReleaseCalls[0].Extent.StartOffset -lt $GuestEntryDiagnosticCalls[0].Extent.StartOffset
    )

$GuestPhaseDefinition = Find-FunctionDefinitionAst $ControllerAst 'Invoke-GpuGuestPhase'
$GuestPhaseText = [string]$GuestPhaseDefinition.Extent.Text
$GuestResponseWaitOffset = $GuestPhaseText.IndexOf('Wait-WindowsGuestInfoEvidence', [StringComparison]::Ordinal)
$GuestResponseSaveOffset = $GuestPhaseText.IndexOf('$script:GuestInfoResponses.Add', [StringComparison]::Ordinal)
$GuestResponseVerdictOffset = $GuestPhaseText.IndexOf('if ([string]$Response.Result.Status', [StringComparison]::Ordinal)
$GuestFailureBranches = @($GuestPhaseDefinition.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.IfStatementAst] -and
                $Node.Clauses.Count -eq 1 -and
                $Node.Clauses[0].Item1.Extent.Text.Contains('$Response.Result.Status') -and
                $Node.Clauses[0].Item1.Extent.Text.Contains('$Response.Result.RunId')
        }, $true))
$FailureCleanupOrderPassed = $false
if ($GuestFailureBranches.Count -eq 1) {
    $FailureBranch = $GuestFailureBranches[0].Clauses[0].Item2
    $FailureCleanupCalls = @($FailureBranch.FindAll({
                param($Node)
                $Node -is [Management.Automation.Language.CommandAst] -and
                    $Node.GetCommandName() -ceq 'Clear-VmwareGuestInfoVariables'
            }, $true))
    $FailureDiagnoseCalls = @($FailureBranch.FindAll({
                param($Node)
                $Node -is [Management.Automation.Language.CommandAst] -and
                    $Node.GetCommandName() -ceq 'Invoke-GpuGuestPhase' -and
                    $Node.Extent.Text -match "-Mode\s+'Diagnose'"
            }, $true))
    $LateEvidenceReads = @($FailureBranch.FindAll({
                param($Node)
                $Node -is [Management.Automation.Language.CommandAst] -and
                    $Node.GetCommandName() -ceq 'Invoke-VmwareGuestInfoRead'
            }, $true))
    $FailureCleanupOrderPassed = ($FailureCleanupCalls.Count -eq 1 -and
        $FailureDiagnoseCalls.Count -eq 1 -and
        $LateEvidenceReads.Count -eq 2 -and
        $FailureCleanupCalls[0].Extent.Text.Contains('$Transport.RequestVariableNames') -and
        $FailureCleanupCalls[0].Extent.Text.Contains('$Response.ResponseVariableNames') -and
        $LateEvidenceReads[1].Extent.EndOffset -lt $FailureCleanupCalls[0].Extent.StartOffset -and
        $FailureCleanupCalls[0].Extent.StartOffset -lt $FailureDiagnoseCalls[0].Extent.StartOffset)
}
$AllGuestPhaseCleanupCalls = @($GuestPhaseDefinition.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.CommandAst] -and
                $Node.GetCommandName() -ceq 'Clear-VmwareGuestInfoVariables'
        }, $true))
$script:GuestControllerFailureCleanupOrderPassed =
    ($FailureCleanupOrderPassed -and $AllGuestPhaseCleanupCalls.Count -eq 2 -and
        $GuestResponseWaitOffset -ge 0 -and $GuestResponseSaveOffset -gt $GuestResponseWaitOffset -and
        $GuestResponseVerdictOffset -gt $GuestResponseSaveOffset)

$script:ReviewKey = [byte[]](1..32)
$script:Selected = [pscustomobject]@{
    VmxPath = 'synthetic-vmx'
    MailboxCredentialFile = 'synthetic-mailbox'
}
$script:ValidationProfile = 'Model'
$script:StartupStatus = 'NOT_RUN'
$ExpectedBuild = 26200
$ExpectedUbr = 9457
$ExpectedCpus = 4
function Get-MailboxKeyBytes {
    param($Selected)
    return ,([byte[]]$script:ReviewKey.Clone())
}

$script:Passed = 0
function Assert-Check {
    param([bool]$Condition, [string]$Name)
    if (-not $Condition) { throw "FAIL: $Name" }
    $script:Passed++
    Write-Output "PASS: $Name"
}
Assert-Check $script:GuestStageReclamationContractPassed `
    'Stage request reclamation is guarded by LocalSystem and authenticated-package validation and precedes entry diagnostics'
Assert-Check $script:GuestControllerFailureCleanupOrderPassed `
    'failed response preserves late inspection reads then releases owned variables before Diagnose while retaining success-path cleanup'

$GuestReleaseDefinition = Find-FunctionDefinitionAst $GuestAst 'Clear-AuthenticatedStageRequestChunks'
$GuestChunkAssignments = @($GuestReleaseDefinition.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.AssignmentStatementAst] -and
                $Node.Left.Extent.Text -ceq '$ChunkBytes'
        }, $true))
Assert-Check ($GuestChunkAssignments.Count -eq 1 -and
    $GuestChunkAssignments[0].Right.Extent.Text -cmatch '^[0-9]+$' -and
    [int]$GuestChunkAssignments[0].Right.Extent.Text -eq $script:VmwareGuestInfoChunkBytes) `
    'guest consumed-request chunk size matches the imported HostRunner producer constant'

$SaveRunResultText = [string]$FunctionDefinitions['Save-RunResult'].Extent.Text
$ControllerText = [string]$ControllerAst.Extent.Text
$RunRootInitialization = $ControllerText.IndexOf('$script:RunRoot = $RunDirectory', [StringComparison]::Ordinal)
$SignedNativePrerequisiteCheck = $ControllerText.IndexOf(
    'SignedNative requires ShimPath, CertificatePath, VerifierPath, and VerifierSha256',
    [StringComparison]::Ordinal)
$ConfigureCall = $ControllerText.IndexOf('Configure-DisposableClone -Lines $Lines', [StringComparison]::Ordinal)
$VmWriteAttempt = $ControllerText.IndexOf('$script:VmWriteAttempted = $true', [StringComparison]::Ordinal)
Assert-Check ($RunRootInitialization -ge 0 -and
    $SignedNativePrerequisiteCheck -gt $RunRootInitialization -and
    $SaveRunResultText.Contains('VmStartAttempted = [bool]$script:VmStartAttempted') -and
    $SaveRunResultText.Contains('VmWriteAttempted = [bool]$script:VmWriteAttempted') -and
    $SaveRunResultText.Contains('BeforeCreated = $script:BeforeSnapshotCreated') -and
    $SaveRunResultText.Contains('AfterCreated = $script:AfterSnapshotCreated') -and
    $VmWriteAttempt -ge 0 -and $ConfigureCall -gt $VmWriteAttempt) `
    'preflight failures persist explicit no-start/no-write/snapshot evidence before VM lifecycle operations'

function Assert-Throws {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Operation,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$MessagePattern
    )
    $Message = $null
    try { $null = & $Operation }
    catch { $Message = [string]$_.Exception.Message }
    Assert-Check ($null -ne $Message -and $Message -match $MessagePattern) $Name
}
Assert-Check $script:HostConstantsImported `
    'all GuestInfo literals import from actual HostRunner AST and resolve the base'

$BuildRoot = Join-Path $RepositoryRoot '.codex-build'
if (-not (Test-Path -LiteralPath $BuildRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $BuildRoot | Out-Null
}
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$FixtureRoot = Join-Path $BuildRoot ('gpu-controller-contract-' + [Guid]::NewGuid().ToString('N'))
$FixtureRoot = [IO.Path]::GetFullPath($FixtureRoot)
$VmxFixtureRoot = Join-Path $FixtureRoot 'vm'
$OutsideFixtureRoot = Join-Path $FixtureRoot 'outside'
$JunctionPaths = [Collections.Generic.List[string]]::new()
$Hmac = $null
$OriginalGuestCapturedProcess = Get-Item Function:\Invoke-CapturedProcess -ErrorAction SilentlyContinue
$OriginalGuestPathGuard = Get-Item Function:\Assert-GuestPathWithoutReparsePoints -ErrorAction SilentlyContinue

try {
    New-Item -ItemType Directory -Path $VmxFixtureRoot, $OutsideFixtureRoot | Out-Null

    # Exercise only the extracted guest cleanup helper. VMware Tools is modeled
    # in memory; this test never launches vmtoolsd or mutates a live VM.
    $script:OriginalGuestPathGuardScript = (Get-Item Function:\Assert-GuestPathWithoutReparsePoints).ScriptBlock
    function Assert-GuestPathWithoutReparsePoints {
        param([Parameter(Mandatory = $true)][string]$Path)
        $CanonicalVmtoolsd = 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'
        if ([string]::Equals([IO.Path]::GetFullPath($Path), $CanonicalVmtoolsd,
                [StringComparison]::OrdinalIgnoreCase)) {
            return [pscustomobject]@{
                PSIsContainer = $false
                Name = 'vmtoolsd.exe'
                Length = 1
                Attributes = [IO.FileAttributes]::Normal
            }
        }
        return & $script:OriginalGuestPathGuardScript -Path $Path
    }
    $script:MockGuestInfoValues = @{}
    $script:MockGuestInfoWrites = [Collections.Generic.List[object]]::new()
    $script:MockGuestInfoFailureAt = 0
    function Invoke-CapturedProcess {
        param([string]$FilePath, [string[]]$Arguments, [int]$TimeoutSeconds, [string]$Label)
        if ($Arguments.Count -ne 2 -or $Arguments[0] -cne '--cmd' -or
            $Arguments[1] -cnotmatch '^info-set guestinfo\.([A-Za-z0-9_.-]+) (MA==)$') {
            throw 'Synthetic GuestInfo mock received a noncanonical command.'
        }
        $VariableName = $Matches[1]
        $Value = $Matches[2]
        $script:MockGuestInfoWrites.Add([pscustomobject]@{
            FilePath = $FilePath
            TimeoutSeconds = $TimeoutSeconds
            Label = $Label
            Name = $VariableName
            Value = $Value
        })
        if ($script:MockGuestInfoFailureAt -gt 0 -and
            $script:MockGuestInfoWrites.Count -eq $script:MockGuestInfoFailureAt) {
            return [pscustomobject]@{ ExitCode = 1; Stdout = ''; Stderr = 'synthetic failure' }
        }
        $script:MockGuestInfoValues[$VariableName] = $Value
        return [pscustomobject]@{ ExitCode = 0; Stdout = ''; Stderr = '' }
    }

    $GuestExpectedRunRoot = Join-Path $FixtureRoot 'ProgramData\UEFILoaderRunner\runs'
    $GuestRunId = 'uefi-windows-probe-20261020-160000-0123456789'
    $GuestRunRoot = Join-Path $GuestExpectedRunRoot $GuestRunId
    New-Item -ItemType Directory -Path $GuestRunRoot -Force | Out-Null
    $GuestPackagePath = Join-Path $GuestRunRoot 'package.zip'
    $GuestPackageStream = [IO.File]::Open($GuestPackagePath, [IO.FileMode]::CreateNew)
    try { $GuestPackageStream.SetLength(762672) } finally { $GuestPackageStream.Dispose() }
    $GuestPackagePinBefore = (Get-FileHash -LiteralPath $GuestPackagePath -Algorithm SHA256).Hash
    $GuestPackageBytes = (Get-Item -LiteralPath $GuestPackagePath).Length
    $GuestChunkCount = [int][Math]::Ceiling([double]$GuestPackageBytes / 9000)
    $GuestRequestChars = 0
    $GuestRemainingChars = [int][Math]::Ceiling([double]$GuestPackageBytes / 3) * 4
    for ($GuestIndex = 0; $GuestIndex -lt $GuestChunkCount; $GuestIndex++) {
        $ChunkName = 'uefiloader.request.chunk.' + $GuestRunId + '.' + ('{0:D6}' -f $GuestIndex)
        $ChunkLength = [int][Math]::Min(12000, $GuestRemainingChars)
        $script:MockGuestInfoValues[$ChunkName] = 'A' * $ChunkLength
        $GuestRequestChars += $ChunkLength
        $GuestRemainingChars -= $ChunkLength
    }
    Assert-Check ($GuestChunkCount -eq 85 -and $GuestRequestChars -eq 1016896) `
        '762,672-byte request fixture models the bounded 85-chunk GuestInfo transport'

    $script:GpuMode = 'Stage'
    $script:VmGuardPassed = $true
    $script:GuestPackageAuthenticated = $true
    $ReleasedChunkCount = Clear-AuthenticatedStageRequestChunks -GuestRunId $GuestRunId `
        -GuestRunRoot $GuestRunRoot -ExpectedGuestRunRoot $GuestExpectedRunRoot `
        -VmtoolsdPath 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'
    $MockGuestInfoNames = @($script:MockGuestInfoWrites | ForEach-Object Name)
    $WrongGuestInfoNames = @($MockGuestInfoNames | Where-Object {
            $_ -notmatch ('^uefiloader\.request\.chunk\.' + [regex]::Escape($GuestRunId) + '\.[0-9]{6}$')
        })
    $PostReleaseGuestInfoChars = 0
    foreach ($StoredValue in $script:MockGuestInfoValues.Values) {
        $PostReleaseGuestInfoChars += [string]$StoredValue.Length
    }
    $SyntheticResponseChars = 100000
    $SyntheticGuestInfoCapacity = 1100000
    Assert-Check ($ReleasedChunkCount -eq 85 -and $MockGuestInfoNames.Count -eq 85 -and
        $WrongGuestInfoNames.Count -eq 0 -and
        @($MockGuestInfoNames | Select-Object -Unique).Count -eq 85 -and
        @($script:MockGuestInfoValues.Values | Where-Object { $_ -cne 'MA==' }).Count -eq 0 -and
        ($GuestRequestChars + $SyntheticResponseChars) -gt $SyntheticGuestInfoCapacity -and
        ($PostReleaseGuestInfoChars + $SyntheticResponseChars) -lt $SyntheticGuestInfoCapacity -and
        (Get-FileHash -LiteralPath $GuestPackagePath -Algorithm SHA256).Hash -ceq $GuestPackagePinBefore) `
        'Stage reclamation tombstones only authenticated current-run request chunks before response capacity is needed and retains package evidence'

    $script:MockGuestInfoWrites.Clear()
    $script:MockGuestInfoFailureAt = 3
    Assert-Throws {
        Clear-AuthenticatedStageRequestChunks -GuestRunId $GuestRunId `
            -GuestRunRoot $GuestRunRoot -ExpectedGuestRunRoot $GuestExpectedRunRoot `
            -VmtoolsdPath 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'
    } 'failed GuestInfo tombstone aborts the bounded cleanup' 'failed to release'
    Assert-Check ($script:MockGuestInfoWrites.Count -eq 3) `
        'GuestInfo tombstone failure stops before any later request chunk writes'
    $script:MockGuestInfoFailureAt = 0

    $NoWriteCases = @(
        [pscustomobject]@{ Name = 'invalid RunId'; RunId = 'uefi-windows-probe-bad'; RunRoot = $GuestRunRoot; ExpectedRoot = $GuestExpectedRunRoot; Vmtoolsd = 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'; Guard = $true; Auth = $true; Mode = 'Stage'; Pattern = 'RunId is invalid' },
        [pscustomobject]@{ Name = 'outside RunRoot'; RunId = $GuestRunId; RunRoot = (Join-Path $OutsideFixtureRoot $GuestRunId); ExpectedRoot = $GuestExpectedRunRoot; Vmtoolsd = 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'; Guard = $true; Auth = $true; Mode = 'Stage'; Pattern = 'outside its exact authenticated run directory' },
        [pscustomobject]@{ Name = 'noncanonical vmtoolsd path'; RunId = $GuestRunId; RunRoot = $GuestRunRoot; ExpectedRoot = $GuestExpectedRunRoot; Vmtoolsd = (Join-Path $FixtureRoot 'vmtoolsd.exe'); Guard = $true; Auth = $true; Mode = 'Stage'; Pattern = 'canonical x64/x86' },
        [pscustomobject]@{ Name = 'LocalSystem guard not passed'; RunId = $GuestRunId; RunRoot = $GuestRunRoot; ExpectedRoot = $GuestExpectedRunRoot; Vmtoolsd = 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'; Guard = $false; Auth = $true; Mode = 'Stage'; Pattern = 'only after the VMware/SYSTEM' },
        [pscustomobject]@{ Name = 'package authentication not passed'; RunId = $GuestRunId; RunRoot = $GuestRunRoot; ExpectedRoot = $GuestExpectedRunRoot; Vmtoolsd = 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'; Guard = $true; Auth = $false; Mode = 'Stage'; Pattern = 'only after the VMware/SYSTEM' }
    )
    foreach ($NoWriteCase in $NoWriteCases) {
        $script:MockGuestInfoWrites.Clear()
        $script:VmGuardPassed = $NoWriteCase.Guard
        $script:GuestPackageAuthenticated = $NoWriteCase.Auth
        $script:GpuMode = $NoWriteCase.Mode
        Assert-Throws {
            Clear-AuthenticatedStageRequestChunks -GuestRunId $NoWriteCase.RunId `
                -GuestRunRoot $NoWriteCase.RunRoot -ExpectedGuestRunRoot $NoWriteCase.ExpectedRoot `
                -VmtoolsdPath $NoWriteCase.Vmtoolsd
        } "$($NoWriteCase.Name) rejects before GuestInfo writes" $NoWriteCase.Pattern
        Assert-Check ($script:MockGuestInfoWrites.Count -eq 0) `
            "$($NoWriteCase.Name) performs no GuestInfo writes"
    }

    $script:GpuMode = 'Stage'
    $script:VmGuardPassed = $true
    $script:GuestPackageAuthenticated = $true
    $OriginalGuestPackageLength = (Get-Item -LiteralPath $GuestPackagePath).Length
    $OversizeStream = [IO.File]::Open($GuestPackagePath, [IO.FileMode]::Open)
    try { $OversizeStream.SetLength(4194305) } finally { $OversizeStream.Dispose() }
    $script:MockGuestInfoWrites.Clear()
    Assert-Throws {
        Clear-AuthenticatedStageRequestChunks -GuestRunId $GuestRunId `
            -GuestRunRoot $GuestRunRoot -ExpectedGuestRunRoot $GuestExpectedRunRoot `
            -VmtoolsdPath 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'
    } 'oversize authenticated package is rejected before writes' 'exceeds 4 MiB'
    Assert-Check ($script:MockGuestInfoWrites.Count -eq 0) `
        'oversize authenticated package performs no GuestInfo writes'
    $ChunkLimitStream = [IO.File]::Open($GuestPackagePath, [IO.FileMode]::Open)
    try { $ChunkLimitStream.SetLength(2304001) } finally { $ChunkLimitStream.Dispose() }
    Assert-Throws {
        Clear-AuthenticatedStageRequestChunks -GuestRunId $GuestRunId `
            -GuestRunRoot $GuestRunRoot -ExpectedGuestRunRoot $GuestExpectedRunRoot `
            -VmtoolsdPath 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'
    } 'request beyond the 256-chunk limit is rejected before writes' 'chunk count is outside the bounded range'
    Assert-Check ($script:MockGuestInfoWrites.Count -eq 0) `
        'request beyond the 256-chunk limit performs no GuestInfo writes'
    $RestorePackageLength = [IO.File]::Open($GuestPackagePath, [IO.FileMode]::Open)
    try { $RestorePackageLength.SetLength($OriginalGuestPackageLength) } finally { $RestorePackageLength.Dispose() }

    Remove-Item -LiteralPath $GuestPackagePath -Force
    Assert-Throws {
        Clear-AuthenticatedStageRequestChunks -GuestRunId $GuestRunId `
            -GuestRunRoot $GuestRunRoot -ExpectedGuestRunRoot $GuestExpectedRunRoot `
            -VmtoolsdPath 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'
    } 'missing package.zip is rejected before writes' 'Cannot find path|does not exist|cannot find'
    Assert-Check ($script:MockGuestInfoWrites.Count -eq 0) `
        'missing package.zip performs no GuestInfo writes'
    $GuestPackageStream = [IO.File]::Open($GuestPackagePath, [IO.FileMode]::CreateNew)
    try { $GuestPackageStream.SetLength($OriginalGuestPackageLength) } finally { $GuestPackageStream.Dispose() }

    $GuestJunctionTarget = Join-Path $OutsideFixtureRoot 'guest-run-root-target'
    New-Item -ItemType Directory -Path $GuestJunctionTarget | Out-Null
    $GuestRunRootItem = Get-Item -LiteralPath $GuestRunRoot -Force
    if (($GuestRunRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Guest run-root fixture unexpectedly became a reparse point before the test.'
    }
    Remove-Item -LiteralPath $GuestRunRoot -Recurse -Force
    $GuestRunRootJunction = Join-Path $GuestExpectedRunRoot $GuestRunId
    New-Item -ItemType Junction -Path $GuestRunRootJunction -Target $GuestJunctionTarget | Out-Null
    $JunctionPaths.Add($GuestRunRootJunction)
    $script:MockGuestInfoWrites.Clear()
    Assert-Throws {
        Clear-AuthenticatedStageRequestChunks -GuestRunId $GuestRunId `
            -GuestRunRoot $GuestRunRootJunction -ExpectedGuestRunRoot $GuestExpectedRunRoot `
            -VmtoolsdPath 'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe'
    } 'reparse-point run root is rejected before writes' 'reparse point'
    Assert-Check ($script:MockGuestInfoWrites.Count -eq 0) `
        'reparse-point run root performs no GuestInfo writes'

    $script:GpuMode = 'Inspect'
    $script:VmGuardPassed = $false
    $script:GuestPackageAuthenticated = $false
    $script:MockGuestInfoWrites.Clear()
    $InspectReleaseCount = Clear-AuthenticatedStageRequestChunks -GuestRunId 'invalid' `
        -GuestRunRoot 'invalid' -ExpectedGuestRunRoot 'invalid' -VmtoolsdPath 'invalid'
    Assert-Check ($InspectReleaseCount -eq 0 -and $script:MockGuestInfoWrites.Count -eq 0) `
        'Inspect mode does not validate, alter, or publish GuestInfo request state'
    if ($null -ne $OriginalGuestCapturedProcess) {
        Set-Item Function:\Invoke-CapturedProcess -Value $OriginalGuestCapturedProcess.ScriptBlock
    } else {
        Remove-Item Function:\Invoke-CapturedProcess -ErrorAction SilentlyContinue
    }
    if ($null -ne $OriginalGuestPathGuard) {
        Set-Item Function:\Assert-GuestPathWithoutReparsePoints -Value $OriginalGuestPathGuard.ScriptBlock
    } else {
        Remove-Item Function:\Assert-GuestPathWithoutReparsePoints -ErrorAction SilentlyContinue
    }

    $PinnedVerifierMessage = 'Authenticode integrity and signer pinning test passed'
    $ValidVerifierProcess = [pscustomobject]@{
        ExitCode = 0
        TimedOut = $false
        OutputTooLarge = $false
        Stdout = $PinnedVerifierMessage + "`r`n"
        Stderr = ''
    }
    Assert-Check (Test-SignedNativeSignatureEvidence -SignatureStatus 'UnknownError' `
            -VerifierResult $ValidVerifierProcess) `
        'UnknownError host chain metadata is accepted only with pinned verifier success'
    Assert-Check (Test-SignedNativeSignatureEvidence -SignatureStatus 'Valid' `
            -VerifierResult $ValidVerifierProcess) `
        'Valid host signature metadata still requires the pinned verifier success marker'
    Assert-Check (-not (Test-SignedNativeSignatureEvidence -SignatureStatus 'UnknownError' `
            -VerifierResult ([pscustomobject]@{
                ExitCode = 1; TimedOut = $false; OutputTooLarge = $false
                Stdout = ''; Stderr = 'tampered signed PE rejected'
            }))) `
        'tampered signed PE verifier failure cannot pass with UnknownError host metadata'
    Assert-Check (-not (Test-SignedNativeSignatureEvidence -SignatureStatus 'UnknownError' `
            -VerifierResult ([pscustomobject]@{
                ExitCode = 0; TimedOut = $true; OutputTooLarge = $false
                Stdout = $PinnedVerifierMessage; Stderr = ''
            }))) `
        'timed-out Authenticode verifier is rejected'
    Assert-Check (-not (Test-SignedNativeSignatureEvidence -SignatureStatus 'UnknownError' `
            -VerifierResult ([pscustomobject]@{
                ExitCode = 0; TimedOut = $false; OutputTooLarge = $true
                Stdout = $PinnedVerifierMessage; Stderr = ''
            }))) `
        'oversized Authenticode verifier output is rejected'
    Assert-Check (-not (Test-SignedNativeSignatureEvidence -SignatureStatus 'HashMismatch' `
            -VerifierResult $ValidVerifierProcess)) `
        'invalid host signature status is rejected even if verifier metadata is malformed'
    Assert-Check (-not (Test-SignedNativeSignatureEvidence -SignatureStatus 'UnknownError' `
            -VerifierResult $null)) `
        'missing Authenticode verifier result is rejected'

    $SignedArtifactPath = Join-Path $FixtureRoot 'signed-artifact-fixture.exe'
    $SignedVerifierPath = Join-Path $FixtureRoot 'pinned-verifier-fixture.exe'
    $SignedCertificatePath = Join-Path $FixtureRoot 'pinned-certificate-fixture.cer'
    [IO.File]::WriteAllBytes($SignedArtifactPath, [byte[]]@(0x4d, 0x5a, 0x01, 0x02))
    [IO.File]::WriteAllBytes($SignedVerifierPath, [byte[]]@(0x4d, 0x5a, 0x05, 0x06))
    [IO.File]::WriteAllBytes($SignedCertificatePath, [byte[]]@(0x30, 0x04, 0x02, 0x01, 0x01))
    $SignedVerifierPin = (Get-Sha256 $SignedVerifierPath)
    $script:SignedNativeVerifierEvidence = [Collections.Generic.List[object]]::new()
    $script:MockSignatureStatus = 'UnknownError'
    $script:MockSignatureThumbprint = 'AABBCCDDEEFF00112233445566778899AABBCCDD'
    $script:MockVerifierResult = $ValidVerifierProcess
    $script:MockVerifierArguments = $null
    $script:MockVerifierCalls = 0
    $OriginalAuthFunction = Get-Item Function:\Get-AuthenticodeSignature -ErrorAction SilentlyContinue
    $OriginalVerifierFunction = Get-Item Function:\Invoke-LoggedUhrProcess -ErrorAction SilentlyContinue
    Set-Item Function:\Get-AuthenticodeSignature -Value {
        param([string]$FilePath)
        return [pscustomobject]@{
            Status = $script:MockSignatureStatus
            StatusMessage = 'Synthetic local chain root is not trusted.'
            SignerCertificate = [pscustomobject]@{ Thumbprint = $script:MockSignatureThumbprint }
        }
    }
    Set-Item Function:\Invoke-LoggedUhrProcess -Value {
        param([string]$FilePath, [string[]]$ArgumentList, [string]$WorkingDirectory,
            [int]$TimeoutSeconds)
        $script:MockVerifierCalls++
        $script:MockVerifierArguments = @($ArgumentList)
        return $script:MockVerifierResult
    }
    try {
        $UnknownErrorEvidence = Get-SignedNativeArtifactEvidence -Path $SignedArtifactPath `
            -Label 'Synthetic signed PE' -ExpectedSignerThumbprint $script:MockSignatureThumbprint `
            -CertificatePath $SignedCertificatePath -VerifierPath $SignedVerifierPath `
            -ExpectedVerifierSha256 $SignedVerifierPin
        Assert-Check ($UnknownErrorEvidence.SignatureStatus -ceq 'UnknownError' -and
            $UnknownErrorEvidence.Verifier.VerifierStatus -ceq 'PINNED_SIGNATURE_PASS' -and
            $UnknownErrorEvidence.Verifier.VerifierSha256 -ceq $SignedVerifierPin -and
            $script:MockVerifierArguments.Count -eq 2 -and
            $script:MockVerifierArguments[0] -ceq $SignedArtifactPath -and
            $script:MockVerifierArguments[1] -ceq $SignedCertificatePath) `
            'artifact preflight runs the hash-pinned verifier against the PE and supplied DER certificate'

        $script:MockVerifierResult = [pscustomobject]@{
            ExitCode = 1; TimedOut = $false; Stdout = ''
            Stderr = 'tampered signed PE was not rejected'
        }
        $TamperedError = $null
        try {
            $null = Get-SignedNativeArtifactEvidence -Path $SignedArtifactPath `
                -Label 'Tampered signed PE' -ExpectedSignerThumbprint $script:MockSignatureThumbprint `
                -CertificatePath $SignedCertificatePath -VerifierPath $SignedVerifierPath `
                -ExpectedVerifierSha256 $SignedVerifierPin
        }
        catch { $TamperedError = [string]$_.Exception.Message }
        Assert-Check ($TamperedError -match 'failed the pinned Authenticode verifier' -and
            $script:SignedNativeVerifierEvidence[$script:SignedNativeVerifierEvidence.Count - 1].VerifierStatus -ceq 'PINNED_SIGNATURE_REJECTED') `
            'tampered signed PE is rejected and its bounded verifier failure evidence is retained'

        $script:MockVerifierResult = $null
        $MissingVerifierError = $null
        try {
            $null = Get-SignedNativeArtifactEvidence -Path $SignedArtifactPath `
                -Label 'Missing verifier result' -ExpectedSignerThumbprint $script:MockSignatureThumbprint `
                -CertificatePath $SignedCertificatePath -VerifierPath $SignedVerifierPath `
                -ExpectedVerifierSha256 $SignedVerifierPin
        }
        catch { $MissingVerifierError = [string]$_.Exception.Message }
        Assert-Check ($MissingVerifierError -match 'failed the pinned Authenticode verifier') `
            'missing verifier process result fails closed'

        $VerifierCallsBeforeBadPin = $script:MockVerifierCalls
        $BadPinError = $null
        try {
            $null = Get-SignedNativeArtifactEvidence -Path $SignedArtifactPath `
                -Label 'Verifier pin mismatch' -ExpectedSignerThumbprint $script:MockSignatureThumbprint `
                -CertificatePath $SignedCertificatePath -VerifierPath $SignedVerifierPath `
                -ExpectedVerifierSha256 ('0' * 64)
        }
        catch { $BadPinError = [string]$_.Exception.Message }
        Assert-Check ($BadPinError -match 'verifier SHA-256 does not match' -and
            $script:MockVerifierCalls -eq $VerifierCallsBeforeBadPin -and
            $script:SignedNativeVerifierEvidence[$script:SignedNativeVerifierEvidence.Count - 1].VerifierStatus -ceq 'VERIFIER_PIN_MISMATCH') `
            'verifier hash mismatch is recorded and rejected before executable invocation'
    }
    finally {
        if ($null -ne $OriginalAuthFunction) {
            Set-Item Function:\Get-AuthenticodeSignature -Value $OriginalAuthFunction.ScriptBlock
        } else { Remove-Item Function:\Get-AuthenticodeSignature -ErrorAction SilentlyContinue }
        if ($null -ne $OriginalVerifierFunction) {
            Set-Item Function:\Invoke-LoggedUhrProcess -Value $OriginalVerifierFunction.ScriptBlock
        } else { Remove-Item Function:\Invoke-LoggedUhrProcess -ErrorAction SilentlyContinue }
    }

    $SecureBootCases = @(
        [pscustomobject]@{ Name = 'DWORD 0 reports false'; Value = 0; Missing = $false; AccessFailure = $false; Expected = 'False' }
        [pscustomobject]@{ Name = 'DWORD 1 reports true'; Value = 1; Missing = $false; AccessFailure = $false; Expected = 'True' }
        [pscustomobject]@{ Name = 'missing registry value reports unknown'; Value = $null; Missing = $true; AccessFailure = $false; Expected = 'Null' }
        [pscustomobject]@{ Name = 'invalid registry value reports unknown'; Value = 2; Missing = $false; AccessFailure = $false; Expected = 'Null' }
        [pscustomobject]@{ Name = 'registry access failure reports unknown'; Value = $null; Missing = $false; AccessFailure = $true; Expected = 'Null' }
    )
    foreach ($Getter in @(
            [pscustomobject]@{ Name = 'guest'; Definition = $GuestSecureBootGetter }
            [pscustomobject]@{ Name = 'startup'; Definition = $StartupSecureBootGetter }
        )) {
        $RegistryCommands = @($Getter.Definition.FindAll({
                    param($Node)
                    $Node -is [Management.Automation.Language.CommandAst]
                }, $true))
        Assert-Check ($RegistryCommands.Count -eq 1 -and
            $RegistryCommands[0].GetCommandName() -ceq 'Get-Item' -and
            $Getter.Definition.Extent.Text.Contains('HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State')) `
            "$($Getter.Name) SecureBoot getter uses only the mocked read-only registry path"
        foreach ($Case in $SecureBootCases) {
            $ActualSecureBoot = Invoke-MockedSecureBootValue `
                -DefinitionText $Getter.Definition.Extent.Text `
                -Value $Case.Value -Missing $Case.Missing -AccessFailure $Case.AccessFailure
            $CaseMatches = switch ($Case.Expected) {
                'True' { ($ActualSecureBoot -is [bool]) -and $ActualSecureBoot }
                'False' { ($ActualSecureBoot -is [bool]) -and (-not $ActualSecureBoot) }
                'Null' { $null -eq $ActualSecureBoot }
            }
            Assert-Check $CaseMatches "$($Getter.Name) SecureBoot getter: $($Case.Name)"
        }
    }

    $ByteSequenceCases = & {
        param($Definition)
        . ([scriptblock]::Create($Definition))
        [pscustomobject]@{
            Present = Test-ByteSequence -Haystack ([byte[]]@(0x10, 0x30, 0x82, 0x01, 0x02, 0x20)) `
                -Needle ([byte[]]@(0x30, 0x82, 0x01, 0x02))
            Absent = Test-ByteSequence -Haystack ([byte[]]@(0x10, 0x30, 0x82, 0x01, 0x02, 0x20)) `
                -Needle ([byte[]]@(0x30, 0x82, 0x01, 0x03))
            LongerNeedle = Test-ByteSequence -Haystack ([byte[]]@(0x30, 0x82)) `
                -Needle ([byte[]]@(0x30, 0x82, 0x01))
            EmptyNeedle = Test-ByteSequence -Haystack ([byte[]]@(0x30, 0x82)) `
                -Needle ([byte[]]::new(0))
        }
    } $GuestByteSequence.Extent.Text
    Assert-Check ($ByteSequenceCases.Present -and -not $ByteSequenceCases.Absent -and
        -not $ByteSequenceCases.LongerNeedle -and -not $ByteSequenceCases.EmptyNeedle) `
        'Secure Boot certificate byte search matches exact embedded DER and rejects absent, longer, and empty needles'

    $ExpectedCertificatePath = Join-Path $FixtureRoot 'expected-public-certificate.der'
    $ExpectedCertificateBytes = [byte[]]@(0x30, 0x04, 0x02, 0x01, 0x7f)
    [IO.File]::WriteAllBytes($ExpectedCertificatePath, $ExpectedCertificateBytes)
    $CertificateTrust = & {
        param($ByteSequenceDefinition, $EvidenceDefinition, $CertificatePath, $ExpectedBytes)
        . ([scriptblock]::Create($ByteSequenceDefinition))
        . ([scriptblock]::Create($EvidenceDefinition))
        $script:MockDatabaseName = $null
        $script:MockDatabaseRecords = @(
            [pscustomobject]@{ Bytes = [byte[]]@(0x01, 0x02, 0x30, 0x04, 0x02, 0x01, 0x7f, 0x03) }
        )
        function Get-SecureBootUEFI {
            param([string]$Name, [string]$ErrorAction)
            $script:MockDatabaseName = $Name
            if ($Name -cne 'db' -or $ErrorAction -cne 'Stop') {
                throw 'Unexpected Secure Boot variable request.'
            }
            return $script:MockDatabaseRecords
        }
        $present = Get-ExpectedCertificateDbEvidence -CertificatePath $CertificatePath
        $presentName = $script:MockDatabaseName
        $script:MockDatabaseRecords = @(
            [pscustomobject]@{ Bytes = [byte[]]@(0x01, 0x02, 0x30, 0x04, 0x02, 0x01, 0x70, 0x03) }
        )
        $absent = Get-ExpectedCertificateDbEvidence -CertificatePath $CertificatePath
        $script:MockDatabaseRecords = @([pscustomobject]@{ Bytes = [byte[]]::new(0) })
        $malformedMessage = $null
        try { $null = Get-ExpectedCertificateDbEvidence -CertificatePath $CertificatePath }
        catch { $malformedMessage = [string]$_.Exception.Message }
        [pscustomobject]@{
            Present = $present
            Absent = $absent
            RequestedVariable = $presentName
            MalformedMessage = $malformedMessage
        }
    } $GuestByteSequence.Extent.Text $GuestCertificateDbEvidence.Extent.Text `
        $ExpectedCertificatePath $ExpectedCertificateBytes
    Assert-Check ($CertificateTrust.RequestedVariable -ceq 'db' -and
        $CertificateTrust.Present.ExpectedCertificateDerPresent -and
        $CertificateTrust.Present.ExpectedCertificateSha256 -ceq
            (Get-FileHash -LiteralPath $ExpectedCertificatePath -Algorithm SHA256).Hash -and
        $CertificateTrust.Present.Inspection -ceq 'BOUNDED_RAW_DER_SUBSEQUENCE_ONLY') `
        'guest trust proof requests db and reports only bounded raw DER presence with the expected certificate hash'
    Assert-Check (-not $CertificateTrust.Absent.ExpectedCertificateDerPresent) `
        'guest trust proof fails closed when expected certificate DER is absent from db'
    Assert-Check ($CertificateTrust.MalformedMessage -match 'unreadable record') `
        'guest trust proof rejects malformed db records'

    $SignedNativeSuccessOutput = @(
        'uefi hypervisor: DMA isolation admission: PASS'
        'uefi hypervisor: BSP firmware return: PASS'
    ) -join "`r`n"
    Assert-Check (Test-SignedNativeEfiMarkers $SignedNativeSuccessOutput) `
        'SignedNative EFI evidence requires DMA admission and BSP firmware return markers'
    Assert-Check (-not (Test-SignedNativeEfiMarkers (@(
                'GPU_HAL_MODEL:ALL_PASS'
                'GPU_HAL_VM:ROOT_WC_RENDER PASS'
                'GPU_HAL_VM:VMX_FIRMWARE_RETURN PASS'
            ) -join "`r`n"))) `
        'synthetic model and root-WC self-test markers cannot satisfy SignedNative EFI evidence'
    Assert-Check (-not (Test-SignedNativeEfiMarkers ($SignedNativeSuccessOutput +
            "`r`nuefi hypervisor: GPU admission: FAIL"))) `
        'SignedNative EFI failure markers override otherwise-present PASS markers'

    $StartupTemplate = @'
profile=__GPU_VALIDATION_PROFILE__
secure_boot_expected=__GPU_EXPECTED_SECURE_BOOT__
'@
    $RenderedStartupProfiles = foreach ($Profile in @('Model', 'SignedNative')) {
        $Rendered = Render-GpuStartupTemplate -Template $StartupTemplate -Profile $Profile
        $Encoded = [Convert]::ToBase64String([Text.UTF8Encoding]::new($false).GetBytes($Rendered))
        [Text.UTF8Encoding]::new($false).GetString([Convert]::FromBase64String($Encoded))
    }
    Assert-Check ($RenderedStartupProfiles[0] -match '(?m)^profile=Model$' -and
        $RenderedStartupProfiles[0] -match '(?m)^secure_boot_expected=False$' -and
        $RenderedStartupProfiles[1] -match '(?m)^profile=SignedNative$' -and
        $RenderedStartupProfiles[1] -match '(?m)^secure_boot_expected=True$' -and
        ($RenderedStartupProfiles -join "`n") -notmatch '__GPU_[A-Z0-9_]+__') `
        'startup template renders profile and Secure Boot values before nested Base64 encoding'
    Assert-Throws {
        Render-GpuStartupTemplate -Template ($StartupTemplate + "`nprofile=__GPU_VALIDATION_PROFILE__") `
            -Profile SignedNative
    } 'startup template rejects duplicated security profile tokens' 'exactly once'
    Assert-Throws {
        Render-GpuStartupTemplate -Template 'profile=Model' -Profile Model
    } 'startup template rejects a missing Secure Boot expectation token' 'exactly once'
    $GuestRendererDefinition = Find-FunctionDefinitionAst $ControllerAst 'Render-GpuGuestScript'
    $GuestRendererText = $GuestRendererDefinition.Extent.Text
    Assert-Check ($GuestRendererText.IndexOf('Render-GpuStartupTemplate', [StringComparison]::Ordinal) -ge 0 -and
        $GuestRendererText.IndexOf('Render-GpuStartupTemplate', [StringComparison]::Ordinal) -lt
        $GuestRendererText.IndexOf('ToBase64String($StartupRenderedBytes)', [StringComparison]::Ordinal)) `
        'production guest renderer resolves startup tokens before embedding startup bytes'

    $SecureBootDisableCalls = @($ControllerAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.CommandAst] -and
                $Node.GetCommandName() -ceq 'Set-CloneSecureBoot' -and
                $Node.Extent.Text -match '(?i)\$false\s*$'
        }, $true))
    $DisableCallGuard = $null
    if ($SecureBootDisableCalls.Count -eq 1) {
        $DisableCallGuard = $SecureBootDisableCalls[0].Parent
        while ($null -ne $DisableCallGuard -and
            $DisableCallGuard -isnot [Management.Automation.Language.IfStatementAst]) {
            $DisableCallGuard = $DisableCallGuard.Parent
        }
    }
    Assert-Check ($SecureBootDisableCalls.Count -eq 1 -and $null -ne $DisableCallGuard -and
        $DisableCallGuard.Clauses.Count -eq 1 -and
        $DisableCallGuard.Clauses[0].Item1.Extent.Text -match "ValidationProfile\s+-ceq\s+'Model'" -and
        $null -ne $DisableCallGuard.ElseClause -and
        $DisableCallGuard.ElseClause.Extent.Text -notmatch 'Set-CloneSecureBoot') `
        'only Model profile disables clone Secure Boot; SignedNative branch retains it'

    $SignedShimBranches = @($GuestAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.IfStatementAst] -and
                $Node.Extent.Text.Contains('Copy-Item -LiteralPath $VerifiedShimPath')
        }, $true))
    $ShimBranch = if ($SignedShimBranches.Count -eq 1) { $SignedShimBranches[0] } else { $null }
    Assert-Check ($null -ne $ShimBranch -and $ShimBranch.Clauses.Count -eq 1 -and
        $ShimBranch.Clauses[0].Item1.Extent.Text -match "ValidationProfile\s+-ceq\s+'SignedNative'" -and
        $GuestAst.Extent.Text.Contains("Join-Path `$StageDirectory 'shimx64.efi'") -and
        $ShimBranch.Clauses[0].Item2.Extent.Text.Contains('$VerifiedShimPath') -and
        $null -ne $ShimBranch.ElseClause -and $ShimBranch.ElseClause.Extent.Text.Contains('$LoaderPath')) `
        'SignedNative stages the verified vendor shim as shimx64; Model keeps its direct-loader fixture'

    $InstallerProfileBranches = @($GuestAst.FindAll({
            param($Node)
            $Node -is [Management.Automation.Language.IfStatementAst] -and
                $Node.Extent.Text.Contains("'legacy'") -and
                $Node.Extent.Text.Contains("'vm-direct'")
        }, $true))
    $InstallerProfileBranch = if ($InstallerProfileBranches.Count -eq 1) {
        $InstallerProfileBranches[0]
    } else { $null }
    Assert-Check ($null -ne $InstallerProfileBranch -and $InstallerProfileBranch.Clauses.Count -eq 1 -and
        $InstallerProfileBranch.Clauses[0].Item1.Extent.Text -match "ValidationProfile\s+-ceq\s+'SignedNative'" -and
        $InstallerProfileBranch.Clauses[0].Item2.Extent.Text.Contains("'legacy'") -and
        $null -ne $InstallerProfileBranch.ElseClause -and
        $InstallerProfileBranch.ElseClause.Extent.Text.Contains("'vm-direct'")) `
        'SignedNative uses the legacy shim chain while Model retains vm-direct boot'

    $ValidSignedNativeHandoff = @(
        'schema=1'
        'state=READY_BOOT_MANAGER'
        'restore=PASS'
        'payload=PASS'
    ) -join "`r`n"
    $ValidSignedNativeDiagnostic = @(
        'schema=1'
        'payload_launch=ok'
        'payload_status=0x0000000000000000'
        'shim_load_phase=firmware_load'
        'shim_load_status=0x0000000000000000'
    ) -join "`r`n"
    Assert-Check (Test-SignedNativePayloadEvidence -HandoffStatus $ValidSignedNativeHandoff `
            -PayloadDiagnostic $ValidSignedNativeDiagnostic) `
        'SignedNative payload proof accepts exact successful handoff, firmware launch, and EFI status evidence'
    foreach ($InvalidSignedNativeCase in @(
            [pscustomobject]@{ Name = 'payload handoff failed'; Handoff = $ValidSignedNativeHandoff.Replace('payload=PASS', 'payload=FAILED'); Diagnostic = $ValidSignedNativeDiagnostic }
            [pscustomobject]@{ Name = 'nonzero payload status'; Handoff = $ValidSignedNativeHandoff; Diagnostic = $ValidSignedNativeDiagnostic.Replace('payload_status=0x0000000000000000', 'payload_status=0x8000000000000001') }
            [pscustomobject]@{ Name = 'launch stage failed'; Handoff = $ValidSignedNativeHandoff; Diagnostic = $ValidSignedNativeDiagnostic.Replace('payload_launch=ok', 'payload_launch=failed') }
            [pscustomobject]@{ Name = 'shim stage failed'; Handoff = $ValidSignedNativeHandoff; Diagnostic = $ValidSignedNativeDiagnostic.Replace('shim_load_status=0x0000000000000000', 'shim_load_status=0x8000000000000001') }
            [pscustomobject]@{ Name = 'duplicate diagnostic key'; Handoff = $ValidSignedNativeHandoff; Diagnostic = $ValidSignedNativeDiagnostic + "`r`npayload_launch=ok" }
            [pscustomobject]@{ Name = 'malformed diagnostic line'; Handoff = $ValidSignedNativeHandoff; Diagnostic = $ValidSignedNativeDiagnostic + "`r`nnot-a-status" }
        )) {
        Assert-Check (-not (Test-SignedNativePayloadEvidence `
                -HandoffStatus $InvalidSignedNativeCase.Handoff `
                -PayloadDiagnostic $InvalidSignedNativeCase.Diagnostic)) `
            "SignedNative payload proof rejects $($InvalidSignedNativeCase.Name)"
    }

    $OpenSerialPath = Join-Path $FixtureRoot 'open-vmware-serial.txt'
    $OpenSerialWriter = $null
    try {
        $OpenSerialWriter = [IO.File]::Open($OpenSerialPath,
            [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
        $SerialBytes = [Text.Encoding]::ASCII.GetBytes('abc')
        $OpenSerialWriter.Write($SerialBytes, 0, $SerialBytes.Length)
        $OpenSerialWriter.Flush($true)
        $OpenSerialDigest = Get-Sha256 $OpenSerialPath
        Assert-Check ($OpenSerialDigest -ceq 'BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD') `
            'SHA-256 reads a flushed VMware serial file while its shared writer remains open'
    }
    catch {
        Assert-Check $false "SHA-256 reads a flushed VMware serial file while its shared writer remains open ($($_.Exception.Message))"
    }
    finally {
        if ($null -ne $OpenSerialWriter) { $OpenSerialWriter.Dispose() }
    }

    # Exercise the two production writers in an isolated child scope. Only their
    # FunctionDefinitionAst bodies are dot-sourced; guest worker entrypoints stay inert.
    $TextOutputPath = Join-Path $FixtureRoot 'empty-worker-text.txt'
    $ByteOutputPath = Join-Path $FixtureRoot 'empty-startup-bytes.bin'
    $EmptyWriterResults = & {
        param($TextWriterDefinition, $ByteWriterDefinition, $TextPath, $BytesPath)
        . ([scriptblock]::Create($TextWriterDefinition))
        Write-Utf8NoBom -Path $TextPath -Text ''
        . ([scriptblock]::Create($ByteWriterDefinition))
        Write-Bytes -Path $BytesPath -Bytes ([byte[]]::new(0))
        [pscustomobject]@{
            TextFileExists = Test-Path -LiteralPath $TextPath -PathType Leaf
            TextFileLength = (Get-Item -LiteralPath $TextPath).Length
            ByteFileExists = Test-Path -LiteralPath $BytesPath -PathType Leaf
            ByteFileLength = (Get-Item -LiteralPath $BytesPath).Length
        }
    } $GuestTextWriter.Extent.Text $StartupByteWriter.Extent.Text $TextOutputPath $ByteOutputPath
    Assert-Check ($EmptyWriterResults.TextFileExists -and $EmptyWriterResults.TextFileLength -eq 0) `
        'guest UTF-8 writer accepts mandatory empty text and writes a zero-byte file'
    Assert-Check ($EmptyWriterResults.ByteFileExists -and $EmptyWriterResults.ByteFileLength -eq 0) `
        'startup byte writer accepts an empty mandatory byte array and writes a zero-byte file'

    # HostRunner's authenticated result.json is a transport envelope; the
    # guest inspection metadata, including SecureBoot and the boot-manager
    # digest, arrives in its authenticated stdout evidence archive.
    $InspectionEvidence = Join-Path $FixtureRoot 'inspection-evidence'
    New-Item -ItemType Directory -Path $InspectionEvidence | Out-Null
    $InspectionResultPath = Join-Path $InspectionEvidence 'result.json'
    $InspectionStdoutPath = Join-Path $InspectionEvidence 'stdout.txt'
    $HostRunnerResult = [ordered]@{
        SchemaVersion = 1
        RunId = 'gpu-hal-inspect-run-20261019-0123456789'
        Status = 'PASS'
        Message = 'Guest task completed.'
        CompletedUtc = '2026-10-19T12:00:00Z'
        Evidence = @()
    } | ConvertTo-Json -Compress
    [IO.File]::WriteAllText($InspectionResultPath, $HostRunnerResult,
        [Text.UTF8Encoding]::new($false))
    $InspectionPaths = [pscustomobject]@{ Evidence = $InspectionEvidence }
    $BootManagerHash = 'A' * 64
    $ValidInspectionOutput = @(
        'WindowsBuild=26200.9457'
        'DisplayVersion=24H2'
        'ExpectedBuild=26200.9457'
        'ExpectedCpus=4 ActualCpus=4'
        "BootManagerSha256=$BootManagerHash"
        'SecureBoot=True'
    ) -join "`r`n"
    [IO.File]::WriteAllText($InspectionStdoutPath, $ValidInspectionOutput,
        [Text.UTF8Encoding]::new($false))
    $Inspection = Assert-GuestInspection -Paths $InspectionPaths -Phase 'Inspect'
    Assert-Check ($Inspection.Status -ceq 'PASS' -and
        $Inspection.RunId -ceq 'gpu-hal-inspect-run-20261019-0123456789' -and
        $Inspection.SecureBoot -eq $true -and
        $Inspection.BootManagerSha256 -ceq $BootManagerHash) `
        'HostRunner result without SecureBoot accepts and parses stdout metadata'

    $MissingBootHashOutput = $ValidInspectionOutput.Replace("BootManagerSha256=$BootManagerHash`r`n", '')
    [IO.File]::WriteAllText($InspectionStdoutPath, $MissingBootHashOutput,
        [Text.UTF8Encoding]::new($false))
    Assert-Throws {
        Assert-GuestInspection -Paths $InspectionPaths -Phase 'Inspect'
    } 'missing boot-manager hash metadata is rejected' 'inspection evidence did not match'

    $DuplicateBootHashOutput = $ValidInspectionOutput + "`r`nBootManagerSha256=$BootManagerHash"
    [IO.File]::WriteAllText($InspectionStdoutPath, $DuplicateBootHashOutput,
        [Text.UTF8Encoding]::new($false))
    Assert-Throws {
        Assert-GuestInspection -Paths $InspectionPaths -Phase 'Inspect'
    } 'duplicate boot-manager hash metadata is rejected' 'inspection evidence did not match'

    $MalformedBootHashOutput = $ValidInspectionOutput.Replace(
        "BootManagerSha256=$BootManagerHash", 'BootManagerSha256=NOT-A-SHA256')
    [IO.File]::WriteAllText($InspectionStdoutPath, $MalformedBootHashOutput,
        [Text.UTF8Encoding]::new($false))
    Assert-Throws {
        Assert-GuestInspection -Paths $InspectionPaths -Phase 'Inspect'
    } 'malformed boot-manager hash metadata is rejected' 'inspection evidence did not match'

    $OutsideVmx = Join-Path $OutsideFixtureRoot 'source.vmx'
    [IO.File]::WriteAllText($OutsideVmx, 'vhv.enable = "TRUE"')
    Assert-Throws {
        Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot -Vmx $OutsideVmx
    } 'VMX outside the private build root is rejected' 'prepared disposable clone under|private build workspace'

    $MissingVmx = Join-Path $VmxFixtureRoot 'missing-disk.vmx'
    [IO.File]::WriteAllText($MissingVmx, "vhv.enable = `"TRUE`"`r`nscsi0:0.fileName = `"missing.vmdk`"")
    Assert-Throws {
        Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot -Vmx $MissingVmx
    } 'missing VMDK is rejected' 'VMDK backing descriptor is missing or outside'

    $ExternalParentDisk = Join-Path $OutsideFixtureRoot 'external-parent.vmdk'
    [IO.File]::WriteAllText($ExternalParentDisk, @'
# Disk DescriptorFile
CID=87654321
parentCID=ffffffff
RW 8 SPARSE "external-parent-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $OutsideFixtureRoot 'external-parent-flat.vmdk'), 'synthetic extent')
    $ExternalChildDisk = Join-Path $VmxFixtureRoot 'external-parent-child.vmdk'
    [IO.File]::WriteAllText($ExternalChildDisk, @"
# Disk DescriptorFile
CID=12345678
parentCID=87654321
parentFileNameHint = `"$ExternalParentDisk`"
RW 8 SPARSE `"external-child-flat.vmdk`"
"@)
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'external-child-flat.vmdk'), 'synthetic extent')
    Assert-Throws {
        Assert-StandaloneDisk -DiskPath $ExternalChildDisk -BuildRoot $VmxFixtureRoot
    } 'VMDK parent descriptor outside the private root is rejected' 'escapes the private build workspace'

    $GoodVmx = Join-Path $VmxFixtureRoot 'standalone.vmx'
    $GoodDisk = Join-Path $VmxFixtureRoot 'standalone.vmdk'
    [IO.File]::WriteAllText($GoodVmx, "vhv.enable = `"TRUE`"`r`nscsi0:0.fileName = `"standalone.vmdk`"")
    [IO.File]::WriteAllText($GoodDisk, @'
# Disk DescriptorFile
CID=12345678
parentCID=ffffffff
createType="monolithicSparse"
RW 8 SPARSE "standalone-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'standalone-flat.vmdk'), 'synthetic extent')
    try {
        $null = Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot -Vmx $GoodVmx
        Assert-Check $true 'standalone private VMDK is accepted'
    }
    catch { Assert-Check $false "standalone private VMDK is accepted ($($_.Exception.Message))" }

    $ModelMissingConfiguredNvramVmx = Join-Path $VmxFixtureRoot 'model-missing-configured-nvram.vmx'
    [IO.File]::WriteAllText($ModelMissingConfiguredNvramVmx,
        "vhv.enable = `"TRUE`"`r`nscsi0:0.fileName = `"standalone.vmdk`"`r`nnvram = `"missing-configured.nvram`"")
    Assert-Throws {
        Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot `
            -Vmx $ModelMissingConfiguredNvramVmx -ValidationProfile Model
    } 'Model rejects an explicitly configured missing NVRAM file' 'references a missing NVRAM file'

    $PinnedPublicCertificate = Join-Path $VmxFixtureRoot 'pinned-public-certificate.cer'
    $PinnedPublicCertificateBytes = [byte[]]@(0x30, 0x05, 0x02, 0x01, 0x01, 0x04, 0x01)
    [IO.File]::WriteAllBytes($PinnedPublicCertificate, $PinnedPublicCertificateBytes)
    function New-TestSignedNativeClone {
        param(
            [string]$CaseName,
            [string]$SecureBoot = 'TRUE',
            [string]$DbCertificate = 'hypersinv-public.cer',
            [string]$DbAppend = 'TRUE',
            [switch]$OmitDbCertificate,
            [switch]$WrongCertificateBytes,
            [ValidateSet('None', 'File', 'Directory', 'Junction')][string]$NvramCollision = 'None'
        )
        $vmIdentity = ([Guid]::NewGuid().ToString('N')).Substring(0, 8)
        $cloneRoot = Join-Path $VmxFixtureRoot ("gpu_payload_vm\vm-20261020T123456Z-$vmIdentity\clone")
        [void](New-Item -ItemType Directory -Path $cloneRoot -Force)
        $vmxPath = Join-Path $cloneRoot 'hsinv-Windows-25H2.vmx'
        $diskPath = Join-Path $cloneRoot 'test.vmdk'
        [IO.File]::WriteAllText($diskPath, @'
# Disk DescriptorFile
CID=12345678
parentCID=ffffffff
createType="monolithicSparse"
RW 8 SPARSE "test-flat.vmdk"
'@)
        [IO.File]::WriteAllText((Join-Path $cloneRoot 'test-flat.vmdk'), 'synthetic extent')
        $vmxLines = [Collections.Generic.List[string]]::new()
        $vmxLines.Add('vhv.enable = "TRUE"')
        $vmxLines.Add('scsi0:0.fileName = "test.vmdk"')
        $vmxLines.Add('nvram = "gpu-payload-test.nvram"')
        $vmxLines.Add("uefi.secureBoot.enabled = `"$SecureBoot`"")
        if (-not $OmitDbCertificate) {
            $vmxLines.Add("uefi.secureBoot.dbDefault.file0 = `"$DbCertificate`"")
        }
        $vmxLines.Add("uefi.secureBoot.dbDefault.append = `"$DbAppend`"")
        [IO.File]::WriteAllLines($vmxPath, $vmxLines.ToArray(), [Text.UTF8Encoding]::new($false))

        $cloneCertificate = Join-Path $cloneRoot 'hypersinv-public.cer'
        $certificateBytes = if ($WrongCertificateBytes) {
            [byte[]]@(0x30, 0x05, 0x02, 0x01, 0x02, 0x04, 0x01)
        } else { $PinnedPublicCertificateBytes }
        [IO.File]::WriteAllBytes($cloneCertificate, $certificateBytes)
        $nvramPath = Join-Path $cloneRoot 'gpu-payload-test.nvram'
        switch ($NvramCollision) {
            'File' { [IO.File]::WriteAllBytes($nvramPath, [byte[]]@(1, 2, 3)) }
            'Directory' { [void](New-Item -ItemType Directory -Path $nvramPath) }
            'Junction' {
                $target = Join-Path $OutsideFixtureRoot ("nvram-target-$vmIdentity")
                [void](New-Item -ItemType Directory -Path $target)
                [void](New-Item -ItemType Junction -Path $nvramPath -Target $target)
                $JunctionPaths.Add($nvramPath)
            }
        }
        return [pscustomobject]@{ VmxPath = $vmxPath; CloneRoot = $cloneRoot; NvramPath = $nvramPath }
    }

    $ValidSignedNativeClone = New-TestSignedNativeClone -CaseName 'valid'
    try {
        $null = Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot `
            -Vmx $ValidSignedNativeClone.VmxPath -ValidationProfile SignedNative `
            -CertificatePath $PinnedPublicCertificate
        Assert-Check $true 'SignedNative accepts pinned Secure Boot defaults and an absent fresh test NVRAM'
    }
    catch { Assert-Check $false "SignedNative accepts pinned Secure Boot defaults and an absent fresh test NVRAM ($($_.Exception.Message))" }

    $InvalidSignedNativeClones = @()
    $InvalidSignedNativeClones += [pscustomobject]@{
        Name = 'Secure Boot disabled'; Clone = (New-TestSignedNativeClone -CaseName 'secureboot-off' -SecureBoot 'FALSE')
    }
    $InvalidSignedNativeClones += [pscustomobject]@{
        Name = 'dbDefault certificate setting missing'; Clone = (New-TestSignedNativeClone -CaseName 'missing-cert-setting' -OmitDbCertificate)
    }
    $InvalidSignedNativeClones += [pscustomobject]@{
        Name = 'dbDefault certificate path changed'; Clone = (New-TestSignedNativeClone -CaseName 'wrong-cert-setting' -DbCertificate 'other.cer')
    }
    $InvalidSignedNativeClones += [pscustomobject]@{
        Name = 'dbDefault append disabled'; Clone = (New-TestSignedNativeClone -CaseName 'append-disabled' -DbAppend 'FALSE')
    }
    $InvalidSignedNativeClones += [pscustomobject]@{
        Name = 'dbDefault certificate does not match its pin'; Clone = (New-TestSignedNativeClone -CaseName 'certificate-mismatch' -WrongCertificateBytes)
    }
    $InvalidSignedNativeClones += [pscustomobject]@{
        Name = 'fresh NVRAM file collision'; Clone = (New-TestSignedNativeClone -CaseName 'nvram-file-collision' -NvramCollision File)
    }
    $InvalidSignedNativeClones += [pscustomobject]@{
        Name = 'fresh NVRAM directory collision'; Clone = (New-TestSignedNativeClone -CaseName 'nvram-directory-collision' -NvramCollision Directory)
    }
    $InvalidSignedNativeClones += [pscustomobject]@{
        Name = 'fresh NVRAM reparse collision'; Clone = (New-TestSignedNativeClone -CaseName 'nvram-junction-collision' -NvramCollision Junction)
    }
    foreach ($InvalidSignedNative in $InvalidSignedNativeClones) {
        Assert-Throws {
            Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot `
                -Vmx $InvalidSignedNative.Clone.VmxPath -ValidationProfile SignedNative `
                -CertificatePath $PinnedPublicCertificate
        } "SignedNative rejects $($InvalidSignedNative.Name)" `
            'SignedNative|NVRAM|certificate|Secure Boot|reparse|path boundary'
    }

    $ParentChainDisk = Join-Path $VmxFixtureRoot 'backing-child.vmdk'
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'backing-parent.vmdk'), @'
# Disk DescriptorFile
CID=11223344
parentCID=ffffffff
RW 8 SPARSE "backing-parent-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'backing-parent-flat.vmdk'), 'synthetic extent')
    [IO.File]::WriteAllText($ParentChainDisk, @'
# Disk DescriptorFile
CID=AABBCCDD
parentCID=11223344
parentFileNameHint = "backing-parent.vmdk"
RW 8 SPARSE "backing-child-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'backing-child-flat.vmdk'), 'synthetic extent')
    try {
        Assert-StandaloneDisk -DiskPath $ParentChainDisk -BuildRoot $VmxFixtureRoot
        Assert-Check $true 'private parent VMDK chain with matching CIDs is accepted'
    }
    catch { Assert-Check $false "private parent VMDK chain with matching CIDs is accepted ($($_.Exception.Message))" }

    $MissingParentDisk = Join-Path $VmxFixtureRoot 'missing-parent-child.vmdk'
    [IO.File]::WriteAllText($MissingParentDisk, @'
# Disk DescriptorFile
CID=11112222
parentCID=33334444
parentFileNameHint = "missing-parent.vmdk"
RW 8 SPARSE "missing-parent-child-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'missing-parent-child-flat.vmdk'), 'synthetic extent')
    Assert-Throws {
        Assert-StandaloneDisk -DiskPath $MissingParentDisk -BuildRoot $VmxFixtureRoot
    } 'missing VMDK parent is rejected' 'backing descriptor is missing or outside'

    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'cycle-a.vmdk'), @'
# Disk DescriptorFile
CID=AAAA0001
parentCID=BBBB0002
parentFileNameHint = "cycle-b.vmdk"
RW 8 SPARSE "cycle-a-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'cycle-b.vmdk'), @'
# Disk DescriptorFile
CID=BBBB0002
parentCID=AAAA0001
parentFileNameHint = "cycle-a.vmdk"
RW 8 SPARSE "cycle-b-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'cycle-a-flat.vmdk'), 'synthetic extent')
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'cycle-b-flat.vmdk'), 'synthetic extent')
    Assert-Throws {
        Assert-StandaloneDisk -DiskPath (Join-Path $VmxFixtureRoot 'cycle-a.vmdk') `
            -BuildRoot $VmxFixtureRoot
    } 'cyclic VMDK backing chain is rejected' 'contains a cycle'

    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'cid-mismatch-parent.vmdk'), @'
# Disk DescriptorFile
CID=99998888
parentCID=ffffffff
RW 8 SPARSE "cid-mismatch-parent-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'cid-mismatch-parent-flat.vmdk'), 'synthetic extent')
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'cid-mismatch-child.vmdk'), @'
# Disk DescriptorFile
CID=77776666
parentCID=11112222
parentFileNameHint = "cid-mismatch-parent.vmdk"
RW 8 SPARSE "cid-mismatch-child-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'cid-mismatch-child-flat.vmdk'), 'synthetic extent')
    Assert-Throws {
        Assert-StandaloneDisk -DiskPath (Join-Path $VmxFixtureRoot 'cid-mismatch-child.vmdk') `
            -BuildRoot $VmxFixtureRoot
    } 'parent CID mismatch is rejected' 'parent CID does not match'

    $Payload = [ordered]@{
        SchemaVersion = 1
        RunId = 'gpu-hal-windows-test-001'
        Status = 'PASS'
        ValidationProfile = 'Model'
        ExpectedSecureBoot = $false
        SecureBoot = $false
        ExpectedCertificateDerPresent = $null
        ExpectedCertificateSha256 = $null
        CertificateDbInspection = $null
        HandoffStatus = $null
        HandoffStatusBytes = $null
        HandoffStatusCapturedBytes = $null
        HandoffStatusTooLarge = $null
        PayloadDiagnostic = $null
        PayloadDiagnosticBytes = $null
        PayloadDiagnosticCapturedBytes = $null
        PayloadDiagnosticTooLarge = $null
        DiagnosticTextEncoding = $null
        CurrentBuild = $ExpectedBuild
        Ubr = $ExpectedUbr
        ExpectedCpus = $ExpectedCpus
        CpuPassCount = $ExpectedCpus
        HealthPassed = $true
        BootManagerUnchanged = $true
        BootManagerSha256Before = ('A' * 64)
        BootManagerSha256After = ('A' * 64)
    } | ConvertTo-Json -Compress
    $PayloadBytes = [Text.UTF8Encoding]::new($false).GetBytes($Payload)
    $Hmac = [Security.Cryptography.HMACSHA256]::new($script:ReviewKey)
    $Digest = $Hmac.ComputeHash($PayloadBytes)
    $DigestHex = ([BitConverter]::ToString($Digest)).Replace('-', '')
    $PayloadBase64 = [Convert]::ToBase64String($PayloadBytes)
    $ValidFrame = "GPU_HAL_WINDOWS:RESULT:$PayloadBase64.$DigestHex"
    $Accepted = Get-AuthenticatedWindowsResult -SerialText $ValidFrame `
        -ExpectedRunId 'gpu-hal-windows-test-001'
    Assert-Check ($null -ne $Accepted) 'valid HMAC, run, build, CPU, health, and boot hash are accepted'

    $WrongHmacFrame = "GPU_HAL_WINDOWS:RESULT:$PayloadBase64.$(('0' * 64))"
    $WrongHmacResult = Get-AuthenticatedWindowsResult -SerialText $WrongHmacFrame `
        -ExpectedRunId 'gpu-hal-windows-test-001'
    Assert-Check ($null -eq $WrongHmacResult) 'wrong HMAC is rejected'

    $WrongRunResult = Get-AuthenticatedWindowsResult -SerialText $ValidFrame `
        -ExpectedRunId 'gpu-hal-windows-other'
    Assert-Check ($null -eq $WrongRunResult) 'wrong run identity is rejected'

    $MissingHealthPayload = [ordered]@{
        SchemaVersion = 1
        RunId = 'gpu-hal-windows-test-001'
        Status = 'PASS'
        CurrentBuild = $ExpectedBuild
        Ubr = $ExpectedUbr
        ExpectedCpus = $ExpectedCpus
        CpuPassCount = $ExpectedCpus
        BootManagerUnchanged = $true
        BootManagerSha256Before = ('A' * 64)
        BootManagerSha256After = ('A' * 64)
    } | ConvertTo-Json -Compress
    $MissingHealthBytes = [Text.UTF8Encoding]::new($false).GetBytes($MissingHealthPayload)
    $MissingHealthDigest = $Hmac.ComputeHash($MissingHealthBytes)
    $MissingHealthHex = ([BitConverter]::ToString($MissingHealthDigest)).Replace('-', '')
    $MissingHealthFrame = "GPU_HAL_WINDOWS:RESULT:$([Convert]::ToBase64String($MissingHealthBytes)).$MissingHealthHex"
    $MissingHealthResult = Get-AuthenticatedWindowsResult -SerialText $MissingHealthFrame `
        -ExpectedRunId 'gpu-hal-windows-test-001'
    Assert-Check ($null -eq $MissingHealthResult) 'missing Windows health proof is rejected'

    $StartupRunId = 'gpu-hal-startup-fail-20261019-0123456789'
    $SignedFailurePayload = [ordered]@{
        SchemaVersion = 1
        RunId = $StartupRunId
        Status = 'FAIL'
        ValidationProfile = 'Model'
        SecureBoot = $false
        ExpectedSecureBoot = $false
        ExpectedCertificateDerPresent = $null
        ExpectedCertificateSha256 = $null
        CertificateDbInspection = $null
        HandoffStatus = $null
        HandoffStatusBytes = $null
        HandoffStatusCapturedBytes = $null
        HandoffStatusTooLarge = $null
        PayloadDiagnostic = $null
        PayloadDiagnosticBytes = $null
        PayloadDiagnosticCapturedBytes = $null
        PayloadDiagnosticTooLarge = $null
        DiagnosticTextEncoding = $null
        Message = 'Synthetic authenticated startup failure.'
    } | ConvertTo-Json -Compress
    $SignedFailureBytes = [Text.UTF8Encoding]::new($false).GetBytes($SignedFailurePayload)
    $SignedFailureDigest = $Hmac.ComputeHash($SignedFailureBytes)
    $SignedFailureHex = ([BitConverter]::ToString($SignedFailureDigest)).Replace('-', '')
    $SignedFailureFrame = "GPU_HAL_WINDOWS:RESULT:$([Convert]::ToBase64String($SignedFailureBytes)).$SignedFailureHex"
    $ForgedFailureHex = if ($SignedFailureHex[0] -ceq '0') {
        '1' + $SignedFailureHex.Substring(1)
    } else { '0' + $SignedFailureHex.Substring(1) }
    $ForgedFailureFrame = "GPU_HAL_WINDOWS:RESULT:$([Convert]::ToBase64String($SignedFailureBytes)).$ForgedFailureHex"

    # Exercise the real waiter and HMAC parser with only serial reads/sleep
    # mocked. A same-run signed FAIL must terminate before the absent EFI log
    # can trigger its longer wait timeout; an invalid MAC must not be trusted.
    $OriginalReadBoundedText = [scriptblock]::Create(
        $FunctionDefinitions['Read-BoundedText'].Extent.Text)
    $OriginalStartSleep = Get-Command Start-Sleep -CommandType Function -ErrorAction SilentlyContinue
    $OldTimeoutVariable = Get-Variable -Name TimeoutSeconds -Scope Script -ErrorAction SilentlyContinue
    $OldTimeoutSeconds = if ($null -ne $OldTimeoutVariable) { $OldTimeoutVariable.Value } else { $null }
    $script:Serial0Path = 'synthetic-empty-efi-serial'
    $script:Serial1Path = 'synthetic-windows-com2-serial'
    $script:WaitMockEfiText = ''
    $script:WaitMockWindowsText = ''
    $script:WaitMockReadCount = 0
    $script:WaitMockSleepCount = 0
    Set-Item -Path Function:\Read-BoundedText -Value {
        param([string]$Path, [int]$MaximumBytes = 33554432)
        $script:WaitMockReadCount++
        if ($Path -ceq $script:Serial0Path) { return $script:WaitMockEfiText }
        if ($Path -ceq $script:Serial1Path) { return $script:WaitMockWindowsText }
        throw "Unexpected synthetic serial path: $Path"
    }
    Set-Item -Path Function:\Start-Sleep -Value {
        param([int]$Milliseconds, [int]$Seconds)
        $script:WaitMockSleepCount++
    }
    try {
        $TimeoutSeconds = 30
        $script:EfiStatus = 'NOT_RUN'
        $script:StartupStatus = 'NOT_RUN'
        $script:WaitMockWindowsText = $SignedFailureFrame
        $script:WaitMockReadCount = 0
        $script:WaitMockSleepCount = 0
        $SignedFailureError = $null
        try { $null = Wait-WindowsVmEvidence -StartupRunId $StartupRunId }
        catch { $SignedFailureError = [string]$_.Exception.Message }
        Assert-Check ($SignedFailureError -match 'authenticated Windows startup result failed: Synthetic authenticated startup failure' -and
            $script:WaitMockReadCount -eq 2 -and $script:WaitMockSleepCount -eq 0) `
            'signed same-run startup failure is authenticated and aborts before EFI timeout'

        $TimeoutSeconds = 0
        $script:EfiStatus = 'NOT_RUN'
        $script:StartupStatus = 'NOT_RUN'
        $script:WaitMockWindowsText = $ForgedFailureFrame
        $script:WaitMockReadCount = 0
        $script:WaitMockSleepCount = 0
        $ForgedFailureError = $null
        try { $null = Wait-WindowsVmEvidence -StartupRunId $StartupRunId }
        catch { $ForgedFailureError = [string]$_.Exception.Message }
        Assert-Check ($ForgedFailureError -match 'Timed out waiting for EFI model' -and
            $ForgedFailureError -notmatch 'authenticated Windows startup result failed' -and
            $script:WaitMockReadCount -eq 2 -and $script:WaitMockSleepCount -eq 1) `
            'forged startup FAIL HMAC is ignored and cannot preempt EFI timeout'
    }
    finally {
        Set-Item -Path Function:\Read-BoundedText -Value $OriginalReadBoundedText
        if ($null -ne $OriginalStartSleep) {
            Set-Item -Path Function:\Start-Sleep -Value $OriginalStartSleep.ScriptBlock
        } else {
            Remove-Item -Path Function:\Start-Sleep -Force -ErrorAction SilentlyContinue
        }
        if ($null -ne $OldTimeoutVariable) {
            Set-Variable -Name TimeoutSeconds -Scope Script -Value $OldTimeoutSeconds
        } else {
            Remove-Variable -Name TimeoutSeconds -Scope Script -ErrorAction SilentlyContinue
        }
        Remove-Variable -Name Serial0Path, Serial1Path, WaitMockEfiText,
            WaitMockWindowsText, WaitMockReadCount, WaitMockSleepCount,
            EfiStatus, StartupStatus -Scope Script -ErrorAction SilentlyContinue
    }

    # Run02 regression: PowerShell binds mandatory string-list parameters against
    # blank VMX lines, and callers may also pass an initially empty line list.
    $EmptyLines = [Collections.Generic.List[string]]::new()
    Set-VmxSetting -Lines $EmptyLines -Key 'firmware' -Value 'efi'
    Assert-Check ($EmptyLines.Count -eq 1 -and $EmptyLines[0] -ceq 'firmware = "efi"') `
        'empty VMX line list accepts a mandatory setting'
    $BlankLines = [Collections.Generic.List[string]]::new([string[]]@('', ' ', 'vhv.enable = "TRUE"', ''))
    Set-VmxSetting -Lines $BlankLines -Key 'snapshot.disabled' -Value 'TRUE'
    Assert-Check ($BlankLines.Count -eq 5 -and $BlankLines[0] -ceq '' -and
        $BlankLines[1] -ceq ' ' -and $BlankLines[3] -ceq '' -and
        $BlankLines[4] -ceq 'snapshot.disabled = "TRUE"') `
        'blank VMX lines survive settings update without binder failure'

    $JunctionTarget = Join-Path $OutsideFixtureRoot 'junction-target'
    New-Item -ItemType Directory -Path $JunctionTarget | Out-Null
    $Junction = Join-Path $VmxFixtureRoot 'ancestor-junction'
    New-Item -ItemType Junction -Path $Junction -Target $JunctionTarget | Out-Null
    $JunctionPaths.Add($Junction)
    [IO.File]::WriteAllText((Join-Path $JunctionTarget 'alias.vmx'),
        "vhv.enable = `"TRUE`"`r`nscsi0:0.fileName = `"alias.vmdk`"")
    [IO.File]::WriteAllText((Join-Path $JunctionTarget 'alias.vmdk'), @'
# Disk DescriptorFile
CID=12345678
parentCID=ffffffff
RW 8 SPARSE "alias-flat.vmdk"
'@)
    [IO.File]::WriteAllText((Join-Path $JunctionTarget 'alias-flat.vmdk'), 'synthetic extent')
    Assert-Throws {
        Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot `
            -Vmx (Join-Path $Junction 'alias.vmx')
    } 'ancestor junction is rejected' 'reparse point|path boundary'

    $script:CommandLog = Join-Path $FixtureRoot 'redaction.jsonl'
    $script:CommandNumber = 0
    $script:CommandLogLimit = 256
    $SyntheticPassword = 'synthetic-vm-password'
    $SyntheticGuestInfo = 'synthetic-guestinfo-payload'
    Write-CommandRecord -FilePath 'synthetic-vmrun.exe' `
        -ArgumentList @('-vp', $SyntheticPassword, 'writeVariable', 'fixture.vmx',
            'guestVar', 'fixture.request.chunk.000000', $SyntheticGuestInfo) `
        -WorkingDirectory $FixtureRoot -TimeoutSeconds 3 -Redactions @($SyntheticPassword)
    $CommandLogText = Get-Content -Raw -LiteralPath $script:CommandLog
    Assert-Check ($CommandLogText.Contains('redacted') -and
        $CommandLogText.Contains('guestinfo-value-redacted') -and
        -not $CommandLogText.Contains($SyntheticPassword) -and
        -not $CommandLogText.Contains($SyntheticGuestInfo)) `
        'command log redacts synthetic VM password and GuestInfo payload'

    $script:VmStartAttempted = $true
    $script:VmOffConfirmed = $false
    $script:Selected = [pscustomobject]@{ VmxPath = 'synthetic-vmx' }
    $script:CleanupFailures = [Collections.Generic.List[string]]::new()
    $script:StopEvents = [Collections.Generic.List[string]]::new()
    $script:RunningProbes = 0
    $script:StopCalls = [Collections.Generic.List[string]]::new()
    function Test-TargetVmRunning {
        $script:RunningProbes++
        if ($script:RunningProbes -eq 1) { return $true }
        throw 'synthetic process-list failure'
    }
    function Invoke-TargetVmrun {
        param([string[]]$Operation, [int]$Timeout)
        $script:StopCalls.Add(($Operation -join ' '))
        return [pscustomobject]@{ ExitCode = 0; TimedOut = $false }
    }
    Stop-OnlyTargetVm
    Assert-Check (-not $script:VmOffConfirmed -and $script:CleanupFailures.Count -gt 0) `
        'uncertain VM state remains fail-closed and records cleanup failure'
    Assert-Check (($script:StopCalls -join '|') -match 'soft' -and
        ($script:StopCalls -join '|') -match 'hard') `
        'uncertain VM stop attempts bounded soft and hard stop'

    $HardVmx = Join-Path $VmxFixtureRoot 'hardlink.vmx'
    $HardDisk = Join-Path $VmxFixtureRoot 'hardlink.vmdk'
    $OutsideHardlinkDir = Join-Path $OutsideFixtureRoot 'hardlink-vm'
    New-Item -ItemType Directory -Path $OutsideHardlinkDir | Out-Null
    $OutsideExtent = Join-Path $OutsideHardlinkDir 'shared-flat.vmdk'
    $BuildExtent = Join-Path $VmxFixtureRoot 'shared-flat.vmdk'
    [IO.File]::WriteAllText($OutsideExtent, 'synthetic shared extent')
    New-Item -ItemType HardLink -Path $BuildExtent -Target $OutsideExtent | Out-Null
    [IO.File]::WriteAllText($HardVmx,
        "vhv.enable = `"TRUE`"`r`nscsi0:0.fileName = `"hardlink.vmdk`"")
    [IO.File]::WriteAllText($HardDisk, @'
# Disk DescriptorFile
CID=12345678
parentCID=ffffffff
RW 8 SPARSE "shared-flat.vmdk"
'@)
    $LinkCount = Get-WindowsHardLinkCount $BuildExtent
    Assert-Check ($LinkCount -eq 2) 'Win32 file metadata observes the synthetic outside hard link'
    Assert-Throws {
        Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot -Vmx $HardVmx
    } 'hard-linked VMDK extent is rejected' 'exactly one filesystem link'

    $OutsideVmx = Join-Path $OutsideFixtureRoot 'shared-source.vmx'
    $HardVmxAlias = Join-Path $VmxFixtureRoot 'hardlinked-source.vmx'
    [IO.File]::WriteAllText($OutsideVmx,
        "vhv.enable = `"TRUE`"`r`nscsi0:0.fileName = `"standalone.vmdk`"")
    New-Item -ItemType HardLink -Path $HardVmxAlias -Target $OutsideVmx | Out-Null
    Assert-Throws {
        Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot -Vmx $HardVmxAlias
    } 'hard-linked VMX is rejected' 'exactly one filesystem link'

    $OutsideDisk = Join-Path $OutsideFixtureRoot 'shared-source.vmdk'
    $HardDiskAlias = Join-Path $VmxFixtureRoot 'hardlinked-source.vmdk'
    [IO.File]::WriteAllText((Join-Path $VmxFixtureRoot 'descriptor-flat.vmdk'), 'synthetic extent')
    [IO.File]::WriteAllText($OutsideDisk, @'
# Disk DescriptorFile
CID=12345678
parentCID=ffffffff
RW 8 SPARSE "descriptor-flat.vmdk"
'@)
    New-Item -ItemType HardLink -Path $HardDiskAlias -Target $OutsideDisk | Out-Null
    $DiskHardlinkVmx = Join-Path $VmxFixtureRoot 'disk-hardlink.vmx'
    [IO.File]::WriteAllText($DiskHardlinkVmx,
        "vhv.enable = `"TRUE`"`r`nscsi0:0.fileName = `"hardlinked-source.vmdk`"")
    Assert-Throws {
        Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot -Vmx $DiskHardlinkVmx
    } 'hard-linked VMDK descriptor is rejected' 'exactly one filesystem link'

    $OutsideNvram = Join-Path $OutsideFixtureRoot 'shared-source.nvram'
    $NvramAlias = Join-Path $VmxFixtureRoot 'shared-source.nvram'
    [IO.File]::WriteAllText($OutsideNvram, 'synthetic NVRAM')
    New-Item -ItemType HardLink -Path $NvramAlias -Target $OutsideNvram | Out-Null
    $NvramVmx = Join-Path $VmxFixtureRoot 'nvram-hardlink.vmx'
    [IO.File]::WriteAllText($NvramVmx,
        "vhv.enable = `"TRUE`"`r`nscsi0:0.fileName = `"standalone.vmdk`"`r`nnvram = `"shared-source.nvram`"")
    Assert-Throws {
        Assert-PreparedClone -Root $RepositoryRoot -BuildRoot $VmxFixtureRoot -Vmx $NvramVmx
    } 'hard-linked NVRAM is rejected' 'exactly one filesystem link'

    Write-Output "RESULT: $script:Passed pure controller checks passed."
}
finally {
    if ($null -ne $Hmac) { $Hmac.Dispose() }
    if (Test-Path -LiteralPath $FixtureRoot) {
        $ExpectedBuildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot '.codex-build'))
        $ExpectedFixtureRoot = Join-Path $ExpectedBuildRoot ([IO.Path]::GetFileName($FixtureRoot))
        $ActualParent = [IO.Path]::GetFullPath((Split-Path -Parent $FixtureRoot))
        $FixtureItem = Get-Item -LiteralPath $FixtureRoot -Force
        $IsExpectedRoot = $FixtureRoot.Equals(
            [IO.Path]::GetFullPath($ExpectedFixtureRoot), [StringComparison]::OrdinalIgnoreCase) -and
            $ActualParent.Equals($ExpectedBuildRoot, [StringComparison]::OrdinalIgnoreCase) -and
            $FixtureItem.Name -match '^gpu-controller-contract-[0-9a-f]{32}$' -and
            (($FixtureItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0)
        if (-not $IsExpectedRoot) {
            Write-Warning "Fixture cleanup skipped because the absolute root failed ownership validation: $FixtureRoot"
        }
        else {
            foreach ($JunctionPath in $JunctionPaths) {
                if (Test-Path -LiteralPath $JunctionPath) {
                    $JunctionItem = Get-Item -LiteralPath $JunctionPath -Force
                    if (($JunctionItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
                        throw "Fixture junction cleanup refused a non-reparse entry: $JunctionPath"
                    }
                    [IO.Directory]::Delete($JunctionPath, $false)
                }
            }
            $RemainingReparsePoints = @(Get-ChildItem -LiteralPath $FixtureRoot -Force -Recurse |
                Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 })
            if ($RemainingReparsePoints.Count -gt 0) {
                throw "Fixture cleanup refused nested reparse points: $($RemainingReparsePoints[0].FullName)"
            }
            Remove-Item -LiteralPath $FixtureRoot -Recurse -Force
        }
    }
}
