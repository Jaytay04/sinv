[CmdletBinding()]
param(
    [string]$ConfigPath = 'C:\ProgramData\HypersinvRunner\guest-runner.config.json',
    [switch]$Once
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Test-SafeRunId([string]$Value) {
    return $Value -cmatch `
        '^gate[0-9]+-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$'
}

function Test-SafeGateName([string]$Value) {
    return $Value -cmatch '^gate[0-9]+-[a-z0-9-]+\.ps1$'
}

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path)
}

function Test-PathWithin([string]$Root, [string]$Path) {
    $RootFull = (Get-FullPath $Root).TrimEnd('\') + '\'
    return (Get-FullPath $Path).StartsWith(
        $RootFull,
        [StringComparison]::OrdinalIgnoreCase)
}

function Expand-SafeZip([string]$ArchivePath, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination | Out-Null
    $Archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        foreach ($Entry in $Archive.Entries) {
            $Name = $Entry.FullName.Replace('/', '\')
            if ([string]::IsNullOrWhiteSpace($Name) -or
                [IO.Path]::IsPathRooted($Name) -or
                $Name.Contains(':') -or
                @($Name.Split('\') | Where-Object { $_ -eq '..' }).Count -ne 0) {
                throw "Unsafe ZIP entry: $($Entry.FullName)"
            }

            $Target = Join-Path $Destination $Name
            if (-not (Test-PathWithin $Destination $Target)) {
                throw "ZIP entry escaped the staging root: $($Entry.FullName)"
            }
            if ($Name.EndsWith('\')) {
                New-Item -ItemType Directory -Force -Path $Target | Out-Null
                continue
            }

            $Parent = Split-Path -Parent $Target
            New-Item -ItemType Directory -Force -Path $Parent | Out-Null
            $Input = $Entry.Open()
            try {
                $Output = [IO.File]::Open(
                    $Target,
                    [IO.FileMode]::CreateNew,
                    [IO.FileAccess]::Write,
                    [IO.FileShare]::None)
                try { $Input.CopyTo($Output) }
                finally { $Output.Dispose() }
            }
            finally { $Input.Dispose() }
        }
    }
    finally { $Archive.Dispose() }
}

function Assert-StageHashes([string]$Stage) {
    $SumsPath = Join-Path $Stage 'SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $SumsPath -PathType Leaf)) {
        throw 'The package does not contain SHA256SUMS.txt.'
    }

    foreach ($Line in Get-Content -LiteralPath $SumsPath) {
        if ($Line -cnotmatch '^([0-9A-F]{64})  ([A-Za-z0-9_.-]+)$') {
            throw "Invalid checksum line: $Line"
        }
        $Expected = $Matches[1]
        $Name = $Matches[2]
        $Path = Join-Path $Stage $Name
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Checksum artifact is missing: $Name"
        }
        $Actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        if ($Actual -cne $Expected) {
            throw "Checksum mismatch: $Name"
        }
    }
}

function Write-AtomicJson($Value, [string]$Path) {
    $Temporary = "$Path.tmp"
    $Value | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $Temporary -Encoding utf8
    Move-Item -LiteralPath $Temporary -Destination $Path -Force
}

function Publish-Failure(
    [string]$ResponseDirectory,
    [string]$RunId,
    [string]$Message) {
    New-Item -ItemType Directory -Force -Path $ResponseDirectory | Out-Null
    Write-AtomicJson ([ordered]@{
        SchemaVersion = 1
        RunId = $RunId
        Status = 'REJECTED'
        Message = $Message
        CompletedUtc = [DateTime]::UtcNow.ToString('o')
    }) (Join-Path $ResponseDirectory 'result.json')
}

function Invoke-Request($Config, [string]$RequestPath) {
    $Request = Get-Content -Raw -LiteralPath $RequestPath | ConvertFrom-Json
    $RunId = [string]$Request.RunId
    if (-not (Test-SafeRunId $RunId)) {
        throw "Unsafe request RunId: $RunId"
    }
    $ResponseDirectory = Join-Path $Config.MailboxRoot "responses\$RunId"

    try {
        if ($Request.SchemaVersion -ne 1) {
            throw "Unsupported request schema: $($Request.SchemaVersion)"
        }
        if (-not (Test-SafeGateName ([string]$Request.GateScript))) {
            throw "Gate script is not allowlisted: $($Request.GateScript)"
        }
        if ($Request.TimeoutSeconds -lt 30 -or $Request.TimeoutSeconds -gt 7200) {
            throw 'Request timeout is outside the allowed range.'
        }
        if ([string]$Request.PackageRelativePath -cne `
                "artifacts/$RunId/package.zip") {
            throw 'PackageRelativePath is not canonical.'
        }
        if ([string]$Request.PackageSha256 -cnotmatch '^[0-9A-F]{64}$') {
            throw 'PackageSha256 is invalid.'
        }

        $PackagePath = Join-Path $Config.MailboxRoot `
            ([string]$Request.PackageRelativePath).Replace('/', '\')
        if (-not (Test-PathWithin $Config.MailboxRoot $PackagePath) -or
            -not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
            throw 'The package path is absent or escaped the mailbox.'
        }
        $PackageHash = (Get-FileHash -LiteralPath $PackagePath `
            -Algorithm SHA256).Hash
        if ($PackageHash -cne [string]$Request.PackageSha256) {
            throw 'The mailbox package hash does not match the request.'
        }

        $RunRoot = Join-Path $Config.LocalRunRoot $RunId
        if (Test-Path -LiteralPath $RunRoot) {
            throw 'The local RunId has already been used.'
        }
        $ExtractRoot = Join-Path $RunRoot 'extracted'
        $EvidenceRoot = Join-Path $RunRoot 'evidence'
        New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
        Expand-SafeZip $PackagePath $ExtractRoot

        $GateCandidates = @(Get-ChildItem -LiteralPath $ExtractRoot -Recurse `
            -File -Filter ([string]$Request.GateScript))
        if ($GateCandidates.Count -ne 1) {
            throw "Expected one gate script; found $($GateCandidates.Count)."
        }
        $Stage = $GateCandidates[0].Directory.FullName
        Assert-StageHashes $Stage

        foreach ($Required in @(
                'hypervisor_driver.sys',
                'hypervisor_driver.pdb',
                'hypervisor_loader.exe',
                'hypervisor_loader.pdb',
                [string]$Request.GateScript)) {
            if (-not (Test-Path -LiteralPath (Join-Path $Stage $Required) `
                    -PathType Leaf)) {
                throw "Required staged artifact is missing: $Required"
            }
        }

        $Signature = Get-AuthenticodeSignature -LiteralPath `
            (Join-Path $Stage 'hypervisor_driver.sys')
        if ($Signature.Status -ne `
                [Management.Automation.SignatureStatus]::Valid -or
            $Signature.SignerCertificate.Subject -cne 'CN=hypersinv') {
            throw 'Driver signature is not valid for CN=hypersinv.'
        }

        New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
        New-Item -ItemType Directory -Force -Path $ResponseDirectory | Out-Null
        $Stdout = Join-Path $ResponseDirectory 'guest-stdout.txt'
        $Stderr = Join-Path $ResponseDirectory 'guest-stderr.txt'
        $GatePath = Join-Path $Stage ([string]$Request.GateScript)
        $Arguments = @(
            '-NoProfile',
            '-NonInteractive',
            '-ExecutionPolicy', 'Bypass',
            '-File', ('"{0}"' -f $GatePath),
            '-Stage', ('"{0}"' -f $Stage),
            '-EvidenceRoot', ('"{0}"' -f $EvidenceRoot)
        )
        $StartedUtc = [DateTime]::UtcNow
        $Process = Start-Process -FilePath `
            "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
            -ArgumentList $Arguments -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr

        if (-not $Process.WaitForExit([int]$Request.TimeoutSeconds * 1000)) {
            $QuarantinePath = Join-Path $Config.LocalRunRoot 'QUARANTINED.json'
            Write-AtomicJson ([ordered]@{
                SchemaVersion = 1
                RunId = $RunId
                ProcessId = $Process.Id
                Message = 'A gate exceeded its deadline. Revert or inspect the VM before clearing quarantine.'
                CreatedUtc = [DateTime]::UtcNow.ToString('o')
            }) $QuarantinePath
            Write-AtomicJson ([ordered]@{
                SchemaVersion = 1
                RunId = $RunId
                Status = 'HUNG'
                ProcessId = $Process.Id
                Message = 'The gate exceeded its deadline. It was not terminated. Preserve the VM and debugger.'
                StartedUtc = $StartedUtc.ToString('o')
                CompletedUtc = [DateTime]::UtcNow.ToString('o')
            }) (Join-Path $ResponseDirectory 'result.json')
            return 'QUARANTINE'
        }

        $EvidenceArchives = @(Get-ChildItem -LiteralPath $EvidenceRoot `
            -Filter '*.zip' -File -ErrorAction SilentlyContinue)
        foreach ($Archive in $EvidenceArchives) {
            Copy-Item -LiteralPath $Archive.FullName -Destination $ResponseDirectory
        }
        $Status = if ($Process.ExitCode -eq 0) { 'PASS' } else { 'FAIL' }
        Write-AtomicJson ([ordered]@{
            SchemaVersion = 1
            RunId = $RunId
            Status = $Status
            ExitCode = $Process.ExitCode
            PackageSha256 = $PackageHash
            StartedUtc = $StartedUtc.ToString('o')
            CompletedUtc = [DateTime]::UtcNow.ToString('o')
            EvidenceArchives = @($EvidenceArchives | ForEach-Object Name)
        }) (Join-Path $ResponseDirectory 'result.json')
        return $Status
    }
    catch {
        Publish-Failure $ResponseDirectory $RunId $_.Exception.Message
        return 'REJECTED'
    }
}

