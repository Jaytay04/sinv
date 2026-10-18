[CmdletBinding()]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$handoff = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$worktreeRoot = Split-Path -Parent $handoff
$mainRoot = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$runtimeRoot = Join-Path (Join-Path $handoff 'reports') 'f6\runtime'
$testId = [Guid]::NewGuid().ToString('N')
$fixtureRoot = Join-Path $runtimeRoot ('root-boundaries.' + $testId)
$reportPath = Join-Path $runtimeRoot ('root-boundaries.' + $testId + '.json')
$records = [Collections.Generic.List[object]]::new()

function Get-ProductionFunction([string]$Path, [string]$Name) {
    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($Path, [ref]$tokens, [ref]$parseErrors)
    if (@($parseErrors).Count -gt 0) {
        throw ('PowerShell syntax check failed for ' + $Path + ': ' + ($parseErrors.Message -join '; '))
    }
    $definitions = @($ast.FindAll({
        param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name
    }, $true))
    if ($definitions.Count -ne 1) { throw ('Expected one production function ' + $Name + ' in ' + $Path) }
    return [scriptblock]::Create($definitions[0].Extent.Text)
}

function Assert-Pass([string]$Name, [scriptblock]$Body) {
    & $Body
    $records.Add([ordered]@{ name = $Name; pass = $true })
}

function Assert-Reject([string]$Name, [scriptblock]$Body, [string]$ExpectedMessage) {
    $message = $null
    try { & $Body } catch { $message = $_.Exception.Message }
    if ([string]::IsNullOrWhiteSpace($message) -or $message -notmatch $ExpectedMessage) {
        throw ('Boundary rejection test failed: ' + $Name + '; actual=' + $message)
    }
    $records.Add([ordered]@{ name = $Name; pass = $true; error = $message })
}

function Get-Sha256Bytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
}

