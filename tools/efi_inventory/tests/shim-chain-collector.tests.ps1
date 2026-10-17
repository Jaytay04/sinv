$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$script:WorkflowScript = Join-Path $PSScriptRoot '..\shim-chain-collector.ps1'
$script:Base = [IO.Path]::GetFullPath((Join-Path $script:RepoRoot '.codex-build\shim_chain_host_tests'))
$script:RunRoot = Join-Path $script:Base ('run-' + [Guid]::NewGuid().ToString('N'))
$script:LoaderBuild = Join-Path $script:RunRoot 'loader-build'
$script:InventoryBuild = Join-Path $script:RunRoot 'inventory-build'
$script:SignedDirectory = Join-Path $script:RunRoot 'signed'
$script:EspRoot = Join-Path $script:RunRoot 'esp'
$script:FixtureDirectory = Join-Path $script:RunRoot 'sources'
$script:Passed = 0
$script:RejectSignaturePath = $null

function Assert-True([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Equal([string]$Expected,[string]$Actual,[string]$Message) {
    if ($Expected -cne $Actual) {
        throw ($Message + [Environment]::NewLine + 'Expected: ' + $Expected + [Environment]::NewLine + 'Actual:   ' + $Actual)
    }
}

function Assert-Fails([scriptblock]$Action,[string]$Needle,[string]$Message) {
    $failed = $false
    try { & $Action }
    catch {
        if ($_.Exception.Message -like ('*' + $Needle + '*')) { $failed = $true }
        else { throw }
    }
    if (-not $failed) { throw ($Message + ' did not fail with expected evidence.') }
}

function New-EfiFixture([string]$Path,[byte]$Marker) {
    $bytes = New-Object byte[] 512
    $pe = 0x80
    $bytes[0] = 0x4D
    $bytes[1] = 0x5A
    [Array]::Copy([BitConverter]::GetBytes([uint32]$pe),0,$bytes,0x3C,4)
    $bytes[$pe] = 0x50
    $bytes[$pe + 1] = 0x45
    $bytes[$pe + 2] = 0
    $bytes[$pe + 3] = 0
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x8664),0,$bytes,$pe + 4,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]1),0,$bytes,$pe + 6,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]0xF0),0,$bytes,$pe + 20,2)
    $optional = $pe + 24
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x20B),0,$bytes,$optional,2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]10),0,$bytes,$optional + 68,2)
    $bytes[511] = $Marker
    [IO.File]::WriteAllBytes($Path,$bytes)
}

function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Pass([string]$Message) {
    Write-Output ('PASS ' + $Message)
    $script:Passed++
}

New-Item -ItemType Directory -Path $script:LoaderBuild -Force | Out-Null
New-Item -ItemType Directory -Path $script:InventoryBuild -Force | Out-Null
New-Item -ItemType Directory -Path $script:FixtureDirectory -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $script:EspRoot 'EFI\Microsoft\Boot') -Force | Out-Null

