param(
  [string]$Host_ = '192.168.50.59',
  [int]$Port = 2323,
  [string]$Cmd = '',
  [string]$Out = 'E:\DeepSeekRuntime\APP\_probe\net-shot.png',
  [string]$Raw = 'E:\DeepSeekRuntime\APP\_probe\net-shot.raw',
  [int]$WaitMs = 1200,
  [int]$Scale = 2
)
# 通过 TCP 调试台（2323）抓一帧：可选先发命令（page N / tap X Y ...），再 shot
$ErrorActionPreference = 'Continue'
$tcp = New-Object System.Net.Sockets.TcpClient
try { $tcp.Connect($Host_, $Port) } catch { Write-Host "connect failed: $($_.Exception.Message)"; exit 1 }
$ns = $tcp.GetStream()
$ns.ReadTimeout = 2000
$sw = New-Object IO.StreamWriter($ns)
$sw.AutoFlush = $true
$buf = New-Object byte[] 65536
$sb = New-Object Text.StringBuilder

function Drain([int]$ms) {
  $t0 = Get-Date
  while (((Get-Date) - $t0).TotalMilliseconds -lt $ms) {
    if ($ns.DataAvailable) {
      try { $n = $ns.Read($buf, 0, $buf.Length) } catch { break }
      if ($n -gt 0) { [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $n)) }
    } else { Start-Sleep -Milliseconds 40 }
  }
}

Start-Sleep -Milliseconds 300
[void](Drain 300)
if ($Cmd) { $sw.WriteLine($Cmd); [void](Drain $WaitMs) }
$sw.WriteLine('shot')

$ms = New-Object IO.MemoryStream
$t0 = Get-Date
$last = Get-Date
$done = $false
while (((Get-Date) - $t0).TotalSeconds -lt 40 -and -not $done) {
  if ($ns.DataAvailable) {
    try { $n = $ns.Read($buf, 0, $buf.Length) } catch { break }
    if ($n -gt 0) {
      $ms.Write($buf, 0, $n)
      $last = Get-Date
      $tail = [Text.Encoding]::UTF8.GetString($ms.ToArray(), [Math]::Max(0, $ms.Length - 16), [Math]::Min(16, $ms.Length))
      if ($ms.Length -gt 16 -and $tail.Contains('SHOT END')) { $done = $true }
    }
  } elseif (((Get-Date) - $last).TotalMilliseconds -gt 3000) { break }
  Start-Sleep -Milliseconds 15
}
$tcp.Close()

$bytes = $ms.ToArray()
[IO.File]::WriteAllBytes($Raw, $bytes)
Write-Host "captured $($bytes.Length) bytes"
& 'E:\DeepSeekRuntime\APP\_tools\shot2png.ps1' -Log $Raw -Out $Out -Scale $Scale
