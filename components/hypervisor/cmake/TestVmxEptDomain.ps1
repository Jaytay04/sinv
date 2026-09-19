param(
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [Parameter(Mandatory = $true)][string]$HeaderPath,
    [Parameter(Mandatory = $true)][string]$CoreCMakePath,
    [Parameter(Mandatory = $true)][string]$DriverMainPath,
    [Parameter(Mandatory = $true)][string]$IoctlPath,
    [Parameter(Mandatory = $true)][string]$RuntimePath,
    [Parameter(Mandatory = $true)][string]$HookPath,
    [Parameter(Mandatory = $true)][string]$TestPath
)
$ErrorActionPreference = 'Stop'
$source = Get-Content -Raw -LiteralPath $SourcePath
$header = Get-Content -Raw -LiteralPath $HeaderPath
$core = Get-Content -Raw -LiteralPath $CoreCMakePath
$test = Get-Content -Raw -LiteralPath $TestPath

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Ordered([string]$Text, [string[]]$Tokens) {
    $at = 0
    foreach ($token in $Tokens) {
        $next = $Text.IndexOf($token, $at, [StringComparison]::Ordinal)
        if ($next -lt 0) { return $false }
        $at = $next + $token.Length
    }
    return $true
}
function Section([string]$Text, [string]$Start, [string]$End) {
    $begin = $Text.IndexOf($Start, [StringComparison]::Ordinal)
    $finish = $Text.IndexOf($End, $begin + $Start.Length,
        [StringComparison]::Ordinal)
    if ($begin -lt 0 -or $finish -lt 0) { return '' }
    return $Text.Substring($begin, $finish - $begin)
}
function Verify([string]$Text) {
    $initialize = Section $Text 'VmxEptDomainInitialize(' 'VmxEptDomainPrepareRootEnter('
    $enter = Section $Text 'VmxEptDomainPrepareRootEnter(' 'VmxEptDomainBeginMutation('
    $use = Section $Text 'VmxEptDomainBeginUse(' 'VmxEptDomainBuildInvalidation('
    $build = Section $Text 'VmxEptDomainBuildInvalidation(' 'VmxEptDomainValidateInvalidationReport('
    $validate = Section $Text 'VmxEptDomainValidateInvalidationReport(' 'VmxEptDomainCommitInvalidation('
    $commit = Section $Text 'VmxEptDomainCommitInvalidation(' 'VmxEptDomainPrepareRootRelease('
    $release = Section $Text 'VmxEptDomainPrepareRootRelease(' 'VmxEptDomainDestroy('
    $destroy = $Text.Substring($Text.IndexOf('VmxEptDomainDestroy(',
        [StringComparison]::Ordinal))
    return (Ordered $initialize @(
        '!InitializeInputsSeparate(',
        'capabilities_snapshot = *capabilities;',
        'identity_snapshot = *identity;',
        'plan_snapshot = *plan;',
        'populate_snapshot = *populate;',
        'binding_snapshot = *binding;',
        '!InitializeStorageSeparate(',
        '!DomainZero(domain)',
        'VmxEptValidateStorageBinding(',
        '&capabilities_snapshot, &identity_snapshot, &plan_snapshot',
        'VmxEptBuildPointer(',
        'VmxEptPopulateIdentityMap(',
        '&capabilities_snapshot, &identity_snapshot, &plan_snapshot',
        'staged.MutationGeneration = 1;',
        'staged.InvalidatedGeneration = 1;',
        '*domain = staged;'
    )) -and (Ordered $enter @(
        'VmxEptDomainPrepareRootEnter(',
        'domain->Stage == VMX_EPT_DOMAIN_STAGE_READY',
        'VmxEptDomainAdoptRoot(',
        'VmxEptDomainPrepareRootEnter(domain)',
        'domain->Stage = VMX_EPT_DOMAIN_STAGE_ROOT_OWNED;'
    )) -and (Ordered $use @(
        'return VmxEptDomainBeginMutation(domain);'
    )) -and (Ordered $build @(
        'domain->Stage != VMX_EPT_DOMAIN_STAGE_ROOT_OWNED',
        'domain->MutationGeneration == domain->InvalidatedGeneration',
        'VmxEptBuildInvalidationDescriptor(',
        'domain->PendingGeneration = domain->MutationGeneration;',
        '*request = staged_request;',
        '*descriptor = staged_descriptor;'
    )) -and (Ordered $validate @(
        'domain->PendingGeneration != domain->MutationGeneration',
        'report->Attempted != 1',
        'report->EptPointer != domain->EptPointer',
        'report->InstructionResult == VMX_EPT_INSTRUCTION_SUCCESS',
        'return VMX_EPT_DOMAIN_OK;',
        'report->InstructionResult == VMX_EPT_INSTRUCTION_FAIL_VALID',
        'return VMX_EPT_DOMAIN_INSTRUCTION_FAILED;'
    )) -and (Ordered $commit @(
        'VmxEptDomainValidateInvalidationReport(domain, report)',
        'status != VMX_EPT_DOMAIN_OK',
        'domain->InvalidatedGeneration = domain->PendingGeneration;',
        'domain->PendingGeneration = 0;'
    )) -and (Ordered $release @(
        'VmxEptDomainPrepareRootRelease(',
        'domain->Stage != VMX_EPT_DOMAIN_STAGE_ROOT_OWNED',
        'domain->PendingGeneration != 0',
        'domain->MutationGeneration != domain->InvalidatedGeneration',
        'VmxEptDomainCompleteRootRelease(',
        'VmxEptDomainPrepareRootRelease(domain)',
        'domain->Stage = VMX_EPT_DOMAIN_STAGE_READY;'
    )) -and (Ordered $destroy @(
        'domain->Stage != VMX_EPT_DOMAIN_STAGE_READY',
        'domain->MutationGeneration != domain->InvalidatedGeneration',
        'memset(domain, 0, sizeof(*domain));'
    ))
}

