# build.ps1 -- 编译 / 烧录 ESP32 FMO Companion
#
#   .\build\build.ps1                 # 只编译（产物在 build\out）
#   .\build\build.ps1 -Upload         # 编译 + 烧录（自动找串口）
#   .\build\build.ps1 -Upload -Port COM5
#   .\build\build.ps1 -Monitor        # 串口监视
#   .\build\build.ps1 -Clean          # 清构建目录
#
# 依赖：
#   arduino-cli（PATH 里能找到，或用 -ArduinoCli 指定路径）
#   arduino-cli core install esp32:esp32@3.1.1
param(
  [switch]$Upload,
  [switch]$Monitor,
  [switch]$Clean,
  [string]$Port = '',
  [string]$ArduinoCli = 'arduino-cli'
)

$ErrorActionPreference = 'Continue'
$root   = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$sketch = Join-Path $root 'src\FmoCompanion'
$libs   = Join-Path $root 'libs'
$out    = Join-Path $root 'build\out'

$fqbn = 'esp32:esp32:waveshare_esp32_s3_touch_lcd_185:PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600'

if (-not (Get-Command $ArduinoCli -ErrorAction SilentlyContinue)) {
  Write-Host "找不到 arduino-cli：$ArduinoCli" -ForegroundColor Red
  Write-Host "装法：https://arduino.github.io/arduino-cli/ 然后 arduino-cli core install esp32:esp32@3.1.1"
  exit 1
}

if ($Clean) {
  if (Test-Path $out) { Remove-Item $out -Recurse -Force }
  Write-Host "已清理 $out"
  exit 0
}

New-Item -ItemType Directory -Force -Path $out | Out-Null

if (-not $Port) {
  $Port = (Get-CimInstance Win32_PnPEntity |
           Where-Object { $_.Name -match '\((COM\d+)\)' } |
           ForEach-Object { [regex]::Match($_.Name, '\((COM\d+)\)').Groups[1].Value } |
           Select-Object -First 1)
}

Write-Host "=== 编译 ===" -ForegroundColor Cyan
& $ArduinoCli compile --fqbn $fqbn --libraries $libs --output-dir $out $sketch
if ($LASTEXITCODE -ne 0) { Write-Host "编译失败" -ForegroundColor Red; exit 1 }

if ($Upload) {
  if (-not $Port) { Write-Host "没找到串口，请用 -Port COMx 指定" -ForegroundColor Red; exit 1 }
  Write-Host "=== 烧录到 $Port ===" -ForegroundColor Cyan
  & $ArduinoCli upload -p $Port --fqbn $fqbn --input-dir $out $sketch
  if ($LASTEXITCODE -ne 0) { Write-Host "烧录失败" -ForegroundColor Red; exit 1 }
}

if ($Monitor) {
  if (-not $Port) { Write-Host "没找到串口，请用 -Port COMx 指定" -ForegroundColor Red; exit 1 }
  & $ArduinoCli monitor -p $Port --config baudrate=115200
}

Write-Host "完成。产物在 $out" -ForegroundColor Green
