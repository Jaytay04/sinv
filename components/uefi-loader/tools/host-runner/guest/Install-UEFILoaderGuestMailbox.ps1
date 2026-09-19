[CmdletBinding()]
param(
    [string]$InstallRoot = 'C:\ProgramData\UEFILoaderRunner',
    [string]$TaskName = 'UEFILoaderGuestMailbox'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([IO.Path]::GetFullPath($InstallRoot) -cne
        'C:\ProgramData\UEFILoaderRunner' -or
    $TaskName -cne 'UEFILoaderGuestMailbox') {
    throw 'The guest runner installer accepts only the fixed clone configuration.'
}

$Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
if (-not $Principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this installer from an elevated session inside the disposable VM.'
}
if ($Identity.IsSystem) {
    throw 'The bootstrap installer must run from the disposable VM administrator session.'
}

$Source = Join-Path $PSScriptRoot 'Invoke-UEFILoaderGuestInfo.ps1'
$ServiceSource = Join-Path $PSScriptRoot 'UEFILoaderGuestMailboxService.cs'
$KeySource = Join-Path $PSScriptRoot 'uefiloader-mailbox.key'
foreach ($Path in @($Source, $ServiceSource, $KeySource)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "The guest runner source is missing: $Path"
    }
}
if ((Get-Item -LiteralPath $KeySource).Length -ne 32) {
    throw 'The guest runner authentication key must contain exactly 32 bytes.'
}

$VmtoolsdCandidates = @(
    'C:\Program Files\VMware\VMware Tools\vmtoolsd.exe',
    'C:\Program Files (x86)\VMware\VMware Tools\vmtoolsd.exe')
$VmtoolsdPath = $VmtoolsdCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if ($null -eq $VmtoolsdPath) {
    throw 'VMware Tools vmtoolsd.exe was not found at a fixed supported path.'
}

New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
$Administrators = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-544')
$System = [Security.Principal.SecurityIdentifier]::new('S-1-5-18')
$Users = [Security.Principal.SecurityIdentifier]::new('S-1-5-32-545')

