#include "Display_ST77916.h"

#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "esp_intr_alloc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_st77916.h"

#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_panel_ops.h"

#define LCD_OPCODE_WRITE_CMD        (0x02ULL)
#define LCD_OPCODE_READ_CMD         (0x0BULL)
#define LCD_OPCODE_WRITE_COLOR      (0x32ULL)

static const st77916_lcd_init_cmd_t vendor_specific_init_new[] = {
  {0xF0, (uint8_t []){0x28}, 1, 0},
  {0xF2, (uint8_t []){0x28}, 1, 0},
  {0x73, (uint8_t []){0xF0}, 1, 0},
  {0x7C, (uint8_t []){0xD1}, 1, 0},
  {0x83, (uint8_t []){0xE0}, 1, 0},
  {0x84, (uint8_t []){0x61}, 1, 0},
  {0xF2, (uint8_t []){0x82}, 1, 0},
  {0xF0, (uint8_t []){0x00}, 1, 0},
  {0xF0, (uint8_t []){0x01}, 1, 0},
  {0xF1, (uint8_t []){0x01}, 1, 0},
  {0xB0, (uint8_t []){0x56}, 1, 0},
  {0xB1, (uint8_t []){0x4D}, 1, 0},
  {0xB2, (uint8_t []){0x24}, 1, 0},
  {0xB4, (uint8_t []){0x87}, 1, 0},
  {0xB5, (uint8_t []){0x44}, 1, 0},
  {0xB6, (uint8_t []){0x8B}, 1, 0},
  {0xB7, (uint8_t []){0x40}, 1, 0},
  {0xB8, (uint8_t []){0x86}, 1, 0},
  {0xBA, (uint8_t []){0x00}, 1, 0},
  {0xBB, (uint8_t []){0x08}, 1, 0},
  {0xBC, (uint8_t []){0x08}, 1, 0},
  {0xBD, (uint8_t []){0x00}, 1, 0},
  {0xC0, (uint8_t []){0x80}, 1, 0},
  {0xC1, (uint8_t []){0x10}, 1, 0},
  {0xC2, (uint8_t []){0x37}, 1, 0},
  {0xC3, (uint8_t []){0x80}, 1, 0},
  {0xC4, (uint8_t []){0x10}, 1, 0},
  {0xC5, (uint8_t []){0x37}, 1, 0},
  {0xC6, (uint8_t []){0xA9}, 1, 0},
  {0xC7, (uint8_t []){0x41}, 1, 0},
  {0xC8, (uint8_t []){0x01}, 1, 0},
  {0xC9, (uint8_t []){0xA9}, 1, 0},
  {0xCA, (uint8_t []){0x41}, 1, 0},
  {0xCB, (uint8_t []){0x01}, 1, 0},
  {0xD0, (uint8_t []){0x91}, 1, 0},
  {0xD1, (uint8_t []){0x68}, 1, 0},
  {0xD2, (uint8_t []){0x68}, 1, 0},
  {0xF5, (uint8_t []){0x00, 0xA5}, 2, 0},
  {0xDD, (uint8_t []){0x4F}, 1, 0},
  {0xDE, (uint8_t []){0x4F}, 1, 0},
  {0xF1, (uint8_t []){0x10}, 1, 0},
  {0xF0, (uint8_t []){0x00}, 1, 0},
  {0xF0, (uint8_t []){0x02}, 1, 0},
  {0xE0, (uint8_t []){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
  {0xE1, (uint8_t []){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
  {0xF0, (uint8_t []){0x10}, 1, 0},
  {0xF3, (uint8_t []){0x10}, 1, 0},
  {0xE0, (uint8_t []){0x07}, 1, 0},
  {0xE1, (uint8_t []){0x00}, 1, 0},
  {0xE2, (uint8_t []){0x00}, 1, 0},
  {0xE3, (uint8_t []){0x00}, 1, 0},
  {0xE4, (uint8_t []){0xE0}, 1, 0},
  {0xE5, (uint8_t []){0x06}, 1, 0},
  {0xE6, (uint8_t []){0x21}, 1, 0},
  {0xE7, (uint8_t []){0x01}, 1, 0},
  {0xE8, (uint8_t []){0x05}, 1, 0},
  {0xE9, (uint8_t []){0x02}, 1, 0},
  {0xEA, (uint8_t []){0xDA}, 1, 0},
  {0xEB, (uint8_t []){0x00}, 1, 0},
  {0xEC, (uint8_t []){0x00}, 1, 0},
  {0xED, (uint8_t []){0x0F}, 1, 0},
  {0xEE, (uint8_t []){0x00}, 1, 0},
  {0xEF, (uint8_t []){0x00}, 1, 0},
  {0xF8, (uint8_t []){0x00}, 1, 0},
  {0xF9, (uint8_t []){0x00}, 1, 0},
  {0xFA, (uint8_t []){0x00}, 1, 0},
  {0xFB, (uint8_t []){0x00}, 1, 0},
  {0xFC, (uint8_t []){0x00}, 1, 0},
  {0xFD, (uint8_t []){0x00}, 1, 0},
  {0xFE, (uint8_t []){0x00}, 1, 0},
  {0xFF, (uint8_t []){0x00}, 1, 0},
  {0x60, (uint8_t []){0x40}, 1, 0},
  {0x61, (uint8_t []){0x04}, 1, 0},
  {0x62, (uint8_t []){0x00}, 1, 0},
  {0x63, (uint8_t []){0x42}, 1, 0},
  {0x64, (uint8_t []){0xD9}, 1, 0},
  {0x65, (uint8_t []){0x00}, 1, 0},
  {0x66, (uint8_t []){0x00}, 1, 0},
  {0x67, (uint8_t []){0x00}, 1, 0},
  {0x68, (uint8_t []){0x00}, 1, 0},
  {0x69, (uint8_t []){0x00}, 1, 0},
  {0x6A, (uint8_t []){0x00}, 1, 0},
  {0x6B, (uint8_t []){0x00}, 1, 0},
  {0x70, (uint8_t []){0x40}, 1, 0},
  {0x71, (uint8_t []){0x03}, 1, 0},
  {0x72, (uint8_t []){0x00}, 1, 0},
  {0x73, (uint8_t []){0x42}, 1, 0},
  {0x74, (uint8_t []){0xD8}, 1, 0},
  {0x75, (uint8_t []){0x00}, 1, 0},
  {0x76, (uint8_t []){0x00}, 1, 0},
  {0x77, (uint8_t []){0x00}, 1, 0},
  {0x78, (uint8_t []){0x00}, 1, 0},
  {0x79, (uint8_t []){0x00}, 1, 0},
  {0x7A, (uint8_t []){0x00}, 1, 0},
  {0x7B, (uint8_t []){0x00}, 1, 0},
  {0x80, (uint8_t []){0x48}, 1, 0},
  {0x81, (uint8_t []){0x00}, 1, 0},
  {0x82, (uint8_t []){0x06}, 1, 0},
  {0x83, (uint8_t []){0x02}, 1, 0},
  {0x84, (uint8_t []){0xD6}, 1, 0},
  {0x85, (uint8_t []){0x04}, 1, 0},
  {0x86, (uint8_t []){0x00}, 1, 0},
  {0x87, (uint8_t []){0x00}, 1, 0},
  {0x88, (uint8_t []){0x48}, 1, 0},
  {0x89, (uint8_t []){0x00}, 1, 0},
  {0x8A, (uint8_t []){0x08}, 1, 0},
  {0x8B, (uint8_t []){0x02}, 1, 0},
  {0x8C, (uint8_t []){0xD8}, 1, 0},
  {0x8D, (uint8_t []){0x04}, 1, 0},
  {0x8E, (uint8_t []){0x00}, 1, 0},
  {0x8F, (uint8_t []){0x00}, 1, 0},
  {0x90, (uint8_t []){0x48}, 1, 0},
  {0x91, (uint8_t []){0x00}, 1, 0},
  {0x92, (uint8_t []){0x0A}, 1, 0},
  {0x93, (uint8_t []){0x02}, 1, 0},
  {0x94, (uint8_t []){0xDA}, 1, 0},
  {0x95, (uint8_t []){0x04}, 1, 0},
  {0x96, (uint8_t []){0x00}, 1, 0},
  {0x97, (uint8_t []){0x00}, 1, 0},
  {0x98, (uint8_t []){0x48}, 1, 0},
  {0x99, (uint8_t []){0x00}, 1, 0},
  {0x9A, (uint8_t []){0x0C}, 1, 0},
  {0x9B, (uint8_t []){0x02}, 1, 0},
  {0x9C, (uint8_t []){0xDC}, 1, 0},
  {0x9D, (uint8_t []){0x04}, 1, 0},
  {0x9E, (uint8_t []){0x00}, 1, 0},
  {0x9F, (uint8_t []){0x00}, 1, 0},
  {0xA0, (uint8_t []){0x48}, 1, 0},
  {0xA1, (uint8_t []){0x00}, 1, 0},
  {0xA2, (uint8_t []){0x05}, 1, 0},
  {0xA3, (uint8_t []){0x02}, 1, 0},
  {0xA4, (uint8_t []){0xD5}, 1, 0},
  {0xA5, (uint8_t []){0x04}, 1, 0},
  {0xA6, (uint8_t []){0x00}, 1, 0},
  {0xA7, (uint8_t []){0x00}, 1, 0},
  {0xA8, (uint8_t []){0x48}, 1, 0},
  {0xA9, (uint8_t []){0x00}, 1, 0},
  {0xAA, (uint8_t []){0x07}, 1, 0},
  {0xAB, (uint8_t []){0x02}, 1, 0},
  {0xAC, (uint8_t []){0xD7}, 1, 0},
  {0xAD, (uint8_t []){0x04}, 1, 0},
  {0xAE, (uint8_t []){0x00}, 1, 0},
  {0xAF, (uint8_t []){0x00}, 1, 0},
  {0xB0, (uint8_t []){0x48}, 1, 0},
  {0xB1, (uint8_t []){0x00}, 1, 0},
  {0xB2, (uint8_t []){0x09}, 1, 0},
  {0xB3, (uint8_t []){0x02}, 1, 0},
  {0xB4, (uint8_t []){0xD9}, 1, 0},
  {0xB5, (uint8_t []){0x04}, 1, 0},
  {0xB6, (uint8_t []){0x00}, 1, 0},
  {0xB7, (uint8_t []){0x00}, 1, 0},
  
  {0xB8, (uint8_t []){0x48}, 1, 0},
  {0xB9, (uint8_t []){0x00}, 1, 0},
  {0xBA, (uint8_t []){0x0B}, 1, 0},
  {0xBB, (uint8_t []){0x02}, 1, 0},
  {0xBC, (uint8_t []){0xDB}, 1, 0},
  {0xBD, (uint8_t []){0x04}, 1, 0},
  {0xBE, (uint8_t []){0x00}, 1, 0},
  {0xBF, (uint8_t []){0x00}, 1, 0},
  {0xC0, (uint8_t []){0x10}, 1, 0},
  {0xC1, (uint8_t []){0x47}, 1, 0},
  {0xC2, (uint8_t []){0x56}, 1, 0},
  {0xC3, (uint8_t []){0x65}, 1, 0},
  {0xC4, (uint8_t []){0x74}, 1, 0},
  {0xC5, (uint8_t []){0x88}, 1, 0},
  {0xC6, (uint8_t []){0x99}, 1, 0},
  {0xC7, (uint8_t []){0x01}, 1, 0},
  {0xC8, (uint8_t []){0xBB}, 1, 0},
  {0xC9, (uint8_t []){0xAA}, 1, 0},
  {0xD0, (uint8_t []){0x10}, 1, 0},
  {0xD1, (uint8_t []){0x47}, 1, 0},
  {0xD2, (uint8_t []){0x56}, 1, 0},
  {0xD3, (uint8_t []){0x65}, 1, 0},
  {0xD4, (uint8_t []){0x74}, 1, 0},
  {0xD5, (uint8_t []){0x88}, 1, 0},
  {0xD6, (uint8_t []){0x99}, 1, 0},
  {0xD7, (uint8_t []){0x01}, 1, 0},
  {0xD8, (uint8_t []){0xBB}, 1, 0},
  {0xD9, (uint8_t []){0xAA}, 1, 0},
  {0xF3, (uint8_t []){0x01}, 1, 0},
  {0xF0, (uint8_t []){0x00}, 1, 0},
  {0x21, (uint8_t []){0x00}, 1, 0},
  {0x11, (uint8_t []){0x00}, 1, 120},
  {0x29, (uint8_t []){0x00}, 1, 0},  
};
void ST7701_Reset(){
  Set_EXIO(EXIO_PIN2,Low);
  vTaskDelay(pdMS_TO_TICKS(10));
  Set_EXIO(EXIO_PIN2,High);
  vTaskDelay(pdMS_TO_TICKS(50));
}
void LCD_Init() {        
  ST77916_Init();
  Touch_Init();
}

static void test_draw_bitmap(esp_lcd_panel_handle_t panel_handle)
{
  uint16_t row_line = ((EXAMPLE_LCD_WIDTH / EXAMPLE_LCD_COLOR_BITS) << 1) >> 1;
  uint8_t byte_per_pixel = EXAMPLE_LCD_COLOR_BITS / 8;
  uint8_t *color = (uint8_t *)heap_caps_calloc(1, row_line * EXAMPLE_LCD_HEIGHT * byte_per_pixel, MALLOC_CAP_DMA);


  for (int j = 0; j < EXAMPLE_LCD_COLOR_BITS; j++) {
      for (int i = 0; i < row_line * EXAMPLE_LCD_HEIGHT; i++) {
          for (int k = 0; k < byte_per_pixel; k++) {
              color[i * byte_per_pixel + k] = (SPI_SWAP_DATA_TX(BIT(j), EXAMPLE_LCD_COLOR_BITS) >> (k * 8)) & 0xff;
          }
      }
      esp_lcd_panel_draw_bitmap(panel_handle, 0, j * row_line, EXAMPLE_LCD_HEIGHT, (j + 1) * row_line, color);
  }
  free(color);
}

esp_lcd_panel_handle_t panel_handle = NULL;

// ================= [AttitudeBall] 颜色传输完成同步 =================
// 背景：LVGL 的绘制缓冲只有 1/10 屏，整屏刷新时会分多段写入【同一块】内存。
// 如果不等 DMA 真正完成就调用 lv_disp_flush_ready()，下一段数据会覆盖正在
// 发送的内容，画面就出现上下条带"交错/撕裂"（切换界面时整屏刷新，最明显）。
static SemaphoreHandle_t s_color_done_sem = NULL;
static volatile bool s_color_pending = false;

static bool lcd_on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
  (void)io; (void)edata; (void)user_ctx;
  BaseType_t hp = pdFALSE;
  s_color_pending = false;
  if (s_color_done_sem) xSemaphoreGiveFromISR(s_color_done_sem, &hp);
  return hp == pdTRUE;
}

void LCD_WaitFlush(void) {
  if (!s_color_done_sem || !s_color_pending) return;
  // 100 ms 超时兜底：万一回调没来也不会把主循环卡死
  if (xSemaphoreTake(s_color_done_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
    s_color_pending = false;
  }
}
// ==================================================================

int QSPI_Init(void){
  // [AttitudeBall] 创建"颜色传输完成"信号量（只需一次）
  if (s_color_done_sem == NULL) {
    s_color_done_sem = xSemaphoreCreateBinary();
  }
  static const spi_bus_config_t host_config = {            
    .data0_io_num = ESP_PANEL_LCD_SPI_IO_DATA0,                    
    .data1_io_num = ESP_PANEL_LCD_SPI_IO_DATA1,                   
    .sclk_io_num = ESP_PANEL_LCD_SPI_IO_SCK,                   
    .data2_io_num = ESP_PANEL_LCD_SPI_IO_DATA2,                    
    .data3_io_num = ESP_PANEL_LCD_SPI_IO_DATA3,                    
    .data4_io_num = -1,                       
    .data5_io_num = -1,                      
    .data6_io_num = -1,                       
    .data7_io_num = -1,                      
    .max_transfer_sz = ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE, 
    .flags = SPICOMMON_BUSFLAG_MASTER,       
    .intr_flags = 0,                            
  };
  if(spi_bus_initialize(ESP_PANEL_HOST_SPI_ID_DEFAULT, &host_config, SPI_DMA_CH_AUTO) != ESP_OK){
    printf("The SPI initialization failed.\r\n");
    return 0;
  }
  printf("The SPI initialization succeeded.\r\n");
  
  esp_lcd_panel_io_spi_config_t io_config ={
    .cs_gpio_num = ESP_PANEL_LCD_SPI_IO_CS,               
    .dc_gpio_num = -1,                  
    .spi_mode = ESP_PANEL_LCD_SPI_MODE,                      
    .pclk_hz = 5 * 1000 * 1000,       
    .trans_queue_depth = ESP_PANEL_LCD_SPI_TRANS_QUEUE_SZ,            
    .on_color_trans_done = lcd_on_color_trans_done,          // [AttitudeBall] 传输完成回调
    .user_ctx = NULL,                   
    .lcd_cmd_bits = ESP_PANEL_LCD_SPI_CMD_BITS,                 
    .lcd_param_bits = ESP_PANEL_LCD_SPI_PARAM_BITS,                
    .flags = {                          
      .dc_low_on_data = 0,            
      .octal_mode = 0,                
      .quad_mode = 1,                 
      .sio_mode = 0,                  
      .lsb_first = 0,                 
      .cs_high_active = 0,            
    },                                  
  };
  esp_lcd_panel_io_handle_t io_handle = NULL;
  if(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ESP_PANEL_HOST_SPI_ID_DEFAULT, &io_config, &io_handle) != ESP_OK){
    printf("Failed to set LCD communication parameters -- SPI\r\n");
    return 0;
  }
  printf("LCD communication parameters are set successfully -- SPI\r\n");

  printf("Install LCD driver of st77916\r\n");
  st77916_vendor_config_t vendor_config={  
    .flags = {
      .use_qspi_interface = 1,
    },
  };
  // [AttitudeBall] 官方留下的调试占位打印，已注释掉，避免启动刷屏
  esp_err_t ret;
  int lcd_cmd = 0x04;
  uint8_t register_data[4]; 
  size_t param_size = sizeof(register_data);
  lcd_cmd &= 0xff;
  lcd_cmd <<= 8;
  lcd_cmd |= LCD_OPCODE_READ_CMD << 24;  // Use the read opcode instead of write
  ret = esp_lcd_panel_io_rx_param(io_handle, lcd_cmd, register_data, param_size); 
  if (ret == ESP_OK) {
    printf("Register 0x04 data: %02x %02x %02x %02x\n", register_data[0], register_data[1], register_data[2], register_data[3]);
  } else {
    printf("Failed to read register 0x04, error code: %d\n", ret);
  } 
  // panel_io_spi_del(io_handle);
  io_config.pclk_hz = ESP_PANEL_LCD_SPI_CLK_HZ;
  if(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ESP_PANEL_HOST_SPI_ID_DEFAULT, &io_config, &io_handle) != ESP_OK){
    printf("Failed to set LCD communication parameters -- SPI\r\n");
    return 0;
  }
  printf("LCD communication parameters are set successfully -- SPI\r\n");
  
  // Check register values and configure accordingly
  if (register_data[0] == 0x00 && register_data[1] == 0x7F && register_data[2] == 0x7F && register_data[3] == 0x7F) {
    // Handle the case where the register data matches this pattern
    printf("Vendor-specific initialization for case 1.\n");
  }
  else if (register_data[0] == 0x00 && register_data[1] == 0x02 && register_data[2] == 0x7F && register_data[3] == 0x7F) {
    // Provide vendor-specific initialization commands if register data matches this pattern
    vendor_config.init_cmds = vendor_specific_init_new;
    vendor_config.init_cmds_size = sizeof(vendor_specific_init_new) / sizeof(st77916_lcd_init_cmd_t);
    printf("Vendor-specific initialization for case 2.\n");
  }
  // [AttitudeBall] 官方留下的调试占位打印，已注释掉，避免启动刷屏
 
  esp_lcd_panel_dev_config_t panel_config={
    .reset_gpio_num = EXAMPLE_LCD_PIN_NUM_RST,                                     
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,                   
    .data_endian = LCD_RGB_DATA_ENDIAN_BIG,                       
    .bits_per_pixel = EXAMPLE_LCD_COLOR_BITS,                                 
    .flags = {                                                    
      .reset_active_high = 0,                                   
    },                                                            
    .vendor_config = (void *) &vendor_config,                                  
  };
  esp_lcd_new_panel_st77916(io_handle, &panel_config, &panel_handle);

  esp_lcd_panel_reset(panel_handle);
  esp_lcd_panel_init(panel_handle);
  // esp_lcd_panel_invert_color(panel_handle,false);

  esp_lcd_panel_disp_on_off(panel_handle, true);
  test_draw_bitmap(panel_handle);
  return 1;
}

void ST77916_Init() {
  ST7701_Reset();
  pinMode(ESP_PANEL_LCD_SPI_IO_TE, OUTPUT);
  if(!QSPI_Init()){
    printf("ST77916 Failed to be initialized\r\n");
  }
}

void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend,uint16_t* color)
{ 
  uint32_t size = (Xend - Xstart +1 ) * (Yend - Ystart + 1);
  for (size_t i = 0; i < size; i++) {
    color[i] = (((color[i] >> 8) & 0xFF) | ((color[i] << 8) & 0xFF00));
  }
  // for (size_t i = 0; i < size; i++) {
  //   color[i] = 0xFFFF;
  // }
  Xend = Xend + 1;      // esp_lcd_panel_draw_bitmap: x_end End index on x-axis (x_end not included)
  Yend = Yend + 1;      // esp_lcd_panel_draw_bitmap: y_end End index on y-axis (y_end not included)
  if (Xend > EXAMPLE_LCD_WIDTH)
    Xend = EXAMPLE_LCD_WIDTH;
  if (Yend > EXAMPLE_LCD_HEIGHT)
    Yend = EXAMPLE_LCD_HEIGHT;
    
  // printf("Xstart = %d    Ystart = %d    Xend = %d    Yend = %d \r\n",Xstart, Ystart, Xend, Yend);
  s_color_pending = true;   // [AttitudeBall] 标记有传输在飞，供 LCD_WaitFlush() 判断
  esp_lcd_panel_draw_bitmap(panel_handle, Xstart, Ystart, Xend, Yend, color);                     // x_end End index on x-axis (x_end not included)
}


uint8_t LCD_Backlight = 50;
// backlight
void Backlight_Init()
{
  ledcAttach(LCD_Backlight_PIN, Frequency, Resolution);   
  ledcWrite(LCD_Backlight_PIN, Dutyfactor);  
  Set_Backlight(LCD_Backlight);      //0~100                 
}

void Set_Backlight(uint8_t Light)                     
{
  // [AttitudeBall] 原代码为 `Light > Backlight_MAX || Light < 0`，
  // 但 Light 是 uint8_t，`< 0` 恒为假会产生编译警告，这里去掉。
  if(Light > Backlight_MAX)
    printf("Set Backlight parameters in the range of 0 to 100 \r\n");
  else{
    uint32_t Backlight = Light*10;
    if(Backlight == 1000)
      Backlight = 1024;
    ledcWrite(LCD_Backlight_PIN, Backlight);
  }
}

// ================= [FmoCompanion] 自绘帧缓冲刷屏 & 屏幕方向 =================
// LCD_addWindow() 内部会就地做字节交换（ST77916 走 QSPI，像素按大端发出）。
// 自绘层已经在刷屏缓冲里交换过，所以这里直接送，避免二次交换把缓冲区搞坏。
void LCD_DrawRaw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, const uint16_t *color)
{
  if (!panel_handle) return;
  if (x0 > EXAMPLE_LCD_WIDTH) x0 = EXAMPLE_LCD_WIDTH;
  if (y0 > EXAMPLE_LCD_HEIGHT) y0 = EXAMPLE_LCD_HEIGHT;
  if (x1 > EXAMPLE_LCD_WIDTH) x1 = EXAMPLE_LCD_WIDTH;
  if (y1 > EXAMPLE_LCD_HEIGHT) y1 = EXAMPLE_LCD_HEIGHT;
  if (x1 <= x0 || y1 <= y0) return;
  s_color_pending = true;
  esp_lcd_panel_draw_bitmap(panel_handle, x0, y0, x1, y1, color);
}

// 屏幕方向：靠 MADCTL 的 MV(bit5) / MX(bit6) / MY(bit7) 组合
void LCD_SetRotation(uint8_t rot)
{
  if (!panel_handle) return;
  bool swap = false, mx = false, my = false;
  switch (rot & 3) {
    case 1: swap = true; mx = true; break;   // 90°
    case 2: mx = true; my = true; break;     // 180°
    case 3: swap = true; my = true; break;   // 270°
    default: break;                          // 0°
  }
  esp_lcd_panel_swap_xy(panel_handle, swap);
  esp_lcd_panel_mirror(panel_handle, mx, my);
}