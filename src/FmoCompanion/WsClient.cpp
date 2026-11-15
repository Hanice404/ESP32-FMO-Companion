#include "WsClient.h"

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <string.h>

void wsMakeKey(char* out, size_t outLen) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint8_t rnd[16];
  for (int i = 0; i < 16; i++) rnd[i] = (uint8_t)esp_random();
  char tmp[26];
  int o = 0;
  for (int i = 0; i < 16; i += 3) {
    const int b0 = rnd[i];
    const int b1 = (i + 1 < 16) ? rnd[i + 1] : 0;
    const int b2 = (i + 2 < 16) ? rnd[i + 2] : 0;
    tmp[o++] = tbl[b0 >> 2];
    tmp[o++] = tbl[((b0 & 3) << 4) | (b1 >> 4)];
    tmp[o++] = (i + 1 < 16) ? tbl[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=';
    tmp[o++] = (i + 2 < 16) ? tbl[b2 & 0x3F] : '=';
  }
  tmp[o] = 0;
  if (outLen == 0) return;
  strncpy(out, tmp, outLen - 1);
  out[outLen - 1] = 0;
}

void WsClient::setTarget(const char* host, uint16_t port, const char* path) {
  _host = host ? host : "";
  _path = path ? path : "/";
  _port = port;
}

bool WsClient::ensureBuf() {
  if (_buf) return true;
  _buf = (uint8_t*)heap_caps_malloc(kMaxMessage, MALLOC_CAP_SPIRAM);
  if (!_buf) _buf = (uint8_t*)malloc(kMaxMessage);
  return _buf != nullptr;
}

void WsClient::close() {
  // 先发一个 close 帧（1000 Normal Closure）再断 TCP：
  // FMO 那台设备的 httpd 连接槽位有限，硬断（直接 stop）会在它那边留下
  // 半开连接，槽位要等很久才回收；发 close 帧能让它立刻释放。
  if (_c.connected()) {
    const uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
    const uint8_t pl[2] = {0x03, 0xE8};
    const uint8_t f[8] = {0x88,
                          0x82,
                          mask[0],
                          mask[1],
                          mask[2],
                          mask[3],
                          (uint8_t)(pl[0] ^ mask[0]),
                          (uint8_t)(pl[1] ^ mask[1])};
    _c.write(f, sizeof(f));
    _c.flush();
    delay(5);
  }
  _c.stop();
  _len = 0;
  _binary = false;
}

bool WsClient::connected() { return _c.connected() && _buf != nullptr; }

bool WsClient::connect(uint32_t timeoutMs) {
  close();
  if (!ensureBuf()) return false;
  _c.setTimeout(2);
  if (!_c.connect(_host.c_str(), _port, timeoutMs)) {
    _c.stop();
    return false;
  }
  _c.setNoDelay(true);

  char key[28];
  wsMakeKey(key, sizeof(key));
  String req;
  req.reserve(220);
  req += "GET ";
  req += _path;
  req += " HTTP/1.1\r\nHost: ";
  req += _host;
  req += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ";
  req += key;
  req += "\r\nSec-WebSocket-Version: 13\r\nOrigin: http://";
  req += _host;
  req += "\r\n\r\n";
  _c.print(req);
  _c.flush();

  // 读响应头（到空行为止）
  String hdr;
  hdr.reserve(320);
  const uint32_t t0 = millis();
  bool okEnd = false;
  while (millis() - t0 < timeoutMs) {
    while (_c.available()) {
      const char ch = (char)_c.read();
      hdr += ch;
      if (hdr.endsWith("\r\n\r\n")) {
        okEnd = true;
        break;
      }
      if (hdr.length() > 1200) break;
    }
    if (okEnd) break;
    if (!_c.connected()) break;
    delay(1);
  }
  if (!okEnd || hdr.indexOf(" 101") < 0) {
    _c.stop();
    return false;
  }
  _len = 0;
  _binary = false;
  _lastRxMs = millis();
  // 给对端一点时间把握手收尾（实测紧跟 101 立刻发首帧，FMO 常常不回应）
  delay(60);
  return true;
}

int WsClient::readExact(uint8_t* dst, size_t n, uint32_t deadline) {
  size_t got = 0;
  while (got < n) {
    const int avail = _c.available();
    if (avail > 0) {
      const int want = (int)((n - got) > (size_t)avail ? (size_t)avail : (n - got));
      const int r = _c.read(dst + got, want);
      if (r > 0) {
        got += (size_t)r;
        _lastRxMs = millis();
        continue;
      }
    }
    if (!_c.connected()) return -1;
    if ((int32_t)(millis() - deadline) > 0) return (int)got;
    delay(1);
  }
  return (int)got;
}

