param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference = 'Stop'
$Shell = New-Object -ComObject WScript.Shell
$Menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'IZ2S Platform'
New-Item -ItemType Directory -Force -Path $Menu | Out-Null
$Link = $Shell.CreateShortcut((Join-Path $Menu 'IZ2S Platform.lnk'))
$Link.TargetPath = Join-Path $Root 'Start.cmd'
$Link.WorkingDirectory = $Root
$Link.Description = 'Open IZ2S Platform in a browser window'
$Link.Save()
