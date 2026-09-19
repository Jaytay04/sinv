[CmdletBinding()]
param(
    [string]$Stage = 'C:\V3H',
    [string]$EvidenceRoot = 'C:\V3H\evidence'
)

$ErrorActionPreference = 'Stop'
$Driver = Join-Path $Stage 'hypervisor_driver.sys'
$Loader = Join-Path $Stage 'hypervisor_loader.exe'
$ManifestPath = Join-Path $Stage 'runtime-v4-release-manifest.json'
$ManifestHashPath = Join-Path $Stage 'runtime-v4-release-manifest.sha256'
$GateScript =
    Join-Path $Stage 'gate39-runtime-v4-release-candidate-lifecycle.ps1'
$RequiredNames = @(
    'hypervisor_driver.sys'
    'hypervisor_driver.pdb'
    'hypervisor_loader.exe'
    'hypervisor_loader.pdb'
    'gate39-runtime-v4-release-candidate-lifecycle.ps1'
    'runtime-v4-release-manifest.json'
    'runtime-v4-release-manifest.sha256'
)
$Required = @($RequiredNames | ForEach-Object { Join-Path $Stage $_ })
$Missing = @($Required | Where-Object {
    -not (Test-Path -LiteralPath $_ -PathType Leaf)
})
if ($Missing.Count -ne 0) {
    throw "Missing staged artifact(s): $($Missing -join ', ')"
}
$Unexpected = @(Get-ChildItem -LiteralPath $Stage -File | Where-Object {
    $_.Name -notin $RequiredNames
})
if ($Unexpected.Count -ne 0) {
    throw "Unexpected release-stage file(s): $($Unexpected.Name -join ', ')"
}

$Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$Principal = [Security.Principal.WindowsPrincipal]::new($Identity)
if (-not $Principal.IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run from elevated Windows PowerShell in the disposable test VM.'
}

if (-not ('Gate39ProcessorGroups' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class Gate39ProcessorGroups {
    [DllImport("kernel32.dll")]
    public static extern ushort GetActiveProcessorGroupCount();
    [DllImport("kernel32.dll")]
    public static extern uint GetActiveProcessorCount(ushort groupNumber);
}
'@
}

$RunName = 'gate39-runtime-v4-release-candidate-lifecycle-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss')
$RunEvidence = Join-Path $EvidenceRoot $RunName
$SummaryPath = Join-Path $RunEvidence 'summary.txt'
$TranscriptPath = Join-Path $RunEvidence 'console.log'
$ZipPath = "$RunEvidence.zip"
$GateStart = Get-Date
$TranscriptStarted = $false
$Passed = $false
$Failure = $null

function Save-ServiceState([string]$Name) {
    $Output = & sc.exe query hypersinv 2>&1
    $Code = $LASTEXITCODE
    $Output | Out-File -LiteralPath (Join-Path $RunEvidence "$Name.txt") `
        -Encoding utf8
    $Output | Write-Host
    [pscustomobject]@{
        ExitCode = $Code
        Output = ($Output -join [Environment]::NewLine)
    }
}

function Invoke-Loader([string]$Name, [string]$Arguments) {
    $Stdout = Join-Path $RunEvidence "$Name-stdout.txt"
    $Stderr = Join-Path $RunEvidence "$Name-stderr.txt"
    $Process = Start-Process -FilePath $Loader -ArgumentList $Arguments `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
    $OutText = Get-Content -Raw -LiteralPath $Stdout
    $ErrText = Get-Content -Raw -LiteralPath $Stderr
    if ($OutText) { $OutText | Write-Host }
    if ($ErrText) { $ErrText | Write-Host }
    [pscustomobject]@{
        ExitCode = [int]$Process.ExitCode
        Stdout = $OutText
        Stderr = $ErrText
    }
}

function Get-ExactVpidRecords([string]$Text) {
    $Lines = @($Text -split "`r?`n" | Where-Object {
        $_ -like 'VPID CPU: *'
    })
    $Expected = @(
        'VPID CPU: index=0 group=0 number=0 vpid=1 observed=1 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022'
        'VPID CPU: index=1 group=0 number=1 vpid=2 observed=2 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022'
        'VPID CPU: index=2 group=1 number=0 vpid=3 observed=3 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022'
        'VPID CPU: index=3 group=1 number=1 vpid=4 observed=4 owner=1 flags=0x0000001F capabilities=0x0000001F secondary=0x00000022'
    )
    if ($Lines.Count -ne $Expected.Count) {
        throw "Expected four exact VPID records; observed $($Lines.Count)."
    }
    for ($Index = 0; $Index -lt $Expected.Count; $Index++) {
        if ($Lines[$Index] -ne $Expected[$Index]) {
            throw "Unexpected VPID record at index ${Index}: $($Lines[$Index])"
        }
    }
    $Lines
}

