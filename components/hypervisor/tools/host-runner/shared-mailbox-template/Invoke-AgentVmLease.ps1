[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Acquire', 'Status', 'List', 'Renew', 'Release')]
    [string]$Action,

    [string]$Resource,
    [string]$Agent,
    [string]$Project,
    [string]$RunId,
    [string]$LeaseToken,

    [ValidateRange(5, 480)]
    [int]$LeaseMinutes = 120,

    [switch]$TakeOverExpiredLease
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SchemaVersion = 1
$StartupResource = 'host-vm-startup'
$StateRoot = Join-Path $PSScriptRoot 'state'
$ResourceStateRoot = Join-Path $StateRoot 'resources'
$HistoryRoot = Join-Path $PSScriptRoot 'history'
$CoordinationPath = Join-Path $StateRoot 'lease-coordination.lock'

function Assert-ResourceName([string]$Name) {
    if ($Name -cnotmatch '^[a-z0-9][a-z0-9.-]{2,63}$' -or
        ($Name -cne $StartupResource -and
         $Name -cnotmatch '^vm-[a-z0-9][a-z0-9.-]*$')) {
        throw "Resource must be '$StartupResource' or a lowercase vm-* identifier (3-64 characters)."
    }
    return $Name
}

if ($Action -ceq 'List') {
    if (-not [string]::IsNullOrWhiteSpace($Resource)) {
        $Resource = Assert-ResourceName $Resource
    }
}
else {
    if ([string]::IsNullOrWhiteSpace($Resource)) {
        throw 'Resource is required. Use one exact vm-* identifier or host-vm-startup.'
    }
    $Resource = Assert-ResourceName $Resource
}

function Initialize-Mailbox {
    foreach ($Path in @($StateRoot, $ResourceStateRoot, $HistoryRoot)) {
        if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
            New-Item -ItemType Directory -Path $Path | Out-Null
        }
    }
}

function Invoke-WithCoordinationLock([scriptblock]$Operation) {
    $Deadline = [DateTime]::UtcNow.AddSeconds(10)
    $Stream = $null
    while ($null -eq $Stream) {
        try {
            $Stream = [IO.File]::Open(
                $CoordinationPath,
                [IO.FileMode]::OpenOrCreate,
                [IO.FileAccess]::ReadWrite,
                [IO.FileShare]::None)
        }
        catch [IO.IOException] {
            if ([DateTime]::UtcNow -ge $Deadline) {
                throw 'Timed out waiting for the VM-lease coordination lock.'
            }
            Start-Sleep -Milliseconds 50
        }
    }
    try {
        return & $Operation
    }
    finally {
        $Stream.Dispose()
    }
}

function Get-LeasePath([string]$Name) {
    Assert-ResourceName $Name | Out-Null
    return Join-Path $ResourceStateRoot "$Name.json"
}

function Get-ResourceHistoryRoot([string]$Name) {
    Assert-ResourceName $Name | Out-Null
    $Path = Join-Path $HistoryRoot $Name
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
    return $Path
}

function Get-UtcText([DateTime]$Value) {
    return $Value.ToUniversalTime().ToString('o')
}

function Get-TokenHash([string]$Token) {
    if ([string]::IsNullOrWhiteSpace($Token)) {
        throw 'LeaseToken is required.'
    }
    $Bytes = [Text.Encoding]::UTF8.GetBytes($Token)
    $Hasher = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($Hasher.ComputeHash($Bytes))).Replace('-', '')
    }
    finally {
        $Hasher.Dispose()
        [Array]::Clear($Bytes, 0, $Bytes.Length)
    }
}

function ConvertTo-LeaseBytes($Lease) {
    $Json = $Lease | ConvertTo-Json -Depth 5
    return [Text.UTF8Encoding]::new($false).GetBytes($Json + "`n")
}

