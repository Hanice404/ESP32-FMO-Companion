#pragma once
#include "TCA9554PWR.h"
#include "Touch_CST816.h"

#define LCD_Backlight_PIN   5
#define PWM_Channel     1       // PWM Channel   
#define Frequency       20000   // PWM frequencyconst        
#define Resolution      10       // PWM resolution ratio     MAX:13
#define Dutyfactor      500     // PWM Dutyfactor      
#define Backlight_MAX   100

#define EXAMPLE_LCD_WIDTH                   (360)
#define EXAMPLE_LCD_HEIGHT                  (360)
#define EXAMPLE_LCD_COLOR_BITS              (16)

#define ESP_PANEL_HOST_SPI_ID_DEFAULT       (SPI2_HOST)
#define ESP_PANEL_LCD_SPI_MODE              (0)                   // 0/1/2/3, typically set to 0
#define ESP_PANEL_LCD_SPI_CLK_HZ            (80 * 1000 * 1000)    // Should be an integer divisor of 80M, typically set to 40M
#define ESP_PANEL_LCD_SPI_TRANS_QUEUE_SZ    (10)                  // Typically set to 10
// ↑ 注意：不要设成 0。IDF 的 spi_bus_add_device 会用该值创建队列，
//   长度为 0 时 FreeRTOS 断言失败（实测：xQueueGenericCreate assert 崩溃）。
//   撕裂问题改用"等传输完成信号量"解决，见 LCD_WaitFlush()。
#define ESP_PANEL_LCD_SPI_CMD_BITS          (32)                  // Typically set to 32
#define ESP_PANEL_LCD_SPI_PARAM_BITS        (8)                   // Typically set to 8

#define ESP_PANEL_LCD_SPI_IO_TE             (18)
#define ESP_PANEL_LCD_SPI_IO_SCK            (40)
#define ESP_PANEL_LCD_SPI_IO_DATA0          (46)
#define ESP_PANEL_LCD_SPI_IO_DATA1          (45)
#define ESP_PANEL_LCD_SPI_IO_DATA2          (42)
#define ESP_PANEL_LCD_SPI_IO_DATA3          (41)
#define ESP_PANEL_LCD_SPI_IO_CS             (21)
#define EXAMPLE_LCD_PIN_NUM_RST             (-1)    // EXIO2
#define EXAMPLE_LCD_PIN_NUM_BK_LIGHT        (-1)

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL       (1)
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL

#define ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE   (2048)

extern uint8_t LCD_Backlight;

void ST77916_Init();

void LCD_Init();
void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend,uint16_t* color);

// [FmoCompanion] ---- 自绘帧缓冲刷屏 ----
// 说明：color 里的像素必须【已经做过字节交换】（与 LCD_addWindow 内部一致），
//       x1 / y1 是排他上界（和 esp_lcd_panel_draw_bitmap 一致）。
void LCD_DrawRaw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, const uint16_t *color);
// 屏幕方向：0=0° 1=90° 2=180° 3=270°（靠 MADCTL 的 MV/MX/MY）
void LCD_SetRotation(uint8_t rot);

// [AttitudeBall] 等待上一段颜色数据真正发送完成（供 LVGL 在复用绘制缓冲前调用）
void LCD_WaitFlush(void);

// backlight
void Backlight_Init();
void Set_Backlight(uint8_t Light);  