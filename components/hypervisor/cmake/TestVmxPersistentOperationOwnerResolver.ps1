param(
    [Parameter(Mandatory=$true)][string]$SourcePath,
    [Parameter(Mandatory=$true)][string]$HeaderPath,
    [Parameter(Mandatory=$true)][string]$TestPath)
$ErrorActionPreference='Stop'
function A([bool]$c,[string]$m){if(-not $c){throw $m}}
function Section([string]$s){
    $b=$s.IndexOf('VMX_PERSISTENT_STATUS VmxPersistentOperationOwnerResolveOperations')
    A ($b-ge 0) 'resolver missing'
    return $s.Substring($b)
}
function Ordered([string]$s,[string[]]$tokens){
    $at=0
    foreach($token in $tokens){
        $next=$s.IndexOf($token,$at)
        A ($next-ge 0) "missing/order: $token"
        $at=$next+$token.Length
    }
}
function Verify([string]$s,[string]$h,[string]$t){
    $r=Section $s
    foreach($token in @(
        'one exact owner-authenticated VMX operation table',
        'preserves out_operations',
        'no allocation, platform',
        'CPU pinning, IRQL transition, or VMX instruction',
        'VmxPersistentOperationOwnerResolveOperations')){A ($h.Contains($token)) "header: $token"}
    Ordered $r @(
        'OwnerConsistent(owner)',
        'identity == NULL',
        'out_operations == NULL',
        'index >= owner->Count',
        'OutputStorageValid(owner, identity, out_operations',
        'VmxProcessorIdentityEqual(identity, &owner->Identities[index])',
        'SlotConsistent(owner, index)',
        '*out_operations = owner->Contract.VmxOperations[index]',
        'return VMX_PERSISTENT_STATUS_OK')
    A ($r-notmatch '(?i)Allocate|Free|Pin|Restore|EnterInstructionLevel\s*\(|LeaveInstructionLevel\s*\(|Vmxon\s*\(|Vmxoff\s*\(|__vmx') 'resolver side effect'
    foreach($token in @('memset(&resolved,0xA5,sizeof(resolved));resolved_before=resolved','resolve operations','exact operations copy','resolve failure preserve','resolve overlap','resolve retained mutation','resolve mutation preserve')){A ($t.Contains($token)) "test: $token"}
}
$s=Get-Content -Raw $SourcePath
$h=Get-Content -Raw $HeaderPath
$t=Get-Content -Raw $TestPath
Verify $s $h $t
$mutations=@(
    $s.Replace('OwnerConsistent(owner)','OwnerConsistent(NULL)'),
    $s.Replace('index >= owner->Count','index > owner->Count'),
    $s.Replace('!OutputStorageValid(owner, identity, out_operations,','!OutputStorageValid(owner, identity, identity,'),
    $s.Replace('!VmxProcessorIdentityEqual(identity, &owner->Identities[index])','!VmxProcessorIdentityEqual(identity, identity)'),
    $s.Replace('!SlotConsistent(owner, index)','!SlotConsistent(owner, 0)'),
    $s.Replace('owner->Contract.VmxOperations[index]','owner->Contract.VmxOperations[0]')
)
foreach($m in $mutations){
    $rejected=$false
    try{Verify $m $h $t}catch{$rejected=$true}
    A $rejected 'accepted source mutation'
}
$testMutations=@(
    $t.Replace('"resolve operations"','"resolver removed"'),
    $t.Replace('"resolve failure preserve"','"resolver failure removed"'),
    $t.Replace('"resolve overlap"','"resolver overlap removed"'),
    $t.Replace('"resolve retained mutation"','"resolver retained removed"')
)
foreach($m in $testMutations){
    $rejected=$false
    try{Verify $s $h $m}catch{$rejected=$true}
    A $rejected 'accepted test mutation'
}
Write-Host 'PASS: persistent operation-owner resolver contracts verified'