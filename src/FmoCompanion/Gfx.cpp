// Gfx.cpp —— 帧缓冲绘制与刷屏实现
#include "Gfx.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "Display_ST77916.h"
#include "Fonts.h"

namespace gfx {
namespace {

constexpr int kBandH = 24;           // 每次刷屏的行数（24*360*2 = 17 KB，内部 RAM）
constexpr int kClipR = 177;          // 圆屏可见半径（360 圆屏留 3px 边）
constexpr int kClipR2 = kClipR * kClipR;

uint16_t* s_fb = nullptr;            // PSRAM 帧缓冲
uint16_t* s_band = nullptr;          // 内部 RAM 刷屏缓冲
uint16_t* s_prev = nullptr;          // 切页动画：上一帧
uint16_t* s_target = nullptr;        // 切页动画：目标帧

// 脏矩形（空：dx0 > dx1）
int s_dx0 = 1, s_dy0 = 1, s_dx1 = 0, s_dy1 = 0;

// 裁剪矩形
int s_cx = 0, s_cy = 0, s_cw = SCREEN_W, s_ch = SCREEN_H;

inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
inline uint16_t bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

void mark(int x0, int y0, int x1, int y1) {
  if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
  if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
  if (x0 < s_dx0) s_dx0 = x0;
  if (y0 < s_dy0) s_dy0 = y0;
  if (x1 > s_dx1) s_dx1 = x1;
  if (y1 > s_dy1) s_dy1 = y1;
}

inline bool inside(int x, int y) {
  if (x < s_cx || y < s_cy || x >= s_cx + s_cw || y >= s_cy + s_ch) return false;
  const int dx = x - SCREEN_W / 2, dy = y - SCREEN_H / 2;
  return dx * dx + dy * dy <= kClipR2;
}

inline void px(int x, int y, uint16_t c) {
  if (!inside(x, y)) return;
  s_fb[(size_t)y * SCREEN_W + x] = c;
}

// ------------------------------------------------------------------ 文本
uint32_t utf8Next(const char* s, int& i) {
  const uint8_t c = (uint8_t)s[i];
  if (c < 0x80) {
    i += 1;
    return c;
  }
  int n = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) {
    n = 1;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    n = 2;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    n = 3;
    cp = c & 0x07;
  } else {
    i += 1;
    return '?';
  }
  for (int k = 0; k < n; k++) {
    const uint8_t cc = (uint8_t)s[i + 1 + k];
    if (cc == 0) {
      i += 1;
      return '?';
    }
    cp = (uint32_t)((cp << 6) | (cc & 0x3F));
  }
  i += n + 1;
  return cp;
}

// 贴一个字形；同行的连续亮点合并成 1 个矩形，减少调用次数
void blitBits(int x, int y, const fonts::BitFont& f, const uint8_t* bits, int scale, uint16_t c) {
  for (int r = 0; r < f.h; r++) {
    const uint8_t* row = bits + (size_t)r * f.bpr;
    int col = 0;
    while (col < f.w) {
      const bool on = (row[col >> 3] & (0x80 >> (col & 7))) != 0;
      if (!on) {
        col++;
        continue;
      }
      int run = 1;
      while (col + run < f.w) {
        const bool nxt = (row[(col + run) >> 3] & (0x80 >> ((col + run) & 7))) != 0;
        if (!nxt) break;
        run++;
      }
      fillRect(x + col * scale, y + r * scale, run * scale, scale, c);
      col += run;
    }
  }
}

}  // namespace

