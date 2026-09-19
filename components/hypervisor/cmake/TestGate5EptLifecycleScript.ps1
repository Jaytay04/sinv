param(
 [Parameter(Mandatory=$true)][string]$ScriptPath,
 [Parameter(Mandatory=$true)][string]$DocPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n missing/out of order: $x";$p=$q}}
function V([string]$s,[string]$d){
 foreach($x in @('Start-Transcript','service-before','ExitCode -ne 1060','Start-Process -FilePath $Loader -ArgumentList ''load''','SUCCESS: Hypervisor loaded on $Processors cores and verified by exact V3 status.','INFO: Exact EPT READY status is verified.','service-after-load','Start-Process -FilePath $Loader -ArgumentList ''unload''','SUCCESS: Hypervisor unloaded','service-after-unload','Stop-Transcript','tar.exe -a -c -f')){A ($s.Contains($x)) "script token $x"}
 O $s @('Start-Transcript','service-before','Start-Process -FilePath $Loader -ArgumentList ''load''','$Load.ExitCode -ne 0','$LoadText.Contains($RuntimeProof)','$LoadText.Contains($EptProof)','service-after-load','Start-Process -FilePath $Loader -ArgumentList ''unload''','$Unload.ExitCode -ne 0','service-after-unload','$After.ExitCode -ne 1060','Stop-Transcript','tar.exe -a -c -f') 'supervised gate'
 A ($s-notmatch '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') 'script mutates boot/signing/SCM or reboots'
 A ($s-notmatch '(?im)\b(for|foreach|while)\s*\([^\r\n]*(load|unload)') 'automated lifecycle loop forbidden'
 foreach($x in @('Persistent Runtime + EPT supervised Gate 5','signed artifact','WinDbg','gate5-ept-lifecycle.ps1','do not retry','Runtime `LOADED`','EPT `READY`','Runtime/EPT `DETACHED`','absence')){A ($d.Contains($x)) "doc token $x"}
}
$s=[IO.File]::ReadAllText((Resolve-Path $ScriptPath));$d=[IO.File]::ReadAllText((Resolve-Path $DocPath));V $s $d
$mutations=@(
 $s.Replace('$LoadText.Contains($EptProof)','$true'),
 $s.Replace('$Load.ExitCode -ne 0','$false'),
 $s.Replace("Start-Process -FilePath `$Loader -ArgumentList 'unload'","Start-Process -FilePath `$Loader -ArgumentList 'status'"),
 $s.Replace('$After.ExitCode -ne 1060','$false'),
 $s.Replace('Stop-Transcript | Out-Null','Write-Host transcript-open'))
foreach($m in $mutations){A ($m-ne$s) 'vacuous mutation';$bad=$false;try{V $m $d}catch{$bad=$true};A $bad 'accepted script mutation'}
'Verified supervised Gate 5: baseline absence, Runtime+EPT LOAD proof, exact unload/detach proof, stop-first behavior, and closed evidence archive.'
