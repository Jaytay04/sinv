Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-HrVmrunOperationTail {
    param(
        [Parameter(Mandatory=$true)]
        [ValidateSet('RevertSnapshot', 'Start', 'Stop')]
        [string]$Operation,
        [Parameter(Mandatory=$true)][string]$VmxPath,
        [string]$SnapshotName
    )

    switch ($Operation) {
        'RevertSnapshot' {
            if ([string]::IsNullOrWhiteSpace($SnapshotName)) {
                throw 'RevertSnapshot requires SnapshotName.'
            }
            return @('revertToSnapshot', $VmxPath, $SnapshotName)
        }
        'Start' {
            return @('start', $VmxPath, 'nogui')
        }
        'Stop' {
            return @('stop', $VmxPath, 'hard')
        }
    }
}

function Get-HrVmxMemoryMb {
    param(
        [Parameter(Mandatory=$true)][string]$VmxPath
    )

    if (-not (Test-Path -LiteralPath $VmxPath -PathType Leaf)) {
        throw "Configured VMX not found: $VmxPath"
    }
    $Text = Get-Content -Raw -LiteralPath $VmxPath
    if ($Text -notmatch '(?m)^memsize\s*=\s*"([0-9]+)"\s*$') {
        throw 'The VMX does not expose a numeric memsize.'
    }
    $MemoryMb = 0L
    if (-not [long]::TryParse($Matches[1], [ref]$MemoryMb) -or
        $MemoryMb -lt 256 -or $MemoryMb -gt 1048576) {
        throw "VMX memsize is outside the safe range: $($Matches[1]) MB"
    }
    return $MemoryMb
}

function Get-HrVmxNicState {
    param(
        [Parameter(Mandatory=$true)][string]$VmxPath
    )

    if (-not (Test-Path -LiteralPath $VmxPath -PathType Leaf)) {
        throw "Configured VMX not found: $VmxPath"
    }
    $Text = Get-Content -Raw -LiteralPath $VmxPath
    $Present = @(
        [regex]::Matches($Text, '(?im)^ethernet[0-9]+\.present\s*=\s*"(TRUE|FALSE)"\s*$') |
            ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() }
    )
    $Connected = @(
        [regex]::Matches($Text, '(?im)^ethernet[0-9]+\.startConnected\s*=\s*"(TRUE|FALSE)"\s*$') |
            ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() }
    )
    [pscustomobject]@{
        AdapterCount = $Present.Count
        Present = $Present
        StartConnected = $Connected
        Disconnected = ($Present.Count -eq 0 -or
            @($Present | Where-Object { $_ -eq 'TRUE' }).Count -eq 0) -and
            (@($Connected | Where-Object { $_ -eq 'TRUE' }).Count -eq 0)
    }
}

