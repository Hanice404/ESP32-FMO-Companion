// WsClient.h —— 极简 WebSocket 客户端（RFC6455 客户端侧，够用即止）
//
// 为什么不用现成库：FMO 需要 3 条独立连接（/ws 请求响应、/events 只推、/audio 裸 PCM），
// 且服务端会把一条 JSON 拆成多帧发；自己实现可以精确控制分片重组、超时和二进制通道，
// 也避免再引第三方库。
#pragma once

#include <Arduino.h>
#include <WiFiClient.h>

class WsClient {
 public:
  static const size_t kMaxMessage = 16384;  // 单条消息上限（含分片累计）

  void setTarget(const char* host, uint16_t port, const char* path);
  bool connect(uint32_t timeoutMs = 4000);
  void close();
  bool connected();

  // 发送一条文本消息（客户端→服务端必须加掩码）
  bool sendText(const char* text);
  bool sendPing();

  // 非阻塞读：返回 1 = 收到一条完整消息，0 = 暂无数据，-1 = 连接异常/关闭
  int poll(uint32_t waitMs);

  // 最近一条消息
  bool isBinary() const { return _binary; }
  const char* text() const { return (const char*)_buf; }
  size_t length() const { return _len; }
  const uint8_t* bytes() const { return _buf; }

 private:
  int readFrame(uint32_t deadline);
  int readExact(uint8_t* dst, size_t n, uint32_t deadline);
  bool ensureBuf();

  WiFiClient _c;
  String _host;
  String _path;
  uint16_t _port = 80;

  uint8_t* _buf = nullptr;   // 消息重组缓冲（PSRAM）
  size_t _len = 0;           // 当前消息已收字节
  bool _binary = false;
  uint8_t _frameOp = 0;
  bool _finFrame = false;
  uint32_t _lastRxMs = 0;
};

// 生成 16 字节随机数并 base64（用于 Sec-WebSocket-Key）
void wsMakeKey(char* out, size_t outLen);
