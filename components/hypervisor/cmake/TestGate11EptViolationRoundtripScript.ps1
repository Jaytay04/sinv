param(
 [Parameter(Mandatory=$true)][string]$ScriptPath,
 [Parameter(Mandatory=$true)][string]$DocPath,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function A($x,$m){if(-not$x){throw $m}}
function O($t,$v,$n){$p=0;foreach($x in $v){$q=$t.IndexOf($x,$p,[StringComparison]::Ordinal);A($q-ge0)"$n $x";$p=$q+$x.Length}}
function V($s,$d,$t){
 O $s @('$GateScript = Join-Path $Stage ''gate11-ept-violation-roundtrip.ps1''','$Required = @($Driver, $DriverPdb, $Loader, $LoaderPdb, $GateScript)','$Principal.IsInRole','Start-Transcript','GetActiveProcessorGroupCount','GetActiveProcessorCount([uint16]$Group)','$GroupCount -ne 2','$Processors -ne 4','Where-Object ActiveProcessors -ne 2','$Before = Save-ServiceState','if ($Before.ExitCode -ne 1060)','=== LOAD ===',"-ArgumentList 'load'",'$RuntimeProof','INFO: Exact EPT READY status is verified.','service-after-load','=== ONE-SHOT EPT VIOLATION ROUNDTRIP ===',"-ArgumentList 'ept-violation-roundtrip'",'SUCCESS: One-shot EPT violation VM-exit roundtrip verified.','if ($Roundtrip.ExitCode -ne 0 -or -not $RoundtripText.Contains($RoundtripProof))','=== UNLOAD ===',"-ArgumentList 'unload'",'SUCCESS: Hypervisor unloaded','service-after-unload','if ($After.ExitCode -ne 1060)','$CrashDumps','MEMORY.DMP','ViolationRoundtrip=PASS','Stop-Transcript','tar.exe -a -c -f $ZipPath') 'Gate11 order'
 foreach($x in @('deliberately triggers one supervised write EPT violation','Stop; do not retry or run unload manually')){A($s.Contains($x))"script boundary $x"}
 foreach($x in @('Gate 11','gate11-ept-violation-roundtrip.ps1','ept-violation-roundtrip','One-shot EPT violation VM-exit roundtrip','two processor groups','VM-exit reason 48','guest physical address','EPT pointer','.symfix','.sympath+','.reload /f','.logopen /t','.logclose','dedicated snapshotted VM','do not retry')){A($d.Contains($x))"doc $x"}
 A($d -match 'same\s+guest RIP')'doc same guest RIP'
 A(([regex]::Matches($t,'test_gate11_ept_violation_roundtrip_script_contracts')).Count-eq1)'test once'
}
$s=Get-Content -Raw $ScriptPath;$d=Get-Content -Raw $DocPath;$t=Get-Content -Raw $TestsCMake;V $s $d $t
$mut=@(
 $s.Replace("-ArgumentList 'ept-violation-roundtrip'","-ArgumentList 'hook-roundtrip'"),
 $s.Replace('SUCCESS: One-shot EPT violation VM-exit roundtrip verified.','SUCCESS: unchecked'),
 $s.Replace('$GroupCount -ne 2','$false'),
 $s.Replace('if ($Roundtrip.ExitCode -ne 0','if ($false'),
 $s.Replace('if ($After.ExitCode -ne 1060)','if ($false)'),
 $s.Replace('& tar.exe -a -c -f $ZipPath','Write-Host')
)
foreach($q in $mut){A($q-ne$s)'vacuous mutation';$bad=$false;try{V $q $d $t}catch{$bad=$true};A($bad)'accepted mutation'}
Write-Host 'PASS: Gate11 is a supervised, two-group, fail-closed, evidence-first one-shot EPT-violation VM gate.'