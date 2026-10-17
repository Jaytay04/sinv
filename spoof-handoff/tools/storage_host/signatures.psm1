Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$script:StorageTools=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$reference=Join-Path $script:StorageTools 'device_vm\build-storage.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($reference,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Existing storage signature verifier has syntax errors.'}
foreach($name in @('Get-PinnedDriverSignatureEvidence','Get-PinnedCatalogSignatureEvidence')){
    $definitions=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    if($definitions.Count -ne 1){throw ('Expected one existing verifier: '+$name)}
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}
function Test-StorageHostSignatures {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$PackageDirectory,[Parameter(Mandatory=$true)][string]$CertificateFile)
    $thumbprint='59DBD6780CFE60C020B3F4511211FEC22C397B85'
    if((Get-FileHash -LiteralPath $CertificateFile -Algorithm SHA256).Hash -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'){throw 'Public signing certificate SHA256 differs.'}
    $cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes($CertificateFile))
    if($cert.Thumbprint -cne $thumbprint -or $cert.NotBefore.ToUniversalTime() -gt [DateTime]::UtcNow -or $cert.NotAfter.ToUniversalTime() -le [DateTime]::UtcNow){throw 'Pinned public signing certificate is invalid or expired.'}
    $driver=Get-PinnedDriverSignatureEvidence (Join-Path $PackageDirectory 'client_storage_filter.sys') (Join-Path $script:StorageTools 'smbios_boot\verify-signatures.ps1') $thumbprint $cert (Join-Path $script:StorageTools 'device_filter\sign.ps1')
    $catalog=Get-PinnedCatalogSignatureEvidence (Join-Path $PackageDirectory 'client_storage_filter.cat') $thumbprint $cert
    [pscustomobject]@{driver=$driver;catalog=$catalog;kernelAdmissionProven=$false}
}
Export-ModuleMember -Function Test-StorageHostSignatures
