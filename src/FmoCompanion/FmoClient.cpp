// FmoClient.cpp —— 精简版客户端：当前频道 / 台站列表（分页）/ 当前发言人
#include "FmoClient.h"

#include <ArduinoJson.h>
#include <string.h>

namespace {

void copyStr(char* dst, size_t n, const char* src) {
  if (!dst || n == 0) return;
  if (!src) {
    dst[0] = 0;
    return;
  }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

}  // namespace

bool FmoClient::begin() {
  _s.rev++;
  _s.lastErr[0] = 0;
  _stnStart = 0;
  _tInfo = _tStation = _tQso = _tList = 0;
  return true;
}

void FmoClient::setHost(const String& h) {
  if (h == _host) return;
  _host = h;
  _ws.setTarget(_host.c_str(), FMO_HTTP_PORT, FMO_WS_PATH);
  _ev.setTarget(_host.c_str(), FMO_HTTP_PORT, FMO_EVENTS_PATH);
  if (_ws.connected()) _ws.close();
  if (_ev.connected()) _ev.close();
  _s.linkUp = false;
  _s.evUp = false;
  _s.rev++;
  DBG("[fmo] host -> %s\n", _host.c_str());
}

// ------------------------------------------------------------------ 请求队列
void FmoClient::enqueue(const char* type, const char* sub, const char* dataJson, const char* expectOverride) {
  const uint8_t next = (uint8_t)((_qt + 1) % 6);
  if (next == _qh) {
    DBG("[fmo] queue full, drop %s/%s\n", type, sub);
    return;
  }
  Req& r = _q[_qt];
  snprintf(r.body, sizeof(r.body), "{\"type\":\"%s\",\"subType\":\"%s\",\"data\":%s}", type, sub,
           dataJson ? dataJson : "{}");
  if (expectOverride && expectOverride[0]) {
    snprintf(r.expect, sizeof(r.expect), "%s", expectOverride);
  } else {
    snprintf(r.expect, sizeof(r.expect), "%s/%sResponse", type, sub);
  }
  r.tries = 0;
  _qt = next;
}

void FmoClient::reqStation(uint16_t uid) {
  char d[32];
  snprintf(d, sizeof(d), "{\"uid\":%u}", (unsigned)uid);
  enqueue("station", "setCurrent", d);
  _tStation = 0;
}

// 台站列表翻页：多取一条（STATION_ROWS+1）用来判断还有没有下一页
void FmoClient::reqStationPage(uint16_t start) {
  _stnStart = start;
  char d[48];
  snprintf(d, sizeof(d), "{\"start\":%u,\"count\":%u}", (unsigned)start, (unsigned)(STATION_ROWS + 1));
  enqueue("station", "getListRange", d, "station/getListResponse");
  _tList = millis();
}

void FmoClient::reqRefreshAll() {
  enqueue("user", "getInfo", "{}");
  enqueue("station", "getCurrent", "{}");
  reqStationPage(_stnStart);
  enqueue("qso", "getList", "{\"page\":0,\"pageSize\":1}");
}

void FmoClient::prepForFlash(uint32_t suspendMs) {
  if (_ev.connected()) _ev.close();
  if (_ws.connected()) _ws.close();
  _pending = false;
  _qh = _qt = 0;
  _s.linkUp = false;
  _s.evUp = false;
  _suspendUntil = millis() + suspendMs;
  _s.rev++;
}

void FmoClient::pumpQueue(uint32_t now) {
  if (!_ws.connected()) return;
  if (_pending && (now - _pendingAt) > REQ_TIMEOUT_MS) {
    Req& r = _q[_qh];
    if (r.tries < 2) {
      if (_ws.sendText(r.body)) {
        r.tries++;
        _pendingAt = now;
        return;
      }
    }
    copyStr(_s.lastErr, sizeof(_s.lastErr), "timeout");
    _qh = (uint8_t)((_qh + 1) % 6);
    _pending = false;
    _s.rev++;
  }
  if (!_pending && _qh != _qt) {
    Req& r = _q[_qh];
    DBG("[fmo] -> %s\n", r.expect);
    if (_ws.sendText(r.body)) {
      r.tries = 1;
      _pending = true;
      _pendingAt = now;
      _s.txCount++;
    } else {
      _s.linkUp = false;
    }
  }
}

// ------------------------------------------------------------------ 解析
void FmoClient::onText(const char* json, size_t len) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, (const uint8_t*)json, len);
  if (err) {
    DBG("[fmo] json err %s\n", err.c_str());
    return;
  }
  const char* type = doc["type"] | "";
  const char* sub = doc["subType"] | "";
  if (!type[0] || !sub[0]) return;

  _s.rxCount++;
  _s.lastDataMs = millis();
  DBG("[fmo] <- %s/%s\n", type, sub);

  char key[44];
  snprintf(key, sizeof(key), "%s/%s", type, sub);
  bool matched = false;
  if (_pending && strcmp(key, _q[_qh].expect) == 0) {
    _s.rttMs = millis() - _pendingAt;
    _s.lastErr[0] = 0;
    _qh = (uint8_t)((_qh + 1) % 6);
    _pending = false;
    matched = true;
  }

  JsonObjectConst data = doc["data"].as<JsonObjectConst>();

  if (strcmp(type, "user") == 0 && strcmp(sub, "getInfoResponse") == 0) {
    copyStr(_s.callsign, sizeof(_s.callsign), data["callsign"] | "");
    _s.uid = (uint16_t)(data["uid"] | 0);
    _s.hasInfo = _s.callsign[0] != 0;
    _s.rev++;
  } else if (strcmp(type, "station") == 0 && strcmp(sub, "getCurrentResponse") == 0) {
    _s.stUid = (uint16_t)(data["uid"] | 0);
    copyStr(_s.stName, sizeof(_s.stName), data["name"] | "");
    _s.hasStation = _s.stUid != 0;
    _s.rev++;
  } else if (strcmp(type, "station") == 0 && strcmp(sub, "getListResponse") == 0) {
    JsonArrayConst list = data["list"].as<JsonArrayConst>();
    uint8_t n = 0, total = 0;
    for (JsonObjectConst it : list) {
      total++;
      if (n < STATION_ROWS) {
        _s.stn[n].uid = (uint16_t)(it["uid"] | 0);
        copyStr(_s.stn[n].name, sizeof(_s.stn[n].name), it["name"] | "");
        n++;
      }
    }
    _s.stnN = n;
    _s.stnHasNext = total > STATION_ROWS;   // 多取了一条还在 → 有下一页
    _s.stnStart = _stnStart;
    _s.stnPage = (uint8_t)(_stnStart / STATION_ROWS);
    _s.rev++;
  } else if (strcmp(type, "qso") == 0 && strcmp(sub, "getListResponse") == 0) {
    // 只要最新一条：兜底显示"最近通话"
    JsonArrayConst list = data["list"].as<JsonArrayConst>();
    for (JsonObjectConst it : list) {
      copyStr(_s.lastCall, sizeof(_s.lastCall), it["toCallsign"] | "");
      _s.lastTs = it["timestamp"] | 0;
      break;
    }
    _s.rev++;
  } else if (!matched) {
    // /events 推送：当前发言人
    if (strcmp(type, "qso") == 0) {
      const char* call = data["callsign"] | "";
      if (!call[0]) call = data["toCallsign"] | "";
      if (call[0]) {
        if (strcmp(call, _s.actCall) != 0) {
          copyStr(_s.actCall, sizeof(_s.actCall), call);
          _s.rev++;
        }
        if (!_s.actLive) {
          _s.actLive = true;
          _s.rev++;
        }
        _s.actTs = millis();
      }
    }
  }
}