function Assert-LeaseShape($Lease, [string]$ExpectedResource) {
    $Required = @(
        'SchemaVersion', 'Resource', 'LeaseId', 'TokenHash', 'Agent',
        'Project', 'RunId', 'Machine', 'ProcessId', 'AcquiredUtc',
        'HeartbeatUtc', 'ExpiresUtc', 'State'
    )
    foreach ($Name in $Required) {
        if ($null -eq $Lease.PSObject.Properties[$Name]) {
            throw "The active VM-test lease is malformed: missing $Name. Fail closed."
        }
    }
    if ($Lease.SchemaVersion -ne $SchemaVersion -or
        $Lease.Resource -cne $ExpectedResource -or
        $Lease.LeaseId -cnotmatch '^[0-9a-f]{32}$' -or
        $Lease.TokenHash -cnotmatch '^[0-9A-F]{64}$' -or
        $Lease.State -notin @('ACTIVE', 'RELEASED', 'EXPIRED_TAKEN_OVER')) {
        throw "The active VM-test lease for '$ExpectedResource' has invalid values. Fail closed."
    }
    [DateTime]::Parse(
        [string]$Lease.ExpiresUtc,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind) | Out-Null
    return $Lease
}

function Read-Lease([string]$Name) {
    $Path = Get-LeasePath $Name
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    try {
        $Lease = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    }
    catch {
        throw "The active VM-test lease for '$Name' cannot be parsed. Fail closed: $($_.Exception.Message)"
    }
    return Assert-LeaseShape $Lease $Name
}

function Get-ExistingResources {
    $Names = [Collections.Generic.List[string]]::new()
    if (Test-Path -LiteralPath $ResourceStateRoot -PathType Container) {
        foreach ($File in Get-ChildItem -LiteralPath $ResourceStateRoot -File) {
            if ($File.Extension -cne '.json') {
                continue
            }
            $Name = [IO.Path]::GetFileNameWithoutExtension($File.Name)
            Assert-ResourceName $Name | Out-Null
            $Names.Add($Name)
        }
    }
    return @($Names | Sort-Object -Unique)
}