function Get-HrVmxGraphicsState {
    param(
        [Parameter(Mandatory=$true)][string]$VmxPath
    )

    if (-not (Test-Path -LiteralPath $VmxPath -PathType Leaf)) {
        throw "Configured VMX not found: $VmxPath"
    }
    $Text = Get-Content -Raw -LiteralPath $VmxPath
    if ($Text -notmatch '(?m)^mks\.enable3d\s*=\s*"(TRUE|FALSE)"\s*$') {
        throw 'The VMX does not expose an explicit mks.enable3d setting.'
    }
    $Enable3d = $Matches[1] -ceq 'TRUE'
    if ($Text -notmatch '(?m)^svga\.graphicsMemoryKB\s*=\s*"([0-9]+)"\s*$') {
        throw 'The VMX does not expose numeric svga.graphicsMemoryKB.'
    }
    $GraphicsMemoryKb = 0L
    if (-not [long]::TryParse($Matches[1], [ref]$GraphicsMemoryKb) -or
        $GraphicsMemoryKb -lt 16384 -or $GraphicsMemoryKb -gt 67108864) {
        throw "VMX graphics memory is outside the parseable range: $($Matches[1]) KB"
    }
    $CheckpointGraphicsMemoryKb = $GraphicsMemoryKb
    $CheckpointMatches = @([regex]::Matches($Text,
        '(?m)^vmotion\.svga\.graphicsMemoryKB\s*=\s*"([0-9]+)"\s*$'))
    if ($CheckpointMatches.Count -gt 1) {
        throw 'The VMX contains duplicate vmotion.svga.graphicsMemoryKB settings.'
    }
    if ($CheckpointMatches.Count -eq 1) {
        if (-not [long]::TryParse(
                $CheckpointMatches[0].Groups[1].Value,
                [ref]$CheckpointGraphicsMemoryKb) -or
            $CheckpointGraphicsMemoryKb -lt 16384 -or
            $CheckpointGraphicsMemoryKb -gt 67108864) {
            throw "VMX checkpoint graphics memory is outside the parseable range: $($CheckpointMatches[0].Groups[1].Value) KB"
        }
    }
    return [pscustomobject]@{
        Enable3d = $Enable3d
        GraphicsMemoryKb = $GraphicsMemoryKb
        GraphicsMemoryMb = [math]::Ceiling($GraphicsMemoryKb / 1024)
        CheckpointGraphicsMemoryKb = $CheckpointGraphicsMemoryKb
        CheckpointGraphicsMemoryMb = [math]::Ceiling(
            $CheckpointGraphicsMemoryKb / 1024)
    }
}

function Get-HrVmxSerialPipeState {
    param(
        [Parameter(Mandatory=$true)][string]$VmxPath,
        [Parameter(Mandatory=$true)][string]$DebuggerPipe
    )

    if (-not (Test-Path -LiteralPath $VmxPath -PathType Leaf)) {
        throw "Configured VMX not found: $VmxPath"
    }
    if ($DebuggerPipe -cnotmatch '^\\\\\.\\pipe\\[A-Za-z0-9_-]+$') {
        throw 'DebuggerPipe must be a local named-pipe path.'
    }
    $Text = Get-Content -Raw -LiteralPath $VmxPath
    $ReadSetting = {
        param([string]$Name)
        $Pattern = '(?im)^' + [regex]::Escape($Name) +
            '\s*=\s*"([^"\r\n]+)"\s*$'
        $MatchesForSetting = @([regex]::Matches($Text, $Pattern))
        if ($MatchesForSetting.Count -gt 1) {
            throw "The VMX contains duplicate $Name settings."
        }
        if ($MatchesForSetting.Count -eq 0) { return $null }
        return $MatchesForSetting[0].Groups[1].Value
    }

    $Present = & $ReadSetting 'serial0.present'
    $FileType = & $ReadSetting 'serial0.fileType'
    $FileName = & $ReadSetting 'serial0.fileName'
    $Endpoint = & $ReadSetting 'serial0.pipe.endPoint'
    $TryNoRxLoss = & $ReadSetting 'serial0.tryNoRxLoss'
    return [pscustomobject]@{
        Present = $Present -ceq 'TRUE'
        FileType = $FileType
        FileName = $FileName
        Endpoint = $Endpoint
        TryNoRxLoss = $TryNoRxLoss
        Compatible = $Present -ceq 'TRUE' -and
            $FileType -ceq 'pipe' -and
            -not [string]::IsNullOrWhiteSpace($FileName) -and
            $FileName.Equals($DebuggerPipe,
                [StringComparison]::OrdinalIgnoreCase) -and
            $Endpoint -ceq 'server' -and
            $TryNoRxLoss -ceq 'FALSE'
    }
}

Export-ModuleMember -Function @(
    'Get-HrVmrunOperationTail',
    'Get-HrVmxMemoryMb',
    'Get-HrVmxNicState',
    'Get-HrVmxGraphicsState',
    'Get-HrVmxSerialPipeState'
)