// ------------------------------------------------------------------ 轮询
void FmoClient::pollTimers(uint32_t now) {
  if (_qh != _qt) return;
  // 身份拉取失败（设备忙 / 刚重连）时快速重试，别让呼号空着等 2 分钟
  const uint32_t infoGap = _s.hasInfo ? POLL_INFO_MS : 15000;
  if (_tInfo == 0 || now - _tInfo > infoGap) {
    _tInfo = now;
    enqueue("user", "getInfo", "{}");
    return;
  }
  if (_tStation == 0 || now - _tStation > POLL_STATION_MS) {
    _tStation = now;
    enqueue("station", "getCurrent", "{}");
    return;
  }
  if (_tQso == 0 || now - _tQso > POLL_QSO_MS) {
    _tQso = now;
    enqueue("qso", "getList", "{\"page\":0,\"pageSize\":1}");
    return;
  }
  if (_tList == 0 || now - _tList > POLL_LIST_MS) {
    _tList = now;
    reqStationPage(_stnStart);
    return;
  }
}

// ------------------------------------------------------------------ 主循环
void FmoClient::loop() {
  const uint32_t now = millis();
  if (_host.isEmpty()) return;
  if (_suspendUntil != 0 && (int32_t)(now - _suspendUntil) < 0) return;
  _suspendUntil = 0;

  // 发言人状态：推送后 20 s 内算"正在讲话"
  if (_s.actLive && (int32_t)(now - _s.actTs) > 20000) {
    _s.actLive = false;
    _s.rev++;
  }

  if (!_ws.connected()) {
    if (_nextTryAt == 0 || (int32_t)(now - _nextTryAt) >= 0) {
      _lastLinkTry = now;
      if (_ws.connect(2500)) {
        DBG("[fmo] /ws connected to %s\n", _host.c_str());
        _s.linkUp = true;
        _s.rev++;
        _failStreak = 0;
        _nextTryAt = 0;
        _s.lastDataMs = now;
        _qh = _qt = 0;
        _pending = false;
        reqRefreshAll();
        _tInfo = _tStation = _tQso = _tList = now;
      } else {
        if (_s.linkUp) {
          _s.linkUp = false;
          _s.rev++;
        }
        if (_failStreak < 0xFFFF) _failStreak++;
        copyStr(_s.lastErr, sizeof(_s.lastErr), "connect fail");
        uint32_t wait = 2000u << (_failStreak > 3 ? 3 : (_failStreak - 1));
        if (wait > 20000u) wait = 20000u;
        _nextTryAt = now + wait;
      }
    }
  } else {
    _s.linkUp = true;
    const int r = _ws.poll(3);
    if (r == 1 && !_ws.isBinary()) onText(_ws.text(), _ws.length());
    else if (r < 0) {
      _s.linkUp = false;
      _pending = false;
      _s.rev++;
      DBG("[fmo] /ws closed\n");
    }
    pumpQueue(now);
    pollTimers(now);
  }

  // 90 s 没有任何数据就回收连接（有符号比较，避免 lastDataMs 比 now 新时溢出）
  if (_ws.connected() && _s.lastDataMs != 0 && (int32_t)(now - _s.lastDataMs) > 90000) {
    DBG("[fmo] link idle 90s, recycle\n");
    _ws.close();
    _s.linkUp = false;
    _nextTryAt = now + 1000;
    _s.rev++;
  }

  // 推送通道：主通道没起来就不连
  if (!_ws.connected()) {
    _s.evUp = false;
    if (_ev.connected()) _ev.close();
    return;
  }
  if (!_ev.connected()) {
    if (_lastEvTry == 0 || now - _lastEvTry > 5000) {
      _lastEvTry = now;
      if (_ev.connect(2000)) {
        DBG("[fmo] /events connected\n");
        _s.evUp = true;
        _s.rev++;
      } else {
        _s.evUp = false;
      }
    }
  } else {
    _s.evUp = true;
    const int r = _ev.poll(0);
    if (r == 1 && !_ev.isBinary()) onText(_ev.text(), _ev.length());
    else if (r < 0) {
      _s.evUp = false;
      _s.rev++;
    }
  }
}
