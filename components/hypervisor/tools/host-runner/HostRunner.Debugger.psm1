Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-HrBoundedLogTail {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [int]$MaxLines = 80,
        [int]$MaxCharacters = 8192
    )
    if ($MaxLines -lt 1 -or $MaxCharacters -lt 1) {
        throw 'MaxLines and MaxCharacters must be positive.'
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $Info = Get-Item -LiteralPath $Path
    if ($Info.Length -le 0) { return '' }
    # Read only a bounded byte window from the captured end of the file. This
    # avoids Get-Content line buffering, which can allocate or wait forever on
    # a giant unterminated line while a debugger is flooding the log.
    $RequestedBytes = [long]$MaxCharacters * 4
    $WindowBytes = [int][Math]::Min([long]$Info.Length,
        [long][Math]::Min(1048576, [Math]::Max(4096, $RequestedBytes)))
    $Buffer = [byte[]]::new($WindowBytes)
    $Stream = $null
    $ReadTotal = 0
    try {
        $Stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        [void]$Stream.Seek(-$WindowBytes, [IO.SeekOrigin]::End)
        while ($ReadTotal -lt $WindowBytes) {
            $Read = $Stream.Read($Buffer, $ReadTotal, $WindowBytes - $ReadTotal)
            if ($Read -le 0) { break }
            $ReadTotal += $Read
        }
    }
    finally {
        if ($null -ne $Stream) { $Stream.Dispose() }
    }
    if ($ReadTotal -le 0) { return '' }
    $Text = [Text.Encoding]::UTF8.GetString($Buffer, 0, $ReadTotal)
    $Lines = @($Text -split "`r?`n")
    if ($Lines.Count -gt $MaxLines) {
        $Text = @($Lines | Select-Object -Last $MaxLines) -join [Environment]::NewLine
    }
    if ($Text.Length -gt $MaxCharacters) {
        return $Text.Substring($Text.Length - $MaxCharacters)
    }
    return $Text
}

function Get-HrDebuggerStickyState {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $Value = (Get-Content -Raw -LiteralPath $Path -ErrorAction SilentlyContinue).Trim()
    if ($Value -in @('FATAL', 'FLOOD')) { return $Value }
    return $null
}

function Set-HrDebuggerStickyState {
    param(
        [string]$Path,
        [Parameter(Mandatory=$true)][ValidateSet('FATAL', 'FLOOD')]
        [string]$State
    )
    if ([string]::IsNullOrWhiteSpace($Path)) { return }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    Set-Content -LiteralPath $Path -Value $State -Encoding ascii
}

