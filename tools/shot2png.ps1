param(
  [string]$Log = 'E:\DeepSeekRuntime\APP\_probe\shot.raw',
  [string]$Out = 'E:\DeepSeekRuntime\APP\_probe\shot.png',
  [int]$Scale = 2
)
# 把设备串口吐出的帧缓冲（Dxxx: + 每像素 4 位 hex，RGB565）还原成 PNG
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$bytes = [IO.File]::ReadAllBytes($Log)
$text = [Text.Encoding]::ASCII.GetString($bytes)
$lines = $text -split "`r?`n"

$rows = @{}
$header = ''
foreach ($ln in $lines) {
  if ($ln -match 'SHOT (\d+) (\d+) 565') {
    $header = $ln
    continue
  }
  if ($header -and $ln -match '^D(\d{3}):([0-9a-fA-F]{1440})$') {
    $rows[[int]$Matches[1]] = $Matches[2]
  }
}
if (-not $header) { throw "no SHOT header in $Log" }
Write-Host "header: $header   rows captured: $($rows.Count)/360"

$W = 360; $H = 360
$bmp = New-Object System.Drawing.Bitmap(($W * $Scale), ($H * $Scale))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::Magenta)

for ($y = 0; $y -lt $H; $y++) {
  if (-not $rows.ContainsKey($y)) { continue }
  $hx = $rows[$y]
  for ($x = 0; $x -lt $W; $x++) {
    $cv = [Convert]::ToInt32($hx.Substring($x * 4, 4), 16)
    $r = (($cv -shr 11) -band 0x1F) * 255 / 31
    $gg = (($cv -shr 5) -band 0x3F) * 255 / 63
    $b = ($cv -band 0x1F) * 255 / 31
    $col = [System.Drawing.Color]::FromArgb([int]$r, [int]$gg, [int]$b)
    $br = New-Object System.Drawing.SolidBrush($col)
    $g.FillRectangle($br, ($x * $Scale), ($y * $Scale), $Scale, $Scale)
    $br.Dispose()
  }
}
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved $Out"