[void][IO.Directory]::CreateDirectory($runtimeRoot)
[void][IO.Directory]::CreateDirectory($fixtureRoot)
try {
    $renderPath = Join-Path $PSScriptRoot 'render-active.ps1'
    $expandPath = Join-Path $PSScriptRoot 'expand-phase-evidence.ps1'
    foreach ($path in @($renderPath, $expandPath)) {
        $tokens = $null
        $parseErrors = $null
        [void][Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors)
        if (@($parseErrors).Count -gt 0) { throw ('Syntax check failed for ' + $path + ': ' + ($parseErrors.Message -join '; ')) }
        $records.Add([ordered]@{ name = ('syntax-' + [IO.Path]::GetFileName($path)); pass = $true })
    }

    $mainReports = Join-Path $fixtureRoot 'main\spoof-handoff\reports'
    $worktreeReports = Join-Path $fixtureRoot 'worktree\spoof-handoff\reports'
    $externalRoot = Join-Path $fixtureRoot 'external'
    foreach ($directory in @($mainReports, $worktreeReports, $externalRoot)) {
        [void][IO.Directory]::CreateDirectory($directory)
    }
    $mainInput = Join-Path $mainReports 'main.json'
    $worktreeInput = Join-Path $worktreeReports 'worktree.json'
    $externalInput = Join-Path $externalRoot 'external.json'
    foreach ($path in @($mainInput, $worktreeInput, $externalInput)) {
        [IO.File]::WriteAllText($path, '{}', [Text.UTF8Encoding]::new($false))
    }

    . (Get-ProductionFunction $renderPath 'Assert-RenderPath')
    . (Get-ProductionFunction $renderPath 'Assert-ExactOutputDirectory')
    Assert-Pass 'renderer-accepts-main-report-input' { Assert-RenderPath $mainInput @($mainReports, $worktreeReports) | Out-Null }
    Assert-Pass 'renderer-accepts-worktree-report-input' { Assert-RenderPath $worktreeInput @($mainReports, $worktreeReports) | Out-Null }
    Assert-Reject 'renderer-rejects-external-input' { Assert-RenderPath $externalInput @($mainReports, $worktreeReports) | Out-Null } 'escapes its allowed roots'
    Assert-Reject 'renderer-rejects-missing-input' { Assert-RenderPath (Join-Path $worktreeReports 'missing.json') @($mainReports, $worktreeReports) | Out-Null } 'Missing VM render input'
    Assert-Pass 'renderer-accepts-exact-worktree-output' { Assert-ExactOutputDirectory $runtimeRoot $runtimeRoot | Out-Null }
    Assert-Reject 'renderer-rejects-main-output' { Assert-ExactOutputDirectory (Join-Path $mainRoot 'spoof-handoff\reports\f6\runtime') $runtimeRoot | Out-Null } 'worktree f6 runtime'

    . (Get-ProductionFunction $expandPath 'Assert-PhaseInputPath')
    . (Get-ProductionFunction $expandPath 'Assert-PhaseOutputPath')
    Assert-Pass 'expander-accepts-main-report-metadata' { Assert-PhaseInputPath $mainInput @($mainReports, $worktreeReports) | Out-Null }
    Assert-Pass 'expander-accepts-worktree-report-metadata' { Assert-PhaseInputPath $worktreeInput @($mainReports, $worktreeReports) | Out-Null }
    Assert-Reject 'expander-rejects-external-metadata' { Assert-PhaseInputPath $externalInput @($mainReports, $worktreeReports) | Out-Null } 'escapes its allowed roots'
    Assert-Pass 'expander-accepts-runtime-child-output' { Assert-PhaseOutputPath (Join-Path $runtimeRoot 'expanded\phase') $runtimeRoot $worktreeRoot | Out-Null }
    Assert-Reject 'expander-rejects-main-output' { Assert-PhaseOutputPath (Join-Path $mainRoot 'spoof-handoff\reports\f6\runtime\expanded') $runtimeRoot $worktreeRoot | Out-Null } 'under worktree spoof-handoff'
    Assert-Reject 'expander-rejects-runtime-prefix-sibling' { Assert-PhaseOutputPath ($runtimeRoot + '-escape') $runtimeRoot $worktreeRoot | Out-Null } 'under worktree spoof-handoff'

    Add-Type -AssemblyName System.IO.Compression
    $payload = [Text.Encoding]::UTF8.GetBytes('{"syntheticFixtureOnly":true}')
    $zipPath = Join-Path $fixtureRoot 'valid.zip'
    $zipStream = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $zip = [IO.Compression.ZipArchive]::new($zipStream, [IO.Compression.ZipArchiveMode]::Create, $false)
        try {
            $entryStream = $zip.CreateEntry('f.0001.json').Open()
            try { $entryStream.Write($payload, 0, $payload.Length) } finally { $entryStream.Dispose() }
        } finally { $zip.Dispose() }
    } finally { $zipStream.Dispose() }
    $zipBytes = [IO.File]::ReadAllBytes($zipPath)
    $zipSha = Get-Sha256Bytes $zipBytes
    $entrySha = Get-Sha256Bytes $payload
    $fileRef = [ordered]@{
        archiveLeaf = 'f.0001.json'
        guestPath = 'C:\synthetic-unit-fixture.json'
        sha256 = $entrySha
        bytes = $payload.Length
    }
    $metadata = [ordered]@{
        schema = 'combined-vm-reader-phase.v1'
        status = 'PASS'
        capturedFiles = @($fileRef)
        rawEvidenceBundle = [ordered]@{
            format = 'zip'
            bytes = $zipBytes.Length
            sha256 = $zipSha
            fileCount = 1
            base64 = [Convert]::ToBase64String($zipBytes)
        }
    }
    $metadataPath = Join-Path $fixtureRoot 'metadata.json'
    [IO.File]::WriteAllText($metadataPath, ($metadata | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $expandedRoot = Join-Path $fixtureRoot 'expanded'
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $expandOutput = @(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $expandPath -MetadataPath $metadataPath -OutputDirectory $expandedRoot 2>&1 | ForEach-Object { [string]$_ })
        $expandExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    $expandedFile = Join-Path $expandedRoot 'f.0001.json'
    if ($expandExit -ne 0 -or -not (Test-Path -LiteralPath $expandedFile) -or
        (Get-FileHash -LiteralPath $expandedFile -Algorithm SHA256).Hash -cne $entrySha) {
        throw ('Valid ZIP extraction failed: exit=' + $expandExit + '; output=' + ($expandOutput -join ' | '))
    }
    $records.Add([ordered]@{ name = 'expander-round-trips-valid-hash-pinned-zip-under-worktree'; pass = $true })

    $fixtureMetadata = $metadata | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $fixtureMetadata.schema = 'combined-vm-fixture-correction-action.v1'
    $fixtureMetadataPath = Join-Path $fixtureRoot 'fixture-correction-metadata.json'
    [IO.File]::WriteAllText($fixtureMetadataPath, ($fixtureMetadata | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $fixtureExpandedRoot = Join-Path $fixtureRoot 'fixture-correction-expanded'
    try {
        $ErrorActionPreference = 'Continue'
        $fixtureExpandOutput = @(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $expandPath -MetadataPath $fixtureMetadataPath -OutputDirectory $fixtureExpandedRoot 2>&1 | ForEach-Object { [string]$_ })
        $fixtureExpandExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    $fixtureExpandedFile = Join-Path $fixtureExpandedRoot 'f.0001.json'
    if ($fixtureExpandExit -ne 0 -or -not (Test-Path -LiteralPath $fixtureExpandedFile) -or
        (Get-FileHash -LiteralPath $fixtureExpandedFile -Algorithm SHA256).Hash -cne $entrySha) {
        throw ('Fixture-correction ZIP extraction failed: exit=' + $fixtureExpandExit + '; output=' + ($fixtureExpandOutput -join ' | '))
    }
    $records.Add([ordered]@{ name = 'expander-round-trips-fixture-correction-action-schema'; pass = $true })

    $unknownMetadata = $metadata | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $unknownMetadata.schema = 'combined-vm-unknown-action.v1'
    $unknownMetadataPath = Join-Path $fixtureRoot 'unknown-schema-metadata.json'
    [IO.File]::WriteAllText($unknownMetadataPath, ($unknownMetadata | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $unknownOutputRoot = Join-Path $fixtureRoot 'unknown-schema-output'
    try {
        $ErrorActionPreference = 'Continue'
        $unknownOutput = @(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $expandPath -MetadataPath $unknownMetadataPath -OutputDirectory $unknownOutputRoot 2>&1 | ForEach-Object { [string]$_ })
        $unknownExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($unknownExit -eq 0 -or (Test-Path -LiteralPath $unknownOutputRoot) -or
        ($unknownOutput -join ' ') -notmatch 'Expected passing combined VM reader-phase or fixture-correction action metadata') {
        throw ('Unknown metadata schema was not rejected before output creation: exit=' + $unknownExit + '; output=' + ($unknownOutput -join ' | '))
    }
    $records.Add([ordered]@{ name = 'expander-rejects-unknown-schema-before-output'; pass = $true })

    $failedFixtureMetadata = $metadata | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $failedFixtureMetadata.schema = 'combined-vm-fixture-correction-action.v1'
    $failedFixtureMetadata.status = 'FAIL'
    $failedFixtureMetadataPath = Join-Path $fixtureRoot 'failed-fixture-metadata.json'
    [IO.File]::WriteAllText($failedFixtureMetadataPath, ($failedFixtureMetadata | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $failedFixtureOutputRoot = Join-Path $fixtureRoot 'failed-fixture-output'
    try {
        $ErrorActionPreference = 'Continue'
        $failedFixtureOutput = @(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $expandPath -MetadataPath $failedFixtureMetadataPath -OutputDirectory $failedFixtureOutputRoot 2>&1 | ForEach-Object { [string]$_ })
        $failedFixtureExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($failedFixtureExit -eq 0 -or (Test-Path -LiteralPath $failedFixtureOutputRoot) -or
        ($failedFixtureOutput -join ' ') -notmatch 'Expected passing combined VM reader-phase or fixture-correction action metadata') {
        throw ('Failed fixture-correction metadata was not rejected before output creation: exit=' + $failedFixtureExit + '; output=' + ($failedFixtureOutput -join ' | '))
    }
    $records.Add([ordered]@{ name = 'expander-rejects-failed-fixture-correction-before-output'; pass = $true })

    try {
        $ErrorActionPreference = 'Continue'
        $overwriteOutput = @(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $expandPath -MetadataPath $metadataPath -OutputDirectory $expandedRoot 2>&1 | ForEach-Object { [string]$_ })
        $overwriteExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($overwriteExit -eq 0 -or ($overwriteOutput -join ' ') -notmatch 'Refusing to overwrite a populated evidence directory' -or
        (Get-FileHash -LiteralPath $expandedFile -Algorithm SHA256).Hash -cne $entrySha) {
        throw ('Expander overwrite guard failed: exit=' + $overwriteExit + '; output=' + ($overwriteOutput -join ' | '))
    }
    $records.Add([ordered]@{ name = 'expander-refuses-populated-output'; pass = $true })

    $tampered = $metadata | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $tampered.rawEvidenceBundle.sha256 = ('0' * 64)
    $tamperedPath = Join-Path $fixtureRoot 'tampered-metadata.json'
    [IO.File]::WriteAllText($tamperedPath, ($tampered | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $tamperedOutputRoot = Join-Path $fixtureRoot 'tampered'
    try {
        $ErrorActionPreference = 'Continue'
        $tamperOutput = @(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $expandPath -MetadataPath $tamperedPath -OutputDirectory $tamperedOutputRoot 2>&1 | ForEach-Object { [string]$_ })
        $tamperExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($tamperExit -eq 0 -or (Test-Path -LiteralPath $tamperedOutputRoot)) {
        throw ('Tampered archive was not rejected before output creation: ' + ($tamperOutput -join ' | '))
    }
    $records.Add([ordered]@{ name = 'expander-rejects-tampered-zip-before-output'; pass = $true })

    $unsafe = $metadata | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $unsafe.capturedFiles[0].archiveLeaf = '../escape.json'
    $unsafePath = Join-Path $fixtureRoot 'unsafe-metadata.json'
    [IO.File]::WriteAllText($unsafePath, ($unsafe | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    $unsafeOutputRoot = Join-Path $fixtureRoot 'unsafe-output'
    try {
        $ErrorActionPreference = 'Continue'
        $unsafeOutput = @(& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $expandPath -MetadataPath $unsafePath -OutputDirectory $unsafeOutputRoot 2>&1 | ForEach-Object { [string]$_ })
        $unsafeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($unsafeExit -eq 0 -or (Test-Path -LiteralPath $unsafeOutputRoot)) {
        throw ('Expander traversal guard failed to reject unsafe archive path: ' + ($unsafeOutput -join ' | '))
    }
    $records.Add([ordered]@{ name = 'expander-rejects-traversal-name-before-output'; pass = $true })
} finally {
    if (Test-Path -LiteralPath $fixtureRoot) { Remove-Item -LiteralPath $fixtureRoot -Recurse -Force }
}

$report = [ordered]@{
    schema = 'combined-vm-runtime-root-tests.v1'
    status = 'PASS'
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    tests = @($records.ToArray())
    productionSources = @(
        [ordered]@{ path = $renderPath; sha256 = (Get-FileHash -LiteralPath $renderPath -Algorithm SHA256).Hash },
        [ordered]@{ path = $expandPath; sha256 = (Get-FileHash -LiteralPath $expandPath -Algorithm SHA256).Hash }
    )
    vmOrHostDeviceMutationExecuted = $false
}
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
[pscustomobject]@{
    status = 'PASS'
    tests = $records.Count
    reportPath = $reportPath
    reportSha256 = (Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash
} | ConvertTo-Json