function Get-GateFailureEvents {
    $System = @(Get-WinEvent -FilterHashtable @{
        LogName = 'System'; StartTime = $GateStart
    } -ErrorAction SilentlyContinue | Where-Object {
        ($_.ProviderName -eq 'Microsoft-Windows-Kernel-Power' -and
            $_.Id -eq 41 -and $_.Level -le 2) -or
        ($_.ProviderName -eq 'Microsoft-Windows-WHEA-Logger' -and
            $_.Level -le 2)
    })
    $Application = @(Get-WinEvent -FilterHashtable @{
        LogName = 'Application'; StartTime = $GateStart
    } -ErrorAction SilentlyContinue | Where-Object {
        $_.ProviderName -eq 'Application Error' -and
        $_.Id -eq 1000 -and $_.Level -le 2 -and
        $_.Message -match '(?i)\b(hypervisor_loader\.exe|powershell\.exe)\b'
    })
    @($System + $Application)
}

New-Item -ItemType Directory -Force -Path $RunEvidence | Out-Null

try {
    Start-Transcript -LiteralPath $TranscriptPath -Force | Out-Null
    $TranscriptStarted = $true
    "GateStart=$($GateStart.ToString('o'))"
    "Stage=$Stage"
    "RunEvidence=$RunEvidence"

    $Manifest = Get-Content -Raw -LiteralPath $ManifestPath |
        ConvertFrom-Json
    $RecordedManifestHash =
        (Get-Content -Raw -LiteralPath $ManifestHashPath).Trim().ToUpperInvariant()
    $ActualManifestHash =
        (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash
    if ($RecordedManifestHash -notmatch '^[0-9A-F]{64}$' -or
        $RecordedManifestHash -ne $ActualManifestHash) {
        throw 'Runtime V4 release manifest hash verification failed.'
    }
    if ($Manifest.SchemaVersion -ne 2 -or
        $Manifest.CandidateType -ne
            'hypersinv-runtime-v4-research-release-candidate') {
        throw 'Unsupported Runtime V4 release manifest.'
    }
    if ($Manifest.SourceCommit -notmatch '^[0-9a-f]{40}$' -or
        $Manifest.SourceBranch -ne 'feature/hypervisor-core' -or
        $Manifest.SourceCommitPushed -ne $true) {
        throw 'Runtime V4 source identity is malformed or not pushed.'
    }
    if ($Manifest.WdkVersion -ne '10.0.28000.0' -or
        $Manifest.BuildTargetCount -lt 379 -or
        $Manifest.CTestCount -lt 240) {
        throw 'Runtime V4 static-verification evidence is incomplete.'
    }
    if ($Manifest.DriverSigner -ne 'CN=hypersinv' -or
        $Manifest.PrivateKeyIncluded -ne $false) {
        throw 'Runtime V4 signing or private-key declaration is invalid.'
    }

    $EvidenceRequirements = [ordered]@{
        Gate36EvidenceArchiveSha256 =
            '88F44030664E7554B329B1193B12E9620CBA4505551E990A1D8C253E4075EDBA'
        Gate36WinDbgLogSha256 =
            'B6756C26B4C3D581CEC8480E2329C592937BD23AA26013265209C085749919F1'
        Gate37EvidenceArchiveSha256 =
            '413F8598130E3A8C9164A72B8D3525892919F862656A8DB1B80BF34B767E20FA'
        Gate37WinDbgLogSha256 =
            'BE9AA1CA1B5D6C6FCEE0290E3F437E6A038FEBA881FD62BEAD5D4939E82A7EEE'
        Gate38EvidenceArchiveSha256 =
            '31426B484F5C126F58A4C15086742738D7FB9E10447F7C56C55C99D5B799197E'
        Gate38WinDbgLogSha256 =
            'C8E0777D4FDB4F8393A10A0A935D4A28126775309CA8BF487D36E6079E9780CB'
    }
    foreach ($Name in $EvidenceRequirements.Keys) {
        if ($Manifest.$Name -ne $EvidenceRequirements[$Name]) {
            throw "Runtime V4 evidence identity mismatch: $Name"
        }
    }

    $ExpectedArtifactNames = @(
        'hypervisor_driver.sys'
        'hypervisor_driver.pdb'
        'hypervisor_loader.exe'
        'hypervisor_loader.pdb'
        'gate39-runtime-v4-release-candidate-lifecycle.ps1'
    )
    if (@($Manifest.Artifacts).Count -ne $ExpectedArtifactNames.Count) {
        throw 'Runtime V4 manifest artifact count is invalid.'
    }
    foreach ($Name in $ExpectedArtifactNames) {
        $Entries = @($Manifest.Artifacts | Where-Object { $_.Name -eq $Name })
        if ($Entries.Count -ne 1) {
            throw "Runtime V4 manifest must contain exactly one $Name entry."
        }
        $Path = Join-Path $Stage $Name
        $Item = Get-Item -LiteralPath $Path
        $Hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        if ($Entries[0].Sha256 -ne $Hash -or
            [int64]$Entries[0].Size -ne $Item.Length) {
            throw "Runtime V4 release artifact verification failed: $Name"
        }
    }

    $Forbidden = @(Get-ChildItem -LiteralPath $Stage -File | Where-Object {
        $_.Extension -match '(?i)^\.(pfx|p12|key|cer)$'
    })
    if ($Forbidden.Count -ne 0) {
        throw "Release package contains forbidden signing material: $($Forbidden.Name -join ', ')"
    }

    $Signature = Get-AuthenticodeSignature -LiteralPath $Driver
    $Signature | Select-Object Status, StatusMessage,
        @{Name='Signer'; Expression={$_.SignerCertificate.Subject}} |
        Format-List
    if ($Signature.Status -ne
            [Management.Automation.SignatureStatus]::Valid -or
        $Signature.SignerCertificate.Subject -ne 'CN=hypersinv') {
        throw 'Driver signature is not valid for CN=hypersinv in this VM.'
    }

    Get-ComputerInfo |
        Select-Object WindowsProductName, WindowsVersion, OsBuildNumber |
        Format-List
    Get-CimInstance Win32_ComputerSystem |
        Select-Object Manufacturer, Model, NumberOfLogicalProcessors |
        Format-List
    $GroupCount = [Gate39ProcessorGroups]::GetActiveProcessorGroupCount()
    $GroupSizes = @(for ($Group = 0; $Group -lt $GroupCount; $Group++) {
        [pscustomobject]@{
            Group = $Group
            ActiveProcessors =
                [Gate39ProcessorGroups]::GetActiveProcessorCount(
                    [uint16]$Group)
        }
    })
    $GroupSizes | Format-Table -AutoSize
    $TotalProcessors =
        ($GroupSizes | Measure-Object ActiveProcessors -Sum).Sum
    if ($GroupCount -ne 2 -or $TotalProcessors -ne 4 -or
        @($GroupSizes | Where-Object ActiveProcessors -ne 2).Count -ne 0) {
        throw "Expected 2 groups with 2 processors each; groups=$GroupCount total=$TotalProcessors."
    }

    $Recovery = Get-CimInstance Win32_OSRecoveryConfiguration
    if ($Recovery.DebugInfoType -ne 2 -or $Recovery.AutoReboot -or
        -not $Recovery.WriteDebugInfo) {
        throw 'Crash-dump configuration is incorrect.'
    }
    bcdedit.exe /enum '{current}'
    Get-Item -LiteralPath $Required |
        Select-Object FullName, Length, LastWriteTime | Format-List
    Get-FileHash -LiteralPath $Required -Algorithm SHA256 |
        Format-List Path, Hash

    $BeforeService = Save-ServiceState 'service-before'
    if ($BeforeService.ExitCode -ne 1060) {
        throw "Service was present before LOAD (exit $($BeforeService.ExitCode))."
    }

    $Load = Invoke-Loader 'load' 'load'
    $LoadProof =
        "SUCCESS: Hypervisor loaded on $TotalProcessors cores and verified by exact V3 status."
    if ($Load.ExitCode -ne 0 -or -not $Load.Stdout.Contains($LoadProof) -or
        -not $Load.Stdout.Contains('INFO: Exact EPT READY status is verified.')) {
        throw "LOAD was not verified (exit $($Load.ExitCode)). Stop; do not retry."
    }
    $AfterLoad = Save-ServiceState 'service-after-load'
    if ($AfterLoad.ExitCode -ne 0 -or
        -not $AfterLoad.Output.Contains('STATE              : 4  RUNNING')) {
        throw 'Driver service was not running after LOAD.'
    }

    $Status = Invoke-Loader 'authoritative-status' 'status'
    foreach ($Line in @(
        '=== AUTHORITATIVE STATUS ==='
        '  Persistent Runtime: LOADED'
        '  EPT Runtime:        READY'
        '  Combined Readiness: READY'
        '  CR3 Observation:    INACTIVE'
        '  Live Gate Evidence: EXTERNAL (not encoded by runtime status)'
    )) {
        if (-not $Status.Stdout.Contains($Line)) {
            throw "Authoritative status omitted: $Line"
        }
    }
    if ($Status.ExitCode -ne 0 -or
        $Status.Stdout.Contains('Live Gate Evidence: VERIFIED')) {
        throw 'Authoritative status failed or overclaimed live evidence.'
    }

    $Vpid = Invoke-Loader 'vpid-status' 'vpid-status'
    if ($Vpid.ExitCode -ne 0 -or
        -not $Vpid.Stdout.Contains(
            'SUCCESS: Exact ACTIVE VPID state verified on 4 processors.')) {
        throw 'Runtime V4 release candidate did not prove ACTIVE VPID status.'
    }
    [void]@(Get-ExactVpidRecords $Vpid.Stdout)

    $Unload = Invoke-Loader 'unload' 'unload'
    if ($Unload.ExitCode -ne 0 -or
        -not $Unload.Stdout.Contains('SUCCESS: Hypervisor unloaded')) {
        throw "UNLOAD was not verified (exit $($Unload.ExitCode)). Stop; do not retry."
    }
    $AfterUnload = Save-ServiceState 'service-after-unload'
    if ($AfterUnload.ExitCode -ne 1060) {
        throw "Service removal was not confirmed (exit $($AfterUnload.ExitCode))."
    }

    $FailureEvents = @(Get-GateFailureEvents)
    $FailureEvents | Select-Object TimeCreated, ProviderName, Id,
        LevelDisplayName, Message |
        Export-Clixml -LiteralPath (Join-Path $RunEvidence 'failure-events.xml')
    if ($FailureEvents.Count -ne 0) {
        throw 'Crash, WHEA, or application-failure event detected.'
    }
    $Dump = Get-Item -LiteralPath 'C:\Windows\MEMORY.DMP' `
        -ErrorAction SilentlyContinue
    if ($Dump -and
        $Dump.LastWriteTimeUtc -gt $GateStart.ToUniversalTime()) {
        throw 'A new MEMORY.DMP was generated.'
    }

    @(
        'Result=PASS'
        'Probe=RUNTIME_V4_RELEASE_CANDIDATE_LIFECYCLE'
        "SourceCommit=$($Manifest.SourceCommit)"
        "ManifestSha256=$ActualManifestHash"
        'SignatureStatus=Valid'
        'Signer=CN=hypersinv'
        'PersistentRuntime=LOADED'
        'EptRuntime=READY'
        'CombinedReadiness=READY'
        'Cr3Observation=INACTIVE'
        'LiveGateEvidence=EXTERNAL'
        'Vpids=1,2,3,4'
        'VpidIdentity=EXACT_ACTIVE'
        'ServiceAfterUnload=ABSENT'
        'PrivateKeyIncluded=FALSE'
    ) | Set-Content -LiteralPath $SummaryPath -Encoding utf8
    $Passed = $true
    Write-Host 'PASS: Runtime V4 package provenance, signature, exact VPID status, truthful status, and clean lifecycle were verified.'
}
catch {
    $Failure = $_
    @('Result=FAIL', "Error=$($_.Exception.Message)") |
        Set-Content -LiteralPath $SummaryPath -Encoding utf8
    Write-Host $_.Exception.Message -ForegroundColor Red
}
finally {
    try {
        & sc.exe query hypersinv 2>&1 |
            Out-File -LiteralPath (Join-Path $RunEvidence 'service-on-exit.txt') `
                -Encoding utf8
    }
    catch {}
    try {
        & sc.exe qc hypersinv 2>&1 |
            Out-File -LiteralPath `
                (Join-Path $RunEvidence 'service-config-on-exit.txt') `
                -Encoding utf8
    }
    catch {}
    try {
        @(Get-GateFailureEvents) |
            Select-Object TimeCreated, ProviderName, Id, LevelDisplayName,
                Message |
            Export-Clixml -LiteralPath `
                (Join-Path $RunEvidence 'failure-events-on-exit.xml')
    }
    catch {}
    if ($TranscriptStarted) { Stop-Transcript | Out-Null }
    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }
    & tar.exe -a -c -f $ZipPath -C $EvidenceRoot $RunName
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Evidence archive failed: $ZipPath"
    }
    else {
        Write-Host "Evidence: $ZipPath"
    }
}

if ($Failure) { throw $Failure }
if (-not $Passed) { exit 1 }
