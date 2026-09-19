param(
 [Parameter(Mandatory=$true)][string]$LinkerPath,
 [Parameter(Mandatory=$true)][string]$ObjectPath)
$ErrorActionPreference='Stop'
function A([bool]$C,[string]$M){if(-not$C){throw $M}}
if(-not(Test-Path -LiteralPath $LinkerPath -PathType Leaf)){throw "MSVC linker unavailable: $LinkerPath"}
if(-not(Test-Path -LiteralPath $ObjectPath -PathType Leaf)){throw "INVVPID object unavailable: $ObjectPath"}
$Dump=& $LinkerPath /dump /disasm $ObjectPath 2>&1
if($LASTEXITCODE-ne0){throw "MSVC disassembly failed ($LASTEXITCODE): $($Dump-join[Environment]::NewLine)"}
$Text=$Dump-join[Environment]::NewLine
$Start=$Text.IndexOf('VmxVpidInvvpidAsm:',[StringComparison]::Ordinal)
A ($Start-ge0) 'VmxVpidInvvpidAsm symbol missing'
$Block=$Text.Substring($Start)
$Instructions=@()
foreach($Line in($Block-split'\r?\n')){if($Line-match'^\s{2}([0-9A-F]+):\s+([0-9A-F]{2}(?:\s+[0-9A-F]{2})*)\s{2,}(.+)$'){$Instructions+=(($Matches[3]-replace'\s+',' ').Trim())}}
A ($Instructions.Count-eq13) "INVVPID wrapper instruction count changed: $($Instructions.Count)"
$Expected=@('invvpid rcx,oword ptr [rdx]','pushfq','pop rax','test eax,1',$null,
 'test eax,40h',$null,'mov eax,1','ret','mov eax,2','ret','mov eax,3','ret')
for($I=0;$I-lt$Expected.Count;$I++){if($null-ne$Expected[$I]){A ($Instructions[$I]-ceq$Expected[$I]) "instruction $I changed: $($Instructions[$I])"}}
A ($Instructions[4]-cmatch'^jne [0-9A-F]+$') 'CF branch changed'
A ($Instructions[6]-cmatch'^jne [0-9A-F]+$') 'ZF branch changed'
Write-Host 'PASS: emitted INVVPID wrapper disassembly preserves exact flag/result mapping.'
