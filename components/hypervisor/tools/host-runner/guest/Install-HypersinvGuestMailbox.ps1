[CmdletBinding()]
param(
    [string]$MailboxRoot = '\\vmware-host\Shared Folders\HypersinvRunner',
    [string]$InstallRoot = 'C:\ProgramData\HypersinvRunner',
    [string]$TaskName = 'HypersinvGuestMailbox'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
if (-not $Principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this installer from elevated Windows PowerShell inside the disposable VM.'
}
if (-not (Test-Path -LiteralPath $MailboxRoot -PathType Container)) {
    throw "The VMware shared mailbox is unavailable: $MailboxRoot"
}

New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
$RunnerSource = Join-Path $PSScriptRoot 'HypersinvGuestMailbox.ps1'
if (-not (Test-Path -LiteralPath $RunnerSource -PathType Leaf)) {
    throw "Guest runner source not found: $RunnerSource"
}
$RunnerPath = Join-Path $InstallRoot 'HypersinvGuestMailbox.ps1'
Copy-Item -LiteralPath $RunnerSource -Destination $RunnerPath -Force

$ConfigPath = Join-Path $InstallRoot 'guest-runner.config.json'
[ordered]@{
    SchemaVersion = 1
    MailboxRoot = $MailboxRoot
    LocalRunRoot = 'C:\V3H\host-runner'
    PollMilliseconds = 1000
} | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $ConfigPath -Encoding utf8

$PowerShell = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$Arguments = '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass ' +
    ('-File "{0}" -ConfigPath "{1}"' -f $RunnerPath, $ConfigPath)
$Action = New-ScheduledTaskAction -Execute $PowerShell -Argument $Arguments
$Trigger = New-ScheduledTaskTrigger -AtLogOn -User $Identity.Name
$Settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -MultipleInstances IgnoreNew
$Task = New-ScheduledTask -Action $Action -Trigger $Trigger `
    -Settings $Settings -Principal (New-ScheduledTaskPrincipal `
        -UserId $Identity.Name -LogonType Interactive -RunLevel Highest)

Register-ScheduledTask -TaskName $TaskName -InputObject $Task -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName

Write-Host "PASS: installed and started $TaskName."
Write-Host "Mailbox: $MailboxRoot"
Write-Host "Runner:  $RunnerPath"
