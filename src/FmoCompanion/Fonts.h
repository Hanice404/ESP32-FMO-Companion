// Fonts.h —— 点阵字库访问（16x16 / 24x24 中文 + 8x16 / 12x24 半角 ASCII）
#pragma once

#include <stdint.h>

namespace fonts {

struct BitFont {
  uint8_t w;        // 字格宽（像素）
  uint8_t h;        // 字格高
  uint8_t bpr;      // 每行字节数
  bool ascii;       // true = 固定 ASCII 表（按码点直接索引）
  uint16_t count;
  const uint16_t* cp;    // CJK：升序码点表（ASCII 为 nullptr）
  const uint8_t* bits;
};

// size: 16 或 24
const BitFont& cjk(int size);
const BitFont& ascii(int size);
// 按码点挑字体：<0x80 用 ASCII，否则 CJK
const BitFont& pick(uint32_t cp, int size);
// 取字形位图；找不到返回 nullptr
const uint8_t* glyph(const BitFont& f, uint32_t cp);

}  // namespace fonts