Assert-True (Verify $source) 'domain lifecycle ordering'
Assert-True ($header -match 'complete pairwise separation') 'initialization separation contract'
Assert-True ($header -match 'snapshots all metadata') 'initialization snapshot contract'
Assert-True ($header -notmatch '(?i)ntddk|wdm\.h|wdf|intrin|__vmx|VmxEptInveptAsm') 'header must be WDK/instruction free'
Assert-True ($source -notmatch '(?i)ntddk|wdm\.h|wdf|__vmx|inveptasm|deviceiocontrol|ioctl') 'source must be WDK/native/IOCTL free'
Assert-True (($core | Select-String -Pattern 'vmx_ept_domain\.c' -AllMatches).Matches.Count -eq 1) 'core source exactly once'
foreach ($path in @($DriverMainPath, $IoctlPath, $RuntimePath, $HookPath)) {
    $text = Get-Content -Raw -LiteralPath $path
    Assert-True ($text -notmatch 'VmxEptDomain') "domain must remain unwired: $path"
}
foreach ($token in @(
    'overlapping configuration inputs rejected',
    'configuration overlap preserves domain',
    'configuration overlap preserves storage',
    'configuration and metadata overlap rejected',
    'metadata overlap preserves domain',
    'metadata overlap preserves storage',
    'wrapping configuration span rejected',
    'wrapping span preserves domain',
    'wrapping span preserves storage')) {
    Assert-True ($test.Contains($token)) "test $token"
}

$mutations = @(
    $source.Replace('!InitializeInputsSeparate(', '0 && InitializeInputsSeparate('),
    $source.Replace('capabilities_snapshot = *capabilities;',
        'memset(&capabilities_snapshot, 0, sizeof(capabilities_snapshot));'),
    $source.Replace('plan_snapshot = *plan;',
        'memset(&plan_snapshot, 0, sizeof(plan_snapshot));'),
    $source.Replace('!InitializeStorageSeparate(', '0 && InitializeStorageSeparate('),
    $source.Replace('&capabilities_snapshot, &identity_snapshot, &plan_snapshot',
        'capabilities, identity, plan'),
    $source.Replace('!DomainZero(domain)', '0'),
    $source.Replace('staged.MutationGeneration = 1;', 'staged.MutationGeneration = 0;'),
    $source.Replace('staged.InvalidatedGeneration = 1;', 'staged.InvalidatedGeneration = 0;'),
    $source.Replace('domain->Stage == VMX_EPT_DOMAIN_STAGE_READY', 'domain->Stage == VMX_EPT_DOMAIN_STAGE_ROOT_OWNED'),
    $source.Replace('return VmxEptDomainBeginMutation(domain);', 'return VMX_EPT_DOMAIN_OK;'),
    $source.Replace('domain->PendingGeneration = domain->MutationGeneration;', 'domain->PendingGeneration = 0;'),
    $source.Replace('report->Attempted != 1', 'report->Attempted > 1'),
    $source.Replace('domain->InvalidatedGeneration = domain->PendingGeneration;', 'domain->InvalidatedGeneration = domain->MutationGeneration + 1;'),
    $source.Replace('domain->PendingGeneration = 0;', 'domain->PendingGeneration = domain->MutationGeneration;'),
    $source.Replace('domain->Stage = VMX_EPT_DOMAIN_STAGE_READY;', 'domain->Stage = VMX_EPT_DOMAIN_STAGE_ROOT_OWNED;'),
    $source.Replace('memset(domain, 0, sizeof(*domain));', 'domain->Magic = 0;')
)
for ($i = 0; $i -lt $mutations.Count; ++$i) {
    Assert-True ($mutations[$i] -ne $source) "mutation $i did not apply"
    Assert-True (-not (Verify $mutations[$i])) "accepted domain mutation $i"
}
Write-Host 'PASS: EPT domain ownership contracts'
