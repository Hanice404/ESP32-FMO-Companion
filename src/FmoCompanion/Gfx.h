// Gfx.h —— 自绘图形层（RGB565 帧缓冲 + 图元 + 点阵文本 + 脏区刷屏）
//
// 为什么不用 LVGL：本机界面元素固定（列表 / 罗盘 / 电平条 / 文字），
// 自绘可以把两套点阵字（16/24）直接按位图贴上去，省掉字体转换与 LVGL 移植，
// 只用 1 块 360x360 PSRAM 帧缓冲 + 1 条内部 RAM 刷屏缓冲。
#pragma once

#include <stdint.h>

#include "Config.h"

namespace gfx {

// ---- 颜色 ----
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t C_BG      = rgb(8, 12, 10);      // 军绿黑
constexpr uint16_t C_PANEL   = rgb(16, 26, 20);
constexpr uint16_t C_LINE    = rgb(38, 62, 46);
constexpr uint16_t C_TEXT    = rgb(214, 236, 216);
constexpr uint16_t C_DIM     = rgb(120, 150, 128);
constexpr uint16_t C_ACCENT  = rgb(64, 232, 160);   // 青绿
constexpr uint16_t C_AMBER   = rgb(255, 176, 48);
constexpr uint16_t C_RED     = rgb(240, 72, 64);
constexpr uint16_t C_BLUE    = rgb(88, 168, 255);

// ---- 生命周期 ----
bool begin();                 // 分配帧缓冲 + 刷屏缓冲；返回是否成功
bool ready();
uint16_t* pixels();

// ---- 切页淡入淡出（用两块 PSRAM 帧缓冲做混合）----
bool fadeReady();
void fadeSnapshot();                 // 记住当前帧（旧页面）→ prev
void fadeSaveTarget();               // 记住当前帧（新页面）→ target
void fadeApply(uint8_t alpha);       // fb = mix(prev, target, alpha)
void fadeOut(uint8_t alpha, uint16_t to);   // fb = mix(prev, to, alpha)   —— 淡出
void fadeIn(uint8_t alpha, uint16_t from);  // fb = mix(from, target, alpha) —— 淡入

// ---- 脏区 / 刷屏 ----
void clear(uint16_t color);                 // 整屏填充并标记全脏
void markAll();
void flush();                               // 把脏区推到屏幕（阻塞到 SPI 发完）

// ---- 裁剪 ----
void setClip(int x, int y, int w, int h);
void resetClip();

// ---- 图元（坐标：x,y = 左上角，w,h = 尺寸）----
void fillRect(int x, int y, int w, int h, uint16_t c);
void fillRectBlend(int x, int y, int w, int h, uint16_t c, uint8_t alpha);
void rect(int x, int y, int w, int h, uint16_t c);
void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c);
void roundRect(int x, int y, int w, int h, int r, uint16_t c);
void hline(int x, int y, int w, uint16_t c);
void vline(int x, int y, int h, uint16_t c);
void line(int x0, int y0, int x1, int y1, uint16_t c, int thick = 1);
void fillCircle(int cx, int cy, int r, uint16_t c);
void circle(int cx, int cy, int r, uint16_t c, int thick = 1);
// 角度：0° = 正上方，顺时针增大；用于罗盘指针与环形进度
void ringArc(int cx, int cy, int r, int a0, int a1, int thick, uint16_t c);
void fillWedge(int cx, int cy, int r, int a0, int a1, uint16_t c);
void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

// ---- 文本 ----
// size: 16 或 24（对应两套点阵字）；scale: 整数倍放大
int textWidth(const char* utf8, int size, int scale = 1);
int textHeight(int size, int scale = 1);
// 返回实际绘制宽度；maxW>0 时超宽截断并补 ".."
int text(int x, int y, const char* utf8, int size, uint16_t c, int scale = 1, int maxW = 0);
int textCenter(int cx, int y, const char* utf8, int size, uint16_t c, int scale = 1, int maxW = 0);
int textRight(int xr, int y, const char* utf8, int size, uint16_t c, int scale = 1, int maxW = 0);
// 单行高度 = textHeight；列表里常用"行内左中右"
void textRow(int x, int y, int w, int h, const char* left, const char* right, int size, uint16_t cLeft,
             uint16_t cRight);

}  // namespace gfx
