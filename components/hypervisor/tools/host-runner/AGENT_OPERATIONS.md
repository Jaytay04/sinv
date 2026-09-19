# Agent operation and live-test coordination

This document is for coding agents that may prepare or run gates on the same
physical host. It separates three concerns:

1. Codex TUI access to Computer Use;
2. ownership of the exact VM resource plus serialized host startup; and
3. the host runner's fixed live-test lifecycle.

Computer Use is not the concurrency mechanism. Every agent must acquire the
appropriate shared VM-test lease before starting, reverting, resetting,
debugging, or submitting work to any local test VM.

The shared helper supports resource-scoped leases only. The opt-in protocol
for at most two isolated VMs is defined in
[`CONCURRENT_VM_TESTING.md`](CONCURRENT_VM_TESTING.md). Do not improvise
per-project lock files or bypass the machine-wide helper.

## Shared mailbox

The machine-wide coordination mailbox is:

```text
C:\Users\jayli\OneDrive\Documents\Projects\AgentSharedMailbox
```

Its `Invoke-AgentVmLease.ps1` command serializes lease mutations with one
machine-wide filesystem lock and stores ownership in independent atomic
resource files. This Hypervisor VM uses
`vm-hypervisor-v3h-gate4`. `-Resource` is mandatory for every operation except
`List`; omission fails closed.

Check status:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force

$Mailbox = 'C:\Users\jayli\OneDrive\Documents\Projects\AgentSharedMailbox'
$LeaseCommand = Join-Path $Mailbox 'Invoke-AgentVmLease.ps1'
$VmResource = 'vm-hypervisor-v3h-gate4'

& $LeaseCommand -Action Status -Resource $VmResource
& $LeaseCommand -Action List
```

Acquire before any live VM operation:

```powershell
$VmLease = & $LeaseCommand `
    -Action Acquire `
    -Resource $VmResource `
    -Agent '<agent-or-thread-id>' `
    -Project '<absolute-project-path>' `
    -RunId '<prepared-run-id>' `
    -LeaseMinutes 120

$VmLeaseToken = $VmLease.LeaseToken
```

If acquisition reports an active owner, STOP. Do not start a second VM, attach
a second debugger, revert a snapshot, or submit a gate. Contact the recorded
owner or wait for release.

Concurrent-capable agents also acquire the short `host-vm-startup` lease
around final capacity checks, start, and readiness. Exact requirements and the
failure-isolation rules are in `CONCURRENT_VM_TESTING.md`.

Renew a long-running investigation before expiry:

```powershell
& $LeaseCommand `
    -Action Renew `
    -Resource $VmResource `
    -LeaseToken $VmLeaseToken `
    -LeaseMinutes 120
```

Release only after the VM is powered off or intentionally preserved and the
debugger state is documented:

```powershell
& $LeaseCommand `
    -Action Release `
    -Resource $VmResource `
    -LeaseToken $VmLeaseToken
```

An expired lease is still evidence, not permission to act blindly. Inspect the
recorded project/run, VMware processes, debugger processes, and VM state first.
Only then may an agent use `-TakeOverExpiredLease` while acquiring.

## Codex TUI Computer Use

`--remote` connects a TUI to an app-server WebSocket. It does not start that
server and it does not enable Computer Use by itself. The app server must be
running, and the session must load a profile that enables `computer_use` and a
working `node_repl`/`@oai/sky` service.

The profile must also point `mcp_servers.node_repl.env.CODEX_CLI_PATH` at a
complete Codex application binary directory. That directory must contain, at
minimum, `codex.exe`, `codex-command-runner.exe`, and
`codex-windows-sandbox-setup.exe`. A stale profile that points at a directory
containing only `codex.exe` produces an
`orchestrator_helper_launch_failed ... program not found` error. Changing the
WebSocket port does not repair that failure.

Validate the configured executable before starting the app server:

```powershell
$ProfilePath = "$env:USERPROFILE\.codex\standalone-cua.config.toml"
$ProfileText = Get-Content -LiteralPath $ProfilePath -Raw
$Match = [regex]::Match(
    $ProfileText,
    "(?m)^CODEX_CLI_PATH\s*=\s*'([^']+)'\s*$")

if (-not $Match.Success) {
    throw "CODEX_CLI_PATH is missing from $ProfilePath"
}

$ProfileCodex = $Match.Groups[1].Value
$ProfileBin = Split-Path -Parent $ProfileCodex
$RequiredHelpers = @(
    'codex.exe'
    'codex-command-runner.exe'
    'codex-windows-sandbox-setup.exe'
)

$MissingHelpers = @($RequiredHelpers | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $ProfileBin $_) -PathType Leaf)
})

if ($MissingHelpers.Count -ne 0) {
    throw "Stale standalone-cua profile; missing from ${ProfileBin}: $($MissingHelpers -join ', ')"
}
```

### After updating Codex

Codex Desktop updates may install a new versioned directory under
`%LOCALAPPDATA%\OpenAI\Codex\bin` without rewriting a manually created
`standalone-cua.config.toml`. After every update, validate both:

```text
%USERPROFILE%\.codex\config.toml
%USERPROFILE%\.codex\standalone-cua.config.toml
```

Use the following host-only PowerShell to select the newest complete installed
Codex directory and update `CODEX_CLI_PATH` in both files. It creates a
timestamped backup before writing:

