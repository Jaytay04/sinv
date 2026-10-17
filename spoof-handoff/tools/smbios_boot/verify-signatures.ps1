param([Parameter(Mandatory=$true)][string]$RepoRoot,[Parameter(Mandatory=$true)][string]$OutputBoundary,[string]$SnapshotDirectory,[string]$ArtifactDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Security
. (Join-Path $RepoRoot 'tools\efi_inventory\shim-chain-collector.ps1') -LibraryOnly
if(-not $SnapshotDirectory){throw 'Snapshot directory is required.'}
if(-not $ArtifactDirectory){throw 'Artifact directory is required.'}
Assert-NoReparseAncestors $SnapshotDirectory $OutputBoundary
Assert-NoReparseAncestors $ArtifactDirectory $OutputBoundary
Assert-NoReparseAncestors $ArtifactDirectory ([IO.Path]::GetPathRoot($ArtifactDirectory))
$auditDirectory=Join-Path $SnapshotDirectory 'signatures'
Assert-NoReparseAncestors $auditDirectory $OutputBoundary
New-Item -ItemType Directory -Path $auditDirectory -Force | Out-Null
$env:TEMP=$auditDirectory
$env:TMP=$auditDirectory
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
public static class ReadyCertificateEdge {
    [DllImport("crypt32.dll",SetLastError=true)] static extern bool CryptVerifyCertificateSignatureEx(IntPtr p,uint e,uint t,IntPtr s,uint i,IntPtr c,uint f,IntPtr x);
    public static void Verify(X509Certificate2 subject,X509Certificate2 issuer) {
        if(!CryptVerifyCertificateSignatureEx(IntPtr.Zero,1,2,subject.Handle,2,issuer.Handle,0,IntPtr.Zero)) throw new Win32Exception();
    }
}
'@
function Hex([byte[]]$Bytes){return [BitConverter]::ToString($Bytes).Replace('-','')}
function HashBytes([byte[]]$Bytes){
    $hasher=[Security.Cryptography.SHA256]::Create()
    try{return Hex ($hasher.ComputeHash($Bytes))}finally{$hasher.Dispose()}
}
function DerNode([byte[]]$Bytes,[int]$Offset){
    if($Offset+2 -gt $Bytes.Length){throw 'Truncated DER.'}
    $start=$Offset
    $tag=$Bytes[$Offset++]
    [int]$length=$Bytes[$Offset++]
    if($length -band 0x80){
        $count=$length -band 0x7f
        if($count -lt 1 -or $count -gt 4 -or $Offset+$count -gt $Bytes.Length){throw 'Invalid DER length.'}
        $length=0
        for($i=0;$i -lt $count;$i++){$length=($length -shl 8) -bor $Bytes[$Offset++]}
    }
    if($length -lt 0 -or $Offset+$length -gt $Bytes.Length){throw 'DER outside bounds.'}
    return @{Tag=$tag;Start=$start;Content=$Offset;Length=$length;End=$Offset+$length}
}
function Read-SignatureList([byte[]]$Bytes,[string]$Source){
    $offset=0
    while($offset -lt $Bytes.Length){
        if($Bytes.Length-$offset -lt 28){throw ('Truncated EFI signature list '+$Source)}
        $type=[Guid]::new([byte[]]$Bytes[$offset..($offset+15)]).ToString()
        $size=[BitConverter]::ToUInt32($Bytes,$offset+16)
        $header=[BitConverter]::ToUInt32($Bytes,$offset+20)
        $signature=[BitConverter]::ToUInt32($Bytes,$offset+24)
        if($size -lt 28 -or $size -gt $Bytes.Length-$offset -or $signature -le 16 -or $header -gt $size-28 -or (($size-28-$header)%$signature) -ne 0){throw ('Invalid EFI signature list '+$Source)}
        for($position=$offset+28+$header;$position -lt $offset+$size;$position+=$signature){
            $data=[byte[]]$Bytes[($position+16)..($position+$signature-1)]
            $cert=$null
            if($type -eq 'a5c059a1-94e4-4aa7-87b5-ab155c2bf072'){$cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new($data)}
            [pscustomobject]@{source=$Source;type=$type;data=$data;hex=(Hex $data);certificate=$cert}
        }
        $offset+=$size
    }
}
function Read-Pe([string]$Image){
    Assert-EfiApplication $Image
    $bytes=[IO.File]::ReadAllBytes($Image)
    $pe=[BitConverter]::ToUInt32($bytes,60)
    $optional=$pe+24
    $optionalSize=[BitConverter]::ToUInt16($bytes,$pe+20)
    $count=[BitConverter]::ToUInt16($bytes,$pe+6)
    $stringTable=[BitConverter]::ToUInt32($bytes,$pe+12)+18*[BitConverter]::ToUInt32($bytes,$pe+16)
    $sections=@()
    for($i=0;$i -lt $count;$i++){
        $offset=$optional+$optionalSize+40*$i
        if($offset+40 -gt $bytes.Length){throw 'Truncated PE section table.'}
        $name=[Text.Encoding]::ASCII.GetString($bytes,$offset,8).TrimEnd([char]0)
        if($name.StartsWith('/')){
            $start=$stringTable+[int]$name.Substring(1)
            $end=$start
            while($end -lt $bytes.Length -and $bytes[$end] -ne 0){$end++}
            if($end -ge $bytes.Length){throw 'Unterminated PE section name.'}
            $name=[Text.Encoding]::ASCII.GetString($bytes,$start,$end-$start)
        }
        $raw=[BitConverter]::ToUInt32($bytes,$offset+20)
        $size=[BitConverter]::ToUInt32($bytes,$offset+16)
        if($raw+$size -gt $bytes.Length){throw 'PE section outside image.'}
        $sections+=[pscustomobject]@{name=$name;offset=$raw;size=$size}
    }
    $security=[BitConverter]::ToUInt32($bytes,$optional+144)
    $securitySize=[BitConverter]::ToUInt32($bytes,$optional+148)
    if($security -eq 0 -or $securitySize -lt 8 -or $security+$securitySize -gt $bytes.Length -or ($security%8) -ne 0){throw 'Missing or malformed PE certificate table.'}
    $length=[BitConverter]::ToUInt32($bytes,$security)
    if($length -lt 8 -or $length -gt $securitySize -or [BitConverter]::ToUInt16($bytes,$security+4) -ne 0x200 -or [BitConverter]::ToUInt16($bytes,$security+6) -ne 2){throw 'Invalid WIN_CERTIFICATE.'}
    if((([int64]$length+7) -band -8) -ne $securitySize){throw 'Unexpected additional PE certificate records.'}
    $cms=[Security.Cryptography.Pkcs.SignedCms]::new()
    $cms.Decode([byte[]]$bytes[($security+8)..($security+$length-1)])
    $cms.CheckSignature($true)
    if($cms.SignerInfos.Count -ne 1 -or $cms.ContentInfo.ContentType.Value -cne '1.3.6.1.4.1.311.2.1.4'){throw 'Unexpected Authenticode content or signer count.'}
    $content=$cms.ContentInfo.Content
    $outer=DerNode $content 0
    $data=DerNode $content $outer.Content
    $digestInfo=DerNode $content $data.End
    $algorithm=DerNode $content $digestInfo.Content
    $digest=DerNode $content $algorithm.End
    if($digest.Tag -ne 4 -or $digest.Length -ne 32 -or $cms.SignerInfos[0].DigestAlgorithm.Value -cne '2.16.840.1.101.3.4.2.1'){throw 'Expected SHA-256 Authenticode digest.'}
    return @{bytes=$bytes;sections=$sections;cms=$cms;signer=$cms.SignerInfos[0].Certificate;imageDigest=(Hex ([byte[]]$content[$digest.Content..($digest.End-1)]))}
}
$firmware=Get-Content -LiteralPath (Join-Path $SnapshotDirectory 'firmware-variables.json') -Raw | ConvertFrom-Json
$dbRecord=$firmware | Where-Object name -eq 'db'
$dbxRecord=$firmware | Where-Object name -eq 'dbx'
if(-not $dbRecord.present -or -not $dbxRecord.present){throw 'Firmware db/dbx missing.'}
$db=@(Read-SignatureList ([Convert]::FromBase64String($dbRecord.base64)) 'firmware db')
$dbx=@(Read-SignatureList ([Convert]::FromBase64String($dbxRecord.base64)) 'firmware dbx')
$stage=if($ArtifactDirectory){[IO.Path]::GetFullPath($ArtifactDirectory)}else{Join-Path $SnapshotDirectory 'ESP\EFI\HypersinvShimInventory'}
$mok=[Security.Cryptography.X509Certificates.X509Certificate2]::new([IO.File]::ReadAllBytes((Join-Path $stage 'MOK.cer')))
if($mok.Thumbprint -cne $script:ShimCertThumbprint -or (Get-Sha256 (Join-Path $stage 'MOK.cer')) -cne 'B3C4D8955151A56F2F2D1B4F50BA358C7FC95C75ADCAD49EED0C669FC034F3BE'){throw 'MOK certificate mismatch.'}
[ReadyCertificateEdge]::Verify($mok,$mok)
if($mok.NotBefore.ToUniversalTime() -gt [DateTime]::UtcNow -or $mok.NotAfter.ToUniversalTime() -le [DateTime]::UtcNow){throw 'MOK certificate outside validity period.'}
$parsed=@{}
foreach($name in @('shimx64.efi','grubx64.efi','inventory.efi','mmx64.efi')){$parsed[$name]=Read-Pe (Join-Path $stage $name)}
$shim=$parsed['shimx64.efi']
$vendorSection=$shim.sections | Where-Object name -eq '.vendor_cert'
if(-not $vendorSection){throw 'Shim vendor certificate section missing.'}
$vendorStart=$vendorSection.offset
$vendorSize=[BitConverter]::ToUInt32($shim.bytes,$vendorStart)
$vendorOffset=[BitConverter]::ToUInt32($shim.bytes,$vendorStart+8)
$vendorDbxSize=[BitConverter]::ToUInt32($shim.bytes,$vendorStart+4)
$vendorDbxOffset=[BitConverter]::ToUInt32($shim.bytes,$vendorStart+12)
if($vendorOffset+$vendorSize -gt $vendorSection.size -or $vendorDbxOffset+$vendorDbxSize -gt $vendorSection.size){throw 'Shim vendor data outside section.'}
$vendor=[Security.Cryptography.X509Certificates.X509Certificate2]::new([byte[]]$shim.bytes[($vendorStart+$vendorOffset)..($vendorStart+$vendorOffset+$vendorSize-1)])
[ReadyCertificateEdge]::Verify($vendor,$vendor)
[IO.File]::WriteAllBytes((Join-Path $auditDirectory 'shim-vendor.cer'),$vendor.RawData)
$vendorDbx=@(Read-SignatureList ([byte[]]$shim.bytes[($vendorStart+$vendorDbxOffset)..($vendorStart+$vendorDbxOffset+$vendorDbxSize-1)]) 'shim vendor dbx')
$allRevocations=@($dbx)+@($vendorDbx)
$records=@()
foreach($name in @('shimx64.efi','grubx64.efi','inventory.efi','mmx64.efi')){
    $image=Join-Path $stage $name
    $pe=$parsed[$name]
    $signer=$pe.signer
    $anchor=$null
    $chain=@($signer)
    if($name -in @('grubx64.efi','inventory.efi')){
        if([Convert]::ToBase64String($signer.RawData) -cne [Convert]::ToBase64String($mok.RawData)){throw ('MOK signer mismatch '+$name)}
        [ReadyCertificateEdge]::Verify($signer,$mok)
        $anchor=$mok
    }elseif($name -eq 'mmx64.efi'){
        [ReadyCertificateEdge]::Verify($signer,$vendor)
        $anchor=$vendor
        $chain+=@($vendor)
    }else{
        $issuers=@($db | Where-Object {$null -ne $_.certificate -and $_.certificate.Subject -ceq $signer.Issuer})
        foreach($candidate in $issuers){
            try{[ReadyCertificateEdge]::Verify($signer,$candidate.certificate);$anchor=$candidate.certificate;break}catch{}
        }
        if(-not $anchor){throw 'Shim does not chain to any current firmware db certificate.'}
        $chain+=@($anchor)
    }
    foreach($revocation in $allRevocations){
        if($revocation.type -eq 'c1c41626-504c-4092-aca9-41f936934328' -and $revocation.hex -ceq $pe.imageDigest){throw ('EFI image is hash-revoked: '+$name)}
        foreach($cert in $chain){
            if($revocation.type -eq 'a5c059a1-94e4-4aa7-87b5-ab155c2bf072' -and $revocation.hex -ceq (Hex $cert.RawData)){throw ('EFI certificate is revoked: '+$name)}
            if($revocation.type -eq '3bd2a492-96c0-4079-b420-fcf98ef103ed'){
                $certOuter=DerNode $cert.RawData 0
                $tbs=DerNode $cert.RawData $certOuter.Content
                $tbsHash=HashBytes ([byte[]]$cert.RawData[$tbs.Start..($tbs.End-1)])
                if($revocation.hex.StartsWith($tbsHash,[StringComparison]::Ordinal)){throw ('EFI certificate TBS is revoked: '+$name)}
            }
        }
    }
    $signerPath=Join-Path $auditDirectory ($name+'.signer.cer')
    [IO.File]::WriteAllBytes($signerPath,$signer.RawData)
    $wrong=if($name -in @('grubx64.efi','inventory.efi')){Join-Path $auditDirectory 'shim-vendor.cer'}else{Join-Path $stage 'MOK.cer'}
    $verifierOutput=@(& (Get-AuthenticodeVerifier) $image $signerPath $wrong 2>&1)
    if($LASTEXITCODE -ne 0){throw ('Native integrity/tamper/signer check failed '+$name+': '+($verifierOutput -join '; '))}
    $sbatSection=$pe.sections | Where-Object name -eq '.sbat'
    if(-not $sbatSection){throw ('Missing SBAT '+$name)}
    $sbat=[Text.Encoding]::UTF8.GetString($pe.bytes,$sbatSection.offset,$sbatSection.size).TrimEnd([char]0)
    $records+=[pscustomobject]@{
        name=$name;sha256=Get-Sha256 $image;machine='AMD64';format='PE32+';subsystem='EFI_APPLICATION'
        signer=$signer.Subject;signerThumbprint=$signer.Thumbprint;issuer=$signer.Issuer
        trustAnchor=$anchor.Subject;trustAnchorThumbprint=$anchor.Thumbprint
        authenticodeImageDigest=$pe.imageDigest;pkcs7SignatureValid=$true;certificateEdgeValid=$true
        nativeImageIntegrityValid=$true;tamperedCopyRejected=$true;wrongSignerRejected=$true
        firmwareAndVendorDbxMatch=$false;sbat=$sbat;sections=$pe.sections
    }
}
$result=[ordered]@{
    verifiedUtc=[DateTime]::UtcNow.ToString('o');verdict='PASS';artifactDirectory=$stage;artifacts=$records
    mokCertificate=@{subject=$mok.Subject;thumbprint=$mok.Thumbprint;sha256=Get-Sha256 (Join-Path $stage 'MOK.cer');selfSignatureValid=$true;validNow=$true}
    firmwareDbSignatureTypes=@($db | Group-Object type | Select-Object Name,Count)
    revocationSignatureTypes=@($allRevocations | Group-Object type | Select-Object Name,Count)
    limits='Cryptographic and current db/dbx checks passed; actual firmware/shim acceptance requires the user boot. Microsoft UEFI certificate calendar expiry is recorded by Windows but is not alone an EFI revocation. No trust stores changed.'
}
[IO.File]::WriteAllText((Join-Path $SnapshotDirectory 'signature-verdict.json'),($result | ConvertTo-Json -Depth 12),[Text.Encoding]::UTF8)
$records | Select-Object name,signerThumbprint,trustAnchorThumbprint,nativeImageIntegrityValid,tamperedCopyRejected,wrongSignerRejected | Format-Table -AutoSize
'PASS: all four EFI PE signatures, correct trust anchors, pinned MOK certificate, current db/dbx, tamper and wrong-signer rejection.'
