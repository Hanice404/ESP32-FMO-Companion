param(
  [string]$Port = 'COM6',
  [int]$WaitSec = 10,
  [string]$Out = 'E:\DeepSeekRuntime\APP\_probe\shot.png',
  [string]$Raw = 'E:\DeepSeekRuntime\APP\_probe\shot.raw',
  [string]$Cmd = '',
  [int]$Scale = 2
)
# 抓一帧设备帧缓冲并转 PNG。Cmd 可先发一条串口命令（如 "page 3"），等 WaitSec 后再抓。
$ErrorActionPreference = 'Continue'
$sp = New-Object System.IO.Ports.SerialPort($Port, 115200)
$sp.ReadBufferSize = 1048576
$sp.DtrEnable = $false
$sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Host "open failed: $($_.Exception.Message)"; exit 1 }

function Drain([int]$ms) {
  $t0 = Get-Date
  $ms2 = New-Object System.IO.MemoryStream
  $buf = New-Object byte[] 65536
  while (((Get-Date) - $t0).TotalMilliseconds -lt $ms) {
    $n = $sp.BytesToRead
    if ($n -gt 0) {
      if ($n -gt $buf.Length) { $n = $buf.Length }
      $got = $sp.Read($buf, 0, $n)
      $ms2.Write($buf, 0, $got)
    } else { Start-Sleep -Milliseconds 30 }
  }
  $ms2.ToArray()
}

[void](Drain ($WaitSec * 1000))
if ($Cmd) {
  $sp.Write($Cmd + "`n")
  [void](Drain 1200)
}
$sp.DiscardInBuffer()
$sp.Write("shot`n")

$ms = New-Object System.IO.MemoryStream
$buf = New-Object byte[] 65536
$t0 = Get-Date
$last = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 40) {
  $n = $sp.BytesToRead
  if ($n -gt 0) {
    if ($n -gt $buf.Length) { $n = $buf.Length }
    $got = $sp.Read($buf, 0, $n)
    $ms.Write($buf, 0, $got)
    $last = Get-Date
  } elseif (((Get-Date) - $last).TotalMilliseconds -gt 2000) { break }
  Start-Sleep -Milliseconds 25
}
$sp.Close()
$bytes = $ms.ToArray()
[IO.File]::WriteAllBytes($Raw, $bytes)
Write-Host "captured $($bytes.Length) bytes"
& 'E:\DeepSeekRuntime\APP\_tools\shot2png.ps1' -Log $Raw -Out $Out -Scale $Scale
