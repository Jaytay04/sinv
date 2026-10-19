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
        return @{Paths=@{StageRoot=$directory;Report=(Join-Path $EspRoot $script:ShimReportName);ChainStatus=(Join-Path $directory $script:ShimStatusName)}}
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
        return @{Paths=@{StageRoot=$directory;Report=(Join-Path $EspRoot $script:ShimReportName);ChainStatus=(Join-Path $directory $script:ShimStatusName)}}
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
