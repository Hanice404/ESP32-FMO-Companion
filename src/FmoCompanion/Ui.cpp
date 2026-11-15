#include "Ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <WiFi.h>

#include "Display_ST77916.h"
#include "Settings.h"
#include "Gfx.h"

namespace {

using namespace gfx;

constexpr int W = SCREEN_W;
constexpr int H = SCREEN_H;
constexpr int CX = W / 2;

const char* kPageZh[Ui::P_COUNT] = {"频道", "电台", "音量·亮度"};
const char* kPageEn[Ui::P_COUNT] = {"channel", "station", "setup"};

// 音量/亮度页的滑杆几何（绘制与点击共用）
constexpr int kBarX = 80, kBarW = 200, kBarH = 16;
constexpr int kRowY[2] = {128, 220};        // 两条滑杆的 y
constexpr int kMinusX = 48, kPlusX = 312;

void fmtTime(uint32_t ts, char* out, size_t n) {
  if (ts == 0) {
    snprintf(out, n, "--:--");
    return;
  }
  time_t t = (time_t)ts + 8 * 3600;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  snprintf(out, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

int rssiBars(int rssi) {
  if (rssi > -55) return 4;
  if (rssi > -65) return 3;
  if (rssi > -75) return 2;
  if (rssi > -85) return 1;
  return 0;
}

}  // namespace

void Ui::begin(FmoClient* fmo, AudioOut* audio) {
  _fmo = fmo;
  _audio = audio;
  _dirty = true;
}

void Ui::applyBrightness(uint8_t b) {
  if (b < BRIGHT_MIN) b = BRIGHT_MIN;
  if (b > 100) b = 100;
  _bright = b;
  Set_Backlight(_bright);
  settings::setBrightness(_bright);   // 掉电保持
  _dirty = true;
}

void Ui::adjustBrightness(int delta) { applyBrightness((uint8_t)((int)_bright + delta)); }

void Ui::beginTransition() {
  if (!gfx::fadeReady()) {
    _fading = false;
    return;
  }
  gfx::fadeSnapshot();
  _fadeT0 = millis();
  _fadeTargetSaved = false;
  _fading = true;
}

void Ui::changePage(uint8_t p) {
  if (p >= P_COUNT) p = 0;
  if (p == _page) return;
  beginTransition();
  _page = p;
  _dirty = true;
}

void Ui::setPage(uint8_t p) { changePage(p); }

void Ui::onSwipe(int8_t dir) {
  int p = (int)_page + dir;
  if (p < 0) p = P_COUNT - 1;
  if (p >= P_COUNT) p = 0;
  changePage((uint8_t)p);
}

void Ui::toggleAudio() {
  if (!_audio) return;
  if (_audio->on()) {
    _audio->stop();
    toast("音频已关闭");
  } else {
    _audio->start();
    toast("音频已开启");
  }
  settings::setAudioOn(_audio->on());   // 掉电保持
  _dirty = true;
}

void Ui::toast(const char* msg) {
  if (!msg) return;
  strncpy(_toast, msg, sizeof(_toast) - 1);
  _toast[sizeof(_toast) - 1] = 0;
  _toastUntil = millis() + 2500;
  _dirty = true;
}

void Ui::onTap(uint16_t x, uint16_t y) {
  if (!_fmo) return;
  FmoState& s = _fmo->mut();


  switch (_page) {

    case P_SETUP: {
      // 两条滑杆：上=音量（音频增益），下=亮度
      for (int row = 0; row < 2; row++) {
        const int ry = kRowY[row];
        const int cy = ry + kBarH / 2;
        const int dxm = (int)x - kMinusX;
        const int dxp = (int)x - kPlusX;
        const int dy = (int)y - cy;
        const bool onMinus = (dxm * dxm + dy * dy) <= (22 * 22);
        const bool onPlus = (dxp * dxp + dy * dy) <= (22 * 22);
        const bool onBar = (y >= ry - 6 && y <= ry + kBarH + 6 && x >= kBarX && x <= kBarX + kBarW);
        if (!onMinus && !onPlus && !onBar) continue;

        int cur = (row == 0) ? (_audio ? _audio->gain() : 0) : (int)_bright;
        int nv = cur;
        if (onMinus) nv = cur - 10;
        else if (onPlus) nv = cur + 10;
        else nv = ((int)((x - kBarX) * 100 / kBarW) + 2) / 5 * 5;
        nv = ((nv + 2) / 5) * 5;
        if (nv < 0) nv = 0;
        if (nv > 100) nv = 100;

        if (row == 0) {
          if (_audio) _audio->setGain((uint8_t)nv);
          settings::setGain((uint8_t)nv);
        } else {
          applyBrightness((uint8_t)nv);
        }
        _dirty = true;
        DBG("[ui] setup row=%d -> %d\n", row, nv);
        return;
      }
      break;
    }

    case P_STATION: {
      // 翻页：‹ 上一页 / › 下一页
      const int fy = 268, ar = 20;
      const int dxL = (int)x - (CX - 78), dxR = (int)x - (CX + 78), dy = (int)y - fy;
      if (dxL * dxL + dy * dy <= ar * ar) {
        if (s.stnPage > 0) {
          _fmo->reqStationPage((uint16_t)((s.stnPage - 1) * STATION_ROWS));
          toast("上一页");
        }
        return;
      }
      if (dxR * dxR + dy * dy <= ar * ar) {
        if (s.stnHasNext) {
          _fmo->reqStationPage((uint16_t)((s.stnPage + 1) * STATION_ROWS));
          toast("下一页");
        }
        return;
      }

      // 切台会影响真实电台状态：两步确认
      const int y0 = 94, rowH = 40;
      for (int i = 0; i < s.stnN; i++) {
        const int ry = y0 + i * rowH;
        if (y >= ry && y < ry + rowH) {
          if (s.stn[i].uid == s.stUid) return;
          if (_stnArmed == i && (millis() - _stnArmedAt) < 4000) {
            _fmo->reqStation(s.stn[i].uid);
            toast("正在切换电台…");
            _stnArmed = -1;
          } else {
            _stnArmed = (int8_t)i;
            _stnArmedAt = millis();
          }
          _dirty = true;
          return;
        }
      }
      break;
    }

    default:
      break;
  }
}

// ------------------------------------------------------------------ 装饰
void Ui::drawTopBar() {
  // 呼号居中（左侧只留一个很小的链路指示点，不占视觉重心）
  const FmoState& s = _fmo->state();
  const char* call = s.hasInfo ? s.callsign : "FMO";
  const int w = textWidth(call, 16, 1);
  const int x = CX - w / 2;
  text(x, 30, call, 16, C_TEXT, 1, 120);
  fillCircle(x - 12, 38, 4, s.linkUp ? C_ACCENT : C_RED);
}

void Ui::drawTitle(const char* zh, const char* en, const char* right) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s // %s", zh, en);
  const bool hasRight = right && right[0];
  if (hasRight) textRight(CX + 112, 58, right, 16, C_AMBER, 1, 72);
  textCenter(hasRight ? CX - 26 : CX, 58, buf, 16, C_AMBER, 1, hasRight ? 150 : 190);
  hline(CX - 70, 82, 140, C_LINE);
}

void Ui::drawBottomBar() {

  // 有提示条时，最底行暂时让给提示条
  if (toastActive()) return;

  // 最下方中间：电量（图标 + 百分比） + WiFi 信号格，整组居中
  int pct = 0;
  if (_batV > 1.0f) {
    pct = (int)((_batV - 3.30f) * 100.0f / 0.90f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
  }
  char pctTxt[8];
  snprintf(pctTxt, sizeof(pctTxt), "%d%%", pct);

  const int bw = 22, bh = 11;              // 电池图标
  const int pctW = textWidth(pctTxt, 16, 1);
  const int barsW = 4 * 5 - 2;             // 4 根信号柱
  const int gap1 = 6, gap2 = 12;
  const int groupW = bw + 2 + gap1 + pctW + gap2 + barsW;
  const int gx = CX - groupW / 2;
  const int rowY = 310;

  const uint16_t bc = pct > 50 ? C_ACCENT : (pct > 20 ? C_AMBER : C_RED);
  roundRect(gx, rowY, bw, bh, 2, C_DIM);
  fillRect(gx + bw, rowY + 3, 2, bh - 6, C_DIM);
  if (pct > 0) fillRect(gx + 2, rowY + 2, (bw - 4) * pct / 100, bh - 4, bc);
  text(gx + bw + 2 + gap1, rowY - 2, pctTxt, 16, C_DIM, 1, 40);

  const int bars = rssiBars(WiFi.RSSI());
  const int bxStart = gx + bw + 2 + gap1 + pctW + gap2;
  for (int i = 0; i < 4; i++) {
    const int bhh = 3 + i * 3;
    fillRect(bxStart + i * 5, rowY + bh - bhh, 3, bhh, i < bars ? C_ACCENT : C_LINE);
  }
}

void Ui::drawWrapped(int x, int y, int w, int lineH, int maxLines, const char* s, int size, uint16_t c,
                     bool center) {
  if (!s || !*s) return;
  int line = 0, i = 0;
  const int n = (int)strlen(s);
  while (i < n && line < maxLines) {
    int j = i, lineW = 0;
    while (j < n) {
      int k = j;
      const unsigned char ch = (unsigned char)s[k];
      int clen = 1;
      if ((ch & 0xE0) == 0xC0) clen = 2;
      else if ((ch & 0xF0) == 0xE0) clen = 3;
      else if ((ch & 0xF8) == 0xF0) clen = 4;
      char tmp[5] = {0, 0, 0, 0, 0};
      for (int t = 0; t < clen && k + t < n; t++) tmp[t] = s[k + t];
      const int cw = textWidth(tmp, size, 1);
      if (lineW + cw > w) break;
      lineW += cw;
      j += clen;
    }
    if (j == i) break;
    char buf[96];
    int len = j - i;
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    memcpy(buf, s + i, (size_t)len);
    buf[len] = 0;
    if (center) textCenter(CX, y + line * lineH, buf, size, c, 1, w);
    else text(x, y + line * lineH, buf, size, c, 1, w);
    line++;
    i = j;
  }
}

// ------------------------------------------------------------------ 音频状态（融合进频道页，不显示电平/增益）
void Ui::drawAudioStrip(int y) {
  if (!_audio) return;
  if (!_audio->on()) {
    textCenter(CX, y, "音频关闭 · 长按 BOOT 开启", 16, C_DIM, 1, 300);
    return;
  }
  const bool up = _audio->socketUp();
  const bool liveData = _audio->msSinceData() < 3000;
  if (!up) {
    textCenter(CX, y, "音频 连接中…", 16, C_AMBER, 1, 260);
  } else if (liveData) {
    textCenter(CX, y, "音频 LIVE", 16, C_ACCENT, 1, 220);
  } else {
    textCenter(CX, y, "音频 已开启 · 等待语音", 16, C_DIM, 1, 280);
  }
}
// ------------------------------------------------------------------ 1 屏：频道 + 发言人 + 音频
void Ui::drawMain() {
  const FmoState& s = _fmo->state();
  drawTitle(kPageZh[_page], kPageEn[_page], s.hasStation ? "" : "离线");

  // 当前频道名称（中文 24px，最多两行）
  drawWrapped(CX - 150, 92, 300, 28, 2, s.hasStation ? s.stName : "未连接频道", 24, C_TEXT, true);

  hline(CX - 100, 150, 200, C_LINE);

  textCenter(CX, 158, "当前发言人", 16, C_DIM, 1, 200);

  // 发言人：优先取推送（正在讲话），否则显示最近通话
  const bool live = s.actLive && s.actCall[0];
  const char* who = live ? s.actCall : (s.lastCall[0] ? s.lastCall : "");
  if (who[0]) {
    const uint16_t col = live ? C_ACCENT : C_DIM;
    const int w = textWidth(who, 24, 2);
    const int x = CX - w / 2;
    if (live) fillCircle(x - 12, 200, 5, C_ACCENT);
    text(x, 176, who, 24, col, 2, 300);
  } else {
    textCenter(CX, 182, "无通话", 24, C_LINE, 1, 200);
  }

  // 状态行
  char buf[64];
  if (!settings::wifiConfigured()) {
    textCenter(CX, 230, "未配置 WiFi", 16, C_AMBER, 1, 220);
    textCenter(CX, 254, "串口发 wifi SSID 密码", 16, C_DIM, 1, 280);
    return;
  }
  if (live) {
    textCenter(CX, 230, "正在讲话", 16, C_ACCENT, 1, 220);
  } else if (s.lastCall[0]) {
    char tm[16];
    fmtTime(s.lastTs, tm, sizeof(tm));
    snprintf(buf, sizeof(buf), "最近通话 %s", tm);
    textCenter(CX, 230, buf, 16, C_DIM, 1, 220);
  } else if (!s.linkUp) {
    textCenter(CX, 230, "正在连接 fmo.local …", 16, C_AMBER, 1, 240);
  } else {
    textCenter(CX, 230, "等待通话", 16, C_DIM, 1, 220);
  }

  // 推送通道没连上时提醒一句（发言人只能靠它刷新）
  if (!s.evUp && s.linkUp) textCenter(CX, 246, "推送通道未连", 16, C_LINE, 1, 200);

  drawAudioStrip(254);
}
// ------------------------------------------------------------------ 3 屏：电台
void Ui::drawStation() {
  const FmoState& s = _fmo->state();
  drawTitle(kPageZh[_page], kPageEn[_page], "");

  const int y0 = 94, rowH = 40;
  for (int i = 0; i < STATION_ROWS; i++) {
    if (i >= s.stnN) break;
    const int ry = y0 + i * rowH;
    const bool cur = (s.stn[i].uid == s.stUid);
    const bool armed = (_stnArmed == i) && ((millis() - _stnArmedAt) < 4000);
    if (cur || armed) {
      fillRoundRect(40, ry, 280, rowH - 6, 8, C_PANEL);
      fillRect(40, ry, 4, rowH - 6, armed ? C_AMBER : C_ACCENT);
    }
    char uid[16];
    snprintf(uid, sizeof(uid), "%u", (unsigned)s.stn[i].uid);
    text(56, ry + 9, s.stn[i].name, 16, armed ? C_AMBER : (cur ? C_ACCENT : C_TEXT), 1, 210);
    textRight(312, ry + 9, uid, 16, armed ? C_AMBER : C_DIM, 1, 40);
  }
  if (s.stnN == 0) textCenter(CX, 150, s.linkUp ? "台站列表为空" : "未连接设备", 16, C_DIM, 1, 220);

  // 翻页：‹ 第 N 页 ›
  const int fy = 268;
  const bool canPrev = s.stnPage > 0;
  const bool canNext = s.stnHasNext;
  fillCircle(CX - 78, fy, 20, C_PANEL);
  circle(CX - 78, fy, 20, canPrev ? C_ACCENT : C_LINE, 1);
  fillTriangle(CX - 78 + 6, fy - 9, CX - 78 + 6, fy + 9, CX - 78 - 6, fy, canPrev ? C_ACCENT : C_LINE);
  fillCircle(CX + 78, fy, 20, C_PANEL);
  circle(CX + 78, fy, 20, canNext ? C_ACCENT : C_LINE, 1);
  fillTriangle(CX + 78 - 6, fy - 9, CX + 78 - 6, fy + 9, CX + 78 + 6, fy, canNext ? C_ACCENT : C_LINE);

  char pg[32];
  snprintf(pg, sizeof(pg), "第 %u 页", (unsigned)(s.stnPage + 1));
  textCenter(CX, fy - 8, pg, 16, C_TEXT, 1, 90);

  const bool armed = (_stnArmed >= 0) && ((millis() - _stnArmedAt) < 4000);
  if (armed) textCenter(CX, 240, "再点一次切换电台", 16, C_AMBER, 1, 260);
}

// ------------------------------------------------------------------ 3 屏：音量 · 亮度
void Ui::drawSetup() {
  drawTitle(kPageZh[_page], kPageEn[_page], "");

  const int vol = _audio ? _audio->gain() : 0;
  const int bri = _bright;
  const char* label[2] = {"音量", "亮度"};
  const int val[2] = {vol, bri};

  for (int row = 0; row < 2; row++) {
    const int ry = kRowY[row];
    // 标签 + 数值
    text(kBarX - 44, ry - 26, label[row], 16, C_DIM, 1, 44);
    char num[8];
    snprintf(num, sizeof(num), "%d%%", val[row]);
    textRight(kBarX + kBarW, ry - 26, num, 16, C_ACCENT, 1, 44);

    // 滑杆
    fillRoundRect(kBarX, ry, kBarW, kBarH, kBarH / 2, C_PANEL);
    const int fw = kBarW * val[row] / 100;
    if (fw > 2) fillRoundRect(kBarX, ry, fw, kBarH, kBarH / 2, C_ACCENT);
    // 20 格刻度线
    for (int i = 1; i < 20; i++) {
      vline(kBarX + kBarW * i / 20, ry + 2, kBarH - 4, C_BG);
    }

    // − / +
    fillCircle(kMinusX, ry + kBarH / 2, 20, C_PANEL);
    circle(kMinusX, ry + kBarH / 2, 20, C_LINE, 1);
    textCenter(kMinusX, ry + kBarH / 2 - 8, "-", 16, C_TEXT, 1, 18);
    fillCircle(kPlusX, ry + kBarH / 2, 20, C_PANEL);
    circle(kPlusX, ry + kBarH / 2, 20, C_LINE, 1);
    textCenter(kPlusX, ry + kBarH / 2 - 8, "+", 16, C_TEXT, 1, 18);
  }

  textCenter(CX, 250, "点 - / + 或滑杆 · 上下滑动调亮度", 16, C_DIM, 1, 300);
}
// ------------------------------------------------------------------ 帧
void Ui::drawFrame() {
  clear(C_BG);
  circle(CX, CX, 176, C_LINE, 1);

  drawTopBar();

  switch (_page) {
    case P_MAIN:
      drawMain();
      break;

    case P_STATION:
      drawStation();
      break;
    case P_SETUP:
      drawSetup();
      break;
    default:
      break;
  }

  drawBottomBar();

  if (toastActive()) {
    fillRoundRect(CX - 96, 298, 192, 22, 6, C_PANEL);
    textCenter(CX, 301, _toast, 16, C_AMBER, 1, 184);
  }
}

void Ui::render(bool force) {
  if (!_fmo) return;
  const uint32_t now = millis();
  const uint32_t rev = _fmo->state().rev;
  const bool anim = (_audio && _audio->on());
  constexpr uint32_t kFadeMs = 160;

  if (_fading) {
    if ((int32_t)(now - _fadeT0) >= (int32_t)kFadeMs) {
      _fading = false;
      _fadeTargetSaved = false;
      _lastRev = rev;
      _dirty = false;
      _lastDrawMs = now;
      drawFrame();
      flush();
      return;
    }
    if (now - _lastDrawMs < 42) return;
    _lastDrawMs = now;
    if (!_fadeTargetSaved) {
      drawFrame();
      gfx::fadeSaveTarget();
      _fadeTargetSaved = true;
      _lastRev = rev;
      _dirty = false;
    }
    const uint32_t el = now - _fadeT0;
    const uint32_t split = kFadeMs * 45 / 100;
    if (el < split) {
      gfx::fadeOut((uint8_t)(255 * el / split), C_BG);
    } else {
      int a = (int)(255 * (el - split) / (kFadeMs - split));
      if (a > 255) a = 255;
      gfx::fadeIn((uint8_t)a, C_BG);
    }
    flush();
    return;
  }

  bool need = force || _dirty || (rev != _lastRev);
  if (!need && anim && (now - _lastDrawMs) >= 120) need = true;
  if (!need && _toast[0] && (int32_t)(now - _toastUntil) >= 0) need = true;
  if (!need) return;

  _lastRev = rev;
  _dirty = false;
  _lastDrawMs = now;
  drawFrame();
  flush();
}

void Ui::splash() {
  clear(C_BG);
  circle(CX, CX, 150, C_LINE, 1);
  circle(CX, CX, 120, C_PANEL, 2);
  textCenter(CX, 130, "FMO", 24, C_ACCENT, 2, 240);
  textCenter(CX, 182, "COMPANION", 16, C_TEXT, 1, 240);
  textCenter(CX, 208, "频道 · 音频 · 电台", 16, C_DIM, 1, 240);
  textCenter(CX, 232, "ESP32-S3  ·  360x360", 16, C_LINE, 1, 240);
  fillRect(CX - 60, 262, 120, 2, C_LINE);
  fillRect(CX - 60, 262, 40, 2, C_ACCENT);
  flush();
}