function Set-ProtectedDirectoryAcl([string]$Path) {
    $Acl = [Security.AccessControl.DirectorySecurity]::new()
    $Acl.SetOwner($Administrators)
    $Acl.SetAccessRuleProtection($true, $false)
    foreach ($Rule in @(
            [Security.AccessControl.FileSystemAccessRule]::new(
                $Administrators, 'FullControl', 'ContainerInherit,ObjectInherit',
                'None', 'Allow'),
            [Security.AccessControl.FileSystemAccessRule]::new(
                $System, 'FullControl', 'ContainerInherit,ObjectInherit',
                'None', 'Allow'),
            [Security.AccessControl.FileSystemAccessRule]::new(
                $Users, 'ReadAndExecute', 'ContainerInherit,ObjectInherit',
                'None', 'Allow'))) {
        $Acl.AddAccessRule($Rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $Acl
}

function Set-ProtectedKeyAcl([string]$Path) {
    $Acl = [Security.AccessControl.FileSecurity]::new()
    $Acl.SetOwner($Administrators)
    $Acl.SetAccessRuleProtection($true, $false)
    foreach ($Rule in @(
            [Security.AccessControl.FileSystemAccessRule]::new(
                $Administrators, 'FullControl', 'Allow'),
            [Security.AccessControl.FileSystemAccessRule]::new(
                $System, 'FullControl', 'Allow'))) {
        $Acl.AddAccessRule($Rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $Acl
}

Set-ProtectedDirectoryAcl $InstallRoot
$Runner = Join-Path $InstallRoot 'Invoke-UEFILoaderGuestInfo.ps1'
Copy-Item -LiteralPath $Source -Destination $Runner -Force
$KeyPath = Join-Path $InstallRoot 'mailbox.key'
Copy-Item -LiteralPath $KeySource -Destination $KeyPath -Force
Set-ProtectedKeyAcl $KeyPath
$ConfigPath = Join-Path $InstallRoot 'guest-runner.config.json'
[ordered]@{
    SchemaVersion = 1
    GuestInfoPrefix = 'guestinfo.uefiloader'
    LocalRunRoot = (Join-Path $InstallRoot 'runs')
    KeyPath = $KeyPath
    PollMilliseconds = 1000
    VmtoolsdPath = $VmtoolsdPath
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ConfigPath -Encoding utf8

function Get-SidValue([string]$UserId) {
    try {
        return ([Security.Principal.NTAccount]::new($UserId).Translate(
            [Security.Principal.SecurityIdentifier])).Value
    }
    catch { return $null }
}

$ServiceName = 'UEFILoaderGuestMailbox'
$ServiceBinary = Join-Path $InstallRoot 'UEFILoaderGuestMailboxService.exe'
foreach ($LegacyTaskName in @($TaskName, 'UEFILoaderGuestMailboxAtLogon')) {
    if ($null -ne (Get-ScheduledTask -TaskName $LegacyTaskName `
            -ErrorAction SilentlyContinue)) {
        Unregister-ScheduledTask -TaskName $LegacyTaskName -Confirm:$false
    }
}
$ExistingService = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
if ($null -ne $ExistingService) {
    if ($ExistingService.Status -ne 'Stopped') {
        Stop-Service -Name $ServiceName -Force
    }
    & "$env:SystemRoot\System32\sc.exe" delete $ServiceName | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not remove the prior guest mailbox service.' }
    $DeleteDeadline = [DateTime]::UtcNow.AddSeconds(30)
    while ($null -ne (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue)) {
        if ([DateTime]::UtcNow -ge $DeleteDeadline) {
            throw 'Timed out removing the prior guest mailbox service.'
        }
        Start-Sleep -Milliseconds 250
    }
}
if (Test-Path -LiteralPath $ServiceBinary -PathType Leaf) {
    Remove-Item -LiteralPath $ServiceBinary -Force
}
Add-Type -Path $ServiceSource -OutputAssembly $ServiceBinary `
    -OutputType WindowsApplication `
    -ReferencedAssemblies @('System.dll', 'System.ServiceProcess.dll')
New-Service -Name $ServiceName -BinaryPathName ('"{0}"' -f $ServiceBinary) `
    -DisplayName 'UEFILoader GuestInfo Mailbox' `
    -Description 'Supervises the fixed UEFILoader GuestInfo worker.' `
    -StartupType Automatic -DependsOn 'VMTools' | Out-Null
& "$env:SystemRoot\System32\sc.exe" failure $ServiceName `
    'reset=' '60' 'actions=' 'restart/5000/restart/5000/restart/5000' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Could not configure mailbox service recovery.' }
& "$env:SystemRoot\System32\sc.exe" failureflag $ServiceName '1' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Could not enable mailbox service recovery.' }
Start-Service -Name $ServiceName

$InstalledService = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'"
$Dependencies = @(Get-Service -Name $ServiceName).ServicesDependedOn.Name
if ($null -eq $InstalledService -or
    [string]$InstalledService.StartName -notin @('LocalSystem', '.\LocalSystem') -or
    [string]$InstalledService.StartMode -cne 'Auto' -or
    [string]$InstalledService.State -cne 'Running' -or
    $Dependencies -notcontains 'VMTools' -or
    -not [string]::Equals(
        ([string]$InstalledService.PathName).Trim('"'), $ServiceBinary,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The automatic LocalSystem GuestInfo service is invalid.'
}
$InstallAcl = Get-Acl -LiteralPath $InstallRoot
if (-not $InstallAcl.AreAccessRulesProtected -or
    (Get-SidValue ([string]$InstallAcl.Owner)) -ne $Administrators.Value) {
    throw 'The runner installation directory ACL is not protected.'
}
$KeyAcl = Get-Acl -LiteralPath $KeyPath
if (-not $KeyAcl.AreAccessRulesProtected -or
    (Get-SidValue ([string]$KeyAcl.Owner)) -ne $Administrators.Value) {
    throw 'The runner authentication key ACL is not protected.'
}
Write-Host "PASS: installed and started $ServiceName as an automatic LocalSystem service."
Write-Host "Guest Variables prefix: guestinfo.uefiloader"
Write-Host "Runner: $Runner"
