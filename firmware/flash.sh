#!/usr/bin/env bash
# flash.sh -- 烧录预编译固件（ESP32 FMO Companion），macOS / Linux 用
#
#   ./flash.sh                     # 自动找端口
#   ./flash.sh /dev/ttyACM0
#   ./flash.sh /dev/cu.usbmodem1101 --baud 460800
#
# 需要：Python 3 + esptool（pip3 install esptool）
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-}"
BAUD="${3:-921600}"

if [ "${2:-}" = "--baud" ] && [ -n "${3:-}" ]; then BAUD="$3"; fi

# 找 esptool
if python3 -c "import esptool" >/dev/null 2>&1; then
  ESPTOOL=(python3 -m esptool)
elif command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL=(esptool)
else
  echo "找不到 esptool，请先：pip3 install esptool" >&2
  exit 1
fi

if [ -z "$PORT" ]; then
  if [ -e /dev/ttyACM0 ]; then PORT=/dev/ttyACM0
  elif ls /dev/cu.usbmodem* >/dev/null 2>&1; then PORT=$(ls /dev/cu.usbmodem* | head -n1)
  elif ls /dev/ttyUSB* >/dev/null 2>&1; then PORT=$(ls /dev/ttyUSB* | head -n1)
  else echo "找不到串口，请手动指定，例如 ./flash.sh /dev/ttyACM0" >&2; exit 1
  fi
  echo "使用串口 $PORT"
fi

for f in bootloader.bin partitions.bin boot_app0.bin fmo-companion-app.bin; do
  [ -f "$DIR/$f" ] || { echo "缺少 $f" >&2; exit 1; }
done

echo "烧录中…（若卡住：按住 BOOT 再点 RST 进下载模式）"
"${ESPTOOL[@]}" --chip esp32s3 -p "$PORT" -b "$BAUD" \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0     "$DIR/bootloader.bin" \
  0x8000  "$DIR/partitions.bin" \
  0xe000  "$DIR/boot_app0.bin" \
  0x10000 "$DIR/fmo-companion-app.bin"

cat <<'EOF'

烧录完成。接下来配置 WiFi：
  1) 串口工具连接该端口（115200）
  2) 发送：wifi 你的SSID 你的密码
  3) 板子重启联网后（可选）发送：host 192.168.x.x   # FMO 设备地址
EOF
