[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$VmRoot,

    [Parameter(Mandatory = $true)]
    [string]$WindowsIso,

    [Parameter(Mandatory = $true)]
    [string]$CredentialPath,

    [string]$VmwareVdiskManagerPath =
        'C:\Program Files\VMware\VMware Workstation\vmware-vdiskmanager.exe',

    [ValidateRange(40, 256)]
    [int]$DiskSizeGiB = 80,

    [switch]$AuthorizeVmCreate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $AuthorizeVmCreate) {
    throw 'Creating the disposable Windows VM requires -AuthorizeVmCreate.'
}
foreach ($Path in @($WindowsIso, $CredentialPath, $VmwareVdiskManagerPath)) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required provisioning input is missing: $Path"
    }
}

$ResolvedRoot = [IO.Path]::GetFullPath($VmRoot)
if (Test-Path -LiteralPath $ResolvedRoot) {
    throw "The destination VM directory already exists: $ResolvedRoot"
}
$Parent = Split-Path -Parent $ResolvedRoot
if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
    throw "The destination VM parent does not exist: $Parent"
}
$Drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($ResolvedRoot))
if ($Drive.AvailableFreeSpace -lt 40GB) {
    throw 'At least 40 GiB of host free space is required before creating the disposable VM.'
}

$Credential = Import-Clixml -LiteralPath $CredentialPath
if ($Credential -isnot [Management.Automation.PSCredential]) {
    throw 'The VM credential file is not a DPAPI-protected PSCredential.'
}
$NetworkCredential = $Credential.GetNetworkCredential()
$PlainPassword = [string]$NetworkCredential.Password
$UserName = [string]$Credential.UserName
if ([string]::IsNullOrWhiteSpace($PlainPassword) -or
    $UserName -cnotmatch '^[A-Za-z0-9._-]{1,20}$') {
    throw 'The VM credential has an invalid local-account identity.'
}

$EscapedPassword = [Security.SecurityElement]::Escape($PlainPassword)
$EscapedUser = [Security.SecurityElement]::Escape($UserName)
$VmName = 'UEFILoader-Win11-ExactBuild-Gate'
$DiskPath = Join-Path $ResolvedRoot 'Windows 11 x64.vmdk'
$VmxPath = Join-Path $ResolvedRoot "$VmName.vmx"
$AnswerDisk = Join-Path $ResolvedRoot 'autounattend.flp'
$SerialLog = Join-Path $ResolvedRoot 'provision-serial.log'
$TempRoot = Join-Path $ResolvedRoot '.answer-source'
$AnswerPath = Join-Path $TempRoot 'Autounattend.xml'
$ProvisionPath = Join-Path $TempRoot 'Provision.ps1'

