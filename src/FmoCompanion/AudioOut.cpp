#include "AudioOut.h"

#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

namespace {
constexpr size_t kRingBytes = AUDIO_RING_KB * 1024;
constexpr size_t kChunkSamples = 160;          // 20 ms @ 8 kHz
constexpr size_t kPrimeBytes = 3200;           // 起播前先攒 200 ms

i2s_chan_handle_t s_tx = nullptr;
int16_t s_chunk[kChunkSamples * 2];            // 立体声交织（PCM5101 双声道都要喂）
}  // namespace

bool AudioOut::begin() {
  if (!_ring) {
    _ring = (uint8_t*)heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM);
    if (!_ring) _ring = (uint8_t*)malloc(kRingBytes);
  }
  if (!_ring) {
    DBG("[audio] ring alloc failed\n");
    return false;
  }
  _head = _tail = _fill = 0;

  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 6;
  cc.dma_frame_num = 256;
  if (i2s_new_channel(&cc, &s_tx, nullptr) != ESP_OK) {
    DBG("[audio] i2s_new_channel failed\n");
    return false;
  }
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE);
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = (gpio_num_t)I2S_BCLK_PIN;
  sc.gpio_cfg.ws = (gpio_num_t)I2S_LRCK_PIN;
  sc.gpio_cfg.dout = (gpio_num_t)I2S_DOUT_PIN;
  sc.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK) {
    DBG("[audio] i2s init std failed\n");
    return false;
  }
  if (i2s_channel_enable(s_tx) != ESP_OK) {
    DBG("[audio] i2s enable failed\n");
    return false;
  }
  _i2sOk = true;
  DBG("[audio] i2s ready (%d Hz, BCLK=%d LRCK=%d DOUT=%d)\n", AUDIO_RATE, I2S_BCLK_PIN, I2S_LRCK_PIN,
      I2S_DOUT_PIN);
  return true;
}

void AudioOut::setTarget(const char* host) {
  _host = host ? host : "";
  _ws.setTarget(_host.c_str(), FMO_HTTP_PORT, FMO_AUDIO_PATH);
  if (_ws.connected()) _ws.close();
  _up = false;
}

void AudioOut::start() {
  _want = true;
  _lastTry = 0;
  _startedAt = 0;
}

void AudioOut::stop() {
  _want = false;
  if (_ws.connected()) _ws.close();
  _up = false;
  _head = _tail = _fill = 0;
  _level = 0;
}

void AudioOut::ringWrite(const uint8_t* p, size_t n) {
  if (!_ring) return;
  if (_fill + n > kRingBytes) {
    _dropped += (uint32_t)n;
    return;  // 满了丢新数据（保证延迟不涨）
  }
  for (size_t i = 0; i < n; i++) {
    _ring[_head] = p[i];
    _head = (_head + 1) % kRingBytes;
  }
  _fill += n;
}

size_t AudioOut::ringRead(uint8_t* p, size_t n) {
  if (!_ring) return 0;
  const size_t avail = _fill;
  if (n > avail) n = avail;
  for (size_t i = 0; i < n; i++) {
    p[i] = _ring[_tail];
    _tail = (_tail + 1) % kRingBytes;
  }
  _fill -= n;
  return n;
}

uint32_t AudioOut::bufferedMs() const { return (uint32_t)((uint64_t)_fill * 1000 / (AUDIO_RATE * 2)); }

void AudioOut::playTestTone(uint32_t ms, uint16_t freqHz) {
  if (!_ring) return;
  const uint32_t total = (uint32_t)AUDIO_RATE * ms / 1000;
  uint8_t buf[kChunkSamples * 2];
  float phase = 0.0f;
  const float step = 6.2831853f * (float)freqHz / (float)AUDIO_RATE;
  uint32_t done = 0;
  while (done < total) {
    uint32_t n = total - done;
    if (n > kChunkSamples) n = kChunkSamples;
    for (uint32_t i = 0; i < n; i++) {
      const int16_t v = (int16_t)(sinf(phase) * 12000.0f);
      buf[i * 2] = (uint8_t)(v & 0xFF);
      buf[i * 2 + 1] = (uint8_t)((v >> 8) & 0xFF);
      phase += step;
      if (phase > 6.2831853f) phase -= 6.2831853f;
    }
    ringWrite(buf, n * 2);
    _bytes += n * 2;
    done += n;
  }
  _want = true;       // 让音频任务继续跑（不需要连 FMO）
  _startedAt = millis();
}

