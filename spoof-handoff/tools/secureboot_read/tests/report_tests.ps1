param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(-not ([IO.Path]::GetFullPath($FixtureRoot)).Contains('\spoof-handoff\')){throw 'Test files must remain in the handoff package.'}
New-Item -ItemType Directory -Path $FixtureRoot|Out-Null
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '..\run.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'The command source does not parse.'}
$definition=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Read-ReadProbeReport'},$true)
. ([scriptblock]::Create($definition.Extent.Text))
$id='0123456789ABCDEF0123456789ABCDEF'
$lines=@('HSBREAD001',('build_id='+$id),'mode=read','name=SecureBoot','guid=8be4df61-93ca-11d2-aa0d-00e098032b8c','query_status=0x8000000000000005','query_size=1','first_called=yes','first_status=0x0000000000000000','first_size=1','first_attributes=0x00000006','first_data=01','second_called=yes','second_status=0x0000000000000000','second_size=1','second_attributes=0x00000006','second_data=01','self_check=pass','result=pass','END')
$text=($lines -join "`r`n")+"`r`n"
$path=Join-Path $FixtureRoot 'synthetic-report.txt'
[IO.File]::WriteAllText($path,$text,[Text.Encoding]::ASCII)
$value=Read-ReadProbeReport $path $id '01' 6
if($value.first_data -cne '01'){throw 'Valid synthetic report was not parsed.'}
$cases=@(
    @{name='wrong-build';text=$text.Replace($id,('F'*32))},
    @{name='different-data';text=$text.Replace('second_data=01','second_data=00')},
    @{name='different-attributes';text=$text.Replace('second_attributes=0x00000006','second_attributes=0x00000007')},
    @{name='failed-read';text=$text.Replace('first_status=0x0000000000000000','first_status=0x800000000000000E')},
    @{name='failed-self-check';text=$text.Replace('self_check=pass','self_check=fail')},
    @{name='failed-result';text=$text.Replace('result=pass','result=fail')},
    @{name='wrong-size';text=$text.Replace('first_size=1','first_size=2')},
    @{name='missing-call';text=$text.Replace('second_called=yes','second_called=no')},
    @{name='duplicate-field';text=$text.Replace('second_data=01','first_data=01')},
    @{name='lf-only';text=$text.Replace("`r`n","`n")},
    @{name='truncated';text=$text.Replace("END`r`n",'')}
)
foreach($case in $cases){
    [IO.File]::WriteAllText($path,$case.text,[Text.Encoding]::ASCII)
    $rejected=$false
    try{$null=Read-ReadProbeReport $path $id '01' 6}catch{$rejected=$true}
    if(-not $rejected){throw ('Invalid synthetic report accepted: '+$case.name)}
}
'PASS: 12 report parser cases; synthetic host inputs only, no firmware or device access.'