function Test-LeaseExpired($Lease) {
    $Expiry = [DateTime]::Parse(
        [string]$Lease.ExpiresUtc,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
    return [DateTime]::UtcNow -ge $Expiry
}

function Assert-LeaseOwner($Lease, [string]$Token) {
    $Hash = Get-TokenHash $Token
    if ($Hash -cne [string]$Lease.TokenHash) {
        throw "LeaseToken does not own resource '$($Lease.Resource)'."
    }
}

function Write-NewLease([string]$Name, $Lease) {
    $Path = Get-LeasePath $Name
    $Bytes = ConvertTo-LeaseBytes $Lease
    $Stream = $null
    try {
        $Stream = [IO.FileStream]::new(
            $Path,
            [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write,
            [IO.FileShare]::None)
        $Stream.Write($Bytes, 0, $Bytes.Length)
        $Stream.Flush($true)
    }
    finally {
        if ($null -ne $Stream) { $Stream.Dispose() }
        [Array]::Clear($Bytes, 0, $Bytes.Length)
    }
}

function Rewrite-OwnedLease(
    [string]$Name,
    $ExpectedLease,
    [string]$Token,
    $UpdatedLease
) {
    $Path = Get-LeasePath $Name
    $Stream = [IO.File]::Open(
        $Path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::None)
    try {
        $Reader = [IO.StreamReader]::new(
            $Stream,
            [Text.UTF8Encoding]::new($false),
            $true,
            4096,
            $true)
        try { $CurrentJson = $Reader.ReadToEnd() }
        finally { $Reader.Dispose() }
        $Current = Assert-LeaseShape ($CurrentJson | ConvertFrom-Json) $Name
        if ($Current.LeaseId -cne $ExpectedLease.LeaseId) {
            throw "The active lease for '$Name' changed during the ownership check."
        }
        Assert-LeaseOwner $Current $Token

        $Bytes = ConvertTo-LeaseBytes $UpdatedLease
        try {
            $Stream.Position = 0
            $Stream.SetLength(0)
            $Stream.Write($Bytes, 0, $Bytes.Length)
            $Stream.Flush($true)
        }
        finally {
            [Array]::Clear($Bytes, 0, $Bytes.Length)
        }
    }
    finally {
        $Stream.Dispose()
    }
}

function Move-LeaseToHistory($Lease, [string]$Suffix) {
    $Name = [string]$Lease.Resource
    $Source = Get-LeasePath $Name
    $ResourceHistoryRoot = Get-ResourceHistoryRoot $Name
    $Timestamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff')
    $FileName = "$Timestamp-$($Lease.LeaseId)-$Suffix.json"
    $Destination = Join-Path $ResourceHistoryRoot $FileName
    [IO.File]::Move($Source, $Destination)
    return $Destination
}

function Archive-ExpiredLease([string]$Name, $ExpectedLease) {
    $Path = Get-LeasePath $Name
    $Stream = [IO.File]::Open(
        $Path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::None)
    try {
        $Reader = [IO.StreamReader]::new(
            $Stream,
            [Text.UTF8Encoding]::new($false),
            $true,
            4096,
            $true)
        try { $CurrentJson = $Reader.ReadToEnd() }
        finally { $Reader.Dispose() }
        $Current = Assert-LeaseShape ($CurrentJson | ConvertFrom-Json) $Name
        if ($Current.LeaseId -cne $ExpectedLease.LeaseId -or
            -not (Test-LeaseExpired $Current)) {
            throw "Resource '$Name' changed during expired-lease takeover."
        }
        $Current.State = 'EXPIRED_TAKEN_OVER'
        $Current | Add-Member -NotePropertyName TakenOverUtc `
            -NotePropertyValue (Get-UtcText ([DateTime]::UtcNow)) -Force
        $Bytes = ConvertTo-LeaseBytes $Current
        try {
            $Stream.Position = 0
            $Stream.SetLength(0)
            $Stream.Write($Bytes, 0, $Bytes.Length)
            $Stream.Flush($true)
        }
        finally {
            [Array]::Clear($Bytes, 0, $Bytes.Length)
        }
    }
    finally {
        $Stream.Dispose()
    }
    return Move-LeaseToHistory $Current 'expired'
}

function Get-PublicLease($Lease, [string]$Name) {
    if ($null -eq $Lease) {
        return [pscustomobject]@{
            Resource = $Name
            Status = 'FREE'
        }
    }
    return [pscustomobject]@{
        Resource = $Lease.Resource
        Status = if (Test-LeaseExpired $Lease) { 'EXPIRED' } else { $Lease.State }
        LeaseId = $Lease.LeaseId
        Agent = $Lease.Agent
        Project = $Lease.Project
        RunId = $Lease.RunId
        Machine = $Lease.Machine
        ProcessId = $Lease.ProcessId
        AcquiredUtc = $Lease.AcquiredUtc
        HeartbeatUtc = $Lease.HeartbeatUtc
        ExpiresUtc = $Lease.ExpiresUtc
    }
}

function Get-ConflictText($Lease) {
    $Owner = Get-PublicLease $Lease ([string]$Lease.Resource)
    return "resource $($Owner.Resource) is $($Owner.Status), owned by $($Owner.Agent) for $($Owner.Project), run $($Owner.RunId), until $($Owner.ExpiresUtc)"
}


Initialize-Mailbox

Invoke-WithCoordinationLock {
    switch ($Action) {
        'Status' {
            Get-PublicLease (Read-Lease $Resource) $Resource
            break
        }

        'List' {
            $Names = @(Get-ExistingResources)
            if ($Names.Count -eq 0) {
                [pscustomobject]@{ Resource = '*'; Status = 'FREE' }
                break
            }
            foreach ($Name in $Names) {
                Get-PublicLease (Read-Lease $Name) $Name
            }
            break
        }

        'Acquire' {
            if ([string]::IsNullOrWhiteSpace($Agent)) {
                throw 'Agent is required for Acquire.'
            }
            if ([string]::IsNullOrWhiteSpace($Project) -or
                -not [IO.Path]::IsPathRooted($Project)) {
                throw 'Project must be an absolute path for Acquire.'
            }
            $Project = [IO.Path]::GetFullPath($Project)
            if (-not (Test-Path -LiteralPath $Project -PathType Container)) {
                throw "Project directory does not exist: $Project"
            }
            if ([string]::IsNullOrWhiteSpace($RunId)) {
                $RunId = 'unprepared-investigation'
            }

            $Existing = Read-Lease $Resource
            if ($null -ne $Existing) {
                if (-not (Test-LeaseExpired $Existing)) {
                    throw "Resource '$Resource' is already leased: $(Get-ConflictText $Existing)."
                }
                if (-not $TakeOverExpiredLease) {
                    $Owner = Get-PublicLease $Existing $Resource
                    throw "The '$Resource' lease expired at $($Owner.ExpiresUtc). Inspect its VM/debugger state, then acquire with -TakeOverExpiredLease only if safe."
                }
                Archive-ExpiredLease $Resource $Existing | Out-Null
            }


            $Now = [DateTime]::UtcNow
            $Token = [Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N')
            $Lease = [ordered]@{
                SchemaVersion = $SchemaVersion
                Resource = $Resource
                LeaseId = [Guid]::NewGuid().ToString('N')
                TokenHash = Get-TokenHash $Token
                Agent = $Agent
                Project = $Project
                RunId = $RunId
                Machine = $env:COMPUTERNAME
                ProcessId = $PID
                AcquiredUtc = Get-UtcText $Now
                HeartbeatUtc = Get-UtcText $Now
                ExpiresUtc = Get-UtcText $Now.AddMinutes($LeaseMinutes)
                State = 'ACTIVE'
            }
            try {
                Write-NewLease $Resource $Lease
            }
            catch [IO.IOException] {
                $Owner = Read-Lease $Resource
                if ($null -ne $Owner) {
                    throw "Another agent acquired '$Resource' concurrently: $(Get-ConflictText $Owner)."
                }
                throw
            }

            [pscustomobject]@{
                Resource = $Resource
                Status = 'ACQUIRED'
                LeaseId = $Lease.LeaseId
                LeaseToken = $Token
                ExpiresUtc = $Lease.ExpiresUtc
            }
            break
        }

        'Renew' {
            $Lease = Read-Lease $Resource
            if ($null -eq $Lease) { throw "There is no '$Resource' lease to renew." }
            Assert-LeaseOwner $Lease $LeaseToken
            if ($Lease.State -cne 'ACTIVE') {
                throw "Lease state is $($Lease.State), not ACTIVE."
            }
            if (Test-LeaseExpired $Lease) {
                throw "The '$Resource' lease has expired. Inspect its VM/debugger state before takeover."
            }
            $Now = [DateTime]::UtcNow
            $Lease.HeartbeatUtc = Get-UtcText $Now
            $Lease.ExpiresUtc = Get-UtcText $Now.AddMinutes($LeaseMinutes)
            Rewrite-OwnedLease $Resource $Lease $LeaseToken $Lease
            [pscustomobject]@{
                Resource = $Resource
                Status = 'RENEWED'
                LeaseId = $Lease.LeaseId
                ExpiresUtc = $Lease.ExpiresUtc
            }
            break
        }

        'Release' {
            $Lease = Read-Lease $Resource
            if ($null -eq $Lease) { throw "There is no '$Resource' lease to release." }
            Assert-LeaseOwner $Lease $LeaseToken
            $Lease.State = 'RELEASED'
            $Lease | Add-Member -NotePropertyName ReleasedUtc `
                -NotePropertyValue (Get-UtcText ([DateTime]::UtcNow)) -Force
            Rewrite-OwnedLease $Resource $Lease $LeaseToken $Lease
            $Archive = Move-LeaseToHistory $Lease 'released'
            [pscustomobject]@{
                Resource = $Resource
                Status = 'RELEASED'
                LeaseId = $Lease.LeaseId
                HistoryPath = $Archive
            }
            break
        }
    }
}
