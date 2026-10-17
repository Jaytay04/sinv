[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..\..'))
$evidence=Join-Path $repo 'spoof-handoff\reports\nic-vm-20261017'
$controller=Join-Path $evidence 'vm-control.ps1'
$vmx=Join-Path $repo 'spoof-handoff\artifacts\nic-vm-20261017\vm-plain\DeviceFilter-Win11.vmx'
$vmrun='C:\Program Files\VMware\VMware Workstation\vmrun.exe'
if (-not (Test-Path -LiteralPath $controller -PathType Leaf) -or -not (Test-Path -LiteralPath $vmx -PathType Leaf)) { throw 'The owned NIC VM/controller is missing; recovery cannot infer another target.' }
$records=[Collections.Generic.List[object]]::new()
function Invoke-Control([string]$Action,[string]$Name) {
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$controller,'-Action',$Action,'-SourceVariant','Plain')
    if($Name){$arguments+=@('-Name',$Name)}
    $prior=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$lines=@(& (Join-Path $PSHOME 'powershell.exe') @arguments 2>&1|ForEach-Object {$_.ToString()});$code=$LASTEXITCODE}finally{$ErrorActionPreference=$prior}
    $records.Add([ordered]@{action=$Action;name=$Name;exitCode=$code;output=$lines})
    return $code
}
function Get-RunningVms {
    $lines=@(& $vmrun list 2>&1|ForEach-Object {$_.ToString()})
    if($LASTEXITCODE -ne 0){throw ('Cannot inventory running VMs: '+($lines -join ' '))}
    return @($lines|Where-Object {$_ -match '\.vmx$'})
}
$result=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');vmx=$vmx;snapshot='DeviceFilter-Nic-ColdOriginal';hostSettingsChanged=$false;status='FAIL';commands=@()}
try {
    if(@(Get-RunningVms) -contains $vmx){
        if((Invoke-Control 'Stop' '') -ne 0){
            if((Invoke-Control 'StopHard' '') -ne 0){throw 'Owned VM did not stop; refusing snapshot recovery while it is running.'}
        }
    }
    if(@(Get-RunningVms) -contains $vmx){throw 'Owned VM remains running; refusing snapshot recovery.'}
    if((Invoke-Control 'Revert' 'DeviceFilter-Nic-ColdOriginal') -ne 0){throw 'Owned cold snapshot recovery failed; preserve the exact controller evidence.'}
    if(@(Get-RunningVms) -contains $vmx){throw 'The cold snapshot unexpectedly left the VM running.'}
    $result.status='RECOVERED_AND_POWERED_OFF'
}catch{$result.error=$_.Exception.ToString();throw}
finally{
    $result.commands=@($records.ToArray())
    $path=Join-Path $evidence ('recovery.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N')+'.json')
    [IO.File]::WriteAllText($path,($result|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    Write-Output ('VM RECOVERY EVIDENCE '+$path)
}
Write-Output 'Owned NIC VM restored to its original cold snapshot and powered off. Host policy and adapters were not changed.'