New-Item -ItemType Directory -Path $ResolvedRoot | Out-Null
New-Item -ItemType Directory -Path $TempRoot | Out-Null
try {
    $Answer = @"
<?xml version="1.0" encoding="utf-8"?>
<unattend xmlns="urn:schemas-microsoft-com:unattend">
  <settings pass="windowsPE">
    <component name="Microsoft-Windows-International-Core-WinPE" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
      <SetupUILanguage><UILanguage>en-US</UILanguage></SetupUILanguage>
      <InputLocale>en-US</InputLocale><SystemLocale>en-US</SystemLocale><UILanguage>en-US</UILanguage><UserLocale>en-US</UserLocale>
    </component>
    <component name="Microsoft-Windows-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
      <RunSynchronous>
        <RunSynchronousCommand wcm:action="add"><Order>1</Order><Path>reg add HKLM\SYSTEM\Setup\LabConfig /v BypassTPMCheck /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>
        <RunSynchronousCommand wcm:action="add"><Order>2</Order><Path>reg add HKLM\SYSTEM\Setup\LabConfig /v BypassSecureBootCheck /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>
        <RunSynchronousCommand wcm:action="add"><Order>3</Order><Path>reg add HKLM\SYSTEM\Setup\LabConfig /v BypassCPUCheck /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>
      </RunSynchronous>
      <DiskConfiguration>
        <Disk wcm:action="add">
          <DiskID>0</DiskID><WillWipeDisk>true</WillWipeDisk>
          <CreatePartitions>
            <CreatePartition wcm:action="add"><Order>1</Order><Type>EFI</Type><Size>260</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>2</Order><Type>MSR</Type><Size>16</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>3</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>
          </CreatePartitions>
          <ModifyPartitions>
            <ModifyPartition wcm:action="add"><Order>1</Order><PartitionID>1</PartitionID><Format>FAT32</Format><Label>System</Label></ModifyPartition>
            <ModifyPartition wcm:action="add"><Order>2</Order><PartitionID>3</PartitionID><Format>NTFS</Format><Label>Windows</Label><Letter>C</Letter></ModifyPartition>
          </ModifyPartitions>
        </Disk>
        <WillShowUI>OnError</WillShowUI>
      </DiskConfiguration>
      <ImageInstall><OSImage><InstallFrom><MetaData wcm:action="add"><Key>/IMAGE/INDEX</Key><Value>1</Value></MetaData></InstallFrom><InstallTo><DiskID>0</DiskID><PartitionID>3</PartitionID></InstallTo><WillShowUI>OnError</WillShowUI></OSImage></ImageInstall>
      <UserData><AcceptEula>true</AcceptEula><FullName>UEFILoader Gate</FullName><Organization>UEFILoader</Organization><ProductKey><Key>TX9XD-98N7V-6WMQ6-BX7FG-H8Q99</Key><WillShowUI>Never</WillShowUI></ProductKey></UserData>
    </component>
  </settings>
  <settings pass="specialize">
    <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS"><ComputerName>UEFI-GATE</ComputerName></component>
    <component name="Microsoft-Windows-Deployment" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
      <RunSynchronous>
        <RunSynchronousCommand wcm:action="add"><Order>1</Order><Path>reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System /v EnableLUA /t REG_DWORD /d 0 /f</Path></RunSynchronousCommand>
        <RunSynchronousCommand wcm:action="add"><Order>2</Order><Path>cmd.exe /c mode COM1 BAUD=115200 PARITY=n DATA=8 STOP=1 &amp; echo SETUP_SPECIALIZE&gt;COM1</Path></RunSynchronousCommand>
      </RunSynchronous>
    </component>
  </settings>
  <settings pass="oobeSystem">
    <component name="Microsoft-Windows-International-Core" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS"><InputLocale>en-US</InputLocale><SystemLocale>en-US</SystemLocale><UILanguage>en-US</UILanguage><UserLocale>en-US</UserLocale></component>
    <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
      <TimeZone>Pacific Standard Time</TimeZone>
      <OOBE><HideEULAPage>true</HideEULAPage><HideOnlineAccountScreens>true</HideOnlineAccountScreens><HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE><NetworkLocation>Work</NetworkLocation><ProtectYourPC>3</ProtectYourPC></OOBE>
      <UserAccounts><LocalAccounts><LocalAccount wcm:action="add"><Name>$EscapedUser</Name><Group>Administrators</Group><DisplayName>$EscapedUser</DisplayName><Password><Value>$EscapedPassword</Value><PlainText>true</PlainText></Password></LocalAccount></LocalAccounts></UserAccounts>
      <AutoLogon><Enabled>true</Enabled><LogonCount>2</LogonCount><Username>$EscapedUser</Username><Password><Value>$EscapedPassword</Value><PlainText>true</PlainText></Password></AutoLogon>
      <FirstLogonCommands>
        <SynchronousCommand wcm:action="add"><Order>1</Order><Description>Publish setup readiness</Description><CommandLine>cmd.exe /c mode COM1 BAUD=115200 PARITY=n DATA=8 STOP=1 &amp; echo SETUP_OOBE_READY&gt;COM1</CommandLine></SynchronousCommand>
        <SynchronousCommand wcm:action="add"><Order>2</Order><Description>Install VMware Tools</Description><CommandLine>powershell.exe -NoProfile -ExecutionPolicy Bypass -File A:\Provision.ps1</CommandLine></SynchronousCommand>
      </FirstLogonCommands>
    </component>
  </settings>
</unattend>
"@
    [IO.File]::WriteAllText($AnswerPath, $Answer, [Text.UTF8Encoding]::new($false))
    $Provision = @'
$ErrorActionPreference = 'Stop'
powercfg.exe /change standby-timeout-ac 0 | Out-Null
powercfg.exe /change hibernate-timeout-ac 0 | Out-Null
cmd.exe /c 'mode COM1 BAUD=115200 PARITY=n DATA=8 STOP=1' | Out-Null
$Deadline = [DateTime]::UtcNow.AddHours(2)
$Installer = $null
while ([DateTime]::UtcNow -lt $Deadline -and $null -eq $Installer) {
    foreach ($Letter in 'D'..'Z') {
        foreach ($Name in @('setup.exe', 'setup64.exe')) {
            $Candidate = "$Letter`:\$Name"
            if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
                $Installer = Get-Item -LiteralPath $Candidate
                break
            }
        }
        if ($null -ne $Installer) { break }
    }
    if ($null -eq $Installer) { Start-Sleep -Seconds 10 }
}
if ($null -eq $Installer) {
    cmd.exe /c 'echo SETUP_VMWARE_TOOLS_MISSING>COM1'
    exit 1
}
cmd.exe /c 'echo SETUP_VMWARE_TOOLS_INSTALLING>COM1'
$Process = Start-Process -FilePath $Installer.FullName -ArgumentList @(
    '/S', '/v"/qn REBOOT=ReallySuppress"') -Wait -PassThru
