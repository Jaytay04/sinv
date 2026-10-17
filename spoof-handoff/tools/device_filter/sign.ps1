[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ArtifactDirectory,[Parameter(Mandatory=$true)][string]$ReportDirectory,[Parameter(Mandatory=$true)][string]$SignedDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$signWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$signRepository=[IO.Path]::GetFullPath((Join-Path $signWorktree '..\..\..'))
$signPackage=Join-Path $signRepository 'spoof-handoff'
foreach($signPath in @($ArtifactDirectory,$ReportDirectory,$SignedDirectory)){
    if(-not ([IO.Path]::GetFullPath($signPath)).StartsWith($signPackage+'\',[StringComparison]::OrdinalIgnoreCase)){
        throw 'All signing inputs/outputs must stay under spoof-handoff.'
    }
}
foreach($signPath in @($ReportDirectory,$SignedDirectory)){
    if(Test-Path -LiteralPath $signPath){throw 'Signing output already exists.'}
    New-Item -ItemType Directory -Path $signPath | Out-Null
}
$env:TEMP=$ReportDirectory;$env:TMP=$ReportDirectory
Add-Type -AssemblyName System.Security
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
public static class DeviceFilterCertificateEdge {
    [DllImport("crypt32.dll",SetLastError=true)] static extern bool CryptVerifyCertificateSignatureEx(IntPtr p,uint e,uint t,IntPtr s,uint i,IntPtr c,uint f,IntPtr x);
    public static void Verify(X509Certificate2 subject,X509Certificate2 issuer) {
        if(!CryptVerifyCertificateSignatureEx(IntPtr.Zero,1,2,subject.Handle,2,issuer.Handle,0,IntPtr.Zero)) throw new Win32Exception();
    }
}
'@
$signReference=Join-Path $signWorktree 'spoof-handoff\tools\smbios_boot\verify-signatures.ps1'
$signTokens=$null;$signErrors=$null
$signAst=[Management.Automation.Language.Parser]::ParseFile($signReference,[ref]$signTokens,[ref]$signErrors)
if(@($signErrors).Count -ne 0){throw 'The proven signature parser contains syntax errors.'}
# Reuse the bounded PKCS#7/DER parser without executing the EFI verifier workflow.
foreach($signFunction in @('Hex','HashBytes','DerNode','Read-Pe')){
    $signDefinitions=@($signAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $signFunction},$true))
    if($signDefinitions.Count -ne 1){throw 'Expected exactly one parser function.'}
    $signDefinition=$signDefinitions[0].Extent.Text
    if($signFunction -ceq 'Read-Pe'){
        $signDefinition=$signDefinition.Replace('function Read-Pe','function Read-NativePe').Replace('Assert-EfiApplication $Image','Assert-NativeDriver $Image')
    }
    . ([scriptblock]::Create($signDefinition))
}
function Assert-NativeDriver([string]$Image){
    $signBytes=[IO.File]::ReadAllBytes($Image)
    if($signBytes.Length -lt 256 -or [BitConverter]::ToUInt16($signBytes,0) -ne 0x5A4D){throw 'Invalid DOS header.'}
    $signPe=[BitConverter]::ToUInt32($signBytes,60)
    if($signPe -gt $signBytes.Length-264 -or [BitConverter]::ToUInt32($signBytes,$signPe) -ne 0x4550 -or
        [BitConverter]::ToUInt16($signBytes,$signPe+4) -ne 0x8664 -or
        [BitConverter]::ToUInt16($signBytes,$signPe+24) -ne 0x20B -or
        [BitConverter]::ToUInt16($signBytes,$signPe+24+68) -ne 1){throw 'Expected AMD64 PE32+ native driver.'}
}
$signCertificatePath=Join-Path $signRepository 'components\hypervisor\hypersinv.cer'
$signThumbprint='59DBD6780CFE60C020B3F4511211FEC22C397B85'
if((Get-FileHash -LiteralPath $signCertificatePath).Hash -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'){throw 'Existing public certificate changed.'}
$signCertificate=[Security.Cryptography.X509Certificates.X509Certificate2]::new($signCertificatePath)
[DeviceFilterCertificateEdge]::Verify($signCertificate,$signCertificate)
if($signCertificate.NotBefore.ToUniversalTime() -gt [DateTime]::UtcNow -or $signCertificate.NotAfter.ToUniversalTime() -le [DateTime]::UtcNow){throw 'Existing signer is outside its validity interval.'}
if(-not (Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$signThumbprint)).HasPrivateKey){throw 'The existing private key is unavailable.'}
function Test-SignedNativeDriver([string]$Image){
    $signParsed=Read-NativePe $Image
    if($signParsed.signer.Thumbprint -cne $signThumbprint -or
        [Convert]::ToBase64String($signParsed.signer.RawData) -cne [Convert]::ToBase64String($signCertificate.RawData)){throw 'Unexpected signing certificate.'}
    [DeviceFilterCertificateEdge]::Verify($signParsed.signer,$signCertificate)
    $signBytes=$signParsed.bytes
    $signOptional=[BitConverter]::ToUInt32($signBytes,60)+24
    $signChecksum=$signOptional+64;$signSecurityDirectory=$signOptional+144
    $signHeaders=[BitConverter]::ToUInt32($signBytes,$signOptional+60)
    $signCertificateOffset=[BitConverter]::ToUInt32($signBytes,$signSecurityDirectory)
    $signCertificateSize=[BitConverter]::ToUInt32($signBytes,$signSecurityDirectory+4)
    if($signHeaders -le $signSecurityDirectory+8 -or $signHeaders -gt $signCertificateOffset -or
        $signCertificateOffset+$signCertificateSize -ne $signBytes.Length){throw 'Unsupported signed-driver layout.'}
    $signStream=[IO.MemoryStream]::new()
    try{
        $signStream.Write($signBytes,0,$signChecksum)
        $signStream.Write($signBytes,$signChecksum+4,$signSecurityDirectory-($signChecksum+4))
        $signStream.Write($signBytes,$signSecurityDirectory+8,$signHeaders-($signSecurityDirectory+8))
        $signEnd=$signHeaders
        foreach($signSection in @($signParsed.sections | Where-Object size -gt 0 | Sort-Object offset)){
            if($signSection.offset -ne $signEnd -or $signSection.offset+$signSection.size -gt $signCertificateOffset){throw 'Unexpected PE section overlap/gap.'}
            $signStream.Write($signBytes,$signSection.offset,$signSection.size)
            $signEnd+=$signSection.size
        }
        if($signEnd -lt $signCertificateOffset){$signStream.Write($signBytes,$signEnd,$signCertificateOffset-$signEnd)}
        $signImageDigest=HashBytes $signStream.ToArray()
    }finally{$signStream.Dispose()}
    if($signImageDigest -cne $signParsed.imageDigest){throw 'Driver Authenticode image digest mismatch.'}
    return [pscustomobject]@{path=$Image;sha256=(Get-FileHash -LiteralPath $Image).Hash;signerThumbprint=$signThumbprint;pkcs7SignatureValid=$true;imageDigestMatch=$true;imageDigest=$signImageDigest;pinnedCertificateSignatureValid=$true;windowsKernelAdmissionProven=$false}
}
$signTool='C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe'
$signEvidence=@()
foreach($signMode in @('disabled','enabled')){
    $signDestination=Join-Path $SignedDirectory $signMode
    New-Item -ItemType Directory -Path $signDestination | Out-Null
    foreach($signName in @('client_storage_filter.sys','client_nic_filter.sys')){
        $signSource=Join-Path (Join-Path $ArtifactDirectory $signMode) $signName
        $signImage=Join-Path $signDestination $signName
        Copy-Item -LiteralPath $signSource -Destination $signImage
        $ErrorActionPreference='Continue'
        $signLines=@(& $signTool sign /v /fd SHA256 /s My /sha1 $signThumbprint $signImage 2>&1 | ForEach-Object {$_.ToString()})
        $signExit=$LASTEXITCODE
        $ErrorActionPreference='Stop'
        [IO.File]::WriteAllLines((Join-Path $ReportDirectory ($signMode+'-'+$signName+'.log')),[string[]]$signLines,[Text.UTF8Encoding]::new($false))
        if($signExit -ne 0){throw 'Existing-key signing failed.'}
        $signEvidence+=Test-SignedNativeDriver $signImage
    }
}
$signTamper=Join-Path $SignedDirectory 'tamper-rejection.sys'
$signTamperBytes=[IO.File]::ReadAllBytes($signEvidence[0].path)
$signTamperBytes[64]=$signTamperBytes[64] -bxor 1
[IO.File]::WriteAllBytes($signTamper,$signTamperBytes)
$signTamperRejected=$false
try{$null=Test-SignedNativeDriver $signTamper}catch{if($_.Exception.Message -like '*image digest mismatch*'){$signTamperRejected=$true}else{throw}}
if(-not $signTamperRejected){throw 'The altered-image test was not rejected.'}
[IO.File]::WriteAllText((Join-Path $ReportDirectory 'signature-verdict.json'),([ordered]@{
    result='PASS';files=$signEvidence;tamperedImageRejected=$signTamperRejected;parserReference=$signReference
    parserReferenceSha256=(Get-FileHash -LiteralPath $signReference).Hash;certificateStoresChanged=$false
    privateKeyExported=$false;newSigningKeyCreated=$false;driverInstalledOrLoaded=$false
    limits='Pinned-certificate, PKCS#7, and PE image-digest validation. This is not Windows kernel-admission or native device read-back evidence.'
} | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
'SIGNED STATIC DRIVER COPIES VERIFIED. No imports, installation, or loading.'
