param(
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$KernelSourcePath,
 [Parameter(Mandatory=$true)][string]$AdapterSourcePath,
 [Parameter(Mandatory=$true)][string]$TestPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(!$x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A ($i-ge 0-and$j-gt$i) "section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$h,[string]$k,[string]$a,[string]$t){
 foreach($x in @('PreviousAffinity is opaque','uint64_t PreviousMask','uint16_t PreviousGroup','uint16_t PreviousReserved[3]','bool Pinned')){A ($h.Contains($x)) "token $x"}
 A ($k.Contains('if (!token.Pinned)')) 'core pinned-only contract'
 A (-not $k.Contains('token.PreviousMask == 0')) 'core accepts opaque zero mask'
 $pin=B $a 'static VMX_PERSISTENT_KERNEL_STATUS KernelPersistentPin(' 'static VMX_PERSISTENT_KERNEL_STATUS KernelPersistentQueryCurrent('
 O $pin @('KeSetSystemGroupAffinityThread(&affinity, &previous)','PreviousMask = (uint64_t)previous.Mask','PreviousGroup = previous.Group','PreviousReserved[0] = previous.Reserved[0]','PreviousReserved[1] = previous.Reserved[1]','PreviousReserved[2] = previous.Reserved[2]','Pinned = true') 'pin full opaque capture'
 $restore=B $a 'static VMX_PERSISTENT_KERNEL_STATUS KernelPersistentRestore(' 'const VMX_PERSISTENT_KERNEL_PLATFORM KernelPersistentPlatform'
 A ($restore.Contains('token == NULL || !token->Pinned')) 'restore ownership gate'
 A (-not $restore.Contains('PreviousMask == 0')) 'restore accepts opaque zero mask'
 O $restore @('previous.Mask = (KAFFINITY)token->PreviousMask','previous.Group = token->PreviousGroup','previous.Reserved[0] = token->PreviousReserved[0]','previous.Reserved[1] = token->PreviousReserved[1]','previous.Reserved[2] = token->PreviousReserved[2]','KeRevertToUserGroupAffinityThread(&previous)','RtlZeroMemory(token, sizeof(*token))') 'restore full opaque replay'
 foreach($x in @('f.PinPreviousMask = 0','opaque zero previous mask','zero-mask affinity restored','PreviousReserved[0] = 0x11','PreviousReserved[2] = 0x33')){A ($t.Contains($x)) "test $x"}
}
$h=[IO.File]::ReadAllText((Resolve-Path $HeaderPath));$k=[IO.File]::ReadAllText((Resolve-Path $KernelSourcePath));$a=[IO.File]::ReadAllText((Resolve-Path $AdapterSourcePath));$t=[IO.File]::ReadAllText((Resolve-Path $TestPath));V $h $k $a $t
$mutations=@(
 @($h.Replace('uint16_t PreviousReserved[3]','uint16_t PreviousReserved[2]'),'h'),
 @($k.Replace('if (!token.Pinned) {','if (!token.Pinned || token.PreviousMask == 0) {'),'k'),
 @($a.Replace('token->PreviousReserved[2] = previous.Reserved[2];',''),'a'),
 @($a.Replace('previous.Reserved[1] = token->PreviousReserved[1];',''),'a'),
 @($a.Replace('RtlZeroMemory(token, sizeof(*token));','token->Pinned = false;'),'a'),
 @($t.Replace('f.PinPreviousMask = 0','f.PinPreviousMask = 1'),'t'))
$n=0;foreach($m in $mutations){$n++;$mh=$h;$mk=$k;$ma=$a;$mt=$t;Set-Variable -Name ('m'+$m[1]) -Value $m[0];$bad=$false;try{V $mh $mk $ma $mt}catch{$bad=$true};A $bad "accepted mutation $n"}
Write-Output 'Verified opaque GROUP_AFFINITY capture/replay, zero-mask acceptance, and token consumption.'