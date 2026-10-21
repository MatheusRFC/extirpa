# SPDX-License-Identifier: MIT
# Temporário: mostra tudo o que existe do GridCraft no Registro e no disco (para comparar com o Revo).
param([string]$Label)
$ErrorActionPreference = 'Continue'
"===== $Label ====="
$packed = '5B99A329ED230724BAEC2006910B1CF2'
"--- Registro com 'gridcraft' (HKCU e HKLM\SOFTWARE)"
foreach ($root in 'HKCU', 'HKLM\SOFTWARE') { reg query $root /s /f gridcraft 2>$null | Select-String -NotMatch '^\s*$' | Select-Object -First 300 }
"--- Registro com o código compactado do produto"
reg query HKLM\SOFTWARE /s /f $packed 2>$null | Select-String -NotMatch '^\s*$' | Select-Object -First 200
reg query HKCU /s /f $packed 2>$null | Select-String -NotMatch '^\s*$' | Select-Object -First 50
"--- Registro com o código do produto"
reg query HKLM\SOFTWARE /s /f '923A99B5-32DE-4270-ABCE-026019B0C12F' 2>$null | Select-String -NotMatch '^\s*$' | Select-Object -First 100
"--- UFH\SHC"
reg query 'HKCU\Software\Microsoft\Windows\CurrentVersion\UFH\SHC' 2>$null | Select-String -NotMatch '^\s*$' | Select-Object -First 60
"--- Disco"
foreach ($p in "$env:ProgramFiles\GridCraft", "${env:ProgramFiles(x86)}\GridCraft", "$env:APPDATA\GridCraft", "$env:LOCALAPPDATA\GridCraft", "$env:LOCALAPPDATA\Programs\GridCraft",
               "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\GridCraft.lnk", "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\GridCraft.lnk",
               "$env:PUBLIC\Desktop\GridCraft.lnk", "$env:USERPROFILE\Desktop\GridCraft.lnk") {
  if (Test-Path $p) { "EXISTE $p"; if ((Get-Item $p).PSIsContainer) { Get-ChildItem $p -Recurse -Force | Select-Object -First 40 | ForEach-Object { "    " + $_.FullName } } }
}
Get-ChildItem $env:LOCALAPPDATA, $env:APPDATA, $env:ProgramData -Directory -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'grid|storytold|artcraft|learning' } | ForEach-Object { "EXISTE (nome parecido) " + $_.FullName }
Get-ChildItem C:\Windows\Prefetch -Filter 'GRIDCRAFT*' -ErrorAction SilentlyContinue | ForEach-Object { "PREFETCH " + $_.FullName }
Get-ChildItem C:\Windows\Installer -Force -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt (Get-Date).AddHours(-3) } | ForEach-Object { "INSTALLER " + $_.FullName + " " + $_.LastWriteTime }
