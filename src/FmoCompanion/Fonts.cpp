#include "Fonts.h"

#include <stddef.h>

#include "Font16.h"
#include "Font24.h"
#include "FontAscii16.h"
#include "FontAscii24.h"

namespace fonts {
namespace {

const BitFont kCjk16 = {FONT16_W,   FONT16_H,   FONT16_BPR,   false,
                        FONT16_COUNT, FONT16_CP, FONT16_BITS};
const BitFont kCjk24 = {FONT24_W,   FONT24_H,   FONT24_BPR,   false,
                        FONT24_COUNT, FONT24_CP, FONT24_BITS};
const BitFont kAscii16 = {ASCII16_W, ASCII16_H, ASCII16_BPR, true,
                          ASCII16_COUNT, nullptr, ASCII16_BITS};
const BitFont kAscii24 = {ASCII24_W, ASCII24_H, ASCII24_BPR, true,
                          ASCII24_COUNT, nullptr, ASCII24_BITS};

}  // namespace

const BitFont& cjk(int size) { return (size >= 24) ? kCjk24 : kCjk16; }
const BitFont& ascii(int size) { return (size >= 24) ? kAscii24 : kAscii16; }

const BitFont& pick(uint32_t cp, int size) { return (cp < 0x80) ? ascii(size) : cjk(size); }

const uint8_t* glyph(const BitFont& f, uint32_t cp) {
  const int bytes = f.bpr * f.h;
  if (f.ascii) {
    if (cp < 0x20 || cp > 0x7E) return nullptr;
    return f.bits + (size_t)(cp - 0x20) * bytes;
  }
  // 二分查找
  int lo = 0, hi = (int)f.count - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    const uint32_t v = f.cp[mid];
    if (v == cp) return f.bits + (size_t)mid * bytes;
    if (v < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

}  // namespace fonts
