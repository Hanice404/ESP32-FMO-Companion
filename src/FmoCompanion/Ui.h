// Ui.h —— 360x360 圆屏界面（精简版：2 页）
//   0 频道：当前频道名称 + 当前发言人 + 音频状态/电平（长按 BOOT 开关音频）
//   1 电台：台站列表（两次确认切换）
//   2 音量·亮度：两条可调滑杆
#pragma once

#include <Arduino.h>

#include "AudioOut.h"
#include "Config.h"
#include "FmoClient.h"

class Ui {
 public:
  enum Page { P_MAIN = 0, P_STATION, P_SETUP, P_COUNT };

  void begin(FmoClient* fmo, AudioOut* audio);
  void splash();
  void render(bool force);
  void invalidate() { _dirty = true; }

  void onTap(uint16_t x, uint16_t y);
  void onSwipe(int8_t dir);
  void toggleAudio();   // BOOT 长按 / 调试命令调用

  void setPage(uint8_t p);
  uint8_t page() const { return _page; }
  void setBrightness(uint8_t b) { _bright = b; }
  uint8_t brightness() const { return _bright; }
  void adjustBrightness(int delta);
  void applyBrightness(uint8_t b);
  void setBattery(float v) {
    if ((int)(v * 10) != (int)(_batV * 10)) _dirty = true;
    _batV = v;
  }
  void toast(const char* msg);

 private:
  void drawFrame();
  void drawTopBar();
  void drawBottomBar();
  bool toastActive() const { return _toast[0] && (int32_t)(millis() - _toastUntil) < 0; }
  void drawTitle(const char* zh, const char* en, const char* right);
  void drawMain();
  void drawAudioStrip(int y);
  void drawStation();
  void drawSetup();
  void drawWrapped(int x, int y, int w, int lineH, int maxLines, const char* s, int size, uint16_t c,
                   bool center = false);
  void changePage(uint8_t p);
  void beginTransition();

  FmoClient* _fmo = nullptr;
  AudioOut* _audio = nullptr;
  uint8_t _page = P_MAIN;
  uint8_t _bright = BRIGHT_DEFAULT;
  bool _dirty = true;
  uint32_t _lastRev = 0xFFFFFFFF;
  uint32_t _lastDrawMs = 0;
  bool _fading = false;
  bool _fadeTargetSaved = false;
  uint32_t _fadeT0 = 0;
  float _batV = 0.0f;
  char _toast[48] = {0};
  uint32_t _toastUntil = 0;
  int8_t _stnArmed = -1;
  uint32_t _stnArmedAt = 0;
};
