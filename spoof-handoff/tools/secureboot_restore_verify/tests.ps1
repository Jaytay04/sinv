$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'esp-policy.ps1')
$count=0
function Check([string]$Name,[scriptblock]$Body) { & $Body; $script:count++; 'PASS '+$Name }
function Row([string]$Path,[string]$Hash='A',[bool]$Directory=$false) { [pscustomobject]@{path=$Path;sha256=$Hash;directory=$Directory} }
function Require([bool]$Value) { if(-not $Value){throw 'Unexpected comparison result.'} }
Check 'Unchanged boot image' { $r=@(Compare-RestoreEspFiles @((Row 'EFI\Microsoft\Boot\bootmgfw.efi')) @((Row 'EFI\Microsoft\Boot\bootmgfw.efi'))); Require ($r.Count -eq 1 -and $r[0].pass -and $r[0].classification -eq 'unchanged') }
Check 'Exact boot-status path change is recorded' { $r=@(Compare-RestoreEspFiles @((Row 'EFI\Microsoft\Boot\BOOTSTAT.DAT')) @((Row 'efi/microsoft/boot/bootstat.dat' 'B'))); Require ($r[0].pass -and $r[0].classification -eq 'mutable_boot_status_changed' -and $r[0].expectedSha256 -eq 'A' -and $r[0].actualSha256 -eq 'B') }
Check 'Boot image drift fails' { $r=@(Compare-RestoreEspFiles @((Row 'EFI\Microsoft\Boot\bootmgfw.efi')) @((Row 'EFI\Microsoft\Boot\bootmgfw.efi' 'B'))); Require (-not $r[0].pass) }
Check 'Same leaf in a different directory fails' { $r=@(Compare-RestoreEspFiles @((Row 'EFI\Other\BOOTSTAT.DAT')) @((Row 'EFI\Other\BOOTSTAT.DAT' 'B'))); Require (-not $r[0].pass) }
Check 'Missing boot status fails' { $r=@(Compare-RestoreEspFiles @((Row 'EFI\Microsoft\Boot\BOOTSTAT.DAT')) @()); Require (-not $r[0].pass) }
Check 'Unexpected file fails' { $r=@(Compare-RestoreEspFiles @() @((Row 'EFI\Other\extra.efi'))); Require (-not $r[0].pass) }
Check 'Active BCD retains separate verification scope' { $r=@(Compare-RestoreEspFiles @((Row 'EFI\Microsoft\Boot\BCD' '')) @((Row 'EFI\Microsoft\Boot\BCD' ''))); Require ($r[0].pass -and $r[0].classification -eq 'active_bcd_export_and_enumeration_scope') }
Check 'Other unhashed baseline file fails' { $r=@(Compare-RestoreEspFiles @((Row 'EFI\Other\file.bin' '')) @((Row 'EFI\Other\file.bin' ''))); Require (-not $r[0].pass) }
Check 'File and directory swap fails' { $r=@(Compare-RestoreEspFiles @((Row 'EFI\Other\file.bin')) @((Row 'EFI\Other\file.bin' '' $true))); Require (-not $r[0].pass) }
Check 'Duplicate paths fail' { $caught=$false;try{Compare-RestoreEspFiles @((Row 'EFI\a'),(Row 'efi/a')) @()|Out-Null}catch{$caught=$true};Require $caught }
Check 'Traversal fails' { $caught=$false;try{ConvertTo-RestorePath '..\BOOTSTAT.DAT'|Out-Null}catch{$caught=$true};Require $caught }
Check 'Absolute paths fail' { $caught=$false;try{ConvertTo-RestorePath 'S:\EFI\a'|Out-Null}catch{$caught=$true};Require $caught }
Check 'PowerShell 5 JSON layout rows are individually enumerated' { $rows=@(ConvertFrom-RestoreLayoutJson '[{"path":"EFI/a","sha256":"A","directory":false},{"path":"EFI/b","sha256":"B","directory":false}]'); Require ($rows.Count -eq 2 -and $rows[0].path -eq 'EFI/a' -and $rows[1].path -eq 'EFI/b') }
'PASS total: '+$count