void AudioOut::loop(uint32_t budgetMs) {
  if (!_want) {
    if (_ws.connected()) _ws.close();
    _up = false;
    return;
  }
  const uint32_t now = millis();
  if (!_ws.connected()) {
    if (_lastTry == 0 || now - _lastTry > 3000) {
      _lastTry = now;
      if (_ws.connect(2500)) {
        DBG("[audio] /audio connected\n");
        _up = true;
        _startedAt = now;
      } else {
        _up = false;
      }
    }
    return;
  }
  // 取一到多帧
  const uint32_t until = millis() + budgetMs;
  do {
    const int r = _ws.poll(2);
    if (r == 1) {
      if (_ws.isBinary()) {
        ringWrite(_ws.bytes(), _ws.length());
        _bytes += (uint32_t)_ws.length();
        _lastDataMs = millis();
      }
    } else if (r < 0) {
      _up = false;
      DBG("[audio] /audio closed\n");
      break;
    } else {
      break;
    }
  } while ((int32_t)(millis() - until) < 0);
}

void AudioOut::taskStep() {
  if (!_i2sOk) {
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }
  const size_t wantBytes = kChunkSamples * 2;  // 单声道字节数
  uint8_t raw[wantBytes];
  size_t got = ringRead(raw, wantBytes);

  // 起播条件：缓冲够；断流超过一定时间就重新攒
  if (_fill > 0 && _fill < (uint32_t)kPrimeBytes && _startedAt == 0) {
    // 还没开始播：用静音填空，避免咔哒声
    memset(s_chunk, 0, sizeof(s_chunk));
  }

  if (got < wantBytes) {
    // 数据不够：补静音，保证 I²S 不断流（避免 DMA 重复旧样本产生杂音）
    memset(raw + got, 0, wantBytes - got);
  }

  const int16_t* src = (const int16_t*)raw;
  const int g = _gain;
  int64_t acc = 0;
  const size_t monoN = got / 2;
  for (size_t i = 0; i < wantBytes / 2; i++) {
    int32_t v = src[i];
    v = (v * g) / 100;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    s_chunk[i * 2] = (int16_t)v;
    s_chunk[i * 2 + 1] = (int16_t)v;
    if (i < monoN) acc += (int64_t)src[i] * src[i];
  }

  size_t written = 0;
  const uint8_t* out = (const uint8_t*)s_chunk;
  const size_t outBytes = sizeof(s_chunk);
  while (written < outBytes) {
    size_t w = 0;
    if (i2s_channel_write(s_tx, out + written, outBytes - written, &w, 100) != ESP_OK) break;
    if (w == 0) break;
    written += w;
  }

  // 电平（RMS → 0..100，-48 dB 以下算静音）
  if (monoN > 8) {
    const float rms = sqrtf((float)((double)acc / (double)monoN)) / 32768.0f;
    int lv = 0;
    if (rms > 0.0008f) {
      const float db = 20.0f * log10f(rms);
      lv = (int)((db + 48.0f) * 100.0f / 48.0f);
      if (lv < 0) lv = 0;
      if (lv > 100) lv = 100;
    }
    // 快攻击慢释放
    _level = (lv > _level) ? lv : (_level * 3 + lv) / 4;
  } else {
    _level = (_level * 3) / 4;
  }

  if (got > 0 && _startedAt == 0) _startedAt = millis();
  if (_fill == 0 && _startedAt != 0 && millis() - _startedAt > 1500) _startedAt = 0;
}
