// AudioOut.h —— FMO 实时语音：/audio（8 kHz PCM16 单声道）→ 环形缓冲 → I²S PCM5101
#pragma once

#include <Arduino.h>

#include "Config.h"
#include "WsClient.h"

class AudioOut {
 public:
  bool begin();  // I²S 初始化（失败返回 false，界面会显示"音频不可用"）
  void setTarget(const char* host);

  void start();  // 连接 /audio 并开始推流
  void stop();
  bool on() const { return _want; }

  // net 任务：从 socket 取数据塞进环形缓冲（budgetMs 为本次允许占用时间）
  void loop(uint32_t budgetMs);
  // 音频任务：环形缓冲 → I²S（内部阻塞在 I²S 写入上，节流由硬件决定）
  void taskStep();

  bool socketUp() const { return _up; }
  int level() const { return _level; }  // 0..100 电平
  uint32_t bytes() const { return _bytes; }
  uint32_t dropped() const { return _dropped; }
  uint32_t bufferedMs() const;
  uint32_t msSinceData() const { return _lastDataMs ? (millis() - _lastDataMs) : 0xFFFFFFFFu; }
  // 自检：往环形缓冲灌一段正弦波（不依赖 FMO 推流），用来确认 I²S→喇叭 通路
  void playTestTone(uint32_t ms, uint16_t freqHz = 1000);
  void setGain(uint8_t g) { _gain = g > 100 ? 100 : g; }
  uint8_t gain() const { return _gain; }

 private:
  void ringWrite(const uint8_t* p, size_t n);
  size_t ringRead(uint8_t* p, size_t n);

  WsClient _ws;
  String _host;
  bool _want = false;
  bool _up = false;
  uint32_t _lastTry = 0;
  uint32_t _startedAt = 0;

  uint8_t* _ring = nullptr;
  volatile uint32_t _head = 0;  // 写指针（生产者）
  volatile uint32_t _tail = 0;  // 读指针（消费者）
  volatile uint32_t _fill = 0;  // 已用字节

  uint8_t _gain = AUDIO_GAIN;
  volatile int _level = 0;
  volatile uint32_t _bytes = 0;
  volatile uint32_t _dropped = 0;
  volatile uint32_t _lastDataMs = 0;
  bool _i2sOk = false;
};
