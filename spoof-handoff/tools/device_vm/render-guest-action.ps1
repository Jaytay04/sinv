[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GuestInventory,
    [Parameter(Mandatory=$true)][ValidateSet('PreparePolicy','Install','Readback','Remove','Reboot','Shutdown')][string]$Operation,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [string]$PackageDirectory,
    [ValidateSet('Original','Replacement')][string]$ExpectedMode='Original',
    [ValidateSet('Any','Present','Absent')][string]$FilterState='Any',
    [string]$ReplacementSerial
)
$ErrorActionPreference='Stop'
$vmInventory=Get-Content -LiteralPath $GuestInventory -Raw|ConvertFrom-Json
if($vmInventory.machine.model -notin @('VMware Virtual Platform','VMware20,1') -or [string]::IsNullOrWhiteSpace($vmInventory.machine.uuid)){throw 'A fresh supported VMware guest inventory with UUID is required.'}
$vmSource=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'guest-probe.ps1'))
$vmSource=$vmSource.Replace('VM inventory collected through native storage IOCTL and fresh WMI; no settings changed.','Guest action and fresh read-back collected; inspect operation and guestCommands for exact changes.')
$vmSource=$vmSource.Replace("`$result.Message='VM read-only baseline collected'","`$result.Message='Explicit VM action and read-back completed'")
$vmRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
$vmOutput=[IO.Path]::GetFullPath($OutputPath)
if(-not $vmOutput.StartsWith((Join-Path $vmRepo 'spoof-handoff\reports\device-vm-20261017\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Generated guest script must remain under the VM evidence directory.'}
$vmConfig=[ordered]@{operation=$Operation;expectedUuid=[string]$vmInventory.machine.uuid;expectedPnpId=[string]$vmInventory.disk.pnpDeviceId;originalSerial=[string]$vmInventory.disk.serial;replacementSerial=$ReplacementSerial;expectedMode=$ExpectedMode;filterState=$FilterState;files=@();certificateBase64=$null}
if($Operation -ceq 'PreparePolicy'){$vmConfig.certificateBase64=[Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $vmRepo 'components\hypervisor\hypersinv.cer')))}
if($Operation -ceq 'Install'){
    $packageFull=[IO.Path]::GetFullPath($PackageDirectory)
    if(-not $packageFull.StartsWith((Join-Path $vmRepo 'spoof-handoff\artifacts\device-vm-20261017\build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Guest install package is outside VM build artifacts.'}
    $vmConfig.packageId=([Guid]::NewGuid().ToString('N'))
    $vmConfig.files=@(foreach($name in @('client_storage_filter.sys','client_storage_filter.inf','client_storage_filter.cat')){
        $path=Join-Path $packageFull $name
        [ordered]@{name=$name;sha256=(Get-FileHash -LiteralPath $path).Hash;base64=[Convert]::ToBase64String([IO.File]::ReadAllBytes($path))}
    })
}
$config64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $vmConfig -Depth 8 -Compress)))
$hook=@'
    $vmTestConfig=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('__CONFIG64__'))|ConvertFrom-Json
    $actualUuid=[string](Get-CimInstance Win32_ComputerSystemProduct).UUID
    if($actualUuid -cne $vmTestConfig.expectedUuid){throw ('Guest UUID mismatch: expected='+$vmTestConfig.expectedUuid+' actual='+$actualUuid)}
    $actualDisk=@(Get-CimInstance Win32_DiskDrive|Where-Object {$_.DeviceID -ceq '\\.\PHYSICALDRIVE0'})
    if($actualDisk.Count -ne 1 -or [string]$actualDisk[0].PNPDeviceID -cne $vmTestConfig.expectedPnpId){throw 'Guest boot disk identity differs from the inventory.'}
    $testRoot='C:\ProgramData\DeviceFilterVmTest'
    [void][IO.Directory]::CreateDirectory($testRoot)
    $commandRecords=[Collections.Generic.List[object]]::new()
    function Invoke-GuestCommand([string]$Program,[string[]]$Arguments,[int[]]$Accepted=@(0)){
        $prior=$ErrorActionPreference;$ErrorActionPreference='Continue'
        try{$lines=@(& $Program @Arguments 2>&1|ForEach-Object {[string]$_});$exit=$LASTEXITCODE}finally{$ErrorActionPreference=$prior}
        $commandRecords.Add([ordered]@{program=$Program;arguments=$Arguments;exitCode=$exit;output=$lines})
        if($Accepted -notcontains $exit){throw ('Guest command failed: '+$Program+' exit='+$exit+' output='+($lines -join ' '))}
        return [pscustomobject]@{exitCode=$exit;lines=$lines}
    }
    switch([string]$vmTestConfig.operation){
        'PreparePolicy' {
            if(Confirm-SecureBootUEFI){throw 'Test-signing preparation requires Secure Boot disabled in this disposable guest.'}
            $publicCert=[Security.Cryptography.X509Certificates.X509Certificate2]::new([Convert]::FromBase64String($vmTestConfig.certificateBase64))
            if($publicCert.Thumbprint -cne '59DBD6780CFE60C020B3F4511211FEC22C397B85'){throw 'Guest public signing certificate is not the pinned existing key.'}
            $trust=@(foreach($name in @('Root','TrustedPublisher')){
                $store=[Security.Cryptography.X509Certificates.X509Store]::new($name,'LocalMachine')
                $store.Open('ReadWrite')
                try{$before=@($store.Certificates|Where-Object {$_.Thumbprint -ceq $publicCert.Thumbprint}).Count -gt 0;$store.Add($publicCert);[ordered]@{store=$name;presentBefore=$before;presentAfter=$true}}finally{$store.Close()}
            })
            [void](Invoke-GuestCommand "$env:SystemRoot\System32\bcdedit.exe" @('/set','{current}','testsigning','on'))
            $vmTestConfig|Add-Member NoteProperty trust $trust
        }
        'Install' {
            if(Confirm-SecureBootUEFI){throw 'VM test package cannot be installed with guest Secure Boot enabled.'}
            if(@(Get-CimInstance Win32_SystemDriver -Filter "Name='client_storage_filter'").Count -gt 0){throw 'Storage filter already exists; restore the clean test snapshot first.'}
            $packageRoot=Join-Path $testRoot ('packages\'+$vmTestConfig.packageId)
            [void][IO.Directory]::CreateDirectory($packageRoot)
            foreach($file in $vmTestConfig.files){
                if($file.name -notin @('client_storage_filter.sys','client_storage_filter.inf','client_storage_filter.cat')){throw 'Package file is not allowed.'}
                $target=Join-Path $packageRoot $file.name
                [IO.File]::WriteAllBytes($target,[Convert]::FromBase64String($file.base64))
                if((Get-FileHash -LiteralPath $target).Hash -cne $file.sha256){throw ('Guest package hash mismatch: '+$file.name)}
            }
            $installed=Invoke-GuestCommand "$env:SystemRoot\System32\pnputil.exe" @('/add-driver',(Join-Path $packageRoot 'client_storage_filter.inf'),'/install') @(0,3010)
            $published=@($installed.lines|ForEach-Object {if($_ -match '(?i)Published Name\s*:\s*(oem\d+\.inf)'){$Matches[1]}})
            if($published.Count -ne 1){throw 'Cannot uniquely attribute the installed VM storage package.'}
            [IO.File]::WriteAllText((Join-Path $testRoot 'owned-storage.json'),(ConvertTo-Json -InputObject ([ordered]@{publishedName=$published[0];uuid=$actualUuid;packageId=$vmTestConfig.packageId;infSha256=(Get-FileHash -LiteralPath (Join-Path $packageRoot 'client_storage_filter.inf')).Hash})),[Text.UTF8Encoding]::new($false))
        }
        'Remove' {
            $ownership=Get-Content -LiteralPath (Join-Path $testRoot 'owned-storage.json') -Raw|ConvertFrom-Json
            if($ownership.uuid -cne $actualUuid -or $ownership.publishedName -notmatch '^oem\d+\.inf$'){throw 'VM package ownership is not established.'}
            [void](Invoke-GuestCommand "$env:SystemRoot\System32\pnputil.exe" @('/delete-driver',[string]$ownership.publishedName,'/uninstall') @(0,3010))
        }
        'Reboot' {[void](Invoke-GuestCommand "$env:SystemRoot\System32\shutdown.exe" @('/r','/t','30','/f','/d','p:4:1','/c','Disposable storage filter VM test'))}
        'Shutdown' {[void](Invoke-GuestCommand "$env:SystemRoot\System32\shutdown.exe" @('/s','/t','30','/f','/d','p:4:1','/c','Disposable storage filter VM test complete'))}
        'Readback' { }
        default {throw 'Unknown VM-only operation.'}
    }
'@
$hook=$hook.Replace('__CONFIG64__',$config64)
$anchor='    $os=Get-CimInstance Win32_OperatingSystem'
if(-not $vmSource.Contains($anchor)){throw 'Guest probe mutation anchor was not found.'}
$vmSource=$vmSource.Replace($anchor,($hook+"`r`n"+$anchor))
$evidenceHook=@'
    if($vmTestConfig.operation -in @('Readback','Install','Remove')){
        [void](Invoke-GuestCommand "$env:SystemRoot\System32\pnputil.exe" @('/enum-devices','/instanceid',[string]$vmTestConfig.expectedPnpId,'/stack','/drivers'))
        [void](Invoke-GuestCommand "$env:SystemRoot\System32\sc.exe" @('query','client_storage_filter') @(0,1060))
    }
    $record.operation=[string]$vmTestConfig.operation
    $record.hostOrGuestSettingsChanged=($vmTestConfig.operation -ne 'Readback')
    $record.guestCommands=@($commandRecords.ToArray())
    $record.guestTestConfiguration=[ordered]@{operation=$vmTestConfig.operation;expectedMode=$vmTestConfig.expectedMode;expectedUuid=$vmTestConfig.expectedUuid;expectedPnpId=$vmTestConfig.expectedPnpId}
    $servicePath='HKLM:\SYSTEM\CurrentControlSet\Services\client_storage_filter'
    $record.storageFilterServiceSettings=if(Test-Path -LiteralPath $servicePath){Get-ItemProperty -LiteralPath $servicePath|Select-Object Start,Type,ImagePath,Group,ErrorControl}else{$null}
    if($vmTestConfig.operation -ceq 'Readback'){
        $ioPath=Join-Path $testRoot ('file-io.'+[Guid]::NewGuid().ToString('N')+'.bin')
        $ioBytes=New-Object byte[] (1MB)
        [Random]::new(61417).NextBytes($ioBytes)
        $ioSha=[Security.Cryptography.SHA256]::Create()
        try{
            $expectedHash=([BitConverter]::ToString($ioSha.ComputeHash($ioBytes))).Replace('-','')
            $stream=[IO.FileStream]::new($ioPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None,4096,[IO.FileOptions]::WriteThrough)
            try{$stream.Write($ioBytes,0,$ioBytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
            $actualHash=(Get-FileHash -LiteralPath $ioPath -Algorithm SHA256).Hash
            $record.fileIo=[ordered]@{bytes=$ioBytes.Length;expectedSha256=$expectedHash;actualSha256=$actualHash;match=($actualHash -ceq $expectedHash);writeThrough=$true}
        }finally{
            $ioSha.Dispose()
            if(Test-Path -LiteralPath $ioPath){Remove-Item -LiteralPath $ioPath -Force}
        }
    }
'@
$anchor="    [IO.File]::WriteAllText((Join-Path `$RunRoot 'kernel-metadata.json')"
if(-not $vmSource.Contains($anchor)){throw 'Guest probe evidence anchor was not found.'}
$vmSource=$vmSource.Replace($anchor,($evidenceHook+"`r`n"+$anchor))
$assertion=@'
    if($vmTestConfig.operation -ceq 'Readback'){
        $expectedSerial=if($vmTestConfig.expectedMode -ceq 'Original'){$vmTestConfig.originalSerial}else{$vmTestConfig.replacementSerial}
        $mismatches=@()
        if(-not $record.fileIo.match){$mismatches+=('file I/O SHA256 expected='+$record.fileIo.expectedSha256+' actual='+$record.fileIo.actualSha256)}
        if($record.disk.serial -cne $expectedSerial){$mismatches+=('native serial expected='+$expectedSerial+' actual='+$record.disk.serial)}
        if($record.disk.wmiSerial -cne $expectedSerial){$mismatches+=('WMI serial expected='+$expectedSerial+' actual='+$record.disk.wmiSerial)}
        $running=@($record.storageFilterServices|Where-Object {$_.State -ceq 'Running'}).Count -gt 0
        $stackText=@($commandRecords.ToArray()|Where-Object {$_.program -like '*pnputil.exe'}|ForEach-Object {$_.output}) -join "`n"
        $stackSection=[regex]::Match($stackText,'(?im)^Stack:\s*([^\r\n]+)(?:\r?\n[ \t]+[^\r\n]+)*').Value
        $inStack=$stackSection -match '(?im)^(?:Stack:\s*|[ \t]+)client_storage_filter[ \t]*$'
        if($vmTestConfig.filterState -ceq 'Present' -and (-not $running -or -not $inStack)){$mismatches+=('filter expected=Running and in selected disk stack; actual running='+$running+' inStack='+$inStack)}
        if($vmTestConfig.filterState -ceq 'Absent' -and ($running -or $inStack -or $record.storageFilterServices.Count -gt 0)){$mismatches+=('filter expected=absent; actual running='+$running+' inStack='+$inStack+' services='+$record.storageFilterServices.Count)}
        if($mismatches.Count){throw ($mismatches -join '; ')}
    }
'@
$vmSource=$vmSource.Replace("    `$result.Status='PASS';",($assertion+"`r`n    `$result.Status='PASS';"))
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $vmOutput))
[IO.File]::WriteAllText($vmOutput,$vmSource,[Text.UTF8Encoding]::new($false))
$tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseFile($vmOutput,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ('Rendered guest action has syntax errors: '+($errors.Message -join '; '))}
'GUEST ACTION RENDERED: '+$Operation+'; no guest command was executed.'
