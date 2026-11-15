// FmoCompanion.ino —— FMO 伴侣硬件主程序
// 目标板：微雪 ESP32-S3-Touch-LCD-1.85（360x360 圆屏 / PCM5101 音频 / CST816 触摸）
//
// 结构：
//   netTask   (core 0)  WiFi + FMO 主通道 /ws + 推送通道 /events + 音频通道 /audio
//   audioTask (core 1)  环形缓冲 → I²S（PCM5101）
//   loop      (core 1)  触摸 / 渲染 / 串口命令 / 网络调试台
//
// 调试：USB 串口 115200 与 TCP 2323 两套入口，命令完全一样（含 shot 抓屏）。
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "AudioOut.h"
#include "BAT_Driver.h"
#include "Config.h"
#include "Display_ST77916.h"
#include "FmoClient.h"
#include "Gfx.h"
#include "I2C_Driver.h"
#include "PWR_Key.h"
#include "Settings.h"
#include "TCA9554PWR.h"
#include "Touch_CST816.h"
#include "Ui.h"

static FmoClient g_fmo;
static AudioOut g_audio;
static Ui g_ui;

static bool g_wifiUp = false;
static uint32_t g_lastWifiTry = 0;
static String g_host = "";
static bool g_shotRequest = false;
static volatile bool g_dumping = false;
static uint8_t g_rot = SCREEN_ROT_DEFAULT;
static float g_batV = 0.0f;

// 触摸
static uint8_t g_lastGesture = NONE;
static uint32_t g_gestureAt = 0;
static uint32_t g_tTouch = 0, g_tBat = 0, g_tDebug = 0;
static uint32_t g_touchSeenAt = 0;   // 最近一次"真的按到面板"的时刻

// 网络调试台（TCP 2323）
static WiFiServer g_dbgServer(2323);
static WiFiClient g_dbgClient;
static char g_dbgBuf[160];
static int g_dbgLen = 0;

// 统一日志：DBG 同时写 USB 串口与 TCP 调试台
void dbgLog(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  if (g_dbgClient && g_dbgClient.connected()) g_dbgClient.print(buf);
}

// ------------------------------------------------------------------ 触摸坐标变换
// CST816 上报面板物理坐标；屏幕方向由 MADCTL 改，所以逻辑坐标要反向旋转回来
static void transformTouch(uint16_t& x, uint16_t& y) {
  const uint16_t last = SCREEN_W - 1;
  uint16_t nx = x, ny = y;
  switch (g_rot & 3) {
    case 1: nx = (uint16_t)(last - y); ny = x; break;
    case 2: nx = (uint16_t)(last - x); ny = (uint16_t)(last - y); break;
    case 3: nx = y; ny = (uint16_t)(last - x); break;
    default: return;
  }
  x = nx;
  y = ny;
}

// ------------------------------------------------------------------ 状态 / 抓屏
static void dumpShot(Print& out) {
  const uint16_t* fb = gfx::pixels();
  if (!fb) return;
  g_dumping = true;
  out.printf("SHOT %d %d 565\n", SCREEN_W, SCREEN_H);
  char line[SCREEN_W * 4 + 12];
  for (int y = 0; y < SCREEN_H; y++) {
    int o = 0;
    line[o++] = 'D';
    line[o++] = (char)('0' + (y / 100) % 10);
    line[o++] = (char)('0' + (y / 10) % 10);
    line[o++] = (char)('0' + y % 10);
    line[o++] = ':';
    for (int x = 0; x < SCREEN_W; x++) {
      const uint16_t v = fb[(size_t)y * SCREEN_W + x];
      line[o++] = "0123456789abcdef"[(v >> 12) & 0xF];
      line[o++] = "0123456789abcdef"[(v >> 8) & 0xF];
      line[o++] = "0123456789abcdef"[(v >> 4) & 0xF];
      line[o++] = "0123456789abcdef"[v & 0xF];
    }
    line[o++] = '\n';
    out.write((const uint8_t*)line, o);
  }
  out.printf("SHOT END\n");
  g_dumping = false;
}

