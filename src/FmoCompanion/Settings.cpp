#include "Settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "Config.h"

namespace settings {
namespace {

Preferences s_prefs;
bool s_ready = false;

uint8_t s_bright = BRIGHT_DEFAULT;
bool s_audio = false;
uint8_t s_gain = AUDIO_GAIN;
String s_ssid;
String s_pass;
String s_host;

bool s_dirty = false;
uint32_t s_dirtyAt = 0;

constexpr uint32_t kFlushDelayMs = 900;

void writeNow() {
  if (!s_ready) return;
  s_prefs.putUChar("bright", s_bright);
  s_prefs.putUChar("gain", s_gain);
  s_prefs.putBool("audio", s_audio);
  s_dirty = false;
}

}  // namespace

void begin() {
  s_ready = s_prefs.begin("fmo", false);
  if (!s_ready) {
    DBG("[sett] NVS open failed, using defaults\n");
    return;
  }
  s_bright = s_prefs.getUChar("bright", BRIGHT_DEFAULT);
  s_gain = s_prefs.getUChar("gain", AUDIO_GAIN);
  s_audio = s_prefs.getBool("audio", false);
  s_ssid = s_prefs.getString("ssid", WIFI_SSID);
  s_pass = s_prefs.getString("pass", WIFI_PASS);
  s_host = s_prefs.getString("host", "");
  if (s_bright < BRIGHT_MIN) s_bright = BRIGHT_MIN;
  if (s_bright > 100) s_bright = 100;
  if (s_gain > 100) s_gain = 100;
  DBG("[sett] loaded bright=%u audio=%d gain=%u ssid=%s host=%s\n", (unsigned)s_bright, (int)s_audio,
      (unsigned)s_gain, s_ssid.length() ? s_ssid.c_str() : "(none)", s_host.length() ? s_host.c_str() : "(mdns)");
}

void loop() {
  if (!s_dirty) return;
  if (millis() - s_dirtyAt >= kFlushDelayMs) writeNow();
}

uint8_t brightness() { return s_bright; }

void setBrightness(uint8_t b) {
  if (b < BRIGHT_MIN) b = BRIGHT_MIN;
  if (b > 100) b = 100;
  if (b == s_bright) return;
  s_bright = b;
  s_dirty = true;
  s_dirtyAt = millis();
}

bool audioOn() { return s_audio; }

void setAudioOn(bool on) {
  if (on == s_audio) return;
  s_audio = on;
  writeNow();   // 开关很少按，直接落盘
}

uint8_t gain() { return s_gain; }

void setGain(uint8_t g) {
  if (g > 100) g = 100;
  if (g == s_gain) return;
  s_gain = g;
  s_dirty = true;
  s_dirtyAt = millis();
}

bool wifiConfigured() { return s_ssid.length() > 0; }

String wifiSsid() { return s_ssid; }
String wifiPass() { return s_pass; }

void setWifi(const String& ssid, const String& pass) {
  s_ssid = ssid;
  s_pass = pass;
  if (s_ready) {
    s_prefs.putString("ssid", s_ssid);
    s_prefs.putString("pass", s_pass);
  }
}

String fmoHost() { return s_host; }

void setFmoHost(const String& host) {
  s_host = host;
  if (s_ready) s_prefs.putString("host", s_host);
}

void flush() {
  if (s_dirty) writeNow();
}

}  // namespace settings