. $script:WorkflowScript -Mode Status -LoaderBuildDir $script:LoaderBuild `
    -InventoryBuildDir $script:InventoryBuild -SignedDirectory $script:SignedDirectory -LibraryOnly

function Assert-MokAuthenticode([string]$Image,[string]$Certificate) {
    if ($script:RejectSignaturePath -and [IO.Path]::GetFullPath($Image) -ceq [IO.Path]::GetFullPath($script:RejectSignaturePath)) {
        throw "Injected signature mismatch for $Image"
    }
}

function Get-ShimStageSources {
    $records = @()
    foreach ($file in $script:FixtureFiles) { $records += Get-FileRecord $file.Name $file.Path }
    return @{ Files=$records; CertificateThumbprint=$script:ShimCertThumbprint }
}

function Get-ExistingShimStageSources([string]$StageRoot) {
    $records = @()
    foreach ($name in $script:ShimStageNames) { $records += Get-FileRecord $name (Join-Path $StageRoot $name) }
    $sources = @{ Files=$records; CertificateThumbprint=$script:ShimCertThumbprint }
    $manifest = [IO.File]::ReadAllText((Join-Path $StageRoot 'manifest.sha256'),[Text.Encoding]::ASCII)
    if ($manifest -cne (New-ShimManifest $sources)) { throw 'Existing fixture manifest mismatch.' }
    return $sources
}

$certificatePath = Join-Path $script:RepoRoot 'components\hypervisor\hypersinv.cer'
$fixtureFiles = @()
foreach ($item in @(
    @{Name='shimx64.efi'; Marker=0x21},
    @{Name='grubx64.efi'; Marker=0x22},
    @{Name='inventory.efi'; Marker=0x23},
    @{Name='mmx64.efi'; Marker=0x24}
)) {
    $path = Join-Path $script:FixtureDirectory $item.Name
    New-EfiFixture $path ([byte]$item.Marker)
    $fixtureFiles += @{Name=$item.Name;Path=$path}
}
$certificateFixture = Join-Path $script:FixtureDirectory 'MOK.cer'
Copy-Item -LiteralPath $certificatePath -Destination $certificateFixture
$fixtureFiles += @{Name='MOK.cer';Path=$certificateFixture}
$script:FixtureFiles = $fixtureFiles

$bootManager = Join-Path $script:EspRoot 'EFI\Microsoft\Boot\bootmgfw.efi'
[IO.File]::WriteAllBytes($bootManager,[Text.Encoding]::ASCII.GetBytes('unchanged Windows Boot Manager fixture'))
$bootHash = Get-Hash $bootManager

try {
    Stage-ShimChainToRoot $script:EspRoot 'TEST-ROOT' $script:EspRoot | Out-Null
    $stage = Join-Path (Join-Path $script:EspRoot 'EFI') $script:ShimDirectoryName
    $expectedNames = @($script:ShimOwnedNames | Sort-Object) -join ','
    $actualNames = @(Get-ChildItem -LiteralPath $stage -Force | Select-Object -ExpandProperty Name | Sort-Object) -join ','
    Assert-Equal $expectedNames $actualNames 'Stage must contain only the pinned chain files and manifest.'
    $sources = Get-ShimStageSources
    Assert-Equal (New-ShimManifest $sources) ([IO.File]::ReadAllText((Join-Path $stage 'manifest.sha256'),[Text.Encoding]::ASCII)) 'Exact ordered CRLF manifest'
    Assert-Equal $bootHash (Get-Hash $bootManager) 'Boot Manager hash after stage'
    $receipt = Read-ShimReceipt
    $status = Get-MountedShimStageStatus $script:EspRoot
    Assert-ShimReceipt $receipt $status 'TEST-ROOT' $script:EspRoot
    Pass 'transactional shim-chain stage, exact manifest, receipt, and unchanged boot manager'

    [IO.File]::WriteAllText((Join-Path $stage $script:ShimStatusName),"schema=1`r`nsecure_boot=1`r`nshim_validation=1`r`ncollector_success=1`r`n",[Text.Encoding]::ASCII)
    $status = Get-MountedShimStageStatus $script:EspRoot -AllowArtifacts
    Assert-True ([bool](Invoke-ShimChainStatus $status.Paths.ChainStatus).Success) 'Valid success chain status must pass.'
    [IO.File]::WriteAllText((Join-Path $stage $script:ShimStatusName),"schema=1`r`nsecure_boot=1`r`nshim_validation=1`r`ncollector_success=0`r`n",[Text.Encoding]::ASCII)
    Assert-True (-not [bool](Invoke-ShimChainStatus $status.Paths.ChainStatus).Success) 'Collector failure status must be classified as failure.'
    [IO.File]::WriteAllText((Join-Path $stage $script:ShimStatusName),"schema=1`r`nsecure_boot=1`r`nshim_validation=1`r`ncollector_success=1`r`nextra=1`r`n",[Text.Encoding]::ASCII)
    Assert-Fails { Invoke-ShimChainStatus (Join-Path $stage $script:ShimStatusName) } 'exact schema' 'Unexpected status fields must be rejected.'
    Remove-Item -LiteralPath (Join-Path $stage $script:ShimStatusName) -Force
    Pass 'strict chain-status success and failure records'

    $diagnosticPath = Join-Path $stage $script:ShimDiagnosticName
    $diagnosticFailureText = "schema=1`r`ncollector_launch=not_found`r`ncollector_status=0x800000000000000E`r`n"
    [IO.File]::WriteAllText($diagnosticPath,$diagnosticFailureText,[Text.Encoding]::ASCII)
    $diagnostic = Invoke-ShimChainDiagnostic $diagnosticPath
    Assert-Equal 'not_found' $diagnostic.Launch 'Exact launch phase'
    Assert-Equal '0x800000000000000E' $diagnostic.StatusHex 'Exact EFI status'
    Assert-Equal (Get-Hash $diagnosticPath) $diagnostic.Hash 'Diagnostic hash must describe parsed bytes'
    Assert-True (-not $diagnostic.Success) 'Launch failure must classify as failure.'
    Get-MountedShimStageStatus $script:EspRoot -AllowArtifacts | Out-Null
    Assert-Fails { Stage-ShimChainToRoot $script:EspRoot 'TEST-ROOT' $script:EspRoot } 'Uncollected prior' 'Stage must preserve uncollected diagnostic.'
    Assert-Fails { Get-MountedShimStageStatus $script:EspRoot } 'Uncollected prior' 'Arm preflight must refuse uncollected diagnostic.'
    [IO.File]::WriteAllText($diagnosticPath,"schema=1`r`ncollector_launch=ok`r`ncollector_status=0x0000000000000000`r`n",[Text.Encoding]::ASCII)
    Assert-True ([bool](Invoke-ShimChainDiagnostic $diagnosticPath).Success) 'Successful child status must classify as success.'
    foreach ($malformed in @(
        $diagnosticFailureText.Replace("`r`n","`n"),
        ($diagnosticFailureText + "extra=1`r`n"),
        $diagnosticFailureText.Replace('not_found','unknown'),
        $diagnosticFailureText.Replace('800000000000000E','800000000000000e'),
        $diagnosticFailureText.Replace('800000000000000E','800000000000000G'),
        $diagnosticFailureText.Replace('800000000000000E','80000000000000E'),
        $diagnosticFailureText.Replace('schema=1','schema=2'),
        $diagnosticFailureText.TrimEnd([char]13,[char]10)
    )) {
        [IO.File]::WriteAllText($diagnosticPath,$malformed,[Text.Encoding]::ASCII)
        Assert-Fails { Invoke-ShimChainDiagnostic $diagnosticPath } 'exact schema' 'Malformed diagnostic must be rejected.'
    }
    [IO.File]::WriteAllBytes($diagnosticPath,([byte[]]@(0xEF,0xBB,0xBF)+[Text.Encoding]::ASCII.GetBytes($diagnosticFailureText)))
    Assert-Fails { Invoke-ShimChainDiagnostic $diagnosticPath } 'exact schema' 'BOM must be rejected.'
    [IO.File]::WriteAllBytes($diagnosticPath,([byte[]]@(0xFF)+[Text.Encoding]::ASCII.GetBytes($diagnosticFailureText)))
    Assert-Fails { Invoke-ShimChainDiagnostic $diagnosticPath } 'exact schema' 'Non-ASCII diagnostic must be rejected.'
    [IO.File]::WriteAllText($diagnosticPath,('x' * 4097),[Text.Encoding]::ASCII)
    Assert-Fails { Invoke-ShimChainDiagnostic $diagnosticPath } 'oversized' 'Oversized diagnostic must be rejected.'
    Remove-Item -LiteralPath $diagnosticPath -Force
    Pass 'exact bounded optional diagnostic schema, parsed-byte hash, and stale-output refusal'

    $diagnosticFailureText = "schema=2`r`ncollector_launch=load`r`ncollector_status=0x800000000000001A`r`nshim_load_phase=security2_lookup`r`nshim_load_status=0x800000000000000E`r`n"
    [IO.File]::WriteAllText($diagnosticPath,$diagnosticFailureText,[Text.Encoding]::ASCII)
    $diagnostic = Invoke-ShimChainDiagnostic $diagnosticPath
    Assert-Equal 'load' $diagnostic.Launch 'Schema 2 collector phase'
    Assert-Equal 'security2_lookup' $diagnostic.ShimPhase 'Schema 2 exact security gate'
    Assert-Equal '0x800000000000000E' $diagnostic.ShimStatusHex 'Schema 2 original protocol lookup status'
    Assert-Equal (Get-Hash $diagnosticPath) $diagnostic.Hash 'Schema 2 parsed-byte hash'
    foreach ($validDiagnostic in @(
        "schema=2`r`ncollector_launch=ok`r`ncollector_status=0x0000000000000000`r`nshim_load_phase=firmware_load`r`nshim_load_status=0x0000000000000000`r`n",
        "schema=2`r`ncollector_launch=start`r`ncollector_status=0x8000000000000007`r`nshim_load_phase=firmware_load`r`nshim_load_status=0x0000000000000000`r`n",
        "schema=2`r`ncollector_launch=read`r`ncollector_status=0x8000000000000007`r`nshim_load_phase=not_applicable`r`nshim_load_status=0x8000000000000013`r`n",
        $diagnosticFailureText.Replace('security2_lookup','shim_verify').Replace('800000000000000E','8000000000000003')
    )) {
        [IO.File]::WriteAllText($diagnosticPath,$validDiagnostic,[Text.Encoding]::ASCII)
        Invoke-ShimChainDiagnostic $diagnosticPath | Out-Null
    }
    foreach ($malformed in @(
        $diagnosticFailureText.Replace('security2_lookup','unknown'),
        $diagnosticFailureText.Replace('shim_load_phase','extra_phase'),
        $diagnosticFailureText.Replace('800000000000000E','800000000000000e'),
        $diagnosticFailureText.Replace('800000000000000E','80000000000000E'),
        $diagnosticFailureText.Replace("`r`n","`n"),
        ($diagnosticFailureText + "extra=1`r`n")
    )) {
        [IO.File]::WriteAllText($diagnosticPath,$malformed,[Text.Encoding]::ASCII)
        Assert-Fails { Invoke-ShimChainDiagnostic $diagnosticPath } 'exact schema' 'Malformed security trace must be rejected.'
    }
    foreach ($contradictory in @(
        $diagnosticFailureText.Replace('security2_lookup','not_applicable'),
        $diagnosticFailureText.Replace('collector_launch=load','collector_launch=read'),
        $diagnosticFailureText.Replace('collector_launch=load','collector_launch=start'),
        "schema=2`r`ncollector_launch=read`r`ncollector_status=0x8000000000000007`r`nshim_load_phase=not_applicable`r`nshim_load_status=0x0000000000000000`r`n"
    )) {
        [IO.File]::WriteAllText($diagnosticPath,$contradictory,[Text.Encoding]::ASCII)
        Assert-Fails { Invoke-ShimChainDiagnostic $diagnosticPath } 'contradict' 'Contradictory security trace must preserve evidence.'
    }
    Remove-Item -LiteralPath $diagnosticPath -Force
    Pass 'schema 2 security gates, original API statuses, and contradictory trace rejection'

    Assert-Fails { Assert-SecureBootEnabled $false } 'must be enabled' 'Secure Boot OFF must not arm the shim chain.'
    Assert-SecureBootEnabled $true
    Pass 'Secure Boot ON arm guard'

    $armMocks = @('Assert-Admin','Assert-SecureBootEnabledFromFirmware','Read-ShimReceipt',
        'Mount-Esp','Dismount-Esp','Get-MountedShimStageStatus','Assert-ShimReceipt',
        'Get-BootManagerHash','Invoke-ShimOneShot')
    $originalArmFunctions = @{}
    foreach ($name in $armMocks) { $originalArmFunctions[$name] = (Get-Command $name -CommandType Function).ScriptBlock }
    $script:ArmOneShotCalls = @()
    $script:ArmFirmwareChecks = 0
    $script:ArmAdminChecks = 0
    Set-Item Function:Assert-Admin { $script:ArmAdminChecks++ }
    Set-Item Function:Assert-SecureBootEnabledFromFirmware { $script:ArmFirmwareChecks++ }
    Set-Item Function:Read-ShimReceipt { return [pscustomobject]@{volumeGuid='TEST-ROOT'} }
    Set-Item Function:Mount-Esp { return @{EspRoot=$script:EspRoot;VolumeGuid='TEST-ROOT';Mountvol='mock';MountLetterColon='S:'} }
    Set-Item Function:Dismount-Esp { param($Mount) }
    Set-Item Function:Get-MountedShimStageStatus {
        param([string]$EspRoot,[switch]$AllowArtifacts)
        $directory = Join-Path (Join-Path $EspRoot 'EFI') $script:ShimDirectoryName
        return @{Paths=@{StageRoot=$directory;Report=(Join-Path $EspRoot $script:ShimReportName);ChainStatus=(Join-Path $directory $script:ShimStatusName);Diagnostic=(Join-Path $directory $script:ShimDiagnosticName)}}
    }
    Set-Item Function:Assert-ShimReceipt { param($Receipt,[hashtable]$Status,[string]$ExpectedVolumeGuid,[string]$ExpectedTestRoot) }
    Set-Item Function:Get-BootManagerHash { param([string]$EspRoot) return ('A' * 64) }
    Set-Item Function:Invoke-ShimOneShot {
        param([string]$OneShotMode,[string]$VolumeGuid)
        $script:ArmOneShotCalls += ($OneShotMode + ':' + $VolumeGuid)
    }
    try {
        $armOutput = @(Invoke-ShimArm)
        Assert-Equal 'bootnext:TEST-ROOT' ($script:ArmOneShotCalls -join ',') 'Successful Arm must set only the shim-inventory BootNext profile.'
        Assert-True ($script:ArmAdminChecks -eq 1 -and $script:ArmFirmwareChecks -eq 2) 'Arm must require admin and recheck Secure Boot after the set.'
        Assert-True (($armOutput -join "`n") -match 'No reboot was started') 'Arm must not start a reboot.'
        Pass 'mocked Secure Boot ON Arm invokes the shim-inventory one-shot profile without rebooting'
    } finally {
        foreach ($name in $armMocks) { Set-Item -Path ("Function:" + $name) -Value $originalArmFunctions[$name] }
    }

    $failureStatusPath = Join-Path $stage $script:ShimStatusName
    $failureStatusText = "schema=1`r`nsecure_boot=1`r`nshim_validation=1`r`ncollector_success=0`r`n"
    [IO.File]::WriteAllText($failureStatusPath,$failureStatusText,[Text.Encoding]::ASCII)
    $collectMocks = @('Assert-Admin','Read-ShimReceipt','Mount-Esp','Dismount-Esp',
        'Get-MountedShimStageStatus','Assert-ShimReceipt','Get-BootManagerHash','Invoke-ShimOneShot')
    $originalCollectFunctions = @{}
    foreach ($name in $collectMocks) { $originalCollectFunctions[$name] = (Get-Command $name -CommandType Function).ScriptBlock }
    $script:CollectOneShotCalls = @()
    Set-Item Function:Assert-Admin { }
    Set-Item Function:Read-ShimReceipt { return [pscustomobject]@{volumeGuid='TEST-ROOT'} }
    Set-Item Function:Mount-Esp { return @{EspRoot=$script:EspRoot;VolumeGuid='TEST-ROOT';Mountvol='mock';MountLetterColon='S:'} }
    Set-Item Function:Dismount-Esp { param($Mount) }
    Set-Item Function:Get-MountedShimStageStatus {
        param([string]$EspRoot,[switch]$AllowArtifacts)
        $directory = Join-Path (Join-Path $EspRoot 'EFI') $script:ShimDirectoryName
        return @{Paths=@{StageRoot=$directory;Report=(Join-Path $EspRoot $script:ShimReportName);ChainStatus=(Join-Path $directory $script:ShimStatusName);Diagnostic=(Join-Path $directory $script:ShimDiagnosticName)}}
    }
    Set-Item Function:Assert-ShimReceipt { param($Receipt,[hashtable]$Status,[string]$ExpectedVolumeGuid,[string]$ExpectedTestRoot) }
    Set-Item Function:Get-BootManagerHash { param([string]$EspRoot) return ('B' * 64) }
    Set-Item Function:Invoke-ShimOneShot {
        param([string]$OneShotMode,[string]$VolumeGuid)
        $script:CollectOneShotCalls += ($OneShotMode + ':' + $VolumeGuid)
    }
    try {
        Assert-Fails { Invoke-ShimCollect } 'reported failure; failure artifacts were captured' 'Failed collector must surface a nonzero workflow result after cleanup.'
        Assert-True ($script:CollectOneShotCalls -join ',' -ceq 'off:TEST-ROOT') 'Failure collection must clear only the owned shim-inventory one-shot state.'
        Assert-True (-not (Test-Path -LiteralPath $failureStatusPath)) 'Verified failure status must be removed from the ESP after local capture.'
        $failureCaptures = @(Get-ChildItem -LiteralPath (Join-Path $script:InventoryBuild 'captures') -Filter 'HypersinvShimChainStatus.*.txt')
        Assert-True ($failureCaptures.Count -eq 1) 'Failure status must be preserved in the local captures directory.'
        Assert-Equal $failureStatusText ([IO.File]::ReadAllText($failureCaptures[0].FullName,[Text.Encoding]::ASCII)) 'Failure status capture bytes'
        Pass 'legacy failure collection without diagnostic remains supported'

        [IO.File]::WriteAllText($failureStatusPath,$failureStatusText,[Text.Encoding]::ASCII)
        [IO.File]::WriteAllText($diagnosticPath,$diagnosticFailureText,[Text.Encoding]::ASCII)
        $script:CollectOneShotCalls = @()
        $script:CapturedDiagnosticOutput = [Collections.Generic.List[string]]::new()
        Assert-Fails { Invoke-ShimCollect | ForEach-Object {$script:CapturedDiagnosticOutput.Add([string]$_)} } 'reported failure; failure artifacts were captured' 'Diagnostic failure capture must retain nonzero result.'
        Assert-Equal 'off:TEST-ROOT' ($script:CollectOneShotCalls -join ',') 'Valid failure diagnostic must allow owned cleanup.'
        Assert-True (-not (Test-Path -LiteralPath $diagnosticPath)) 'Copied diagnostic must be removed after verified cleanup.'
        $diagnosticCaptures = @(Get-ChildItem -LiteralPath (Join-Path $script:InventoryBuild 'captures') -Filter 'HypersinvShimChainDiagnostic.*.txt')
        Assert-True ($diagnosticCaptures.Count -eq 1) 'Exact failure diagnostic must be saved locally.'
        Assert-Equal $diagnosticFailureText ([IO.File]::ReadAllText($diagnosticCaptures[0].FullName,[Text.Encoding]::ASCII)) 'Diagnostic capture bytes'
        Assert-True (($script:CapturedDiagnosticOutput -join "`n") -match 'launch=load; EFI status=0x800000000000001A') 'Failure output must show exact collector phase/status.'
        Assert-True (($script:CapturedDiagnosticOutput -join "`n") -match 'Shim load phase=security2_lookup; EFI status=0x800000000000000E') 'Failure output must show the precise security gate and original API status.'
        Pass 'valid failure diagnostic capture, printed exact EFI status and verified cleanup'

        foreach ($badDiagnostic in @('invalid',"schema=1`r`ncollector_launch=ok`r`ncollector_status=0x0000000000000000`r`n",$diagnosticFailureText.Replace('collector_status=0x800000000000001A','collector_status=0x0000000000000000'),$diagnosticFailureText.Replace('collector_launch=load','collector_launch=read'),"schema=1`r`ncollector_launch=ok`r`ncollector_status=0x8000000000000007`r`n")) {
            [IO.File]::WriteAllText($failureStatusPath,$failureStatusText,[Text.Encoding]::ASCII)
            [IO.File]::WriteAllText($diagnosticPath,$badDiagnostic,[Text.Encoding]::ASCII)
            $script:CollectOneShotCalls = @()
            $expectedFailure = if ($badDiagnostic -ceq 'invalid') { 'exact schema' } else { 'contradict' }
            Assert-Fails { Invoke-ShimCollect } $expectedFailure 'Invalid/contradictory diagnostic must prevent cleanup.'
            Assert-True ($script:CollectOneShotCalls.Count -eq 0) 'Unsafe diagnostic must not call firmware cleanup.'
            Assert-True ((Test-Path -LiteralPath $failureStatusPath) -and (Test-Path -LiteralPath $diagnosticPath)) 'Unsafe input must leave ESP evidence intact.'
        }
        Pass 'malformed or contradictory diagnostics preserve evidence without firmware cleanup'

        $collectOffMock = (Get-Command Invoke-ShimOneShot).ScriptBlock
        [IO.File]::WriteAllText($diagnosticPath,$diagnosticFailureText,[Text.Encoding]::ASCII)
        Set-Item Function:Invoke-ShimOneShot {param($OneShotMode,$VolumeGuid) throw 'Injected owned cleanup refusal.'}
        try {
            $script:CapturedDiagnosticOutput = [Collections.Generic.List[string]]::new()
            Assert-Fails { Invoke-ShimCollect | ForEach-Object {$script:CapturedDiagnosticOutput.Add([string]$_)} } 'Injected owned cleanup refusal' 'Cleanup failure must retain capture path.'
            Assert-True (($script:CapturedDiagnosticOutput -join "`n") -match 'DIAGNOSTIC .*HypersinvShimChainDiagnostic') 'Diagnostic path must print before cleanup can throw.'
            Assert-True ((Test-Path -LiteralPath $failureStatusPath) -and (Test-Path -LiteralPath $diagnosticPath)) 'Cleanup refusal must preserve both ESP records.'
        } finally {Set-Item Function:Invoke-ShimOneShot $collectOffMock}
        Pass 'diagnostic capture path survives owned cleanup refusal'

        $script:DiagnosticMutationPath = $diagnosticPath
        Set-Item Function:Invoke-ShimOneShot {
            param($OneShotMode,$VolumeGuid)
            [IO.File]::WriteAllText($script:DiagnosticMutationPath,"schema=1`r`ncollector_launch=start`r`ncollector_status=0x8000000000000007`r`n",[Text.Encoding]::ASCII)
        }
        try {
            Assert-Fails { Invoke-ShimCollect } 'diagnostic changed before cleanup' 'Concurrent diagnostic mutation must stop output removal.'
            Assert-True ((Test-Path -LiteralPath $failureStatusPath) -and (Test-Path -LiteralPath $diagnosticPath)) 'Changed diagnostic must preserve all ESP evidence.'
        } finally {Set-Item Function:Invoke-ShimOneShot $collectOffMock}
        Remove-Item -LiteralPath $failureStatusPath,$diagnosticPath -Force
        Pass 'changed diagnostic after owned cleanup preserves ESP evidence'
    } finally {
        foreach ($name in $collectMocks) { Set-Item -Path ("Function:" + $name) -Value $originalCollectFunctions[$name] }
    }
    Stage-ShimChainToRoot $script:EspRoot 'TEST-ROOT' $script:EspRoot | Out-Null
    Pass 'failure capture reports nonzero after verified cleanup and leaves the chain ready to stage again'

    $script:RejectSignaturePath = Join-Path $stage 'inventory.efi'
    Assert-Fails { Get-MountedShimStageStatus $script:EspRoot } 'Injected signature mismatch' 'MOK signer mismatch must block status verification.'
    $script:RejectSignaturePath = $null
    Pass 'MOK signature mismatch rejection'

    $unknown = Join-Path $stage 'user-owned.bin'
    [IO.File]::WriteAllText($unknown,'preserve me')
    Assert-Fails { Get-MountedShimStageStatus $script:EspRoot } 'Unowned or unsafe' 'Unknown stage content must be rejected.'
    Remove-Item -LiteralPath $unknown -Force
    $rootReport = Join-Path $script:EspRoot $script:ShimReportName
    [IO.File]::WriteAllText($rootReport,'prior report')
    Assert-Fails { Stage-ShimChainToRoot $script:EspRoot 'TEST-ROOT' $script:EspRoot } 'Uncollected prior' 'Stage must reject a stale collector report.'
    Remove-Item -LiteralPath $rootReport -Force
    [IO.File]::WriteAllText((Join-Path $stage $script:ShimStatusName),'stale')
    Assert-Fails { Stage-ShimChainToRoot $script:EspRoot 'TEST-ROOT' $script:EspRoot } 'Uncollected prior' 'Stage must reject stale chain status.'
    Remove-Item -LiteralPath (Join-Path $stage $script:ShimStatusName) -Force
    Pass 'unknown content and stale report/status refusal'

    $originalGrub = [IO.File]::ReadAllBytes((Join-Path $script:FixtureDirectory 'grubx64.efi'))
    $previousStageHashes = @{}
    foreach ($name in $script:ShimOwnedNames) { $previousStageHashes[$name] = Get-Hash (Join-Path $stage $name) }
    New-EfiFixture (Join-Path $script:FixtureDirectory 'grubx64.efi') 0x72
    $originalWriteReceipt = (Get-Command Write-ShimReceipt).ScriptBlock
    function Write-ShimReceipt { throw 'Injected receipt failure for shim-chain rollback.' }
    Assert-Fails { Stage-ShimChainToRoot $script:EspRoot 'TEST-ROOT' $script:EspRoot } 'Injected receipt failure' 'Injected receipt failure must trigger stage rollback.'
    Set-Item Function:Write-ShimReceipt $originalWriteReceipt
    foreach ($name in $script:ShimOwnedNames) {
        Assert-Equal $previousStageHashes[$name] (Get-Hash (Join-Path $stage $name)) "Rollback must restore $name."
    }
    [IO.File]::WriteAllBytes((Join-Path $script:FixtureDirectory 'grubx64.efi'),$originalGrub)
    Assert-Equal $bootHash (Get-Hash $bootManager) 'Boot Manager hash after rollback'
    Pass 'transaction rollback restores previous signed chain'

    $receiptObject = Read-ShimReceipt | ConvertTo-Json -Depth 4 | ConvertFrom-Json
    $receiptObject.grubx64Sha256 = ('0' * 64)
    [IO.File]::WriteAllText($script:ShimReceiptPath,($receiptObject | ConvertTo-Json -Depth 4),[Text.Encoding]::UTF8)
    Assert-Fails { $badReceipt=Read-ShimReceipt; $current=Get-MountedShimStageStatus $script:EspRoot; Assert-ShimReceipt $badReceipt $current 'TEST-ROOT' $script:EspRoot } 'differs from the shim-chain receipt' 'Changed receipt must be rejected.'
    Pass 'tampered receipt rejection'

    Write-Output "Completed $script:Passed shim-chain host checks. No firmware variables were read or changed."
} finally {
    $resolvedRunRoot = [IO.Path]::GetFullPath($script:RunRoot)
    $safePrefix = $script:Base.TrimEnd([char]92) + [IO.Path]::DirectorySeparatorChar
    if ($resolvedRunRoot.StartsWith($safePrefix,[StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedRunRoot -PathType Container)) {
        Remove-Item -LiteralPath $resolvedRunRoot -Recurse -Force
    }
}