static void printState(Print& out) {
  if (g_dumping) return;
  const FmoState& s = g_fmo.state();
  out.printf("[state] host=%s link=%d ev=%d rtt=%ums rx=%u tx=%u wifi=%d rssi=%d\n", g_host.c_str(),
             (int)s.linkUp, (int)s.evUp, (unsigned)s.rttMs, (unsigned)s.rxCount, (unsigned)s.txCount,
             (int)WiFi.status(), WiFi.RSSI());
  out.printf("        call=%s uid=%u station=%u/%s speaker=%s(%s) last=%s stn=%u\n", s.callsign,
             (unsigned)s.uid, (unsigned)s.stUid, s.stName, s.actCall, s.actLive ? "live" : "-",
             s.lastCall, (unsigned)s.stnN);
  out.printf("        page=%u srot=%u bright=%u bootPin=%d audio=%d/%d gain=%u level=%d bytes=%u err=%s bat=%.2fV heap=%u/%u KB\n",
             (unsigned)g_ui.page(), (unsigned)g_rot, (unsigned)g_ui.brightness(), digitalRead(KEY_BOOT_PIN),
             (int)g_audio.on(),
             (int)g_audio.socketUp(), (unsigned)g_audio.gain(), g_audio.level(), (unsigned)g_audio.bytes(), s.lastErr, g_batV,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

// ------------------------------------------------------------------ 命令分发（串口 / TCP 共用）
static void runCommand(const char* buf, Print& out) {
  int a = 0, b = 0;
  if (strncmp(buf, "srot ", 5) == 0) {
    g_rot = (uint8_t)(atoi(buf + 5) & 3);
    LCD_SetRotation(g_rot);
    g_ui.invalidate();
    out.printf("[cmd] srot=%u\n", (unsigned)g_rot);
  } else if (strncmp(buf, "bright ", 7) == 0) {
    g_ui.setBrightness((uint8_t)atoi(buf + 7));
    Set_Backlight(g_ui.brightness());
    settings::setBrightness(g_ui.brightness());
    out.printf("[cmd] bright=%u\n", (unsigned)g_ui.brightness());
  } else if (strncmp(buf, "page ", 5) == 0) {
    g_ui.setPage((uint8_t)atoi(buf + 5));
    out.printf("[cmd] page=%u\n", (unsigned)g_ui.page());
  } else if (strncmp(buf, "tap ", 4) == 0) {
    if (sscanf(buf + 4, "%d %d", &a, &b) == 2) {
      g_ui.onTap((uint16_t)a, (uint16_t)b);
      out.printf("[cmd] tap %d,%d\n", a, b);
    }
  } else if (strncmp(buf, "swipe ", 6) == 0) {
    const char d = buf[6];
    g_ui.onSwipe(d == 'l' || d == 'L' ? 1 : -1);
    out.printf("[cmd] swipe %c -> page %u\n", d, (unsigned)g_ui.page());

  } else if (strncmp(buf, "gain ", 5) == 0) {
    g_audio.setGain((uint8_t)atoi(buf + 5));
    settings::setGain(g_audio.gain());
    g_ui.invalidate();
    out.printf("[cmd] gain=%u\n", (unsigned)g_audio.gain());
  } else if (strncmp(buf, "audio ", 6) == 0) {
    if (buf[6] == '1') g_audio.start();
    else g_audio.stop();
    settings::setAudioOn(g_audio.on());
    g_ui.invalidate();
    out.printf("[cmd] audio=%d\n", (int)g_audio.on());
  } else if (strncmp(buf, "host ", 5) == 0) {
    g_host = String(buf + 5);
    settings::setFmoHost(g_host);
    g_fmo.setHost(g_host);
    g_audio.setTarget(g_host.c_str());
    out.printf("[cmd] host=%s (saved)\n", g_host.c_str());
  } else if (strncmp(buf, "wifi ", 5) == 0) {
    char ssid[40] = {0};
    char pass[64] = {0};
    const int n = sscanf(buf + 5, "%39s %63s", ssid, pass);
    if (n >= 1) {
      settings::setWifi(String(ssid), String(pass));
      out.printf("[cmd] wifi saved: ssid=%s pass=%s, rebooting...\n", ssid, n >= 2 ? "***" : "(empty)");
      settings::flush();
      delay(100);
      ESP.restart();
    } else {
      out.printf("[cmd] usage: wifi SSID PASSWORD\n");
    }

  } else if (strncmp(buf, "refresh", 7) == 0) {
    g_fmo.reqRefreshAll();
    out.printf("[cmd] refresh all\n");
  } else if (strcmp(buf, "stat") == 0) {
    printState(out);
  } else if (strcmp(buf, "shot") == 0) {
    dumpShot(out);
  } else if (strncmp(buf, "fakecall ", 9) == 0) {
    // 调试：伪造一次"当前发言人"推送，用来看 1 屏的讲话态
    FmoState& st = g_fmo.mut();
    strncpy(st.actCall, buf + 9, sizeof(st.actCall) - 1);
    st.actCall[sizeof(st.actCall) - 1] = 0;
    st.actTs = millis();
    st.actLive = true;
    st.rev++;
    g_ui.setPage(Ui::P_MAIN);
    out.printf("[cmd] fake speaker = %s\n", st.actCall);
  } else if (strncmp(buf, "tone", 4) == 0) {
    // 自检：不给 FMO 推流也能验证 I²S→PCM5101→喇叭 通路
    const int ms = atoi(buf + 4) > 0 ? atoi(buf + 4) : 1000;
    g_audio.playTestTone((uint32_t)ms, 1000);
    g_ui.invalidate();
    out.printf("[cmd] tone %d ms @1kHz queued (gain %u%%)\n", ms, (unsigned)g_audio.gain());
  } else if (strcmp(buf, "prep") == 0) {
    // 烧录前清理：礼貌关掉通道、暂停重连、把设置落盘
    settings::flush();
    g_audio.stop();
    g_fmo.prepForFlash(120000);
    out.printf("[cmd] channels closed, safe to flash\n");
  } else if (strcmp(buf, "reboot") == 0) {
    g_audio.stop();
    g_fmo.prepForFlash(3000);
    out.printf("[cmd] rebooting\n");
    delay(50);
    ESP.restart();
  } else if (strcmp(buf, "help") == 0) {
    out.printf("[cmd] stat | shot | page 0/1/2 | tap X Y | swipe l/r | refresh | tone [ms] | prep\n");
    out.printf("      audio 0/1 | gain N | bright N | srot N | host IP | fakecall CALL | reboot\n");
    out.printf("      wifi SSID PASSWORD  (保存 WiFi 并重启)\n");
    out.printf("      (音频开关：长按 BOOT 键，或 audio 0/1)\n");
  } else {
    out.printf("[cmd] unknown: %s\n", buf);
  }
}

static void handleSerial() {
  static char buf[160];
  static int n = 0;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (n < (int)sizeof(buf) - 1) buf[n++] = c;
      continue;
    }
    buf[n] = 0;
    n = 0;
    if (buf[0]) runCommand(buf, Serial);
  }
}