if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw "Guest-runner configuration not found: $ConfigPath"
}
$Config = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json
if ($Config.SchemaVersion -ne 1) {
    throw "Unsupported guest-runner schema: $($Config.SchemaVersion)"
}
foreach ($Directory in @(
        $Config.MailboxRoot,
        (Join-Path $Config.MailboxRoot 'requests'),
        (Join-Path $Config.MailboxRoot 'responses'),
        $Config.LocalRunRoot)) {
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
}
$QuarantinePath = Join-Path $Config.LocalRunRoot 'QUARANTINED.json'
if (Test-Path -LiteralPath $QuarantinePath -PathType Leaf) {
    throw "The guest runner is quarantined: $QuarantinePath"
}

do {
    $Request = Get-ChildItem -LiteralPath `
        (Join-Path $Config.MailboxRoot 'requests') -Filter '*.json' -File |
        Sort-Object CreationTimeUtc | Select-Object -First 1
    if ($null -eq $Request) {
        if ($Once) { break }
        Start-Sleep -Milliseconds ([int]$Config.PollMilliseconds)
        continue
    }

    $Processing = "$($Request.FullName).processing"
    Move-Item -LiteralPath $Request.FullName -Destination $Processing
    try {
        $Outcome = Invoke-Request $Config $Processing
    }
    catch {
        $RejectedDirectory = Join-Path $Config.MailboxRoot 'rejected'
        New-Item -ItemType Directory -Force -Path $RejectedDirectory | Out-Null
        $Fingerprint = (Get-FileHash -LiteralPath $Processing `
            -Algorithm SHA256).Hash.Substring(0, 16).ToLowerInvariant()
        $RejectedRequest = Join-Path $RejectedDirectory `
            ("request-{0}-{1}.json" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $Fingerprint)
        Move-Item -LiteralPath $Processing -Destination $RejectedRequest
        Write-AtomicJson ([ordered]@{
            SchemaVersion = 1
            Status = 'REJECTED'
            Message = $_.Exception.Message
            Request = [IO.Path]::GetFileName($RejectedRequest)
            CompletedUtc = [DateTime]::UtcNow.ToString('o')
        }) ($RejectedRequest + '.result.json')
        if ($Once) { throw }
        continue
    }
    Move-Item -LiteralPath $Processing -Destination `
        ($Processing -replace '\.processing$', '.completed')
    if ($Outcome -eq 'QUARANTINE') {
        throw 'A gate timed out. The mailbox runner is quarantined until the VM is inspected.'
    }
} while (-not $Once)
