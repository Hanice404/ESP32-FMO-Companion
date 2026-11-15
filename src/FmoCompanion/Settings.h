// Settings.h —— 需要掉电保持的本机设置（NVS）：背光亮度、音频开关、音量增益
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace settings {

void begin();                     // 从 NVS 读出上次的值（没有就用默认）
void loop();                      // 延迟落盘（亮度滑动时不必每步都写 flash）

uint8_t brightness();
void setBrightness(uint8_t b);    // 记录 + 延迟落盘

bool audioOn();
void setAudioOn(bool on);         // 立即落盘（开关动作很少）

uint8_t gain();
void setGain(uint8_t g);

// WiFi / FMO 地址（公开固件不含凭据：烧录后串口发 `wifi SSID 密码` 配网）
String wifiSsid();
String wifiPass();
void setWifi(const String& ssid, const String& pass);
String fmoHost();                 // 手动指定的 FMO 地址（空 = 走 fmd.local / 默认 IP）
void setFmoHost(const String& host);

bool wifiConfigured();

void flush();                     // 强制立刻落盘

}  // namespace settings
