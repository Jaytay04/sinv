param([Parameter(Mandatory=$true)][string]$AuditDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$source=Join-Path $PSScriptRoot 'recover.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Recovery source parse failed.'}
foreach($name in @('Assert-DriftEvidence','Assert-DriftFirmwareCoverage','Get-ImageBodyHash')){
    $f=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    if($f.Count -ne 1){throw 'Expected one actual recovery helper.'}
    Invoke-Expression $f[0].Extent.Text
}
$pass=0
$ok=[pscustomobject]@{oldBootManagerSha256='25D8869E3C79083AE62C698A9DB9030748E3267BD53593357447F3F72761D1F2';currentBootManagerSha256='2D60A2203865122DAD22772136B3D905167D9DCF926D602BB5F48B3B6668ABEC';signatureVerdict='PASS';secureBoot=$true;bootNextAbsent=$true;liveAndInstalledExecutableMatch=$true;allOtherFirmwareMatches=$true;allOldDbxRevocationsRetained=$true;addedDbxRevocations=2;currentDbxSha256='80E5BD4420231830E306F46ED94644D2604EE7D78798D65C01B42A844095B093'}
Assert-DriftEvidence $ok;$pass++
foreach($property in @('oldBootManagerSha256','currentBootManagerSha256','currentDbxSha256','signatureVerdict','secureBoot','bootNextAbsent','liveAndInstalledExecutableMatch','allOtherFirmwareMatches','allOldDbxRevocationsRetained','addedDbxRevocations')){
    $bad=$ok|ConvertTo-Json|ConvertFrom-Json
    $bad.$property=if($bad.$property -is [bool]){$false}elseif($bad.$property -is [string]){'fixture-drift'}else{3}
    $threw=$false;try{Assert-DriftEvidence $bad}catch{$threw=$true}
    if(-not $threw){throw ('Unexpected evidence accepted: '+$property)}
    $pass++
}
foreach($name in @('live-bootmgfw.efi','installed-bootmgfw.efi')){
    if((Get-ImageBodyHash (Join-Path $AuditDirectory $name)) -cne 'F1CBD3773547541226DBDB42C42E04CF230F14A1B48E48DBD8AD695ADF7E955C'){throw 'Independent executable-body digest mismatch.'}
    $pass++
}
$state=Get-Content -LiteralPath (Join-Path $AuditDirectory 'prepared-before.json') -Raw|ConvertFrom-Json
$c=Get-Content -LiteralPath (Join-Path $AuditDirectory 'firmware-comparison.json') -Raw|ConvertFrom-Json
$o=Get-Content -LiteralPath (Join-Path $state.snapshot 'firmware-variables.json') -Raw|ConvertFrom-Json
Assert-DriftFirmwareCoverage $c $o;$pass++
foreach($kind in @('empty','truncated','duplicate','wrong-guid','changed-before','changed-current')){
    $bad=$c|ConvertTo-Json -Depth 15|ConvertFrom-Json
    switch($kind){'empty'{$bad=@()};'truncated'{$bad=$bad[0..17]};'duplicate'{$bad[1]=$bad[0]};'wrong-guid'{$bad[0].guid='fixture-wrong'};'changed-before'{$bad[0].before.base64='fixture-changed'};'changed-current'{$bad[0].current.attributes=99}}
    $threw=$false;try{Assert-DriftFirmwareCoverage $bad $o}catch{$threw=$true}
    if(-not $threw){throw ('Incomplete firmware comparison accepted: '+$kind)}
    $pass++
}
$raw=[IO.File]::ReadAllText($source);$start=$raw.IndexOf('$comparison=');$end=$raw.IndexOf("Write-DriftJson `$evidence 'reviewed-evidence.json'",$start)
Invoke-Expression $raw.Substring($start,$end-$start)
if($comparison.Count -ne 19 -or $next.Count -ne 1){throw 'Actual audit input did not bind correctly.'}
$pass++
if($pass -ne 21){throw ('Unexpected test count: '+$pass)}
'PASS recovery host checks: '+$pass+'/21. Actual AST helpers, negative fixtures and captured image hashes; no firmware or ESP changes.'