function Get-HrDebuggerLogTextForScan {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][long]$Length,
        [Parameter(Mandatory=$true)][long]$MaximumLogBytes
    )
    if ($Length -le 0 -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    if ($Length -gt $MaximumLogBytes) {
        return Get-HrBoundedLogTail -Path $Path -MaxCharacters 4096
    }
    # Read exactly the captured length in fixed-size chunks. The returned
    # text is bounded by MaximumLogBytes, and a concurrent writer cannot move
    # this scan past the length observed by the caller.
    $Stream = $null
    $Remaining = $Length
    $Buffer = [byte[]]::new(8192)
    $TextBuilder = [Text.StringBuilder]::new()
    try {
        $Stream = [IO.File]::Open($Path, [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        while ($Remaining -gt 0) {
            $ToRead = [int][Math]::Min($Buffer.Length, $Remaining)
            $Read = $Stream.Read($Buffer, 0, $ToRead)
            if ($Read -le 0) { break }
            $Remaining -= $Read
            [void]$TextBuilder.Append(
                [Text.Encoding]::UTF8.GetString($Buffer, 0, $Read))
        }
    }
    finally {
        if ($null -ne $Stream) { $Stream.Dispose() }
    }
    return $TextBuilder.ToString()
}

function Get-HrDebuggerStartupState {
    param(
        [Parameter(Mandatory=$true)][string]$StandardOutputPath,
        [Parameter(Mandatory=$true)][string]$StandardErrorPath,
        [Parameter(Mandatory=$true)][bool]$ProcessExited,
        [Parameter(Mandatory=$true)][long]$MaximumLogBytes,
        [string]$StickyStatePath
    )
    $OutputLength = if (Test-Path -LiteralPath $StandardOutputPath -PathType Leaf) {
        (Get-Item -LiteralPath $StandardOutputPath).Length
    } else { 0 }
    $ErrorLength = if (Test-Path -LiteralPath $StandardErrorPath -PathType Leaf) {
        (Get-Item -LiteralPath $StandardErrorPath).Length
    } else { 0 }
    $Length = $OutputLength + $ErrorLength
    $OutputTail = Get-HrBoundedLogTail -Path $StandardOutputPath -MaxCharacters 4096
    $ErrorTail = Get-HrBoundedLogTail -Path $StandardErrorPath -MaxCharacters 4096
    $Tail = @(@($OutputTail, $ErrorTail) | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_)
        }) -join [Environment]::NewLine
    $StickyState = Get-HrDebuggerStickyState -Path $StickyStatePath
    $FatalPattern = '(?im)^kd: Could not write to pipe, [0-9]+\s*$|(?im)^(Unable|Failed) to open .*pipe'
    $FatalText = @(
        Get-HrDebuggerLogTextForScan -Path $StandardOutputPath -Length $OutputLength -MaximumLogBytes $MaximumLogBytes
        Get-HrDebuggerLogTextForScan -Path $StandardErrorPath -Length $ErrorLength -MaximumLogBytes $MaximumLogBytes
    ) -join [Environment]::NewLine
    if ($StickyState -in @('FATAL', 'FLOOD')) {
        $State = $StickyState
    } elseif ($FatalText -match $FatalPattern) {
        $State = 'FATAL'
        Set-HrDebuggerStickyState -Path $StickyStatePath -State $State
    } elseif ($Length -gt $MaximumLogBytes) {
        $State = 'FLOOD'
        Set-HrDebuggerStickyState -Path $StickyStatePath -State $State
    } elseif ($ProcessExited) {
        $State = 'EXITED'
    } else {
        # Preserve marker ordering so callers can distinguish initial
        # connection failure from a target that disconnected after connecting.
        $ConnectionMatches = @([regex]::Matches(
            $Tail,
            '(?im)^Connected to Windows .* target at .*ptr64 TRUE\s*$'))
        $DisconnectedMatches = @([regex]::Matches(
            $Tail,
            '(?im)no_debuggee|Waiting to reconnect|not connected'))
        $LastConnection = if ($ConnectionMatches.Count -gt 0) {
            $ConnectionMatches[-1].Index
        } else { -1 }
        $LastDisconnect = if ($DisconnectedMatches.Count -gt 0) {
            $DisconnectedMatches[-1].Index
        } else { -1 }
        if ($LastConnection -gt $LastDisconnect) {
            $State = 'CONNECTED'
        } elseif ($LastDisconnect -ge 0) {
            $State = 'DISCONNECTED'
        } else {
            $State = 'WAITING'
        }
    }
    return [pscustomobject]@{ State = $State; Length = [long]$Length; Tail = $Tail }
}

function Test-HrDebuggerHealthOutput {
    param(
        [Parameter(Mandatory=$true)][string]$Text,
        [Parameter(Mandatory=$true)][string]$Marker
    )
    if ($Text -match '(?im)no_debuggee|Could not write to pipe|not connected') { return $false }
    return $Text.Contains($Marker) -and
        $Text -match '(?im)^Windows (10|11) Kernel Version [0-9]+ .* x64\s*$'
}

function Test-HrSafeDebuggerInput {
    param([Parameter(Mandatory=$true)][string]$Value, [string]$Name = 'input')
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value.Contains("`r") -or
        $Value.Contains("`n") -or $Value.Contains('"')) {
        throw "$Name contains unsafe debugger command-line characters."
    }
}

function Get-HrDebuggerRunLogState {
    param(
        [Parameter(Mandatory=$true)][string]$LogPath,
        [Parameter(Mandatory=$true)][string]$RunId,
        [long]$MaximumLogBytes = 8388608
    )
    if ($MaximumLogBytes -lt 65536) {
        throw 'MaximumLogBytes must be at least 65536.'
    }
    if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
        return [pscustomobject]@{ State = 'MISSING'; Length = 0; MarkerFound = $false; FatalFound = $false; Tail = '' }
    }
    $Length = [long](Get-Item -LiteralPath $LogPath).Length
    if ($Length -gt $MaximumLogBytes) {
        return [pscustomobject]@{
            State = 'FLOOD'
            Length = $Length
            MarkerFound = $false
            FatalFound = $false
            Tail = Get-HrBoundedLogTail -Path $LogPath -MaxCharacters 4096
        }
    }

    $Marker = "HV_RUNNER_BEGIN_$RunId"
    # Current connection state is classified from the bounded stdout/stderr
    # stream. A historical transport-status line must not poison a later run
    # marker in the per-run log.
    $FatalPattern = '(?i)Could not write to pipe, [0-9]+|(?:Unable|Failed) to open .*pipe|\*\*\* Fatal System Error:'
    $MarkerFound = $false
    $FatalFound = $false
    $TailText = ''
    $Stream = $null
    try {
        # Stream exactly the captured length. Never materialize the complete
        # per-run log or follow a writer that grows it after this check.
        $Stream = [IO.File]::Open($LogPath, [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $Remaining = $Length
        $Buffer = [byte[]]::new(8192)
        $Carry = ''
        while ($Remaining -gt 0) {
            $ToRead = [int][Math]::Min($Buffer.Length, $Remaining)
            $Read = $Stream.Read($Buffer, 0, $ToRead)
            if ($Read -le 0) { break }
            $Remaining -= $Read
            $Chunk = [Text.Encoding]::UTF8.GetString($Buffer, 0, $Read)
            $ScanText = $Carry + $Chunk
            if ($ScanText.Contains($Marker)) { $MarkerFound = $true }
            if ($ScanText -match $FatalPattern) { $FatalFound = $true }
            $TailText += $Chunk
            if ($TailText.Length -gt 4096) {
                $TailText = $TailText.Substring($TailText.Length - 4096)
            }
            # Keep enough overlap to catch a token split at a chunk boundary.
            $Carry = if ($ScanText.Length -gt 256) {
                $ScanText.Substring($ScanText.Length - 256)
            } else { $ScanText }
            if ($Remaining -le 0) { break }
        }
        if ($Remaining -gt 0) {
            $FatalFound = $true
            $TailText += "`n[per-run log read ended before captured length]"
        }
    }
    finally {
        if ($null -ne $Stream) { $Stream.Dispose() }
    }
    $State = if ($FatalFound) { 'FATAL' } elseif ($MarkerFound) { 'READY' } else { 'WAITING' }
    return [pscustomobject]@{
        State = $State
        Length = $Length
        MarkerFound = $MarkerFound
        FatalFound = $FatalFound
        Tail = $TailText
    }
}

function ConvertTo-HrDebuggerPathArgument {
    param([Parameter(Mandatory=$true)][string]$Path)
    # Start-Process flattens an argument array into one command line. Quote
    # every file-system argument so spaces cannot change the KD tokenization.
    return '"{0}"' -f $Path
}

function Test-HrCanonicalRunId {
    param([Parameter(Mandatory=$true)][string]$RunId)
    return $RunId -cmatch '^gate[0-9]+-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$'
}

function Get-HrSingleKdArguments {
    <# Pure argument composer for the one directly attached KD process. #>
    param(
        [Parameter(Mandatory=$true)][string]$DebuggerPipe,
        [Parameter(Mandatory=$true)][string]$RunId,
        [Parameter(Mandatory=$true)][string]$SymbolPath,
        [Parameter(Mandatory=$true)][string]$LogPath,
        [Parameter(Mandatory=$true)][string]$CommandFile
    )
    if ($DebuggerPipe -cnotmatch '^\\\\\.\\pipe\\[A-Za-z0-9_-]+$') {
        throw 'DebuggerPipe must be a local named-pipe path.'
    }
    if (-not (Test-HrCanonicalRunId $RunId)) { throw "RunId is not canonical: $RunId" }
    foreach ($Input in @(
            @{ Value = $SymbolPath; Name = 'SymbolPath' },
            @{ Value = $LogPath; Name = 'LogPath' },
            @{ Value = $CommandFile; Name = 'CommandFile' })) {
        Test-HrSafeDebuggerInput -Value $Input.Value -Name $Input.Name
        if (-not [IO.Path]::IsPathRooted($Input.Value)) { throw "$($Input.Name) must be an absolute path." }
    }
    # Microsoft explicitly excludes VMware from resets=0 and reconnect: its
    # named pipe preserves excess bytes across a restart.  Start KD immediately
    # after vmrun creates the server pipe instead of asking KD to reconnect.
    $KernelTransport = "com:pipe,port=$DebuggerPipe,baud=115200"
    return @('-noshell', '-b', '-k', $KernelTransport, '-y',
        (ConvertTo-HrDebuggerPathArgument $SymbolPath), '-logo',
        (ConvertTo-HrDebuggerPathArgument $LogPath), '-cfr',
        (ConvertTo-HrDebuggerPathArgument $CommandFile))
}

function Test-HrDebuggerRunLog {
    param(
        [Parameter(Mandatory=$true)][string]$LogPath,
        [Parameter(Mandatory=$true)][string]$RunId,
        [long]$MaximumLogBytes = 8388608
    )
    $State = Get-HrDebuggerRunLogState -LogPath $LogPath -RunId $RunId `
        -MaximumLogBytes $MaximumLogBytes
    return $State.State -ceq 'READY'
}

Export-ModuleMember -Function @(
    'Get-HrBoundedLogTail', 'Get-HrDebuggerStartupState',
    'Test-HrDebuggerHealthOutput', 'Get-HrDebuggerStickyState',
    'Set-HrDebuggerStickyState', 'Get-HrSingleKdArguments',
    'Get-HrDebuggerRunLogState', 'Test-HrDebuggerRunLog'
)
