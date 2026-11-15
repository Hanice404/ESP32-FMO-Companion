# flash.ps1 -- 烧录预编译固件（ESP32 FMO Companion）
#
#   .\flash.ps1                 # 自动找第一个串口
#   .\flash.ps1 -Port COM5
#   .\flash.ps1 -Erase          # 先整片擦除（会丢掉 NVS 里的 WiFi 配置）
#
# 需要：Python 3 + esptool（pip install esptool）
param(
  [string]$Port = '',
  [int]$Baud = 921600,
  [switch]$Erase
)

$ErrorActionPreference = 'Stop'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path

# 找 esptool：优先 python -m esptool
$python = $null
foreach ($c in @('python', 'python3', 'py')) {
  if (Get-Command $c -ErrorAction SilentlyContinue) { $python = $c; break }
}
$useModule = $false
if ($python) {
  & $python -c "import esptool" 2>$null
  if ($LASTEXITCODE -eq 0) { $useModule = $true }
}
if (-not $useModule) {
  if (Get-Command esptool.py -ErrorAction SilentlyContinue) { $esptool = @('esptool.py') }
  elseif (Get-Command esptool -ErrorAction SilentlyContinue) { $esptool = @('esptool') }
  else {
    Write-Host "找不到 esptool。请先运行：pip install esptool" -ForegroundColor Red
    exit 1
  }
} else {
  $esptool = @($python, '-m', 'esptool')
}

if (-not $Port) {
  $Port = (Get-CimInstance Win32_PnPEntity |
           Where-Object { $_.Name -match '\((COM\d+)\)' } |
           ForEach-Object { [regex]::Match($_.Name, '\((COM\d+)\)').Groups[1].Value } |
           Select-Object -First 1)
  if (-not $Port) { Write-Host "没找到串口，请用 -Port COMx 指定" -ForegroundColor Red; exit 1 }
  Write-Host "使用串口 $Port（如不对请用 -Port 指定）"
}

$files = @(
  @{ addr = '0x0';     name = 'bootloader.bin' },
  @{ addr = '0x8000';  name = 'partitions.bin' },
  @{ addr = '0xe000';  name = 'boot_app0.bin' },
  @{ addr = '0x10000'; name = 'fmo-companion-app.bin' }
)
foreach ($f in $files) {
  if (-not (Test-Path (Join-Path $dir $f.name))) {
    Write-Host "缺少 $($f.name)" -ForegroundColor Red; exit 1
  }
}

if ($Erase) {
  Write-Host "整片擦除中…" -ForegroundColor Yellow
  & $esptool[0] $esptool[1..($esptool.Count-1)] --chip esp32s3 -p $Port erase_flash
  if ($LASTEXITCODE -ne 0) { exit 1 }
}

$args = @()
$args += $esptool[1..($esptool.Count - 1)]
$args += @('--chip', 'esp32s3', '-p', $Port, '-b', "$Baud",
           '--before', 'default_reset', '--after', 'hard_reset',
           'write_flash', '--flash_mode', 'dio', '--flash_size', '16MB', '--flash_freq', '80m')
foreach ($f in $files) { $args += $f.addr; $args += (Join-Path $dir $f.name) }

Write-Host "烧录中…（若卡住：按住 BOOT 再点 RST 进下载模式）" -ForegroundColor Yellow
& $esptool[0] @args
if ($LASTEXITCODE -ne 0) {
  Write-Host "烧录失败。检查串口/线材，或按住 BOOT 再点 RST 后重试。" -ForegroundColor Red
  exit 1
}

Write-Host ""
Write-Host "烧录完成。接下来配置 WiFi：" -ForegroundColor Green
Write-Host "  1) 用串口工具连接 $Port（115200）"
Write-Host "  2) 发送：wifi 你的SSID 你的密码"
Write-Host "  3) 板子重启联网后（可选）发送：host 192.168.x.x   # FMO 设备地址"