int WsClient::readFrame(uint32_t deadline) {
  uint8_t h[2];
  const int r0 = readExact(h, 2, deadline);
  if (r0 <= 0) return r0;   // 0 = 超时无数据
  if (r0 != 2) return -1;

  const bool fin = (h[0] & 0x80) != 0;
  const uint8_t op = (uint8_t)(h[0] & 0x0F);
  const bool masked = (h[1] & 0x80) != 0;
  uint32_t len = (uint32_t)(h[1] & 0x7F);

  if (len == 126) {
    uint8_t e[2];
    if (readExact(e, 2, deadline) != 2) return -1;
    len = ((uint32_t)e[0] << 8) | e[1];
  } else if (len == 127) {
    uint8_t e[8];
    if (readExact(e, 8, deadline) != 8) return -1;
    if (e[0] || e[1] || e[2] || e[3]) return -1;  // 超过 4 GB，肯定不是给我们
    len = ((uint32_t)e[4] << 24) | ((uint32_t)e[5] << 16) | ((uint32_t)e[6] << 8) | e[7];
  }
  if (len > kMaxMessage) return -1;
  if (_len + len > kMaxMessage) return -1;

  uint8_t mask[4] = {0, 0, 0, 0};
  if (masked && readExact(mask, 4, deadline) != 4) return -1;

  if (len) {
    const int got = readExact(_buf + _len, len, deadline);
    if (got != (int)len) return -1;
    if (masked) {
      for (uint32_t i = 0; i < len; i++) _buf[_len + i] ^= mask[i & 3];
    }
    _len += len;
  }
  _frameOp = op;
  _finFrame = fin;
  _lastRxMs = millis();
  return 1;
}

bool WsClient::sendText(const char* text) {
  if (!_c.connected() || !text) return false;
  const size_t n = strlen(text);
  if (n > 65000) return false;

  uint8_t mask[4];
  for (int i = 0; i < 4; i++) mask[i] = (uint8_t)esp_random();

  uint8_t hdr[8];
  int hn = 0;
  hdr[hn++] = 0x81;
  if (n < 126) {
    hdr[hn++] = (uint8_t)(0x80 | n);
  } else {
    hdr[hn++] = 0x80 | 126;
    hdr[hn++] = (uint8_t)((n >> 8) & 0xFF);
    hdr[hn++] = (uint8_t)(n & 0xFF);
  }
  hdr[hn++] = mask[0];
  hdr[hn++] = mask[1];
  hdr[hn++] = mask[2];
  hdr[hn++] = mask[3];

  // 整帧一次性写出：FMO 的 WebSocket 服务端对"头一包、载荷下一包"很敏感，
  // 拆成两次 write 时经常再也等不到响应（实测：PC 端一次写出就能拿到回应）。
  static uint8_t frame[4 + 1024];
  if (n <= sizeof(frame) - sizeof(hdr)) {
    memcpy(frame, hdr, (size_t)hn);
    for (size_t i = 0; i < n; i++) frame[hn + i] = (uint8_t)text[i] ^ mask[i & 3];
    const size_t total = (size_t)hn + n;
    if (_c.write(frame, total) != total) return false;
  } else {
    if (_c.write(hdr, (size_t)hn) != (size_t)hn) return false;
    uint8_t chunk[256];
    size_t sent = 0;
    while (sent < n) {
      size_t k = n - sent;
      if (k > sizeof(chunk)) k = sizeof(chunk);
      for (size_t i = 0; i < k; i++) chunk[i] = (uint8_t)text[sent + i] ^ mask[(sent + i) & 3];
      if (_c.write(chunk, k) != k) return false;
      sent += k;
    }
  }
  _c.flush();
  return true;
}

bool WsClient::sendPing() {
  if (!_c.connected()) return false;
  const uint8_t mask[4] = {1, 2, 3, 4};
  const uint8_t f[6] = {0x89, 0x80, mask[0], mask[1], mask[2], mask[3]};
  return _c.write(f, sizeof(f)) == sizeof(f);
}

int WsClient::poll(uint32_t waitMs) {
  if (!_c.connected()) return -1;
  const uint32_t waitUntil = millis() + waitMs;
  while (!_c.available()) {
    if (!_c.connected()) return -1;
    if ((int32_t)(millis() - waitUntil) > 0) return 0;
    delay(1);
  }

  // 每一次 poll 都是"取一条新消息"：必须清空重组缓冲，
  // 否则第二条消息会拼在第一条后面，JSON 解析只会看到第一条（表现为后续请求全部超时）。
  _len = 0;

  for (;;) {
    const int r = readFrame(millis() + 1200);
    if (r == 0) return 0;
    if (r < 0) {
      close();
      return -1;
    }
    switch (_frameOp) {
      case 0x8:  // close
        close();
        return -1;
      case 0x9: {  // ping → pong（回显载荷，≤125 字节）
        const size_t n = (_len <= 125) ? _len : 0;
        uint8_t mask[4];
        for (int i = 0; i < 4; i++) mask[i] = (uint8_t)esp_random();
        uint8_t hdr[6];
        int hn = 0;
        hdr[hn++] = 0x8A;
        hdr[hn++] = (uint8_t)(0x80 | n);
        hdr[hn++] = mask[0];
        hdr[hn++] = mask[1];
        hdr[hn++] = mask[2];
        hdr[hn++] = mask[3];
        _c.write(hdr, (size_t)hn);
        if (n) {
          uint8_t tmp[125];
          for (size_t i = 0; i < n; i++) tmp[i] = _buf[i] ^ mask[i & 3];
          _c.write(tmp, n);
        }
        _c.flush();
        _len = 0;
        break;
      }
      case 0xA:  // pong
        _len = 0;
        break;
      case 0x1:
        _binary = false;
        if (_finFrame) return 1;
        break;
      case 0x2:
        _binary = true;
        if (_finFrame) return 1;
        break;
      case 0x0:
        if (_finFrame) return 1;
        break;
      default:
        _len = 0;
        break;
    }
    // 等下一帧
    const uint32_t d = millis() + 1200;
    while (!_c.available()) {
      if (!_c.connected()) return -1;
      if ((int32_t)(millis() - d) > 0) {
        close();
        return -1;
      }
      delay(1);
    }
  }
}
