param([ValidateSet('Build','Prepare','Status','Arm','Collect','Rollback','Verify')][string]$Mode='Status')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$writeMode=$Mode
$writeWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$writeRepo=[IO.Path]::GetFullPath((Join-Path $writeWorktree '..\..\..'))
$writePackage=Join-Path $writeRepo 'spoof-handoff'
$writeReports=Join-Path $writePackage 'reports\secureboot-write-20261016'
$writeArtifacts=Join-Path $writePackage 'artifacts\secureboot-write-probe'
$writeInventory=Join-Path $writeArtifacts 'inventory'
$writeSigned=Join-Path $writeWorktree 'spoof-handoff\artifacts\secureboot-write-probe\signed'
$writeLoader=Join-Path $writeRepo '.codex-build\efi_shim_loader'
$writeStatePath=Join-Path $writeArtifacts 'prepared.json'
$writeLeaf='HypersinvSecureBootWrite.txt'
$writeChain=Join-Path $writeRepo 'tools\efi_inventory\shim-chain-collector.ps1'
New-Item -ItemType Directory -Path $writeReports -Force|Out-Null
function New-WritePath([string]$Kind,[string]$Extension='json'){Join-Path $writeReports ($Kind+'.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')+'.'+$Extension)}
function Invoke-WriteChild([string]$Script,[string[]]$Arguments,[string]$Log){
    $previous=$ErrorActionPreference
    try{$ErrorActionPreference='Continue';$output=@(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments 2>&1);$code=$LASTEXITCODE}finally{$ErrorActionPreference=$previous}
    [IO.File]::WriteAllLines($Log,[string[]](@($output|ForEach-Object{[string]$_})+('EXIT='+$code)),[Text.UTF8Encoding]::new($false))
    $output|ForEach-Object{Write-Output ([string]$_)}
    if($code){throw ('Step failed (exit '+$code+'): '+$Script+'; exact output: '+$Log)}
}
function Save-WriteState($State){
    $temporary=$writeStatePath+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllText($temporary,($State|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
    if(Test-Path -LiteralPath $writeStatePath){[IO.File]::Replace($temporary,$writeStatePath,($writeStatePath+'.'+[Guid]::NewGuid().ToString('N')+'.bak'))}
    else{Move-Item -LiteralPath $temporary -Destination $writeStatePath}
}
function Read-WriteState {
    if(-not(Test-Path -LiteralPath $writeStatePath -PathType Leaf)){throw 'The write probe is not prepared.'}
    $state=Get-Content -LiteralPath $writeStatePath -Raw|ConvertFrom-Json
    if($state.schema -ne 1 -or $state.mode -cne 'probe-write-restore'){throw 'Unsupported write-probe state.'}
    foreach($pin in $state.pinnedFiles){if((Get-FileHash -LiteralPath $pin.path -Algorithm SHA256).Hash -cne $pin.sha256){throw ('Prepared input changed: '+$pin.path)}}
    return $state
}
function Get-WriteBootIdentity {(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')}
function Import-WriteLibrary {
    . (Join-Path $PSScriptRoot '..\smbios_boot\snapshot-reference.ps1') -RepoRoot $writeRepo -OutputBoundary $writePackage -LibraryOnly
    . $writeChain -InventoryBuildDir $writeInventory -LoaderBuildDir $writeLoader -SignedDirectory $writeSigned -LibraryOnly
}
function Assert-WriteBaseline([string]$Snapshot){
    $secureBootEnabled=$false;$secureBootError=$null
    try{$secureBootEnabled=[bool](Confirm-SecureBootUEFI)}catch{$secureBootError=$_.Exception.Message}
    Initialize-ReadyFirmwareReader;[ReadyToArmFirmwareRead]::Enable()
    foreach($record in (Get-Content -LiteralPath (Join-Path $Snapshot 'firmware-variables.json') -Raw|ConvertFrom-Json)){
        if($record.name -notmatch '^(BootOrder|BootNext|Boot[0-9A-Fa-f]{4}|SecureBoot|SetupMode|PK|KEK|db|dbx)$'){continue}
        [uint32]$attributes=0;[int]$errorCode=0
        $data=[ReadyToArmFirmwareRead]::Read($record.name,$record.guid,[ref]$attributes,[ref]$errorCode)
        $encoded=if($null -ne $data){[Convert]::ToBase64String($data)}else{$null}
        if($errorCode -ne $record.win32Error -or $attributes -ne $record.attributes -or $encoded -cne $record.base64){throw ('Boot/security mismatch: '+$record.name+'; expected bytes='+$record.base64+'; actual='+$encoded+'; expected Win32='+$record.win32Error+' actual Win32='+$errorCode+'; expected attributes='+$record.attributes+' actual attributes='+$attributes)}
    }
    if(-not $secureBootEnabled){throw ('Confirm-SecureBootUEFI did not confirm Secure Boot ON after all raw boot/security variables matched; error='+$secureBootError)}
}
function Read-WriteProbeReport([string]$Path,[string]$BuildId,[string]$ExpectedData,[uint32]$ExpectedAttributes){
    $bytes=[IO.File]::ReadAllBytes($Path)
    if($bytes.Length -gt 32768 -or @($bytes|Where-Object {$_ -gt 127 -or $_ -eq 0}).Count){throw 'Write-probe report is oversized or non-ASCII.'}
    $text=[Text.Encoding]::ASCII.GetString($bytes)
    if(-not $text.EndsWith("`r`n",[StringComparison]::Ordinal) -or $text.Replace("`r`n",'').Contains("`n")){throw 'Write-probe report has invalid line endings.'}
    $lines=$text.Substring(0,$text.Length-2).Split(@("`r`n"),[StringSplitOptions]::None)
    $keys=@('build_id','mode','name','guid','timestamp_status','timestamp','timestamp_timezone','timestamp_daylight','flag_consumed','flag_status','flag_verify_status','flag_validation','preflight_called','preflight_status','preflight_size','preflight_attributes','preflight_data','intent_flushed','intent_status','set_called','set_status','after_called','after_status','after_size','after_attributes','after_data','restore_called','restore_status','final_called','final_status','final_size','final_attributes','final_data','consistency','verdict','recovered','recovery_action','result')
    if($lines.Count -ne ($keys.Count+2) -or $lines[0] -cne 'HSBWRITE001' -or $lines[-1] -cne 'END'){throw 'Write-probe report is incomplete or has an unsupported schema.'}
    $values=[ordered]@{}
    for($index=0;$index -lt $keys.Count;$index++){
        $prefix=$keys[$index]+'='
        if(-not $lines[$index+1].StartsWith($prefix,[StringComparison]::Ordinal)){throw ('Unexpected write-probe field: expected '+$keys[$index])}
        $values[$keys[$index]]=$lines[$index+1].Substring($prefix.Length)
    }
    if($values.build_id -cne $BuildId -or $values.mode -cne 'probe-write-restore' -or $values.name -cne 'SecureBoot' -or $values.guid -cne '8be4df61-93ca-11d2-aa0d-00e098032b8c'){throw 'Write-probe identity or build mismatch.'}
    foreach($key in @('timestamp_status','flag_status','flag_verify_status','preflight_status','intent_status','set_status','after_status','restore_status','final_status')){
        if($values[$key] -cnotmatch '^(?:0x[0-9A-F]{16}|not_called|not_available)$'){throw ('Malformed EFI status: '+$key+'='+$values[$key])}
    }
    foreach($key in @('timestamp','timestamp_timezone','timestamp_daylight','flag_consumed','flag_validation','intent_flushed','preflight_called','preflight_size','preflight_attributes','preflight_data','set_called','after_called','after_size','after_attributes','after_data','restore_called','final_called','final_size','final_attributes','final_data','consistency','verdict','recovered','recovery_action','result')){
        if($values[$key] -match '[^\x20-\x7E]' -or $values[$key].Length -gt 256){throw ('Malformed write-probe field: '+$key)}
    }
    if($values.timestamp -cnotmatch '^(?:\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}|not_available)$' -or $values.timestamp_timezone -cnotmatch '^(?:-?\d+|not_available)$' -or $values.timestamp_daylight -cnotmatch '^(?:\d+|not_available)$'){throw 'Malformed EFI timestamp evidence.'}
    if($values.flag_validation -cnotin @('not_applicable','valid','absent','size_mismatch','content_mismatch','file_info_unsupported','file_info_query_error','file_info_query_protocol_mismatch','file_info_size_invalid','file_info_read_error','file_info_read_size_invalid','file_info_record_invalid','flag_is_directory','file_read_unsupported','file_read_error','file_read_length_invalid','root_open_unsupported','flag_open_error','flag_handle_invalid','file_read_or_close_unsupported','trailing_read_error','trailing_data','file_close_error','delete_open_unsupported','delete_open_error','delete_handle_invalid','delete_unsupported','delete_error','verify_open_error','still_present')){throw ('Unknown one-shot flag validation result: '+$values.flag_validation)}
    if($values.flag_consumed -cnotin @('yes','no')){throw ('Malformed flag-consumed marker: '+$values.flag_consumed)}
    foreach($prefix in @('preflight','after','final')){
        $called=$values[$prefix+'_called'];$status=$values[$prefix+'_status'];$size=$values[$prefix+'_size'];$attributes=$values[$prefix+'_attributes'];$data=$values[$prefix+'_data']
        if($called -cnotin @('yes','no')){throw ('Malformed '+$prefix+' call marker: '+$called)}
        if($called -ceq 'no'){
            if($status -cne 'not_called' -or $size -cne 'not_available' -or $attributes -cne 'not_available' -or $data -cne 'not_available'){throw ('Uncalled '+$prefix+' read contains fabricated output fields.')}
        }else{
            if($status -cnotmatch '^0x[0-9A-F]{16}$' -or ($size -cne 'not_available' -and $size -cnotmatch '^\d+$') -or ($attributes -cne 'not_available' -and $attributes -cnotmatch '^0x[0-9A-F]{8}$') -or ($data -cne 'not_available' -and $data -cnotmatch '^(?:[0-9A-F]{2})*$')){throw ('Malformed called '+$prefix+' read result.')}
        }
    }
    if($values.set_called -cnotin @('yes','no') -or $values.restore_called -cnotin @('yes','no')){throw 'Malformed SetVariable call marker.'}
    if(($values.set_called -ceq 'yes' -and $values.set_status -cnotmatch '^0x[0-9A-F]{16}$') -or ($values.set_called -ceq 'no' -and $values.set_status -cne 'not_called')){throw 'SetVariable status does not match its call marker.'}
    if(($values.restore_called -ceq 'yes' -and $values.restore_status -cnotmatch '^0x[0-9A-F]{16}$') -or ($values.restore_called -ceq 'no' -and $values.restore_status -cne 'not_called')){throw 'Restore status does not match its call marker.'}
    if($values.intent_flushed -cnotin @('yes','no','not_called') -or ($values.intent_flushed -ceq 'yes' -and $values.intent_status -cne '0x0000000000000000') -or ($values.intent_flushed -ceq 'no' -and $values.intent_status -cnotmatch '^0x[0-9A-F]{16}$') -or ($values.intent_flushed -ceq 'not_called' -and $values.intent_status -cne 'not_called')){throw 'Intent status does not match the recorded intent-flush state.'}
    if($values.consistency -cnotin @('none','set_error_but_off_observed','set_success_but_original_observed','after_unreadable','restore_error_but_original_observed','after_unexpected_value')){throw ('Unknown probe consistency marker: '+$values.consistency)}
    $flagValid=$values.flag_validation -ceq 'valid' -and $values.flag_consumed -ceq 'yes' -and $values.flag_status -ceq '0x0000000000000000' -and $values.flag_verify_status -ceq '0x800000000000000E'
    if($values.flag_validation -ceq 'valid' -and -not $flagValid){throw ('Flag marked valid without proven one-shot consumption: flag='+$values.flag_consumed+' status='+$values.flag_status+' verify='+$values.flag_verify_status)}
    $preflightMatches=$values.preflight_status -ceq '0x0000000000000000' -and $values.preflight_size -ceq '1' -and $values.preflight_attributes -ceq ('0x{0:X8}' -f $ExpectedAttributes) -and $values.preflight_data -ceq $ExpectedData
    $success='0x0000000000000000'
    if($values.set_called -ceq 'yes'){
        if(-not $flagValid -or $values.preflight_called -cne 'yes' -or -not $preflightMatches){throw 'SetVariable was called despite a failed/mismatched one-shot flag or preflight; preserve output and require manual review.'}
        if($values.intent_flushed -cne 'yes' -or $values.intent_status -cne $success){throw ('SET was attempted without a verified flushed intent: flushed='+$values.intent_flushed+' status='+$values.intent_status)}
        if($values.after_called -cne 'yes' -or $values.restore_called -cne 'yes' -or $values.final_called -cne 'yes'){throw 'SET was called without all required read-back and restore calls.'}
        if($values.final_status -cne $success -or $values.final_size -cne '1' -or $values.final_attributes -cne ('0x{0:X8}' -f $ExpectedAttributes) -or $values.final_data -cne $ExpectedData -or $values.recovered -cne 'yes' -or $values.result -cne 'pass' -or $values.recovery_action -cne 'no_recovery_needed'){throw ('Original SecureBoot bytes were not verified after restore: expected='+$ExpectedData+'; final='+$values.final_data+'; final_status='+$values.final_status+'; restore_status='+$values.restore_status+'; recovered='+$values.recovered+'; result='+$values.result+'; recovery_action='+$values.recovery_action)}
        switch($values.verdict){
            'TOGGLE-WORKS' {if($values.set_status -cne $success -or $values.after_status -cne $success -or $values.after_size -cne '1' -or $values.after_attributes -cne ('0x{0:X8}' -f $ExpectedAttributes) -or $values.after_data -cne '00' -or $values.restore_status -cne $success -or $values.consistency -cne 'none'){throw 'TOGGLE-WORKS verdict conflicts with recorded SetVariable/read-back bytes.'}}
            'TOGGLE-REFUSED' {
                if($values.set_status -ceq $success -or $values.after_status -cne $success -or $values.after_size -cne '1' -or $values.after_attributes -cne ('0x{0:X8}' -f $ExpectedAttributes)){throw 'TOGGLE-REFUSED verdict lacks a failed SetVariable status or valid post-SetVariable read.'}
                if($values.after_data -ceq $ExpectedData){if($values.consistency -cne 'none'){throw ('TOGGLE-REFUSED consistency disagrees with original post-SetVariable bytes: '+$values.consistency)}}
                elseif($values.after_data -ceq '00' -and $values.consistency -ceq 'set_error_but_off_observed'){$null=$true}
                else{throw ('TOGGLE-REFUSED post-SetVariable bytes are inconsistent: expected original='+$ExpectedData+' or off=00 with consistency=set_error_but_off_observed; actual='+$values.after_data+' consistency='+$values.consistency)}
            }
            'VARIABLE-WRITE-SILENT-NOOP' {if($values.set_status -cne $success -or $values.after_status -cne $success -or $values.after_size -cne '1' -or $values.after_attributes -cne ('0x{0:X8}' -f $ExpectedAttributes) -or $values.after_data -cne $ExpectedData -or $values.consistency -cne 'set_success_but_original_observed'){throw 'SILENT-NOOP verdict conflicts with raw EFI status, consistency, or post-SetVariable bytes.'}}
            'WRITE-OUTCOME-UNVERIFIED-RECOVERED' {
                if($values.final_data -cne $ExpectedData){throw 'Recovered-unverified verdict lacks an original final value.'}
                $afterReadable=$values.after_called -ceq 'yes' -and $values.after_status -ceq $success -and $values.after_size -ceq '1' -and $values.after_attributes -ceq ('0x{0:X8}' -f $ExpectedAttributes) -and $values.after_data -cmatch '^[0-9A-F]{2}$'
                if(-not $afterReadable -and $values.consistency -cne 'after_unreadable'){throw 'Recovered-unverified report lacks the matching unreadable-after consistency marker.'}
                if($afterReadable){
                    if($values.after_data -ceq $ExpectedData -or ($values.after_data -ceq '00' -and $values.set_status -cne $success)){throw 'Recovered-unverified verdict conflicts with an after-read that requires a REFUSED or silent-no-op classification.'}
                    if($values.restore_status -cne $success){
                        if($values.after_data -ceq $ExpectedData -or $values.consistency -cne 'restore_error_but_original_observed'){throw 'Recovered-unverified report lacks the matching restore-error consistency marker.'}
                    }else{
                        if($values.after_data -ceq '00' -or $values.consistency -cne 'after_unexpected_value'){throw 'Recovered-unverified report lacks the matching unexpected-after-value consistency marker.'}
                    }
                }
            }
            default {throw ('Unexpected successful probe verdict: '+$values.verdict)}
        }
    }else{
        if($values.set_called -cne 'no' -or $values.verdict -cne 'NOT-ATTEMPTED' -or $values.result -cne 'not_attempted'){throw ('Unattempted write has inconsistent call/verdict/result fields: '+$values.set_called+'/'+$values.verdict+'/'+$values.result)}
        if($values.recovered -cne 'not_verified' -or $values.recovery_action -cne 'no_recovery_needed'){throw ('Unexpected recovery state without a SET: '+$values.recovered+'/'+$values.recovery_action)}
        if($values.after_called -cne 'no' -or $values.restore_called -cne 'no' -or $values.final_called -cne 'no' -or $values.consistency -cne 'none'){throw 'A no-SET report contains a post-SET operation or consistency result.'}
        if($values.intent_flushed -ceq 'yes' -and $values.intent_status -ceq $success){throw 'No-SET report says a complete intent was flushed but does not explain why SET was skipped.'}
        if($values.preflight_called -cne 'yes' -and $values.preflight_called -cne 'no'){throw 'Malformed preflight call marker.'}
        if($values.preflight_called -ceq 'no' -and ($values.preflight_status -cne 'not_called' -or $values.preflight_size -cne 'not_available' -or $values.preflight_attributes -cne 'not_available' -or $values.preflight_data -cne 'not_available')){throw 'Uncalled preflight contains fabricated result fields.'}
        if(-not $flagValid -and $values.preflight_called -cne 'no'){throw 'Invalid flag report unexpectedly proceeded to a firmware-variable preflight.'}
    }
    return $values
}
function Write-WriteFlag([string]$EspRoot,[string]$BuildId){
    if($BuildId -cnotmatch '^[0-9A-F]{32}$'){throw 'Invalid prepared build identifier.'}
    $path=Join-Path $EspRoot 'HypersinvSecureBootWrite.flag'
    if(Test-Path -LiteralPath $path){throw 'A one-shot write flag already exists; it will not be overwritten.'}
    Assert-NoReparseAncestors $path $EspRoot
    $temporary=$path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    $content="HSBWRITE001`r`nbuild_id=$BuildId`r`nmode=probe-write-restore`r`nEND`r`n"
    $bytes=[Text.Encoding]::ASCII.GetBytes($content)
    $stream=$null
    try{
        $stream=New-Object IO.FileStream($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)
        $stream.Dispose();$stream=$null
        if((Get-Item -LiteralPath $temporary).Length -ne $bytes.Length){throw 'The temporary one-shot flag length did not verify after flush.'}
        [IO.File]::Move($temporary,$path)
    }catch{
        if($stream){$stream.Dispose()}
        if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}
        throw
    }
    $hash=Get-Sha256 $path
    $sha=[Security.Cryptography.SHA256]::Create();try{$expectedHash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','')}finally{$sha.Dispose()}
    if((Get-Item -LiteralPath $path).Length -ne $bytes.Length -or $hash -cne $expectedHash){throw 'The one-shot flag did not verify byte-for-byte after flush.'}
    return [ordered]@{path=$path;sha256=$hash;length=$bytes.Length}
}
function Get-WriteEspLayout([string]$EspRoot){
    $rows=@()
    foreach($rootItem in @(Get-ChildItem -LiteralPath $EspRoot -Force)){
        if($rootItem.Name -eq 'System Volume Information'){continue}
        $tree=if($rootItem.PSIsContainer){@($rootItem)+@(Get-ChildItem -LiteralPath $rootItem.FullName -Recurse -Force)}else{@($rootItem)}
        foreach($item in $tree){
            if(Test-ReparsePoint $item.FullName){throw ('ESP reparse point refused: '+$item.FullName)}
            $relative=$item.FullName.Substring($EspRoot.Length).TrimStart([char[]]@('\','/')).Replace('/','\')
            if([string]::IsNullOrWhiteSpace($relative)){throw 'Could not create a safe ESP-relative path.'}
            $hash=$null
            if(-not $item.PSIsContainer -and $relative -notmatch '^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[0-9]*)?$'){$hash=Get-Sha256 $item.FullName}
            $rows+=@([ordered]@{path=$relative;directory=[bool]$item.PSIsContainer;sha256=$hash})
        }
    }
    return ,$rows
}
function Compare-WriteEspToSnapshot([string]$Snapshot){
    . (Join-Path $PSScriptRoot '..\secureboot_restore_verify\esp-policy.ps1')
    $expected=ConvertFrom-RestoreLayoutJson ([IO.File]::ReadAllText((Join-Path $Snapshot 'esp-layout.json')))
    $mount=Mount-Esp
    try{$actual=Get-WriteEspLayout $mount.EspRoot}finally{Dismount-Esp $mount}
    $comparison=@(Compare-RestoreEspFiles $expected $actual)
    $path=New-WritePath 'esp-restore-comparison'
    [IO.File]::WriteAllText($path,(ConvertTo-Json -InputObject $comparison -Depth 8),[Text.UTF8Encoding]::new($false))
    $failures=@($comparison|Where-Object {-not $_.pass})
    if($failures.Count){$first=$failures[0];throw ('ESP restore mismatch: '+$first.path+' classification='+$first.classification+' expected='+$first.expectedSha256+' actual='+$first.actualSha256+'; full comparison='+$path)}
    return [ordered]@{comparison=$path;records=$comparison;mutableBootStatus=@($comparison|Where-Object classification -eq 'mutable_boot_status_changed')}
}
function Assert-WriteOutputsAbsent {
    $mount=Mount-Esp
    try{
        foreach($leaf in @('HypersinvSecureBootWrite.flag',$writeLeaf)){
            $path=Join-Path $mount.EspRoot $leaf
            if(Test-Path -LiteralPath $path){throw ('An uncollected write-probe file already exists: '+$leaf)}
        }
    }finally{Dismount-Esp $mount}
}
function Remove-WriteFlagIfUnchanged([string]$EspRoot,[string]$ExpectedHash,[string]$BuildId){
    $path=Join-Path $EspRoot 'HypersinvSecureBootWrite.flag'
    if(-not(Test-Path -LiteralPath $path)){return 'already_absent'}
    if((Test-ReparsePoint $path) -or -not(Test-Path -LiteralPath $path -PathType Leaf)){throw 'Write-probe flag is unsafe; preserved.'}
    $actual=Get-Sha256 $path
    if(-not [string]::IsNullOrWhiteSpace($ExpectedHash) -and $actual -cne $ExpectedHash){throw ('Write-probe flag hash changed; expected='+$ExpectedHash+' actual='+$actual+'; preserved.')}
    if([string]::IsNullOrWhiteSpace($ExpectedHash)){
        $expectedBytes=[Text.Encoding]::ASCII.GetBytes("HSBWRITE001`r`nbuild_id=$BuildId`r`nmode=probe-write-restore`r`nEND`r`n")
        $actualBytes=[IO.File]::ReadAllBytes($path)
        if($actualBytes.Length -ne $expectedBytes.Length -or [Convert]::ToBase64String($actualBytes) -cne [Convert]::ToBase64String($expectedBytes)){throw ('Write-probe flag is not the exact prepared one-shot content; actual SHA-256='+$actual+'; preserved.')}
    }
    Remove-Item -LiteralPath $path -Force
    if(Test-Path -LiteralPath $path){throw 'Owned write-probe flag removal did not verify.'}
    return 'removed_unchanged_owned_flag'
}
function Clear-WriteOneShot([string]$Snapshot,[switch]$CheckSecureBoot){
    $mount=Mount-Esp
    try{
        $before=Get-BootManagerHash $mount.EspRoot
        $previous=$ErrorActionPreference
        try{$ErrorActionPreference='Continue';$output=@(& (Get-InstallTool) oneshot --mode off --profile shim-inventory --esp-volume $mount.VolumeGuid 2>&1);$code=$LASTEXITCODE}finally{$ErrorActionPreference=$previous}
        $lines=@($output|ForEach-Object{([string]$_).Trim()})
        [IO.File]::WriteAllLines((New-WritePath 'rollback-owned-cleanup' 'log'),[string[]]($lines+('EXIT='+$code)),[Text.UTF8Encoding]::new($false))
        if($code -ne 0 -and ($code -ne 1 -or $lines.Count -ne 1 -or $lines[0] -cne 'BootNext cleanup failed: owned Boot#### entry was not found')){throw 'Owned boot cleanup failed; no ESP restoration was performed.'}
        if((Get-BootManagerHash $mount.EspRoot) -cne $before){throw 'Windows Boot Manager changed during cleanup.'}
    }finally{Dismount-Esp $mount}
    if($CheckSecureBoot){Assert-WriteBaseline $Snapshot}
}
function Assert-WriteBcdBaseline([string]$Snapshot){
    $before=[IO.File]::ReadAllLines((Join-Path $Snapshot 'bcd-all.txt'))
    $after=@(& bcdedit.exe /enum all /v);$enumCode=$LASTEXITCODE
    $capture=New-WritePath 'bcd-after' 'txt'
    [IO.File]::WriteAllLines($capture,[string[]]$after,[Text.UTF8Encoding]::new($false))
    if($enumCode -ne 0 -or ($before -join "`n") -cne ($after -join "`n")){throw ('BCD enumeration differs from snapshot; exit='+$enumCode+'; exact output='+$capture)}
    $export=New-WritePath 'bcd-export-after' 'bin'
    & bcdedit.exe /export $export|Out-Null;$exportCode=$LASTEXITCODE
    if($exportCode -ne 0){throw ('BCD export failed during verification; exit='+$exportCode+'; path='+$export)}
    $expectedHash=Get-Sha256 (Join-Path $Snapshot 'system-bcd.export');$actualHash=Get-Sha256 $export
    return [ordered]@{logicalEnumerationIdentical=$true;enumPath=$capture;enumExit=$enumCode;beforeExportPath=(Join-Path $Snapshot 'system-bcd.export');beforeExportSha256=$expectedHash;afterExportPath=$export;afterExportSha256=$actualHash;exportBytesIdentical=($expectedHash -ceq $actualHash);byteComparisonMeaning='Binary hash comparison is recorded as a diagnostic only; logical live /enum all /v equality is the rollback check.'}
}
Set-Location -LiteralPath $writeRepo
$writeExecutionLog=New-WritePath ('command-'+$writeMode) 'log'
trap{($_|Out-String)|Add-Content -LiteralPath $writeExecutionLog;throw $_}
if($writeMode -eq 'Build'){& (Join-Path $PSScriptRoot 'build.ps1');exit $LASTEXITCODE}
. Import-WriteLibrary
Assert-Admin
if($writeMode -eq 'Prepare'){
    if(Test-Path -LiteralPath $writeStatePath){throw 'A preparation journal already exists; use Status or Rollback instead of restaging.'}
    $build=Get-Content -LiteralPath (Join-Path $writeArtifacts 'build-verdict.json') -Raw|ConvertFrom-Json
    if($build.result -cne 'PASS' -or $build.mode -cne 'probe-write-restore' -or -not $build.variableWriteImplemented){throw 'The write-probe build and host tests must pass first.'}
    foreach($pin in $build.pinnedFiles){if((Get-Sha256 $pin.path) -cne $pin.sha256){throw ('Build input changed: '+$pin.path)}}
    $snapshot=Join-Path $writeReports ('recovery.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
    Assert-WriteOutputsAbsent
    Invoke-WriteChild (Join-Path $PSScriptRoot '..\secureboot_read\snapshot.ps1') @('-RepositoryRoot',$writeRepo,'-SnapshotDirectory',$snapshot) (New-WritePath 'snapshot' 'log')
    Assert-WriteBaseline $snapshot
    Invoke-WriteChild $writeChain @('Sign','-InventoryBuildDir',$writeInventory,'-LoaderBuildDir',$writeLoader,'-SignedDirectory',$writeSigned) (New-WritePath 'sign' 'log')
    $verifyStage=Join-Path $snapshot 'signature-stage';New-Item -ItemType Directory -Path $verifyStage|Out-Null
    foreach($name in @('grubx64.efi','inventory.efi','MOK.cer')){Copy-Item -LiteralPath (Join-Path $writeSigned $name) -Destination (Join-Path $verifyStage $name)}
    Copy-Item -LiteralPath (Join-Path $writeRepo 'components\uefi-loader\src\vendor\shimx64.efi') -Destination (Join-Path $verifyStage 'shimx64.efi')
    Copy-Item -LiteralPath (Join-Path $writeInventory 'shim-packages\usr\lib\shim\mmx64.efi') -Destination (Join-Path $verifyStage 'mmx64.efi')
    Invoke-WriteChild (Join-Path $PSScriptRoot '..\smbios_boot\verify-signatures.ps1') @('-RepoRoot',$writeRepo,'-OutputBoundary',$writePackage,'-SnapshotDirectory',$snapshot,'-ArtifactDirectory',$verifyStage) (New-WritePath 'signatures' 'log')
    $pins=@($build.pinnedFiles)
    foreach($file in @(Get-ChildItem -LiteralPath $writeSigned -File)){$pins+=@([ordered]@{path=$file.FullName;sha256=(Get-Sha256 $file.FullName)})}
    foreach($name in @('snapshot.json','firmware-variables.json','esp-layout.json','system-bcd.export','signature-verdict.json')){$path=Join-Path $snapshot $name;$pins+=@([ordered]@{path=$path;sha256=(Get-Sha256 $path)})}
    Assert-WriteOutputsAbsent
    $state=[ordered]@{schema=1;mode='probe-write-restore';phase='staging';buildId=$build.buildId;snapshot=$snapshot;pinnedFiles=$pins;readyToArm=$false;armed=$false;armedFromBoot=$null;liveWriteProven=$false;capture=$null;captureSha256=$null;proof=$null;flagSha256=$null;probeVerdict=$null;rollbackProof=$null}
    Save-WriteState $state
    Assert-WriteOutputsAbsent
    Invoke-WriteChild $writeChain @('Stage','-InventoryBuildDir',$writeInventory,'-LoaderBuildDir',$writeLoader,'-SignedDirectory',$writeSigned) (New-WritePath 'stage' 'log')
    Invoke-WriteChild $writeChain @('Status','-InventoryBuildDir',$writeInventory,'-LoaderBuildDir',$writeLoader,'-SignedDirectory',$writeSigned) (New-WritePath 'status' 'log')
    Assert-WriteBaseline $snapshot
    $state.readyToArm=$true;$state.phase='write-probe-awaiting-user-boot';Save-WriteState $state
    'WRITE PROBE STAGED; NOT ARMED. Recovery snapshot: '+$snapshot
    exit 0
}
$state=Read-WriteState
switch($writeMode){
    'Status'{$state|ConvertTo-Json -Depth 9;Invoke-WriteChild $writeChain @('Status','-InventoryBuildDir',$writeInventory,'-LoaderBuildDir',$writeLoader,'-SignedDirectory',$writeSigned) (New-WritePath 'status' 'log')}
    'Arm'{
        if(-not $state.readyToArm -or $state.armed){throw 'The write probe is not ready for a new Arm.'}
        Assert-WriteBaseline $state.snapshot
        Assert-WriteOutputsAbsent
        $state.armedFromBoot=Get-WriteBootIdentity;$state.phase='arm-pending';Save-WriteState $state
        $armError=$null
        try{
            $mount=Mount-Esp
            try{$flag=Write-WriteFlag $mount.EspRoot $state.buildId;$state.flagSha256=$flag.sha256;$state.phase='one-shot-flag-staged';Save-WriteState $state}finally{Dismount-Esp $mount}
            Invoke-WriteChild $writeChain @('Arm','-InventoryBuildDir',$writeInventory,'-LoaderBuildDir',$writeLoader,'-SignedDirectory',$writeSigned) (New-WritePath 'arm' 'log')
        }catch{$armError=$_.Exception.Message}
        if($armError){
            $cleanupError=$null
            try{$mount=Mount-Esp;try{$flagCleanup=Remove-WriteFlagIfUnchanged $mount.EspRoot $state.flagSha256 $state.buildId}finally{Dismount-Esp $mount};Clear-WriteOneShot $state.snapshot -CheckSecureBoot;$state.flagSha256=$null;$state.armed=$false;$state.readyToArm=$true;$state.phase='arm-failed-cleaned';Save-WriteState $state}catch{$cleanupError=$_.Exception.Message}
            if($cleanupError){throw ('Arm failed: '+$armError+'; owned-state cleanup failed: '+$cleanupError)}
            throw ('Arm failed; owned flag and one-shot state were cleaned: '+$armError)
        }
        $state.armed=$true;$state.readyToArm=$false;$state.phase='user-armed';Save-WriteState $state
    }
    'Collect'{
        if(-not $state.armed -or (Get-WriteBootIdentity) -ceq $state.armedFromBoot){throw 'A user Arm and a subsequent Windows boot are required.'}
        $capture=New-WritePath 'secureboot-write' 'txt';$hash=$null
        $flagWasPresent=$false;$flagPresentHash=$null;$flagHashMismatch=$false;$mount=Mount-Esp
        try{
            $flagPath=Join-Path $mount.EspRoot 'HypersinvSecureBootWrite.flag'
            if(Test-Path -LiteralPath $flagPath){
                if((Test-ReparsePoint $flagPath) -or -not(Test-Path -LiteralPath $flagPath -PathType Leaf) -or (Get-Item -LiteralPath $flagPath).Length -gt 256){throw 'Unsafe one-shot flag; preserved for review.'}
                $flagWasPresent=$true;$flagPresentHash=Get-Sha256 $flagPath
                if($state.flagSha256 -and $flagPresentHash -cne $state.flagSha256){$flagHashMismatch=$true}
            }
            $source=Join-Path $mount.EspRoot $writeLeaf
            if(Test-Path -LiteralPath $source){if((Test-ReparsePoint $source) -or -not(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -gt 32KB){throw 'Unsafe write-probe output; preserved.'};$hash=Get-Sha256 $source;Copy-Item -LiteralPath $source -Destination $capture;if((Get-Sha256 $capture) -cne $hash -or (Get-Sha256 $source) -cne $hash){throw 'Write-probe output changed during capture.'}}
        }finally{Dismount-Esp $mount}
        $chainError=$null
        try{Invoke-WriteChild $writeChain @('Collect','-InventoryBuildDir',$writeInventory,'-LoaderBuildDir',$writeLoader,'-SignedDirectory',$writeSigned) (New-WritePath 'collect' 'log')}catch{$chainError=$_.Exception.Message}
        if(-not $hash){throw ('No boot write-probe report exists. Chain result: '+$chainError)}
        $state.capture=$capture;$state.captureSha256=$hash;Save-WriteState $state
        $expected=@((Get-Content -LiteralPath (Join-Path $state.snapshot 'firmware-variables.json') -Raw|ConvertFrom-Json)|Where-Object name -CEQ 'SecureBoot')
        if($expected.Count -ne 1){throw 'SecureBoot snapshot is ambiguous.'}
        $expectedHex=[BitConverter]::ToString([Convert]::FromBase64String($expected[0].base64)).Replace('-','')
        $fields=Read-WriteProbeReport $capture $state.buildId $expectedHex ([uint32]$expected[0].attributes)
        if($fields.flag_validation -ceq 'valid' -and $flagWasPresent){throw 'The report says the one-shot flag was consumed, but the ESP flag remains; preserved.'}
        if($chainError){throw ('Write report captured and parsed, but the signed chain did not pass: '+$chainError)}
        Assert-WriteBaseline $state.snapshot
        $proof=New-WritePath 'write-proof'
        $probeResult=if($fields.result -ceq 'pass'){'PASS'}else{'NOT_ATTEMPTED'}
        [IO.File]::WriteAllText($proof,([ordered]@{result=$probeResult;scope='Boot-phase SecureBoot variable SetVariable attempt and byte read-back only';fields=$fields;capture=$capture;captureSha256=$hash;snapshot=$state.snapshot;flagExpectedSha256=$state.flagSha256;flagPresent=$flagWasPresent;flagActualSha256=$flagPresentHash;flagHashMismatch=$flagHashMismatch;secureBootPolicyChangeProven=$false}|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
        $mount=Mount-Esp;try{$source=Join-Path $mount.EspRoot $writeLeaf;if((Get-Sha256 $source) -cne $hash){throw 'Captured report changed before cleanup; preserved.'};Remove-Item -LiteralPath $source -Force;if($fields.flag_validation -ceq 'valid' -and (Test-Path -LiteralPath (Join-Path $mount.EspRoot 'HypersinvSecureBootWrite.flag'))){throw 'The one-shot flag reappeared before cleanup; preserved.'}}finally{Dismount-Esp $mount}
        $state.armed=$false;$state.liveWriteProven=($fields.verdict -ceq 'TOGGLE-WORKS');$state.probeVerdict=$fields.verdict;$state.phase=if($probeResult -eq 'PASS'){'write-probe-complete'}else{'write-probe-not-attempted'};$state.proof=$proof;Save-WriteState $state
        'SECUREBOOT WRITE PROBE '+$fields.verdict+' '+$proof
        'Verdict applies to SecureBoot variable bytes only; firmware policy behavior is not inferred.'
    }
    'Rollback'{
        Clear-WriteOneShot $state.snapshot
        $mount=Mount-Esp
        $wmbActual=$null;$restoreSources=$null
        try{
            $snapshot=Get-Content -LiteralPath (Join-Path $state.snapshot 'snapshot.json') -Raw|ConvertFrom-Json
            if($mount.VolumeGuid -cne $snapshot.volumeGuid){throw ('ESP volume mismatch; expected='+$snapshot.volumeGuid+' actual='+$mount.VolumeGuid+'; no ESP writes performed.')}
            $wmbActual=Get-BootManagerHash $mount.EspRoot
            $saved=Join-Path $state.snapshot 'ESP\EFI\HypersinvShimInventory'
            if((Get-Sha256 (Join-Path $saved 'manifest.sha256')) -cne $snapshot.manifestSha256){throw 'Saved original stage manifest changed.'}
            foreach($file in $snapshot.stageFiles){if((Get-Sha256 (Join-Path $saved $file.Name)) -cne $file.Hash){throw ('Saved original stage changed: '+$file.Name)}}
            $flagPath=Join-Path $mount.EspRoot 'HypersinvSecureBootWrite.flag'
            if(Test-Path -LiteralPath $flagPath){$null=Remove-WriteFlagIfUnchanged $mount.EspRoot $state.flagSha256 $state.buildId}
            $probePath=Join-Path $mount.EspRoot $writeLeaf
            if(Test-Path -LiteralPath $probePath){
                if((Test-ReparsePoint $probePath) -or -not(Test-Path -LiteralPath $probePath -PathType Leaf) -or (Get-Item -LiteralPath $probePath).Length -gt 32KB){throw 'Unsafe write-probe report preserved during rollback.'}
                $probeHash=Get-Sha256 $probePath;$probeCopy=New-WritePath 'rollback-write-probe-raw' 'txt';Copy-Item -LiteralPath $probePath -Destination $probeCopy
                if((Get-Sha256 $probeCopy) -cne $probeHash -or (Get-Sha256 $probePath) -cne $probeHash){throw 'Write-probe report changed while captured; ESP copy preserved.'}
                Remove-Item -LiteralPath $probePath -Force
            }
            $stageRoot=Join-Path $mount.EspRoot 'EFI\HypersinvShimInventory'
            Assert-ShimStageNames $stageRoot -AllowStatus
            $current=@{Files=@($script:ShimStageNames|ForEach-Object{Get-FileRecord $_ (Join-Path $stageRoot $_)})}
            if([IO.File]::ReadAllText((Join-Path $stageRoot 'manifest.sha256'),[Text.Encoding]::ASCII) -cne (New-ShimManifest $current)){throw 'Current stage manifest mismatch; no restore performed.'}
            $prepared=Get-ShimStageSources
            foreach($file in $current.Files){$wanted=@($prepared.Files|Where-Object Name -CEQ $file.Name);if($wanted.Count -ne 1 -or ($file.Hash -cne $wanted[0].Hash -and $file.Hash -cne (Get-Sha256 (Join-Path $saved $file.Name)))){throw ('Unexpected stage bytes: '+$file.Name)}}
            foreach($leaf in @('HypersinvEfiInventory.txt','EFI\HypersinvShimInventory\chain-status.txt','EFI\HypersinvShimInventory\chain-diagnostic.txt')){
                $path=Join-Path $mount.EspRoot $leaf;if(-not(Test-Path -LiteralPath $path)){continue}
                if((Test-ReparsePoint $path) -or -not(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -gt 64MB){throw 'Unsafe output preserved during rollback.'}
                $hash=Get-Sha256 $path;$copy=New-WritePath ('rollback-'+[IO.Path]::GetFileNameWithoutExtension($leaf)) 'txt';Copy-Item -LiteralPath $path -Destination $copy
                if((Get-Sha256 $copy) -cne $hash -or (Get-Sha256 $path) -cne $hash){throw 'Output changed during recovery capture.'};Remove-Item -LiteralPath $path -Force
            }
            $restoreSources=Get-ExistingShimStageSources $saved
            foreach($file in $restoreSources.Files){$file.Path=Join-Path $saved $file.Name}
            $restoreSources.CertificateThumbprint=$script:ShimCertThumbprint
            function Get-ShimStageSources {return $restoreSources}
            Stage-ShimChainToRoot $mount.EspRoot $mount.VolumeGuid ''
        }finally{Dismount-Esp $mount}
        $verificationErrors=@();$espProof=$null;$bcdProof=$null
        try{$espProof=Compare-WriteEspToSnapshot $state.snapshot}catch{$verificationErrors+=('ESP: '+$_.Exception.Message)}
        try{$bcdProof=Assert-WriteBcdBaseline $state.snapshot}catch{$verificationErrors+=('BCD: '+$_.Exception.Message)}
        try{Assert-WriteBaseline $state.snapshot}catch{$verificationErrors+=('Firmware/Secure Boot: '+$_.Exception.Message)}
        if($wmbActual -cne $snapshot.bootManagerSha256){$verificationErrors+=('Windows Boot Manager SHA-256 differs from snapshot: expected='+$snapshot.bootManagerSha256+' actual='+$wmbActual+'; the Microsoft file was not changed.')}
        $rollbackProof=New-WritePath 'rollback-proof'
        [IO.File]::WriteAllText($rollbackProof,(ConvertTo-Json -InputObject ([ordered]@{result=if($verificationErrors.Count){'FAIL'}else{'PASS'};snapshot=$state.snapshot;bootManagerExpectedSha256=$snapshot.bootManagerSha256;bootManagerActualSha256=$wmbActual;esp=$espProof;bcd=$bcdProof;errors=$verificationErrors;capturedUtc=[DateTime]::UtcNow.ToString('o')}) -Depth 10),[Text.UTF8Encoding]::new($false))
        $state.armed=$false;$state.readyToArm=$false;$state.rollbackProof=$rollbackProof
        if($verificationErrors.Count){$state.phase='original-stage-restored-verification-failed'}else{$state.phase='original-stage-restored'}
        Save-WriteState $state
        if($verificationErrors.Count){throw ('ESP stage restored, but rollback verification failed: '+($verificationErrors -join ' | ')+'; full evidence='+$rollbackProof)}
        'ORIGINAL STAGE RESTORED. Secure Boot variable bytes, live BCD enumeration, ESP and Windows Boot Manager matched the snapshot. Evidence: '+$rollbackProof
    }
    'Verify'{
        if($state.phase -cne 'original-stage-restored'){throw 'Rollback must restore the original stage first.'}
        Assert-WriteBaseline $state.snapshot
        $espProof=Compare-WriteEspToSnapshot $state.snapshot
        $bcdProof=Assert-WriteBcdBaseline $state.snapshot
        'ORIGINAL BOOT STATE VERIFIED. SecureBoot variable bytes and mode, live BCD enumeration, Windows Boot Manager, and ESP matched the fresh snapshot; BOOTSTAT.DAT mutable changes are itemized in '+$espProof.comparison
    }
}