if ($Process.ExitCode -notin @(0, 3010)) {
    cmd.exe /c "echo SETUP_VMWARE_TOOLS_FAILED_$($Process.ExitCode)>COM1"
    exit $Process.ExitCode
}
foreach ($Path in @(
        'C:\Windows\Panther\unattend.xml',
        'C:\Windows\Panther\Unattend\unattend.xml')) {
    Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
}
$CurrentVersion = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$Tpm = Get-Tpm -ErrorAction Stop
[ordered]@{
    SchemaVersion = 1
    CurrentBuild = [string]$CurrentVersion.CurrentBuild
    UBR = [uint64]$CurrentVersion.UBR
    DisplayVersion = [string]$CurrentVersion.DisplayVersion
    VMwareToolsInstallerExitCode = [int]$Process.ExitCode
    TpmPresent = [bool]$Tpm.TpmPresent
    TpmReady = [bool]$Tpm.TpmReady
} | ConvertTo-Json | Set-Content -LiteralPath 'C:\ProgramData\UEFILoaderProvisioned.json' -Encoding ascii
cmd.exe /c 'echo SETUP_VMWARE_TOOLS_READY>COM1'
shutdown.exe /r /t 5 /f /d p:4:1 /c 'Completing disposable VM provisioning.'
'@
    [IO.File]::WriteAllText($ProvisionPath, $Provision, [Text.UTF8Encoding]::new($false))
    $Generator = Join-Path $PSScriptRoot 'New-Fat12AnswerDisk.ps1'
    & $Generator -OutputPath $AnswerDisk -AutounattendPath $AnswerPath `
        -ProvisionPath $ProvisionPath | Out-Null

    $Arguments = @('-c', '-s', "${DiskSizeGiB}GB", '-a', 'nvme',
        '-t', '0', "`"$DiskPath`"")
    $DiskProcess = Start-Process -FilePath $VmwareVdiskManagerPath `
        -ArgumentList $Arguments -Wait -PassThru -WindowStyle Hidden
    if ($DiskProcess.ExitCode -ne 0 -or
        -not (Test-Path -LiteralPath $DiskPath -PathType Leaf)) {
        throw "VMware virtual-disk creation failed with exit code $($DiskProcess.ExitCode)."
    }

    $VmxLines = @(
        '.encoding = "windows-1252"',
        'config.version = "8"',
        'virtualHW.version = "22"',
        "displayName = `"$VmName`"",
        'msg.autoAnswer = "TRUE"',
        'guestOS = "windows11-64"',
        'firmware = "efi"',
        'uefi.secureBoot.enabled = "FALSE"',
        'managedVM.autoAddVTPM = "software"',
        'pciBridge0.present = "TRUE"',
        'pciBridge4.present = "TRUE"',
        'pciBridge4.virtualDev = "pcieRootPort"',
        'pciBridge4.functions = "8"',
        'pciBridge5.present = "TRUE"',
        'pciBridge5.virtualDev = "pcieRootPort"',
        'pciBridge5.functions = "8"',
        'pciBridge6.present = "TRUE"',
        'pciBridge6.virtualDev = "pcieRootPort"',
        'pciBridge6.functions = "8"',
        'pciBridge7.present = "TRUE"',
        'pciBridge7.virtualDev = "pcieRootPort"',
        'pciBridge7.functions = "8"',
        'pciBridge0.pciSlotNumber = "17"',
        'pciBridge4.pciSlotNumber = "21"',
        'pciBridge5.pciSlotNumber = "22"',
        'pciBridge6.pciSlotNumber = "23"',
        'pciBridge7.pciSlotNumber = "24"',
        'numvcpus = "4"',
        'cpuid.coresPerSocket = "2"',
        'memsize = "8192"',
        'vhv.enable = "TRUE"',
        'mks.enable3d = "FALSE"',
        'nvme0.present = "TRUE"',
        'nvme0.pciSlotNumber = "224"',
        'nvme0:0.present = "TRUE"',
        "nvme0:0.fileName = `"$DiskPath`"",
        'sata0.present = "TRUE"',
        'sata0.pciSlotNumber = "35"',
        'sata0:1.present = "TRUE"',
        'sata0:1.deviceType = "cdrom-image"',
        "sata0:1.fileName = `"$([IO.Path]::GetFullPath($WindowsIso))`"",
        'floppy0.present = "TRUE"',
        'floppy0.fileType = "file"',
        "floppy0.fileName = `"$AnswerDisk`"",
        'floppy0.startConnected = "TRUE"',
        'ethernet0.present = "TRUE"',
        'ethernet0.pciSlotNumber = "-1"',
        'ethernet0.connectionType = "nat"',
        'ethernet0.virtualDev = "e1000e"',
        'ethernet0.startConnected = "TRUE"',
        'serial0.present = "TRUE"',
        'serial0.fileType = "file"',
        "serial0.fileName = `"$SerialLog`"",
        'serial0.yieldOnMsrRead = "TRUE"',
        'RemoteDisplay.vnc.enabled = "TRUE"',
        'RemoteDisplay.vnc.ip = "127.0.0.1"',
        'RemoteDisplay.vnc.port = "5909"',
        'usb.present = "TRUE"',
        'usb.pciSlotNumber = "32"',
        'ehci.present = "TRUE"',
        'ehci.pciSlotNumber = "34"',
        'sound.present = "FALSE"',
        'tools.syncTime = "TRUE"',
        'bios.bootOrder = "cdrom,hdd"')
    [IO.File]::WriteAllText($VmxPath, ($VmxLines -join "`r`n") + "`r`n",
        [Text.Encoding]::ASCII)
}
finally {
    if (-not [string]::IsNullOrEmpty($PlainPassword)) {
        $PlainPassword = ('0' * $PlainPassword.Length)
    }
    $NetworkCredential.Password = ''
    if (Test-Path -LiteralPath $TempRoot -PathType Container) {
        $ResolvedTemp = [IO.Path]::GetFullPath($TempRoot)
        if (-not $ResolvedTemp.StartsWith(
                $ResolvedRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Unsafe answer-source cleanup path.'
        }
        Remove-Item -LiteralPath $ResolvedTemp -Recurse -Force
    }
}

[pscustomobject]@{
    Result = 'PASS'
    VmName = $VmName
    VmxPath = $VmxPath
    DiskPath = $DiskPath
    AnswerDisk = $AnswerDisk
    SerialLog = $SerialLog
    UserName = $UserName
}
