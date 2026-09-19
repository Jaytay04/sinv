[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This is deliberately a source/contract test.  The Guest Variables transport
# is exercised by the host runner against VMware, but its safety invariants
# must remain testable without starting a VM or requiring VMware Tools.
$RunnerRoot = Split-Path -Parent $PSScriptRoot
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $RunnerRoot)

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Get-Source {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required Guest Variables source is missing: $Path"
    }
    return Get-Content -Raw -LiteralPath $Path
}

function Get-GuestInfoLines {
    param([Parameter(Mandatory=$true)][string]$Source)
    return @($Source -split "`r?`n" | Where-Object {
        $_ -match '(?i)GuestInfo|guestinfo|writeVariable|readVariable'
    })
}

function Assert-SourceToken {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Pattern,
        [Parameter(Mandatory=$true)][string]$Message
    )
    Assert-True ($Source -match $Pattern) $Message
}

try {
    $HostRunner = Get-Source (Join-Path $RunnerRoot 'Invoke-UEFILoaderHostRunner.ps1')
    $Core = Get-Source (Join-Path $RunnerRoot 'HostRunner.Core.psm1')
    $GuestInfoPath = Join-Path $RunnerRoot 'guest\Invoke-UEFILoaderGuestInfo.ps1'
    $Guest = Get-Source $GuestInfoPath
    $InstallerPath = Join-Path $RunnerRoot 'guest\Install-UEFILoaderGuestInfo.ps1'
    if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
        $InstallerPath = Join-Path $RunnerRoot 'guest\Install-UEFILoaderGuestMailbox.ps1'
    }
    $Installer = Get-Source $InstallerPath
    $ServiceSourcePath = Join-Path $RunnerRoot `
        'guest\UEFILoaderGuestMailboxService.cs'
    $ServiceSource = Get-Source $ServiceSourcePath
    $ConfigPath = Join-Path $RunnerRoot 'host-runner.config.example.json'
    $Config = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json
    $Windows = $Config.Profiles.Windows

    # Configuration must select the passwordless VMware Guest Variables
    # transport and must not fall back to VMware Guest Operations credentials.
    Assert-True ([string]$Windows.GuestTransport -ceq 'VmwareGuestInfo') `
        'Windows profile must select GuestTransport=VmwareGuestInfo.'
    Assert-True ([string]::IsNullOrWhiteSpace([string]$Windows.GuestCredentialFile)) `
        'Guest Variables transport must not require a Windows guest password.'
    Assert-True ($null -ne $Windows.PSObject.Properties['MailboxCredentialFile'] -and
        -not [string]::IsNullOrWhiteSpace([string]$Windows.MailboxCredentialFile)) `
        'Guest Variables transport must use the stored mailbox HMAC key.'
    Assert-True ($null -eq $Windows.PSObject.Properties['MailboxHostRoot'] -or
        [string]::IsNullOrWhiteSpace([string]$Windows.MailboxHostRoot)) `
        'Guest Variables transport must not depend on a shared-folder mailbox.'

    # Core validation must recognize the exact transport and retain the
    # passwordless invariant when configuration is imported.
    Assert-SourceToken $Core "'VmwareGuestInfo'" `
        'HostRunner.Core does not recognize VmwareGuestInfo.'
    Assert-SourceToken $Core 'GuestTransport.*VmwareGuestInfo' `
        'Core transport validation does not mention VmwareGuestInfo.'

    # Host vmrun accepts the variable name without the guestinfo. prefix;
    # VMware Tools supplies that prefix inside the guest. Keep one stable
    # namespace so host and guest can never drift to different mailboxes.
    foreach ($Token in @(
            "'uefiloader'", 'request\.manifest', 'response\.manifest',
            'request\.chunk', 'response\.chunk', 'worker\.status')) {
        Assert-SourceToken ($HostRunner + "`n" + $Guest) $Token `
            "Guest Variables namespace token is missing: $Token"
    }
    Assert-True ($HostRunner -notmatch '(?i)UHR1|guestinfo\.UHR1|\.req\.c|\.res\.e\.') `
        'Host Guest Variables names use the obsolete UHR1/per-evidence namespace.'
    Assert-True ($Guest -notmatch '(?i)UHR1|guestinfo\.UHR1|\.req\.c|\.res\.e\.') `
        'Guest Guest Variables names use the obsolete UHR1/per-evidence namespace.'

    # VMware variable I/O is the only host-side transport primitive.  Keep
    # these checks line-scoped so Linux's separately authenticated vmrun path
    # may continue to use runProgramInGuest.
    $GuestInfoLines = Get-GuestInfoLines $HostRunner
    Assert-True (@($GuestInfoLines).Count -ge 6) `
        'Host runner has no complete Guest Variables transport implementation.'
    Assert-True (($GuestInfoLines -join "`n") -match '(?i)writeVariable') `
        'Guest Variables transport does not publish through vmrun writeVariable.'
    Assert-True (($GuestInfoLines -join "`n") -match '(?i)readVariable') `
        'Guest Variables transport does not read through vmrun readVariable.'
    Assert-True (($GuestInfoLines -join "`n") -notmatch
        '(?i)-gu\b|-gp\b|runProgramInGuest|Invoke-Expression|Start-Process') `
        'Guest Variables transport exposes guest credentials or arbitrary execution.'
    Assert-SourceToken $HostRunner `
        'Assert-ProfileCredentialsReady[\s\S]*?Test-SharedFolderMailbox[\s\S]*?Test-VmwareGuestInfo' `
        'Credential readiness must exempt both passwordless Windows transports from guest credentials.'
    foreach ($Token in @(
            'Wait-VmwareGuestInfoWorkerReady', 'WorkerHmacSha256',
            'S-1-5-18', 'GuestReadyTimeoutSeconds',
            'fresh authenticated startup heartbeat within')) {
        Assert-SourceToken ($HostRunner + "`n" + $Guest) $Token `
            "Guest Variables authenticated worker-readiness token is missing: $Token"
    }

    # The channel needs explicit bounded framing.  Accept the implementation's
    # descriptive suffix (Size/Bytes/Count) but require all three independent
    # limits to be present and numerically bounded in configuration or source.
    $TransportSource = $HostRunner + "`n" + $Guest + "`n" + $Core
    Assert-SourceToken $TransportSource '(?i)Chunk(Size|Bytes|Length)' `
        'Guest Variables transport has no chunk-size bound.'
    Assert-SourceToken $TransportSource '(?i)Max(imum)?Chunk(s|Count)' `
        'Guest Variables transport has no chunk-count bound.'
    Assert-SourceToken $TransportSource '(?i)(Max(imum)?|Payload|Message).*(Bytes|Size|Length)' `
        'Guest Variables transport has no total-payload bound.'
    $BoundLines = @($TransportSource -split "`r?`n" | Where-Object {
        $_ -match '(?i)(Chunk(Size|Bytes|Length)|Max(imum)?Chunk(s|Count)|Payload|Message).*(=|:|Min|Max|lt|gt|le|ge)'
    })
    Assert-True ($BoundLines.Count -ge 3) `
        'Guest Variables transport does not enforce all framing bounds.'

    # A manifest is the commit marker. Every chunk must be published before
    # the manifest so the guest never consumes a partial request.
    $PublishStart = $HostRunner.IndexOf(
        'function Publish-WindowsGuestInfoRequest',
        [StringComparison]::Ordinal)
    $PublishEnd = $HostRunner.IndexOf(
        'function Wait-WindowsGuestInfoResult',
        [StringComparison]::Ordinal)
    Assert-True ($PublishStart -ge 0 -and $PublishEnd -gt $PublishStart) `
        'Guest Variables request publisher is missing or malformed.'
    $PublishBody = $HostRunner.Substring(
        $PublishStart, $PublishEnd - $PublishStart)
    $ChunkWrite = [regex]::Match($PublishBody,
        '(?is)-Value\s+\$Chunks\[\$Index\]')
    $ManifestWrite = [regex]::Match($PublishBody,
        '(?is)\$ManifestName\s*=.{0,500}-VariableName\s+\$ManifestName')
    Assert-True ($ChunkWrite.Success -and $ManifestWrite.Success) `
        'Guest Variables transport does not expose chunk and manifest publication.'
    Assert-True ($ChunkWrite.Index -lt $ManifestWrite.Index) `
        'Guest Variables transport must publish chunks before manifest-last.'

    # Request and response envelopes are authenticated, and tampering must be
    # rejected before package extraction or evidence consumption.
    foreach ($Token in @(
            'RequestHmacSha256', 'ResponseHmacSha256',
            'HMACSHA256', 'Get-Canonical', 'Test-FixedHexEqual',
            'PackageSha256', 'Get-FileHash', 'Unsafe', 'allowlist')) {
        Assert-SourceToken $TransportSource $Token `
            "Guest Variables authentication/integrity token is missing: $Token"
    }
    Assert-SourceToken $HostRunner '(?i)authentication failed|tamper|hash.*mismatch' `
        'Host runner has no authenticated tamper-rejection failure path.'
    Assert-SourceToken $Guest '(?i)authentication failed|tamper|hash.*mismatch' `
        'Guest runner has no authenticated tamper-rejection failure path.'

    # Request fields and the response are a single shared schema. PackageSize
    # is the byte count; evidence is one bounded evidence.zip stream.
    foreach ($Token in @(
            'PackageSize', 'PackageChunkSize', 'PackageChunkCount',
            'EvidenceSha256', 'EvidenceSize', 'EvidenceChunkSize',
            'EvidenceChunkCount', 'evidence.zip',
            '\$MaxPackageBytes\s*=\s*4MB')) {
        Assert-SourceToken ($HostRunner + "`n" + $Guest) $Token `
            "Guest Variables shared-schema token is missing: $Token"
    }
    Assert-True (($HostRunner + "`n" + $Guest) -notmatch
        '(?i)PackageLength|EvidenceCount|Evidence\.Name|\.res\.e\.') `
        'Guest Variables schema retains obsolete package-length or per-file evidence fields.'
    Assert-SourceToken $HostRunner '(?is)ManifestBytes.{0,250}ToBase64String' `
        'Host must publish base64-encoded JSON manifests.'
    Assert-SourceToken $Guest '(?i)ConvertFrom-Base64Exact' `
        'Guest must decode manifests as bounded canonical base64.'

    foreach ($Token in @(
            'WriteDeadline', 'PickupDeadline', 'Deadline',
            'PollMilliseconds', 'WaitForExit', 'taskkill.exe')) {
        Assert-SourceToken ($HostRunner + "`n" + $Guest) $Token `
            "Guest Variables bounded-wait token is missing: $Token"
    }

    # Package and evidence paths must remain bounded to their run root and to
    # the fixed allowlist; traversal names are not a valid protocol message.
    foreach ($Token in @(
            'Test-PathWithin', 'Path.*canonical', 'allowlisted root files',
            'Unsafe ZIP entry', 'Test-SafeRunId', 'Invoke-WindowsGuestTest.ps1')) {
        Assert-SourceToken $Guest $Token `
            "Guest Variables traversal/allowlist token is missing: $Token"
    }
    Assert-SourceToken $Guest 'IsNullOrWhiteSpace\(\$EvidencePath\)' `
        'Failure responses must accept an absent evidence archive without calling Test-Path on an empty string.'

    # The worker runs under LocalSystem from an automatic Windows service.
    # It depends on VMware Tools and supervises only the fixed worker path,
    # so a powered-off snapshot needs neither autologon nor a guest password.
    foreach ($Token in @(
            'UEFILoaderGuestMailboxService', 'New-Service',
            'StartupType Automatic', "DependsOn 'VMTools'",
            'LocalSystem', 'System.ServiceProcess.dll',
            'failureflag', 'restart/5000')) {
        Assert-SourceToken ($Installer + "`n" + $ServiceSource) $Token `
            "Guest Variables service contract token is missing: $Token"
    }
    foreach ($Token in @(
            'ServiceBase', 'ProcessStartInfo',
            'C:\\ProgramData\\UEFILoaderRunner\\Invoke-UEFILoaderGuestInfo.ps1',
            'C:\\ProgramData\\UEFILoaderRunner\\guest-runner.config.json',
            'CreateNoWindow = true', 'UseShellExecute = false')) {
        Assert-SourceToken $ServiceSource $Token `
            "Guest Variables service supervisor token is missing: $Token"
    }
    Assert-True (($Installer + "`n" + $ServiceSource) -notmatch
        '(?i)AutoAdminLogon|DefaultPassword|New-ScheduledTaskTrigger') `
        'The Windows guest service must not depend on autologon or a scheduled trigger.'
    Assert-True ($Installer -notmatch
        '(?i)Get-Credential|Read-Host|ConvertTo-SecureString') `
        'The Windows guest installer must not prompt for or store a non-empty password.'

    # Compile the service source in a fresh test process so bootstrap does not
    # discover C# syntax or framework-reference errors inside the VM.
    Add-Type -Path $ServiceSourcePath `
        -ReferencedAssemblies @('System.dll', 'System.ServiceProcess.dll') `
        -PassThru | Out-Null

    # Syntax is part of the contract; this catches malformed edits before a
    # live test is attempted.
    foreach ($File in Get-ChildItem -LiteralPath $RunnerRoot -Recurse -File |
            Where-Object { $_.Extension -in @('.ps1', '.psm1') }) {
        $Tokens = $null
        $Errors = $null
        [Management.Automation.Language.Parser]::ParseFile(
            $File.FullName, [ref]$Tokens, [ref]$Errors) | Out-Null
        Assert-True ($Errors.Count -eq 0) "PowerShell parse failed: $($File.Name)"
    }

    Write-Host 'PASS: VMware Guest Variables mailbox contracts verified.'
}
catch {
    Write-Error $_
    exit 1
}
