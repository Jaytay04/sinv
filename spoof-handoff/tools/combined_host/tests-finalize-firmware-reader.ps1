[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$fixtureWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$fixtureRepository=[IO.Path]::GetFullPath((Join-Path $fixtureWorktree '..\..\..'))
$fixtureOutput=Join-Path $fixtureWorktree ('spoof-handoff\reports\cf-tests\firmware-reader.'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixtureOutput)
. (Join-Path $PSScriptRoot 'finalize.ps1') -RepositoryRoot $fixtureRepository -WorktreeRoot $fixtureWorktree -LibraryOnly
if ('ReadyToArmFirmwareRead' -as [type]) { throw 'Fixture test requires a clean PowerShell process; no live reader may be loaded.' }
# Managed fixture only. This class has no native calls or system privileges.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
public static class ReadyToArmFirmwareRead {
    public static readonly Dictionary<string,string> Data = new Dictionary<string,string>();
    public static readonly Dictionary<string,uint> Attributes = new Dictionary<string,uint>();
    public static readonly Dictionary<string,int> Errors = new Dictionary<string,int>();
    public static int EnableCalls;
    public static int ReadCalls;
    public static string Key(string name, string guid) { return name + "|" + new Guid(guid).ToString("D"); }
    public static void Enable() { EnableCalls++; }
    public static byte[] Read(string name, string guid, out uint attributes, out int error) {
        ReadCalls++;
        string key=Key(name,guid);
        attributes=Attributes[key]; error=Errors[key];
        return error != 0 || String.IsNullOrEmpty(Data[key]) ? null : Convert.FromBase64String(Data[key]);
    }
}
'@
function Confirm-SecureBootUEFI { return $true }
$fixtureRun=Join-Path $fixtureWorktree 'spoof-handoff\reports\combined-host-20261018\20261018T205738834Z.bc2a10c95c164531a8a251d591d5bbbe'
$fixtureJournal=(Get-CombinedHostJournal $fixtureRepository $fixtureWorktree $fixtureRun).journal
$snapshot=$fixtureJournal.firmwareSnapshot
foreach ($record in @($snapshot.records)) {
    $key=[ReadyToArmFirmwareRead]::Key($record.name,$record.guid)
    [ReadyToArmFirmwareRead]::Data[$key]=if($record.present){[string]$record.base64}else{$null}
    [ReadyToArmFirmwareRead]::Attributes[$key]=[uint32]$record.attributes
    [ReadyToArmFirmwareRead]::Errors[$key]=[int]$record.win32Error
}
$checks=[Collections.Generic.List[object]]::new()
$matching=Read-FinalizationFirmwareState $snapshot $fixtureRepository $fixtureWorktree $fixtureOutput
$checks.Add([pscustomobject]@{name='Complete firmware reader accepts all 21 matching fixture records';pass=($matching.match -eq $true -and $matching.recordCount -eq 21 -and [ReadyToArmFirmwareRead]::ReadCalls -eq 21 -and [ReadyToArmFirmwareRead]::EnableCalls -eq 1)})
$captured=Get-Content -LiteralPath $matching.path -Raw|ConvertFrom-Json
$checks.Add([pscustomobject]@{name='All eight absent fixture variables preserve null data and error 203';pass=(@($captured.records|Where-Object {-not $_.present}).Count -eq 8 -and @($captured.records|Where-Object {-not $_.present -and ($_.win32Error -ne 203 -or $null -ne $_.base64)}).Count -eq 0)})
$bootNext=@($snapshot.records|Where-Object {$_.name -ceq 'BootNext'})[0]
$bootNextKey=[ReadyToArmFirmwareRead]::Key($bootNext.name,$bootNext.guid)
[ReadyToArmFirmwareRead]::Data[$bootNextKey]='AA=='
[ReadyToArmFirmwareRead]::Attributes[$bootNextKey]=7
[ReadyToArmFirmwareRead]::Errors[$bootNextKey]=0
$failure=$null
try { $null=Read-FinalizationFirmwareState $snapshot $fixtureRepository $fixtureWorktree $fixtureOutput } catch { $failure=$_.Exception.Message }
$checks.Add([pscustomobject]@{name='Complete firmware reader rejects BootNext appearing';pass=($null -ne $failure -and $failure -like '*BootNext*');error=$failure})
[ReadyToArmFirmwareRead]::Data[$bootNextKey]=$null
[ReadyToArmFirmwareRead]::Attributes[$bootNextKey]=0
[ReadyToArmFirmwareRead]::Errors[$bootNextKey]=5
$failure=$null
try { $null=Read-FinalizationFirmwareState $snapshot $fixtureRepository $fixtureWorktree $fixtureOutput } catch { $failure=$_.Exception.Message }
$checks.Add([pscustomobject]@{name='Complete firmware reader rejects access denial as absence';pass=($null -ne $failure -and $failure -like '*BootNext*' -and $failure -like '*error=5*');error=$failure})
$corrupt=ConvertTo-CombinedMutableData ($snapshot|ConvertTo-Json -Depth 15|ConvertFrom-Json)
$corrupt.sha256='0'*64
$callsBefore=[ReadyToArmFirmwareRead]::ReadCalls
$failure=$null
try { $null=Read-FinalizationFirmwareState $corrupt $fixtureRepository $fixtureWorktree $fixtureOutput } catch { $failure=$_.Exception.Message }
$checks.Add([pscustomobject]@{name='Baseline pin mismatch rejects before any fixture read';pass=($null -ne $failure -and [ReadyToArmFirmwareRead]::ReadCalls -eq $callsBefore);error=$failure})
$result=[ordered]@{schema='finalization-firmware-reader-fixture-tests.v1';status=if(@($checks|Where-Object {-not $_.pass}).Count){'FAIL'}else{'PASS'};checks=$checks.Count;passed=@($checks|Where-Object pass).Count;results=@($checks.ToArray());syntheticReader=$true;liveFirmwareRead=$false;machineSettingsChanged=$false}
Write-CombinedHostAtomicJson (Join-Path $fixtureOutput 'tests.json') $result
[pscustomobject]@{status=$result.status;checks=$result.checks;passed=$result.passed;evidenceDirectory=$fixtureOutput;liveFirmwareRead=$false}|ConvertTo-Json -Compress
if ($result.status -cne 'PASS') { throw 'Firmware reader fixture tests failed.' }