```powershell
$BinRoot = "$env:LOCALAPPDATA\OpenAI\Codex\bin"
$Required = @(
    'codex.exe'
    'codex-command-runner.exe'
    'codex-windows-sandbox-setup.exe'
    'codex-code-mode-host.exe'
)

$Candidate = Get-ChildItem -LiteralPath $BinRoot -Directory |
    Where-Object {
        $Directory = $_.FullName
        @($Required | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $Directory $_) -PathType Leaf)
        }).Count -eq 0
    } |
    Sort-Object { (Get-Item (Join-Path $_.FullName 'codex.exe')).LastWriteTimeUtc } `
        -Descending |
    Select-Object -First 1

if ($null -eq $Candidate) {
    throw "No complete Codex binary directory exists under $BinRoot"
}

$NewCodexPath = Join-Path $Candidate.FullName 'codex.exe'
$ConfigFiles = @(
    "$env:USERPROFILE\.codex\config.toml"
    "$env:USERPROFILE\.codex\standalone-cua.config.toml"
)
$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Pattern = "(?m)^CODEX_CLI_PATH\s*=\s*'[^']+'\s*$"

foreach ($ConfigFile in $ConfigFiles) {
    $Text = [IO.File]::ReadAllText($ConfigFile)
    if ([regex]::Matches($Text, $Pattern).Count -ne 1) {
        throw "Expected exactly one CODEX_CLI_PATH in $ConfigFile"
    }
    Copy-Item -LiteralPath $ConfigFile `
        -Destination "$ConfigFile.bak-$Timestamp"
    $Replacement = "CODEX_CLI_PATH = '$NewCodexPath'"
    $Updated = [regex]::Replace($Text, $Pattern, $Replacement)
    [IO.File]::WriteAllText(
        $ConfigFile,
        $Updated,
        [Text.UTF8Encoding]::new($false))
}

Write-Host "CODEX_CLI_PATH=$NewCodexPath"
```

Stop and restart the app-server after this change. Existing app-server
processes retain the configuration loaded at startup. Do not change ports or
Tailscale settings to work around a missing sandbox helper.

Use a dedicated loopback port for each independently started app-server
process. Reusing a port already owned by a listener fails at server startup;
changing the TUI port without starting a listener there also fails.

Start one foreground server terminal:

```powershell
$Codex = "$env:APPDATA\npm\codex.cmd"

& $Codex `
    -p standalone-cua `
    app-server `
    --listen 'ws://127.0.0.1:4501'
```

Then connect from another terminal:

```powershell
& $Codex `
    --remote 'ws://127.0.0.1:4501' `
    -p standalone-cua `
    --approve-for-me `
    -C 'C:\path\to\project' `
    resume --last
```

For a clean capability check, start a new session rather than resuming an old
thread and ask the agent to initialize Computer Use and list available Windows
apps. The agent should have a `node_repl` or unified Computer Use tool and must
be able to import `@oai/sky`. A WebSocket connection alone is not proof that the
Computer Use MCP/service loaded.

Before choosing a port:

```powershell
Get-NetTCPConnection -State Listen -LocalPort 4501 `
    -ErrorAction SilentlyContinue
```

No result means no server is listening. If another Codex app server already
owns a port, either connect to that exact server or start a separate server on
a different free loopback port. Separate ports isolate app-server processes;
they do not isolate the physical Windows desktop. Agents sharing one desktop
must still coordinate UI actions and MUST use the VM-test lease for live gates.

If the agent resumes successfully but has no Computer Use tool, create a new
thread with the same profile before diagnosing the port. A resumed historical
thread is not a useful capability probe when its original tool inventory or
plugin state is stale.

## Computer Use safety boundary

Computer Use may operate ordinary Windows application UI, but it must not be
used to type commands into Windows Terminal, PowerShell, Command Prompt, the
Run dialog, or an IDE terminal. Use the agent's shell tool for terminal work.
Do not use Computer Use to automate authentication, UAC, Windows Security, or
credential prompts.

The host runner is preferred for gates because it removes terminal and WinDbg
GUI automation from the critical path. Computer Use is for observation and
ordinary application interaction only; it is not a substitute for the pinned
mailbox protocol or the elevated per-boot broker.

Agents without Computer Use can still prepare and run a complete gate through
the host runner. They need shell access, the configured current-boot broker,
and the shared VM-test lease; they do not need a visible VMware or WinDbg
window.

## Agent gate protocol

1. Read the project's active progress and test instructions.
2. Run host-only builds and static tests first.
3. Prepare the signed, hash-pinned package without touching the VM.
4. Check and acquire `vm-hypervisor-v3h-gate4`; use the additional
   `host-vm-startup` lease for the final capacity/start/readiness boundary.
5. Run `Doctor`; verify the configured VM and snapshot.
6. Use the fixed broker lifecycle with both explicit live authorizations.
7. On PASS, verify that the broker powered off and restored the VM.
8. On any other result, preserve the VM/debugger and record that state. If the
   approved disposition is stop-only, use `PowerOffPreservedVm` with
   `-AuthorizeVmPowerOff`; it cannot restore a snapshot or select another VM.
9. Release the lease only when the resource disposition is unambiguous.

Never run two local VM gates concurrently unless both runners and the shared
lease helper satisfy every opt-in requirement in
`CONCURRENT_VM_TESTING.md`. A runner that has not migrated to an exact VM
resource must fail closed. Available RAM by itself is not proof of isolation
or permission to overlap tests.