// 网络调试台：USB 串口挂了也能继续调试（这块板的 USB-Serial-JTAG 会偶发卡死）
static void handleTcpDebug() {
  if (!g_dbgClient || !g_dbgClient.connected()) {
    if (g_dbgClient) g_dbgClient.stop();
    WiFiClient c = g_dbgServer.accept();
    if (c) {
      g_dbgClient = c;
      g_dbgClient.setNoDelay(true);
      g_dbgLen = 0;
      g_dbgClient.printf("FMO Companion debug console\n");
      printState(g_dbgClient);
      g_dbgClient.printf("> ");
    }
    return;
  }
  while (g_dbgClient.available()) {
    const char ch = (char)g_dbgClient.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (g_dbgLen < (int)sizeof(g_dbgBuf) - 1) g_dbgBuf[g_dbgLen++] = ch;
      continue;
    }
    g_dbgBuf[g_dbgLen] = 0;
    g_dbgLen = 0;
    if (g_dbgBuf[0]) {
      runCommand(g_dbgBuf, g_dbgClient);
      g_dbgClient.printf("> ");
    }
  }
}

// ------------------------------------------------------------------ WiFi
static void wifiTick() {
  const uint32_t now = millis();
  const String ssid = settings::wifiSsid();
  const String pass = settings::wifiPass();
  if (ssid.isEmpty()) {
    if (g_wifiUp) {
      g_wifiUp = false;
      Serial.println("[wifi] no credentials configured");
    }
    return;   // 未配网：屏幕会提示"未配置 WiFi"
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (g_wifiUp) {
      g_wifiUp = false;
      Serial.println("[wifi] disconnected");
    }
    if (g_lastWifiTry == 0 || now - g_lastWifiTry > 6000) {
      g_lastWifiTry = now;
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      WiFi.begin(ssid.c_str(), pass.c_str());
      Serial.printf("[wifi] connecting to \"%s\" ...\n", ssid.c_str());
    }
    return;
  }
  if (g_wifiUp) return;

  g_wifiUp = true;
  Serial.printf("[wifi] connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  MDNS.begin(DEV_HOSTNAME);
  g_dbgServer.begin();
  g_dbgServer.setNoDelay(true);

  // 地址优先级：手动指定的 host → mDNS 的 fmo.local → 编译期回落 IP
  String ip = settings::fmoHost();
  if (!ip.isEmpty()) {
    Serial.printf("[host] using configured %s\n", ip.c_str());
  }
  for (int attempt = 0; attempt < 3 && ip.isEmpty(); attempt++) {
    if (MDNS.queryHost(FMO_HOST_MDNS, 1500)) {
      const IPAddress resolved = MDNS.queryHost(FMO_HOST_MDNS);
      if (resolved != IPAddress(0, 0, 0, 0)) {
        ip = resolved.toString();
        Serial.printf("[mdns] %s -> %s (attempt %d)\n", FMO_HOST_MDNS, ip.c_str(), attempt + 1);
      }
    }
    if (ip.isEmpty()) delay(400);
  }
  if (ip.isEmpty() && FMO_FALLBACK_IP[0]) {
    ip = FMO_FALLBACK_IP;
    Serial.printf("[mdns] %s 解析失败，回落到 %s\n", FMO_HOST_MDNS, ip.c_str());
  }
  if (ip.isEmpty()) {
    Serial.printf("[host] 未找到 FMO 设备：请串口发 `host 192.168.x.x`\n");
  }
  g_host = ip;
  g_fmo.setHost(g_host);
  g_audio.setTarget(g_host.c_str());

  // 底栏时钟（同步不上也不影响主功能）
  configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org");
}

// ------------------------------------------------------------------ 任务
static void netTask(void* arg) {
  (void)arg;
  for (;;) {
    wifiTick();
    if (g_wifiUp) {
      g_fmo.loop();
      g_audio.loop(15);
    }
    vTaskDelay(pdMS_TO_TICKS(4));
  }
}

static void audioTask(void* arg) {
  (void)arg;
  for (;;) {
    g_audio.taskStep();
  }
}

// ------------------------------------------------------------------ 触摸
// ------------------------------------------------------------------ BOOT 键（GPIO0）：长按开/关实时音频
static uint32_t g_bootDownAt = 0;
static bool g_bootArmed = false;     // 见到松开才武装，避免上电/烧录时误判
static bool g_bootFired = false;
static uint32_t g_tBoot = 0;

static void handleBootKey() {
  const bool down = (digitalRead(KEY_BOOT_PIN) == LOW);
  const uint32_t now = millis();
  if (!down) {
    g_bootArmed = true;
    g_bootDownAt = 0;
    g_bootFired = false;
    return;
  }
  if (!g_bootArmed) return;
  if (g_bootDownAt == 0) g_bootDownAt = now;
  if (!g_bootFired && (now - g_bootDownAt) >= BOOT_LONG_PRESS_MS) {
    g_bootFired = true;
    g_ui.toggleAudio();
    DBG("[boot] long press -> audio %s\n", g_audio.on() ? "ON" : "OFF");
  }
}
static void handleTouch() {
  Touch_Read_Data();
  const uint8_t g = (uint8_t)touch_data.gesture;
  const uint32_t now = millis();
  if (touch_data.points != 0) g_touchSeenAt = now;

  if (g == NONE) {
    g_lastGesture = NONE;
    return;
  }
  // 防误触：CST816 偶尔会在没有实际按压时报出手势，必须最近真的按到过面板才理会
  if (g_touchSeenAt == 0 || (now - g_touchSeenAt) > 500) {
    // CST816 的手势寄存器会一直保持上一次的值，没按压时会反复报同一个手势；
    // 这里直接忽略，并且只在上电后记一条日志（否则每秒刷屏）。
    static bool loggedOnce = false;
    if (!loggedOnce) {
      DBG("[touch] phantom gestures ignored (no press seen)\n");
      loggedOnce = true;
    }
    g_lastGesture = g;
    return;
  }
  if (g == g_lastGesture && (now - g_gestureAt) < 900) return;
  if (now - g_gestureAt < GESTURE_COOLDOWN_MS) return;
  g_lastGesture = g;
  g_gestureAt = now;

  uint16_t x = touch_data.x, y = touch_data.y;
  transformTouch(x, y);
  DBG("[touch] gesture=%u raw=%u,%u -> %u,%u\n", (unsigned)g, touch_data.x, touch_data.y, x, y);

  switch (g) {
    case SWIPE_LEFT:
      g_ui.onSwipe(1);
      break;
    case SWIPE_RIGHT:
      g_ui.onSwipe(-1);
      break;
    case SWIPE_UP:
      g_ui.adjustBrightness(BRIGHT_STEP);
      break;
    case SWIPE_DOWN:
      g_ui.adjustBrightness(-BRIGHT_STEP);
      break;
    case SINGLE_CLICK:
      g_ui.onTap(x, y);
      break;
    case DOUBLE_CLICK:
      g_ui.onSwipe(1);
      break;
    case LONG_PRESS:
      // 音频改由 BOOT 长按控制，这里留空（避免误触）
      break;
    default:
      break;
  }
}

// ------------------------------------------------------------------ setup / loop
void setup() {
  Serial.begin(115200);
  delay(200);
  DBG("\n=== FMO Companion boot ===\n");
  DBG("[reset] reason=%d\n", (int)esp_reset_reason());
  DBG("[heap] internal=%u KB  psram=%u KB\n",
      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

  // 1. 电源：无条件维持 SYS_EN（否则接电池时靠 USB 供电，插拔就掉电）
  PWR_Init();
  digitalWrite(PWR_Control_PIN, HIGH);
  pinMode(KEY_BOOT_PIN, INPUT_PULLUP);

  // 2. 电池
  BAT_Init();

  // 3. 共享 I2C（10/11）
  I2C_Init();

  // 4. 扩展 IO：LCD / 触摸复位都挂在 TCA9554 上，必须早于 LCD_Init
  TCA9554PWR_Init(0x00);

  // 5. 背光先灭
  Backlight_Init();
  LCD_Backlight = 0;
  Set_Backlight(0);

  // 6. LCD + 触摸
  LCD_Init();
  LCD_SetRotation(g_rot);
  DBG("[lcd] init done, rot=%u\n", (unsigned)g_rot);

  // 7. 持久化设置（亮度 / 音频开关 / 音量）
  settings::begin();

  // 8. 图形层
  if (!gfx::begin()) DBG("[gfx] framebuffer alloc FAILED\n");
  g_ui.begin(&g_fmo, &g_audio);
  g_ui.setBrightness(settings::brightness());

  // 9. 首帧（背光未开）
  g_ui.splash();
  LCD_WaitFlush();
  Set_Backlight(settings::brightness());
  DBG("[lcd] backlight on (%u%%)\n", (unsigned)settings::brightness());

  // 10. 音频：恢复上次的音量与开关状态
  if (!g_audio.begin()) DBG("[audio] init failed\n");
  g_audio.setGain(settings::gain());
  if (settings::audioOn()) {
    g_audio.start();
    DBG("[audio] restored ON (gain %u)\n", (unsigned)settings::gain());
  }

  // 10. 客户端 + 网络任务
  g_fmo.begin();
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 4, nullptr, 0);
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 3, nullptr, 1);

  g_tTouch = g_tBat = g_tDebug = millis();
  DBG("[boot] ready (FMO companion v0.2)\n");
}

void loop() {
  const uint32_t now = millis();

  if (g_shotRequest) {
    g_shotRequest = false;
    dumpShot(Serial);
  }

  handleSerial();
  handleTcpDebug();

  if (now - g_tTouch >= TOUCH_POLL_MS) {
    g_tTouch = now;
    handleTouch();
  }

  settings::loop();

  if (now - g_tBoot >= BOOT_POLL_MS) {
    g_tBoot = now;
    handleBootKey();
  }

  if (now - g_tBat >= BAT_PERIOD_MS) {
    g_tBat = now;
    g_batV = BAT_Get_Volts();
    g_ui.setBattery(g_batV);
  }


  g_ui.render(false);

  if (now - g_tDebug >= 5000) {
    g_tDebug = now;
    if (!g_dbgClient || !g_dbgClient.connected()) printState(Serial);
  }

  PWR_Loop();
  delay(2);
}
