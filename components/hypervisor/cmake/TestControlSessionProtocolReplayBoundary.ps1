param(
    [Parameter(Mandatory=$true)][string]$ModelPath,
    [Parameter(Mandatory=$true)][string]$ModelHeader,
    [Parameter(Mandatory=$true)][string]$KmdfPath,
    [Parameter(Mandatory=$true)][string]$UnitTestPath,
    [Parameter(Mandatory=$true)][string]$ProbePath,
    [Parameter(Mandatory=$true)][string]$GateScript,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath,
    [Parameter(Mandatory=$true)][string]$TestsCMake,
    [Parameter(Mandatory=$true)][string]$LoaderCMake
)

$ErrorActionPreference = 'Stop'
function Assert-Contract([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Tokens([string]$Text, [string[]]$Tokens, [string]$Name) {
    foreach ($Token in $Tokens) {
        Assert-Contract $Text.Contains($Token) "$Name missing: $Token"
    }
}
function Verify([string[]]$Text) {
    $Model = $Text[0]; $Header = $Text[1]; $Kmdf = $Text[2]
    $Unit = $Text[3]; $Probe = $Text[4]; $Gate = $Text[5]
    $Roadmap = $Text[6]; $Doc = $Text[7]; $Tests = $Text[8]
    $Loader = $Text[9]
    $Tokens = $null; $Errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput(
        $Gate, [ref]$Tokens, [ref]$Errors)
    Assert-Contract ($Errors.Count -eq 0) 'Gate 53 does not parse'
    Assert-Tokens $Header @(
        'CONTROL_SESSION_PROTOCOL_ATTEMPT_ALLOWED'
        'CONTROL_SESSION_PROTOCOL_ATTEMPT_REPEATED'
        'CONTROL_SESSION_PROTOCOL_ATTEMPT_INVALID_STATE'
        'ControlSessionProtocolAuthorizeAttempt(uint8_t negotiated)'
    ) 'attempt policy header'
    Assert-Tokens $Model @(
        'if (negotiated == 0U)'
        'if (negotiated == 1U)'
        'return CONTROL_SESSION_PROTOCOL_ATTEMPT_INVALID_STATE'
    ) 'attempt policy'
    Assert-Tokens $Kmdf @(
        'ControlSessionProtocolAuthorizeAttempt((uint8_t)*negotiated)'
        'attempt != CONTROL_SESSION_PROTOCOL_ATTEMPT_ALLOWED'
        'return STATUS_ACCESS_DENIED'
        'input_buffer_length != sizeof(*input)'
        'WdfRequestRetrieveInputBuffer'
    ) 'KMDF replay gate'
    Assert-Contract (
        $Kmdf.IndexOf('ControlSessionProtocolAuthorizeAttempt') -lt
        $Kmdf.IndexOf('input_buffer_length != sizeof(*input)')) `
        'replay authorization must precede framing'
    Assert-Contract (
        $Kmdf.IndexOf('ControlSessionProtocolAuthorizeAttempt') -lt
        $Kmdf.IndexOf('WdfRequestRetrieveInputBuffer')) `
        'replay authorization must precede buffer retrieval'
    Assert-Tokens $Unit @(
        'test_authorizes_only_the_initial_attempt'
        'CONTROL_SESSION_PROTOCOL_ATTEMPT_REPEATED'
        'CONTROL_SESSION_PROTOCOL_ATTEMPT_INVALID_STATE'
    ) 'unit test'
    Assert-Tokens $Probe @(
        'InitialNegotiation=ACCEPTED_1_0'
        'ExactReplay=DENIED_%lu'
        'IncompatibleReplay=DENIED_%lu'
        'error != ERROR_ACCESS_DENIED'
        'PostReplayObservation=EXACT_ACTIVE'
    ) 'replay probe'
    Assert-Tokens $Gate @(
        'gate53-control-session-protocol-replay'
        'control_session_protocol_replay_probe.exe'
        'ExactReplay=DENIED_5'
        'IncompatibleReplay=DENIED_5'
        'Probe=CONTROL_SESSION_PROTOCOL_REPLAY'
        "Invoke-Loader 'unload' 'unload'"
        'tar.exe -a -c -f'
    ) 'Gate 53'
    Assert-Contract ($Gate -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
        'Gate 53 mutates signing, boot, SCM, or reboot state'
    Assert-Tokens $Roadmap @(
        'Runtime V15 Roadmap - Immutable Protocol Negotiation'
        'Gate 53'
        'One run is sufficient'
    ) 'roadmap'
    Assert-Tokens $Doc @(
        'Gate 53: immutable protocol negotiation'
        'gate53-control-session-protocol-replay.ps1'
        'do not retry'
    ) 'operator documentation'
    Assert-Tokens $Tests @(
        'test_control_session_protocol'
        'TestControlSessionProtocolReplayBoundary.ps1'
    ) 'tests CMake'
    Assert-Tokens $Loader @(
        'control_session_protocol_replay_probe'
        'control_session_protocol_replay_probe.c'
    ) 'loader CMake'
}

$Paths = @($ModelPath,$ModelHeader,$KmdfPath,$UnitTestPath,$ProbePath,
    $GateScript,$RoadmapPath,$DocPath,$TestsCMake,$LoaderCMake)
$Text = @($Paths | ForEach-Object {
    [IO.File]::ReadAllText((Resolve-Path $_))
})
Verify $Text
$Mutations = @(
    @{ I=0; T=$Text[0].Replace('if (negotiated == 0U)',
        'if (negotiated <= 1U)') }
    @{ I=2; T=$Text[2].Replace(
        'attempt != CONTROL_SESSION_PROTOCOL_ATTEMPT_ALLOWED',
        'attempt == CONTROL_SESSION_PROTOCOL_ATTEMPT_ALLOWED') }
    @{ I=4; T=$Text[4].Replace('error != ERROR_ACCESS_DENIED',
        'error == ERROR_ACCESS_DENIED') }
    @{ I=5; T=$Text[5].Replace('ExactReplay=DENIED_5',
        'ExactReplay=ACCEPTED') }
)
foreach ($Mutation in $Mutations) {
    Assert-Contract ($Mutation.T -ne $Text[$Mutation.I]) `
        'protocol replay mutation was vacuous'
    $Copy = $Text.Clone(); $Copy[$Mutation.I] = $Mutation.T
    $Rejected = $false
    try { Verify $Copy } catch { $Rejected = $true }
    Assert-Contract $Rejected `
        "protocol replay contract accepted mutation index $($Mutation.I)"
}
Write-Host 'PASS: protocol negotiation is an immutable one-shot transition.'
