// Config.h —— FMO 伴侣固件配置（唯一需要改的文件）
#pragma once

// ============================================================
// 1. 网络
// ============================================================
// ⚠️ 出厂为空：烧录后用串口（115200）发 `wifi 你的SSID 你的密码` 配网，
//    存 NVS 后立刻生效并重启；也可以直接在这里填好再编译。
#define WIFI_SSID           ""
#define WIFI_PASS           ""
#define WIFI_CONNECT_MS     20000     // 连不上就继续重试（后台重连，不阻塞界面）

// FMO 设备：默认走 mDNS 名 fmo.local；解析失败时回落到固定 IP
#define FMO_HOST_MDNS       "fmo.local"
// 可选回落地址：mDNS 解析不到 fmo.local 时用。串口发 `host 192.168.x.x` 会持久保存。
#define FMO_FALLBACK_IP     ""
#define FMO_HTTP_PORT       80
#define FMO_WS_PATH         "/ws"       // 主通道：请求 / 响应
#define FMO_EVENTS_PATH     "/events"   // 只推不收：通话/消息/升级推送
#define FMO_AUDIO_PATH      "/audio"    // 二进制 PCM：16-bit LE / 单声道 / 8000 Hz

#define DEV_HOSTNAME        "fmo-companion"   // 局域网名：fmo-companion.local（OTA 用）
#define OTA_PASSWORD        "fmocompanion"

// ============================================================
// 2. 屏幕
// ============================================================
#define SCREEN_W   360
#define SCREEN_H   360
// 屏幕方向：0 / 1 / 2 / 3 = 0° / 90° / 180° / 270°（串口/TCP 命令 `srot N` 可临时改）
// 本机按你的要求默认 180°；改回正装就把它设成 0 重新烧录。
#define SCREEN_ROT_DEFAULT 2

#define BRIGHT_DEFAULT 80     // 背光 0~100
#define BRIGHT_MIN     5
#define BRIGHT_STEP    10
#define UI_FPS_CAP     20     // 界面刷新上限

// ============================================================
// 3. 音频（PCM5101，I²S 输出）
// ============================================================
#define I2S_BCLK_PIN   48
#define I2S_LRCK_PIN   38
#define I2S_DOUT_PIN   47
#define AUDIO_RATE     8000   // FMO /audio 推流为 8 kHz
#define AUDIO_GAIN     70     // 软件音量 0~100
#define AUDIO_RING_KB  48     // 接收环形缓冲（PSRAM）

// ============================================================
// 4. 数据轮询节奏（ms）
// ============================================================
#define POLL_STATION_MS   4000    // 当前频道
#define POLL_QSO_MS       15000   // 最近一条通联（当前发言人的兜底）
#define POLL_INFO_MS      120000  // 身份
#define POLL_LIST_MS      120000  // 台站列表
#define REQ_TIMEOUT_MS    1500    // 设备忙时会丢请求 → 超时补发一次
#define STATION_ROWS      4

// ============================================================
// 5. 交互
// ============================================================
#define TOUCH_POLL_MS      20
// BOOT 键（GPIO0）：长按开/关实时音频
#define KEY_BOOT_PIN       0        // 避开板级 variant 里的 BOOT_PIN
#define BOOT_POLL_MS       30
#define BOOT_LONG_PRESS_MS 700UL
#define GESTURE_COOLDOWN_MS 400
#define PWR_LONG_PRESS_MS  1500
#define BAT_PERIOD_MS      2000

// ============================================================
// 6. 调试
// ============================================================
#define DEBUG_SERIAL 1
#if DEBUG_SERIAL
// 统一日志出口：同时写 USB 串口和 TCP 调试台（2323）
void dbgLog(const char* fmt, ...);
#define DBG(...) dbgLog(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif
