param(
 [Parameter(Mandatory=$true)][string]$ScriptPath,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not$x){throw $m}}
function O($t,$v,$n){$p=0;foreach($x in $v){$q=$t.IndexOf($x,$p,[StringComparison]::Ordinal);A($q-ge0)"$n $x";$p=$q+$x.Length}}
function V($s,$d,$t){
 O $s @('$GateScript = Join-Path $Stage ''gate8-ept-hook-roundtrip.ps1''','$Required = @($Driver, $DriverPdb, $Loader, $LoaderPdb, $GateScript)','$Principal.IsInRole','Start-Transcript','Get-FileHash','Get-AuthenticodeSignature','$Before = Save-ServiceState','if ($Before.ExitCode -ne 1060)','=== LOAD ===',"-ArgumentList 'load'",'$RuntimeProof','SUCCESS: Hypervisor loaded on','$EptProof','INFO: Exact EPT READY status is verified.','service-after-load','=== EPT HOOK ROUNDTRIP ===',"-ArgumentList 'hook-roundtrip'",'SUCCESS: Kernel-owned no-access EPT hook roundtrip verified.','if ($Roundtrip.ExitCode -ne 0 -or -not $RoundtripText.Contains($RoundtripProof))','=== UNLOAD ===',"-ArgumentList 'unload'",'SUCCESS: Hypervisor unloaded','service-after-unload','if ($After.ExitCode -ne 1060)','$CrashDumps','MEMORY.DMP','Result=PASS','Stop-Transcript','tar.exe -a -c -f $ZipPath') 'gate8 order'
 foreach($x in @('Stop; do not retry or run unload manually','does not validate EPT-violation VM-exit routing')){A($s.Contains($x))"stop/boundary $x"}
 A($s-notmatch "ArgumentList 'hook'|ArgumentList 'unhook'|gpa_hex|wcstoull")'no caller-selected page'
 foreach($x in @('Gate 8','gate8-ept-hook-roundtrip.ps1','hook-roundtrip','Kernel-owned no-access EPT hook roundtrip','does not deliberately access the trapped page','does not prove EPT-violation VM-exit routing','.symfix','.sympath+','.reload /f','.logopen /t','.logclose','dedicated snapshotted VM','do not retry LOAD','do not run UNLOAD manually')){A($d.Contains($x))"doc $x"}
 A(([regex]::Matches($t,'test_gate8_ept_hook_roundtrip_script_contracts')).Count-eq1)'test once'
}
$s=Get-Content -Raw $ScriptPath;$d=Get-Content -Raw $DocPath;$t=Get-Content -Raw $TestsCMake;V $s $d $t
$mut=@(
 $s.Replace("-ArgumentList 'hook-roundtrip'","-ArgumentList 'status'"),
 $s.Replace('SUCCESS: Kernel-owned no-access EPT hook roundtrip verified.','SUCCESS: unchecked'),
 $s.Replace('if ($Roundtrip.ExitCode -ne 0','if ($false'),
 $s.Replace('if ($After.ExitCode -ne 1060)','if ($false)'),
 $s.Replace('& tar.exe -a -c -f $ZipPath','Write-Host')
)
foreach($q in $mut){A($q-ne$s)'vacuous script mutation';$bad=$false;try{V $q $d $t}catch{$bad=$true};A($bad)'accepted script mutation'}
Write-Host 'PASS: Gate 8 is evidence-first, no-access, fail-closed, archived, and restricted to the operator VM.'
