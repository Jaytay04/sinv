[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$ReportDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$variantRoot=[IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
$variantPackage=Join-Path $variantRoot 'spoof-handoff'
$variantOutput=[IO.Path]::GetFullPath($ReportDirectory).TrimEnd('\')
if(-not $variantOutput.StartsWith($variantPackage+'\reports\',[StringComparison]::OrdinalIgnoreCase)){
    throw 'Evidence must remain under spoof-handoff/reports.'
}
$variantPrincipal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $variantPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevation is required.'}
if(Test-Path -LiteralPath $variantOutput){throw 'The evidence directory must be new.'}
New-Item -ItemType Directory -Path $variantOutput | Out-Null
$variantTemporary=Join-Path $variantOutput 'temporary'
New-Item -ItemType Directory -Path $variantTemporary | Out-Null
$env:TEMP=$variantTemporary;$env:TMP=$variantTemporary
. (Join-Path $PSScriptRoot '..\tools\smbios_boot\snapshot-reference.ps1') -RepoRoot $variantRoot -OutputBoundary $variantPackage -LibraryOnly
$auditLog=Join-Path $variantOutput 'audit.log'
$variantCommands=[Collections.Generic.List[object]]::new()
$variantAttempts=[Collections.Generic.List[object]]::new()
$variantRestores=[Collections.Generic.List[object]]::new()
function Save-VariantJson([string]$Name,$Value){
    [IO.File]::WriteAllText((Join-Path $variantOutput $Name),(ConvertTo-Json -InputObject $Value -Depth 16),[Text.UTF8Encoding]::new($false))
}
function Invoke-VariantBcd([string[]]$Arguments,[string]$Name){
    # Pass braces literally, without PowerShell interpreting identifiers as script blocks.
    $encoded=@(foreach($argument in $Arguments){
        if($argument.Contains('"') -or $argument.Contains("`r") -or $argument.Contains("`n")){throw 'Unsupported command argument.'}
        if($argument -match '\s'){'"'+[regex]::Replace($argument,'(\\+)$','$1$1')+'"'}else{$argument}
    })
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=Join-Path $env:SystemRoot 'System32\bcdedit.exe'
    $start.Arguments=$encoded -join ' '
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start
    try{
        $startedUtc=[DateTime]::UtcNow.ToString('o')
        if(-not $process.Start()){throw 'BCDEdit did not start.'}
        $stdoutTask=$process.StandardOutput.ReadToEndAsync()
        $stderrTask=$process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $stdout=$stdoutTask.GetAwaiter().GetResult();$stderr=$stderrTask.GetAwaiter().GetResult()
        $result=[pscustomobject]@{
            name=$Name;command=('bcdedit '+($encoded -join ' '));arguments=$Arguments
            startedUtc=$startedUtc;exitCode=$process.ExitCode
            stdoutFile=($Name+'.stdout.txt');stderrFile=($Name+'.stderr.txt')
            stdout=$stdout;stderr=$stderr
        }
        [IO.File]::WriteAllText((Join-Path $variantOutput $result.stdoutFile),$stdout,[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $variantOutput $result.stderrFile),$stderr,[Text.UTF8Encoding]::new($false))
        $variantCommands.Add($result)
        Save-VariantJson 'commands.json' $variantCommands.ToArray()
        return $result
    }finally{$process.Dispose()}
}
function Get-VariantOptions([string]$Object,[object]$Enumeration){
    @(foreach($name in @('secureboot','dbgsigning','testsigning')){
        $match=[regex]::Match($Enumeration.stdout,('(?m)^'+$name+'\s+([^\r\n]+)'))
        [pscustomobject]@{object=$Object;name=$name;present=$match.Success;value=$(if($match.Success){$match.Groups[1].Value.Trim()}else{$null})}
    })
}
function Get-VariantEsp([string]$EspRoot){
    foreach($rootItem in @(Get-ChildItem -LiteralPath $EspRoot -Force)){
        if($rootItem.Name -eq 'System Volume Information'){continue}
        $tree=if($rootItem.PSIsContainer){@($rootItem)+@(Get-ChildItem -LiteralPath $rootItem.FullName -Recurse -Force)}else{@($rootItem)}
        foreach($item in $tree){
            if(Test-ReparsePoint $item.FullName){throw ('ESP reparse point: '+$item.FullName)}
            $relative=$item.FullName.Substring($EspRoot.Length)
            $activeBcd=($relative -match '^EFI\\Microsoft\\Boot\\BCD(?:\.LOG[0-9]*)?$')
            [pscustomobject]@{path=$relative;directory=$item.PSIsContainer;activeBcd=$activeBcd;sha256=$(if(-not $item.PSIsContainer -and -not $activeBcd){Get-Sha256 $item.FullName}else{$null})}
        }
    }
}
$variantMount=$null;$variantFailure=$null;$variantAfterFailure=$null
$variantEspBefore=@();$variantEspAfter=@();$variantBackupChecks=@()
$variantOriginals=@();$variantBefore=$null;$variantSecureBefore=$null
try{
    # Establish recoverable state before invoking any requested setter.
    $variantBefore=Invoke-VariantBcd @('/enum','all','/v') 'baseline-all'
    $variantCurrent=Invoke-VariantBcd @('/enum','{current}','/v') 'baseline-current'
    $variantBootmgr=Invoke-VariantBcd @('/enum','{bootmgr}','/v') 'baseline-bootmgr'
    $variantDbgobj=Invoke-VariantBcd @('/enum','{dbgobj}','/v') 'baseline-dbgobj'
    $variantExportPath=Join-Path $variantOutput 'system-bcd.export'
    $variantExport=Invoke-VariantBcd @('/export',$variantExportPath) 'baseline-export'
    if($variantBefore.exitCode -ne 0 -or $variantCurrent.exitCode -ne 0 -or $variantBootmgr.exitCode -ne 0 -or $variantExport.exitCode -ne 0){throw 'Could not snapshot the live system BCD.'}
    $variantCurrentId=[regex]::Match($variantCurrent.stdout,'(?m)^identifier\s+(\{[0-9A-Fa-f-]{36}\})\s*$').Groups[1].Value
    if(-not $variantCurrentId){throw 'Current loader GUID could not be resolved.'}
    $variantOriginals+=Get-VariantOptions '{current}' $variantCurrent
    $variantOriginals+=Get-VariantOptions '{bootmgr}' $variantBootmgr
    $variantOriginals+=Get-VariantOptions '{dbgobj}' $variantDbgobj
    $variantSecureBefore=Confirm-SecureBootUEFI
    Initialize-ReadyFirmwareReader
    [ReadyToArmFirmwareRead]::Enable()
    $variantOrder=Read-VariableRecord 'BootOrder' $globalGuid
    $variantNext=Read-VariableRecord 'BootNext' $globalGuid
    $variantBootCurrent=Read-VariableRecord 'BootCurrent' $globalGuid
    $variantEntries=@(Read-ReadyBootEntries $variantOrder $variantNext $variantBootCurrent $globalGuid)
    foreach($name in @('SecureBoot','SetupMode','AuditMode','DeployedMode','PK','KEK')){$null=Read-VariableRecord $name $globalGuid}
    foreach($name in @('db','dbx')){$null=Read-VariableRecord $name $imageGuid}
    foreach($name in @('MokListRT','MokListXRT','MokSBStateRT','SbatLevelRT','SbatLevel','SbatPolicy','MokIgnoreDB')){$null=Read-VariableRecord $name $shimGuid}
    Save-VariantJson 'firmware-before.json' $firmwareRecords.ToArray()
    Save-VariantJson 'boot-entries.json' $variantEntries
    Save-VariantJson 'original-options.json' $variantOriginals
    $variantPrior=Join-Path $variantRoot 'spoof-handoff\reports\temporary-cycle-preflight-20261014'
    $variantPriorLayout=Get-Content -LiteralPath (Join-Path $variantPrior 'esp-layout.json') -Raw | ConvertFrom-Json
    $variantMount=Mount-Esp
    $variantEspBefore=@(Get-VariantEsp $variantMount.EspRoot | Sort-Object path)
    foreach($file in @($variantPriorLayout.entries | Where-Object {$_.sha256})){
        $backup=Join-Path (Join-Path $variantPrior 'ESP') $file.backup
        $live=Join-Path $variantMount.EspRoot $file.path
        $backupHash=Get-Sha256 $backup;$liveHash=Get-Sha256 $live
        $variantBackupChecks+=[pscustomobject]@{path=$file.path;expected=$file.sha256;backup=$backup;backupActual=$backupHash;liveActual=$liveHash;matches=($backupHash -ceq $file.sha256 -and $liveHash -ceq $file.sha256)}
    }
    Save-VariantJson 'prior-esp-backup-verification.json' $variantBackupChecks
    Save-VariantJson 'esp-before.json' $variantEspBefore
    $variantPriorPaths=@($variantPriorLayout.entries | Where-Object {$_.sha256} | ForEach-Object path | Sort-Object)
    $variantCurrentPaths=@($variantEspBefore | Where-Object {$_.sha256} | ForEach-Object path | Sort-Object)
    if($variantMount.VolumeGuid -cne $variantPriorLayout.volumeGuid -or @($variantBackupChecks | Where-Object matches -eq $false).Count -ne 0 -or ($variantPriorPaths -join "`n") -cne ($variantCurrentPaths -join "`n")){throw 'The previous ESP backup does not exactly cover current ESP file content.'}
    Save-VariantJson 'snapshot.json' ([ordered]@{
        capturedUtc=[DateTime]::UtcNow.ToString('o');currentOsIdentifier=$variantCurrentId;secureBoot=$variantSecureBefore
        bcdExport=$variantExportPath;bcdExportSha256=Get-Sha256 $variantExportPath
        espVolume=$variantMount.VolumeGuid;verifiedEspBackup=$variantPrior;verifiedFileCount=$variantBackupChecks.Count
        partitions=@(Get-Partition | Select-Object DiskNumber,PartitionNumber,GptType,Size,AccessPaths)
        disks=@(Get-Disk | Select-Object Number,PartitionStyle,IsBoot,IsSystem)
        note='Live BCD export plus raw boot/security variables and a freshly verified existing full ESP file backup. Active BCD hive/logs use the consistent export; filesystem metadata is excluded.'
    })
    # Requested order. Enumerations between setters preserve each resulting live view.
    $null=Invoke-VariantBcd @('/enum','firmware') '01-enum-firmware'
    $null=Invoke-VariantBcd @('/enum','{current}') '02-enum-current'
    foreach($case in @(
        @{name='03-current-dbgsigning-on';object='{current}';type='dbgsigning';arguments=@('/set','{current}','dbgsigning','on')},
        @{name='04-dbgobj-testsigning-on';object='{dbgobj}';type='testsigning';arguments=@('/set','{dbgobj}','testsigning','on')},
        @{name='05-default-secureboot-off';object='{current}';type='secureboot';arguments=@('/set','secureboot','off')},
        @{name='06-bootmgr-secureboot-off';object='{bootmgr}';type='secureboot';arguments=@('/set','{bootmgr}','secureboot','off')}
    )){
        # Journal the original representation before launching a setter, including an interrupted launch.
        $original=@($variantOriginals | Where-Object {$_.object -ceq $case.object -and $_.name -ceq $case.type})
        if($original.Count -ne 1){throw 'Ambiguous original BCD option.'}
        $attempt=[pscustomobject]@{name=$case.name;original=$original[0];result=$null;restored=$false}
        $variantAttempts.Add($attempt)
        Save-VariantJson 'attempts.json' $variantAttempts.ToArray()
        $attempt.result=Invoke-VariantBcd $case.arguments $case.name
        Save-VariantJson 'attempts.json' $variantAttempts.ToArray()
        $null=Invoke-VariantBcd @('/enum',$case.object,'/v') ($case.name+'-resulting-object')
    }
    $null=Invoke-VariantBcd @('/enum','/v') '07-enum-v'
    $null=Invoke-VariantBcd @('/?','ID') '08-help-id'
    $null=Invoke-VariantBcd @('/?','TYPES') '09-help-types'
}catch{$variantFailure=$_.Exception.ToString()}
finally{
    # An absent option must become absent again, rather than acquiring an explicit OFF value.
    foreach($attempt in @($variantAttempts.ToArray())){
        if($null -ne $attempt.result -and $attempt.result.exitCode -ne 0){continue}
        $original=$attempt.original
        $target=if($original.object -ceq '{current}'){$variantCurrentId}else{$original.object}
        $arguments=if($original.present){@('/set',$target,$original.name,$original.value)}else{@('/deletevalue',$target,$original.name)}
        try{
            $restore=Invoke-VariantBcd $arguments ('restore-'+$attempt.name)
            $variantRestores.Add($restore);$attempt.restored=($restore.exitCode -eq 0)
            if(-not $attempt.restored){$variantAfterFailure='An accepted or interrupted write could not be restored.'}
        }catch{$variantAfterFailure=$_.Exception.ToString()}
    }
    Save-VariantJson 'attempts.json' $variantAttempts.ToArray()
    Save-VariantJson 'restores.json' $variantRestores.ToArray()
    $variantFinal=$null;$variantBcdEqual=$false;$variantFirmwareEqual=$false;$variantEspEqual=$false;$variantSecureAfter=$null
    try{
        if($variantMount){
            $variantEspAfter=@(Get-VariantEsp $variantMount.EspRoot | Sort-Object path)
            Save-VariantJson 'esp-after.json' $variantEspAfter
            $variantEspEqual=($variantEspBefore.Count -gt 0 -and ($variantEspBefore | ConvertTo-Json -Depth 5 -Compress) -ceq ($variantEspAfter | ConvertTo-Json -Depth 5 -Compress))
            # BCDEdit renders device paths using current drive-letter mappings.
            Dismount-Esp $variantMount
        }
        $variantFinal=Invoke-VariantBcd @('/enum','all','/v') 'final-all'
        $null=Invoke-VariantBcd @('/enum','{current}','/v') 'final-current'
        $null=Invoke-VariantBcd @('/enum','{bootmgr}','/v') 'final-bootmgr'
        $variantBcdEqual=($null -ne $variantBefore -and $variantFinal.exitCode -eq 0 -and $variantFinal.stdout -ceq $variantBefore.stdout -and $variantFinal.stderr -ceq $variantBefore.stderr)
        $variantSecureAfter=Confirm-SecureBootUEFI
        [ReadyToArmFirmwareRead]::Enable()
        Assert-UnchangedVariables
        $variantFirmwareEqual=$true
    }catch{$variantAfterFailure=$_.Exception.ToString()}
    if($variantMount -and (Test-Path -LiteralPath $variantMount.EspRoot)){Dismount-Esp $variantMount}
    $variantMountRemoved=($null -eq $variantMount -or -not(Test-Path -LiteralPath $variantMount.EspRoot))
    Save-VariantJson 'result.json' ([ordered]@{
        finishedUtc=[DateTime]::UtcNow.ToString('o');attempts=$variantAttempts.ToArray();restores=$variantRestores.ToArray()
        secureBootBefore=$variantSecureBefore;secureBootAfter=$variantSecureAfter
        bcdListingByteIdentical=$variantBcdEqual;capturedFirmwareByteIdentical=$variantFirmwareEqual
        espLayoutAndFileContentIdentical=$variantEspEqual;temporaryEspMountRemoved=$variantMountRemoved
        priorEspBackupVerifiedFiles=$variantBackupChecks.Count;rebooted=$false;driverInstalledOrLoaded=$false
        failure=$variantFailure;verificationFailure=$variantAfterFailure
        note='Accepted BCD writes alone do not establish firmware Secure Boot control or native-reader coverage.'
    })
}
if($variantFailure -or $variantAfterFailure -or -not $variantBcdEqual -or -not $variantFirmwareEqual -or -not $variantEspEqual -or -not $variantMountRemoved -or $variantSecureBefore -ne $variantSecureAfter){throw 'Diagnostic or restoration verification failed; inspect result.json.'}
'LIVE VARIANT DIAGNOSTIC COMPLETE; ORIGINAL BOOT, SECURITY AND ESP STATE VERIFIED.'
