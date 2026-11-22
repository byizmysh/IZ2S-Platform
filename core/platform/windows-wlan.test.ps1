$ErrorActionPreference='Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'windows-wlan.cs')
function Bytes([uint32[]]$Values) {
 $result=[Collections.Generic.List[byte]]::new()
 foreach($value in $Values){$result.AddRange([BitConverter]::GetBytes($value))}
 return ,$result.ToArray()
}
function Equal($Actual,$Expected) { if($Actual -cne $Expected){throw "Expected '$Expected'; got '$Actual'"} }
Equal (([IZ2SWlanProbe]::ParseCapability((Bytes @(2,1,16,16,3,7,8,10)))) -join ',') '7,8,10'
Equal ([IZ2SWlanProbe]::ParseRadio((Bytes @(1,0,1,1)))) $true
Equal ([IZ2SWlanProbe]::ParseRadio((Bytes @(1,0,2,1)))) $false
Equal ([IZ2SWlanProbe]::ParseRadio((Bytes @(1,0,1,2)))) $false
Equal ([IZ2SWlanProbe]::ParseRadio((Bytes @(1,0,0,1)))) $null
Equal ([IZ2SWlanProbe]::ParseRadio((Bytes @(0)))) $null
# One working PHY is enough, but a mixture of off and unknown is not proof all are off.
Equal ([IZ2SWlanProbe]::ParseRadio((Bytes @(2,0,2,1,1,1,1)))) $true
Equal ([IZ2SWlanProbe]::ParseRadio((Bytes @(2,0,2,1,1,0,1)))) $null
foreach($case in @('Capability','Radio')){
 foreach($bytes in @([byte[]]@(),(Bytes @(65)),(Bytes @(2,1,16,16,65)))){
  $rejected=$false
  try { if($case -eq 'Capability'){[IZ2SWlanProbe]::ParseCapability($bytes)|Out-Null}else{[IZ2SWlanProbe]::ParseRadio($bytes)|Out-Null} } catch {$rejected=$true}
  if(-not $rejected){throw "Malformed $case record accepted"}
 }
}
Write-Output 'WLAN_BUFFER_TESTS_OK'
