param(
 [ValidateSet('Inspect','Restart','Recover')][string]$Action='Inspect',
 [string]$InterfaceGuid='',
 [switch]$Elevated
)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false)
$Root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$State=Join-Path $Root 'data\state\windows-wifi-recovery.json'
# Native WLAN profile names are not parsed from localized netsh text.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class IZ2SWlan {
 [DllImport("wlanapi.dll")] static extern uint WlanOpenHandle(uint v,IntPtr r,out uint n,out IntPtr h);
 [DllImport("wlanapi.dll")] static extern uint WlanQueryInterface(IntPtr h,ref Guid id,uint op,IntPtr r,out uint size,out IntPtr data,out uint type);
 [DllImport("wlanapi.dll")] static extern void WlanFreeMemory(IntPtr p);
 [DllImport("wlanapi.dll")] static extern uint WlanCloseHandle(IntPtr h,IntPtr r);
 public static string Profile(string guid) {
  IntPtr h=IntPtr.Zero,p=IntPtr.Zero;uint n,size,type;
  uint code=WlanOpenHandle(2,IntPtr.Zero,out n,out h);
  if(code!=0)throw new InvalidOperationException("WlanOpenHandle: "+code);
  try {
   Guid id=new Guid(guid);code=WlanQueryInterface(h,ref id,7,IntPtr.Zero,out size,out p,out type);
   if(code==5023)return ""; // ERROR_INVALID_STATE: disconnected adapter.
   if(code!=0)throw new InvalidOperationException("WlanQueryInterface: "+code);
   if(size<520)throw new InvalidOperationException("Invalid WLAN connection record");
   return Marshal.PtrToStringUni(IntPtr.Add(p,8),256).TrimEnd('\0');
  } finally {if(p!=IntPtr.Zero)WlanFreeMemory(p);WlanCloseHandle(h,IntPtr.Zero);}
 }
}
'@
function WifiAdapters {
 @(Get-NetAdapter -Physical | Where-Object { [int]$_.NdisPhysicalMedium -in @(1,9) })
}
if($Action -eq 'Inspect') {
 Add-Type -Path (Join-Path $PSScriptRoot 'windows-wlan.cs')
 $inventoryError='';$rows=@()
 try {
  $rows=@(WifiAdapters | ForEach-Object {
   $adapter=$_
   $probe=[IZ2SWlanProbe]::Inspect($adapter.InterfaceGuid.ToString())
   @{
    id=$adapter.InterfaceGuid.ToString();name=$adapter.Name;description=$adapter.InterfaceDescription
    status=$adapter.Status.ToString();connected=($probe.interfaceState -eq 1)
    physicalMedium=[int]$adapter.NdisPhysicalMedium
    driverVersion=[string]$adapter.DriverVersionString;driverProvider=[string]$adapter.DriverProvider
    driverDate=[string]$adapter.DriverDate;wlan=$probe
   }
  })
 } catch { $inventoryError=$_.Exception.Message }
 $wlan=Get-Service WlanSvc -ErrorAction SilentlyContinue
 $npcap=Get-CimInstance Win32_SystemDriver -Filter "Name='npcap'" -ErrorAction SilentlyContinue
 $isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
 ConvertTo-Json -Depth 8 -Compress -InputObject @{
  adapters=$rows;inventoryError=$inventoryError;recoveryAvailable=(Test-Path -LiteralPath $State)
  osVersion=[Environment]::OSVersion.Version.ToString();administrator=$isAdmin
  wlanService=$(if($wlan){$wlan.Status.ToString()}else{'Missing'})
  capture=@{installed=($null -ne $npcap);service=$(if($npcap){[string]$npcap.State}else{'Missing'});helperAvailable=(Test-Path -LiteralPath (Join-Path ([Environment]::SystemDirectory) 'Npcap\WlanHelper.exe'))}
 }
 exit 0
}
$guid=[Guid]::Parse($InterfaceGuid)
$adapter=@(WifiAdapters | Where-Object {$_.InterfaceGuid -eq $guid})
if($adapter.Count -ne 1){throw 'Select one physical Wi-Fi adapter. Ethernet and virtual adapters cannot be restarted here.'}
$adapter=$adapter[0]
$isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(-not $isAdmin){
 if($Elevated){throw 'Administrator permission was not granted.'}
 # An encoded command preserves spaces, apostrophes and non-Latin installation paths.
 $quoted=$PSCommandPath.Replace("'","''")
 $cmd="& '$quoted' -Action $Action -InterfaceGuid '$guid' -Elevated"
 $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($cmd))
 $child=Start-Process -FilePath "$PSHOME\powershell.exe" -Verb RunAs -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -Wait -PassThru
 if($child.ExitCode -ne 0){throw 'Wi-Fi recovery was not confirmed. Use Recover and inspect the saved recovery record.'}
 Write-Output 'IZ2S_WINDOWS_WIFI_RESTORED'
 exit 0
}
$directory=Split-Path -Parent $State
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$lock=$null
try {
 $lock=[IO.File]::Open(($State+'.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 if(Test-Path -LiteralPath $State){
  $saved=Get-Content -LiteralPath $State -Raw | ConvertFrom-Json
  if([Guid]$saved.id -ne $guid){throw 'Recover the previously selected adapter first.'}
  $Action='Recover'
 } else {
  if($Action -eq 'Recover'){throw 'No pending Wi-Fi recovery exists.'}
  if($adapter.Status -eq 'Disabled'){throw 'The adapter was already disabled. Enable it in Windows settings first.'}
  $profile=[IZ2SWlan]::Profile($guid.ToString())
  $saved=@{id=$guid.ToString();profile=$profile;created=[DateTime]::UtcNow.ToString('o')}
  $temporary=$State+'.tmp'
  [IO.File]::WriteAllText($temporary,($saved | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
  Move-Item -LiteralPath $temporary -Destination $State -Force
 }
 try {
  if($Action -eq 'Restart'){
   Disable-NetAdapter -InputObject $adapter -Confirm:$false
   Start-Sleep -Seconds 2
  }
 } finally {
  # Runs even if Disable-NetAdapter reports an error after changing the device.
  Enable-NetAdapter -InputObject $adapter -Confirm:$false
 }
 $deadline=[DateTime]::UtcNow.AddSeconds(60);$connected=$false
 do {
  $adapter=Get-NetAdapter -Physical | Where-Object {$_.InterfaceGuid -eq $guid}
  if($adapter -and $adapter.Status -ne 'Disabled'){
   if(-not $saved.profile){$connected=$true;break}
   & netsh.exe wlan connect "name=$($saved.profile)" "interface=$($adapter.Name)" | Out-Null
   Start-Sleep -Seconds 2
   try {
    $same=[IZ2SWlan]::Profile($guid.ToString()) -eq $saved.profile
    $ipv4=@(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object {$_.IPAddress -notlike '169.254.*' -and $_.AddressState -eq 'Preferred'})
    if($same -and $ipv4.Count){$connected=$true;break}
   }catch{}
  }
  Start-Sleep -Seconds 1
 }while([DateTime]::UtcNow -lt $deadline)
 if(-not $connected){throw 'Wi-Fi profile or IPv4 restoration was not confirmed. Recovery record retained.'}
 Remove-Item -LiteralPath $State
 Write-Output 'IZ2S_WINDOWS_WIFI_RESTORED'
} finally {if($lock){$lock.Dispose()}}
