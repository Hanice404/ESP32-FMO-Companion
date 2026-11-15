// FmoClient.h —— FMO 设备客户端（精简版：只要"当前频道 + 当前发言人 + 台站列表"）
#pragma once

#include <Arduino.h>

#include "Config.h"
#include "WsClient.h"

struct StationItem {
  uint16_t uid;
  char name[48];
};

struct FmoState {
  // 链路
  bool linkUp;
  bool evUp;           // /events 推送通道（当前发言人靠它）
  uint32_t rttMs;
  uint32_t rxCount;
  uint32_t txCount;
  uint32_t lastDataMs;
  // 本机身份
  bool hasInfo;
  char callsign[16];
  uint16_t uid;
  // 当前频道（设备里叫"电台"）
  bool hasStation;
  uint16_t stUid;
  char stName[48];
  // 台站列表（分页）
  StationItem stn[STATION_ROWS];
  uint8_t stnN;
  uint16_t stnStart;    // 本页起始下标
  uint8_t stnPage;      // 第几页（从 0 起）
  bool stnHasNext;      // 还有下一页
  // 当前发言人（/events 的 qso 推送）
  char actCall[16];
  uint32_t actTs;      // 最近一次推送的 millis
  bool actLive;
  // 兜底：最近一条通联的对方呼号
  char lastCall[16];
  uint32_t lastTs;     // 设备时间戳（epoch）
  // 变更计数 / 错误
  uint32_t rev;
  char lastErr[40];
};

class FmoClient {
 public:
  bool begin();
  void loop();

  const FmoState& state() const { return _s; }
  FmoState& mut() { return _s; }

  String host() const { return _host; }
  void setHost(const String& h);

  void reqStation(uint16_t uid);   // 切台（界面已做两次确认）
  void reqStationPage(uint16_t start);  // 台站列表翻页
  void reqRefreshAll();
  void prepForFlash(uint32_t suspendMs);

 private:
  struct Req {
    char body[120];
    char expect[44];
    uint8_t tries;
  };

  void enqueue(const char* type, const char* sub, const char* dataJson, const char* expectOverride = nullptr);
  void pumpQueue(uint32_t now);
  void onText(const char* json, size_t len);
  void pollTimers(uint32_t now);

  WsClient _ws;
  WsClient _ev;
  FmoState _s;

  String _host;

  Req _q[6];
  uint8_t _qh = 0;
  uint8_t _qt = 0;
  bool _pending = false;
  uint32_t _pendingAt = 0;

  uint32_t _lastLinkTry = 0;
  uint32_t _lastEvTry = 0;
  uint32_t _failStreak = 0;
  uint32_t _nextTryAt = 0;
  uint32_t _suspendUntil = 0;

  uint32_t _tStation = 0, _tQso = 0, _tInfo = 0, _tList = 0;
  uint16_t _stnStart = 0;   // 当前要显示的台站页起始下标
};