// ------------------------------------------------------------------ 生命周期
bool begin() {
  if (!s_fb) {
    s_fb = (uint16_t*)heap_caps_malloc((size_t)SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
    if (!s_fb) s_fb = (uint16_t*)malloc((size_t)SCREEN_W * SCREEN_H * 2);
  }
  if (!s_band) {
    s_band = (uint16_t*)heap_caps_malloc((size_t)SCREEN_W * kBandH * 2,
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_band) s_band = (uint16_t*)heap_caps_malloc((size_t)SCREEN_W * kBandH * 2, MALLOC_CAP_8BIT);
  }
  if (!s_fb || !s_band) {
    Serial.printf("[gfx] alloc failed fb=%p band=%p\n", (void*)s_fb, (void*)s_band);
    return false;
  }
  memset(s_fb, 0, (size_t)SCREEN_W * SCREEN_H * 2);
  resetClip();
  markAll();
  return true;
}

bool ready() { return s_fb != nullptr && s_band != nullptr; }

// ------------------------------------------------------------------ 切页动画
bool fadeReady() {
  if (!s_fb) return false;
  if (!s_prev) s_prev = (uint16_t*)heap_caps_malloc((size_t)SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
  if (!s_target) s_target = (uint16_t*)heap_caps_malloc((size_t)SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
  return s_prev != nullptr && s_target != nullptr;
}

void fadeSnapshot() {
  if (!fadeReady()) return;
  memcpy(s_prev, s_fb, (size_t)SCREEN_W * SCREEN_H * 2);
}

void fadeSaveTarget() {
  if (!fadeReady()) return;
  memcpy(s_target, s_fb, (size_t)SCREEN_W * SCREEN_H * 2);
}

// 淡出：旧页 → 纯色（通常用背景色）
void fadeOut(uint8_t alpha, uint16_t to) {
  if (!fadeReady()) return;
  const int a = alpha;
  const int ia = 255 - a;
  const int tr = (to >> 11) & 0x1F, tg = (to >> 5) & 0x3F, tb = to & 0x1F;
  const size_t n = (size_t)SCREEN_W * SCREEN_H;
  for (size_t i = 0; i < n; i++) {
    const uint16_t p = s_prev[i];
    const int pr = (p >> 11) & 0x1F, pg = (p >> 5) & 0x3F, pb = p & 0x1F;
    const int r = (pr * ia + tr * a) / 255;
    const int g = (pg * ia + tg * a) / 255;
    const int b = (pb * ia + tb * a) / 255;
    s_fb[i] = (uint16_t)((r << 11) | (g << 5) | b);
  }
  markAll();
}

// 淡入：纯色 → 新页
void fadeIn(uint8_t alpha, uint16_t from) {
  if (!fadeReady()) return;
  const int a = alpha;
  const int ia = 255 - a;
  const int fr = (from >> 11) & 0x1F, fg = (from >> 5) & 0x3F, fb2 = from & 0x1F;
  const size_t n = (size_t)SCREEN_W * SCREEN_H;
  for (size_t i = 0; i < n; i++) {
    const uint16_t t = s_target[i];
    const int tr = (t >> 11) & 0x1F, tg = (t >> 5) & 0x3F, tb = t & 0x1F;
    const int r = (fr * ia + tr * a) / 255;
    const int g = (fg * ia + tg * a) / 255;
    const int b = (fb2 * ia + tb * a) / 255;
    s_fb[i] = (uint16_t)((r << 11) | (g << 5) | b);
  }
  markAll();
}

void fadeApply(uint8_t alpha) {
  if (!fadeReady()) return;
  const int a = alpha;
  const int ia = 255 - a;
  const size_t n = (size_t)SCREEN_W * SCREEN_H;
  for (size_t i = 0; i < n; i++) {
    const uint16_t p = s_prev[i];
    const uint16_t t = s_target[i];
    const int pr = (p >> 11) & 0x1F, pg = (p >> 5) & 0x3F, pb = p & 0x1F;
    const int tr = (t >> 11) & 0x1F, tg = (t >> 5) & 0x3F, tb = t & 0x1F;
    const int r = (pr * ia + tr * a) / 255;
    const int g = (pg * ia + tg * a) / 255;
    const int b = (pb * ia + tb * a) / 255;
    s_fb[i] = (uint16_t)((r << 11) | (g << 5) | b);
  }
  markAll();
}
uint16_t* pixels() { return s_fb; }

void markAll() { mark(0, 0, SCREEN_W - 1, SCREEN_H - 1); }

void setClip(int x, int y, int w, int h) {
  s_cx = imax(0, x);
  s_cy = imax(0, y);
  s_cw = imax(0, imin(w, SCREEN_W - s_cx));
  s_ch = imax(0, imin(h, SCREEN_H - s_cy));
}

void resetClip() {
  s_cx = 0;
  s_cy = 0;
  s_cw = SCREEN_W;
  s_ch = SCREEN_H;
}

void clear(uint16_t color) {
  if (!s_fb) return;
  for (size_t i = 0; i < (size_t)SCREEN_W * SCREEN_H; i++) s_fb[i] = color;
  markAll();
}

void flush() {
  if (!s_fb || !s_band) return;
  if (s_dx0 > s_dx1) return;
  const int x0 = imax(0, s_dx0), y0 = imax(0, s_dy0);
  const int x1 = imin(SCREEN_W - 1, s_dx1), y1 = imin(SCREEN_H - 1, s_dy1);
  s_dx0 = 1; s_dy0 = 1; s_dx1 = 0; s_dy1 = 0;
  if (x1 < x0 || y1 < y0) return;

  const int w = x1 - x0 + 1;
  for (int y = y0; y <= y1; y += kBandH) {
    const int h = imin(kBandH, y1 - y + 1);
    uint16_t* dst = s_band;
    for (int r = 0; r < h; r++) {
      const uint16_t* src = s_fb + (size_t)(y + r) * SCREEN_W + x0;
      uint16_t* out = dst + (size_t)r * w;
      for (int i = 0; i < w; i++) out[i] = bswap16(src[i]);
    }
    LCD_DrawRaw((uint16_t)x0, (uint16_t)y, (uint16_t)(x0 + w), (uint16_t)(y + h), dst);
    LCD_WaitFlush();
  }
}

// ------------------------------------------------------------------ 图元
void fillRect(int x, int y, int w, int h, uint16_t c) {
  if (!s_fb || w <= 0 || h <= 0) return;
  int x0 = imax(x, s_cx), x1 = imin(x + w - 1, s_cx + s_cw - 1);
  int y0 = imax(y, s_cy), y1 = imin(y + h - 1, s_cy + s_ch - 1);
  if (x1 < x0 || y1 < y0) return;
  for (int yy = y0; yy <= y1; yy++) {
    uint16_t* row = s_fb + (size_t)yy * SCREEN_W;
    for (int xx = x0; xx <= x1; xx++) {
      const int dx = xx - SCREEN_W / 2, dy = yy - SCREEN_H / 2;
      if (dx * dx + dy * dy <= kClipR2) row[xx] = c;
    }
  }
  mark(x0, y0, x1, y1);
}

void fillRectBlend(int x, int y, int w, int h, uint16_t c, uint8_t alpha) {
  if (!s_fb || w <= 0 || h <= 0) return;
  const int x0 = imax(x, s_cx), x1 = imin(x + w - 1, s_cx + s_cw - 1);
  const int y0 = imax(y, s_cy), y1 = imin(y + h - 1, s_cy + s_ch - 1);
  if (x1 < x0 || y1 < y0) return;
  const int a = alpha;
  for (int yy = y0; yy <= y1; yy++) {
    uint16_t* row = s_fb + (size_t)yy * SCREEN_W;
    for (int xx = x0; xx <= x1; xx++) {
      const int dx = xx - SCREEN_W / 2, dy = yy - SCREEN_H / 2;
      if (dx * dx + dy * dy > kClipR2) continue;
      const uint16_t o = row[xx];
      const int orr = (o >> 11) & 0x1F, ogg = (o >> 5) & 0x3F, obb = o & 0x1F;
      const int nr = (c >> 11) & 0x1F, ng = (c >> 5) & 0x3F, nb = c & 0x1F;
      const int rr = (orr * (255 - a) + nr * a) / 255;
      const int gg = (ogg * (255 - a) + ng * a) / 255;
      const int bb = (obb * (255 - a) + nb * a) / 255;
      row[xx] = (uint16_t)((rr << 11) | (gg << 5) | bb);
    }
  }
  mark(x0, y0, x1, y1);
}

void rect(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  hline(x, y, w, c);
  hline(x, y + h - 1, w, c);
  vline(x, y, h, c);
  vline(x + w - 1, y, h, c);
}

void hline(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
void vline(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }

void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  if (r <= 0) {
    fillRect(x, y, w, h, c);
    return;
  }
  const int rr = imin(r, imin(w, h) / 2);
  fillRect(x + rr, y, w - 2 * rr, h, c);
  fillRect(x, y + rr, rr, h - 2 * rr, c);
  fillRect(x + w - rr, y + rr, rr, h - 2 * rr, c);
  // 四角
  for (int dy = 0; dy < rr; dy++) {
    for (int dx = 0; dx < rr; dx++) {
      const int ex = rr - 1 - dx, ey = rr - 1 - dy;
      if (ex * ex + ey * ey > (rr - 1) * (rr - 1) + 1) continue;
      px(x + dx, y + dy, c);
      px(x + w - 1 - dx, y + dy, c);
      px(x + dx, y + h - 1 - dy, c);
      px(x + w - 1 - dx, y + h - 1 - dy, c);
    }
  }
  mark(x, y, x + w - 1, y + h - 1);
}

void roundRect(int x, int y, int w, int h, int r, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  const int rr = imin(r, imin(w, h) / 2);
  hline(x + rr, y, w - 2 * rr, c);
  hline(x + rr, y + h - 1, w - 2 * rr, c);
  vline(x, y + rr, h - 2 * rr, c);
  vline(x + w - 1, y + rr, h - 2 * rr, c);
  for (int a = 0; a <= 90; a += 2) {
    const float rad = (float)a * 0.0174533f;
    const int ox = (int)lroundf(cosf(rad) * (rr - 1));
    const int oy = (int)lroundf(sinf(rad) * (rr - 1));
    px(x + rr - 1 - ox, y + rr - 1 - oy, c);
    px(x + w - rr + ox, y + rr - 1 - oy, c);
    px(x + rr - 1 - ox, y + h - rr + oy, c);
    px(x + w - rr + ox, y + h - rr + oy, c);
  }
  mark(x, y, x + w - 1, y + h - 1);
}

void line(int x0, int y0, int x1, int y1, uint16_t c, int thick) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  const int h = imax(0, thick / 2);
  for (;;) {
    if (thick <= 1) {
      px(x0, y0, c);
    } else {
      fillRect(x0 - h, y0 - h, thick, thick, c);
    }
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
  mark(imin(x0, x1) - h, imin(y0, y1) - h, imax(x0, x1) + h, imax(y0, y1) + h);
}

void fillCircle(int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; y++) {
    const int w = (int)sqrtf((float)(r * r - y * y));
    fillRect(cx - w, cy + y, 2 * w + 1, 1, c);
  }
}

void circle(int cx, int cy, int r, uint16_t c, int thick) {
  if (thick < 1) thick = 1;
  const int steps = imax(360, r * 8);
  for (int i = 0; i < steps; i++) {
    const float a = (float)i * 6.2831853f / steps;
    const int x = cx + (int)lroundf(cosf(a) * r);
    const int y = cy + (int)lroundf(sinf(a) * r);
    fillRect(x - thick / 2, y - thick / 2, thick, thick, c);
  }
  mark(cx - r - thick, cy - r - thick, cx + r + thick, cy + r + thick);
}

void ringArc(int cx, int cy, int r, int a0, int a1, int thick, uint16_t c) {
  if (a1 < a0) { int t = a0; a0 = a1; a1 = t; }
  const int span = a1 - a0;
  const int steps = imax(8, span * 2);
  for (int i = 0; i <= steps; i++) {
    const float ang = ((float)a0 + (float)span * i / steps) * 0.0174533f;
    // 0° = 正上方，顺时针
    const float sx = sinf(ang), sy = -cosf(ang);
    const int x = cx + (int)lroundf(sx * r);
    const int y = cy + (int)lroundf(sy * r);
    fillRect(x - thick / 2, y - thick / 2, thick, thick, c);
  }
  mark(cx - r - thick, cy - r - thick, cx + r + thick, cy + r + thick);
}

void fillWedge(int cx, int cy, int r, int a0, int a1, uint16_t c) {
  if (a1 < a0) { int t = a0; a0 = a1; a1 = t; }
  const int span = a1 - a0;
  for (int i = 0; i <= span; i++) {
    const float ang = (float)(a0 + i) * 0.0174533f;
    const float sx = sinf(ang), sy = -cosf(ang);
    line(cx, cy, cx + (int)lroundf(sx * r), cy + (int)lroundf(sy * r), c, 1);
  }
  mark(cx - r, cy - r, cx + r, cy + r);
}

void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  int minY = imin(y0, imin(y1, y2)), maxY = imax(y0, imax(y1, y2));
  for (int y = minY; y <= maxY; y++) {
    const float f = (float)y;
    // 求交点
    int xs[3];
    int n = 0;
    const int ax[3] = {x0, x1, x2}, ay[3] = {y0, y1, y2};
    for (int e = 0; e < 3; e++) {
      const int a = e, b = (e + 1) % 3;
      if ((ay[a] <= y && ay[b] >= y) || (ay[b] <= y && ay[a] >= y)) {
        if (ay[a] == ay[b]) {
          xs[n++] = ax[a];
          if (n < 3) xs[n++] = ax[b];
        } else {
          const float t = (f - ay[a]) / (float)(ay[b] - ay[a]);
          xs[n++] = (int)lroundf(ax[a] + t * (ax[b] - ax[a]));
        }
        if (n >= 3) break;
      }
    }
    if (n < 2) continue;
    int lo = xs[0], hi = xs[0];
    for (int k = 1; k < n; k++) {
      lo = imin(lo, xs[k]);
      hi = imax(hi, xs[k]);
    }
    fillRect(lo, y, hi - lo + 1, 1, c);
  }
  mark(imin(x0, imin(x1, x2)), minY, imax(x0, imax(x1, x2)), maxY);
}

// ------------------------------------------------------------------ 文本
int textHeight(int size, int scale) { return (size >= 24 ? 24 : 16) * scale; }

int textWidth(const char* utf8, int size, int scale) {
  if (!utf8) return 0;
  int i = 0, w = 0;
  while (utf8[i]) {
    const uint32_t cp = utf8Next(utf8, i);
    w += fonts::pick(cp, size).w * scale;
  }
  return w;
}

int text(int x, int y, const char* utf8, int size, uint16_t c, int scale, int maxW) {
  if (!utf8) return 0;
  const fonts::BitFont& fa = fonts::ascii(size);
  int i = 0, cx = x;
  while (utf8[i]) {
    const uint32_t cp = utf8Next(utf8, i);
    const fonts::BitFont& f = fonts::pick(cp, size);
    const int adv = f.w * scale;
    if (maxW > 0 && (cx - x + adv) > maxW) {
      // 截断：画 ".."，尽量塞下
      int dots = 2;
      const int dotAdv = fa.w * scale;
      while (dots > 0 && (cx - x) + dotAdv * dots > maxW) dots--;
      for (int d = 0; d < dots; d++) {
        const uint8_t* g = fonts::glyph(fa, '.');
        if (g) blitBits(cx + d * dotAdv, y, fa, g, scale, c);
      }
      return cx - x + dotAdv * dots;
    }
    const uint8_t* g = fonts::glyph(f, cp);
    if (!g) {
      // 字库里没有这个字（比如 24px 只收 GB2312）：用 ASCII 的 "?" 顶上，
      // 但横向仍然占一个汉字宽，保证后续文字不错位。
      if (!f.ascii) {
        const uint8_t* q = fonts::glyph(fa, '?');
        if (q) blitBits(cx + (adv - fa.w * scale) / 2, y + (textHeight(size, scale) - fa.h * scale) / 2, fa, q, scale, c);
      }
    } else {
      blitBits(cx, y, f, g, scale, c);
    }
    cx += adv;
  }
  return cx - x;
}

int textCenter(int cx, int y, const char* utf8, int size, uint16_t c, int scale, int maxW) {
  const int w = textWidth(utf8, size, scale);
  int x = cx - w / 2;
  return text(x, y, utf8, size, c, scale, maxW > 0 ? maxW : 0);
}

int textRight(int xr, int y, const char* utf8, int size, uint16_t c, int scale, int maxW) {
  const int w = textWidth(utf8, size, scale);
  return text(xr - w, y, utf8, size, c, scale, maxW > 0 ? maxW : 0);
}

void textRow(int x, int y, int w, int h, const char* left, const char* right, int size, uint16_t cLeft,
             uint16_t cRight) {
  const int th = textHeight(size, 1);
  const int ty = y + (h - th) / 2;
  if (left && *left) text(x, ty, left, size, cLeft, 1, right ? imax(0, w - textWidth(right, size, 1) - 6) : w);
  if (right && *right) textRight(x + w, ty, right, size, cRight, 1, w);
}

}  // namespace gfx
