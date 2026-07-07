#ifndef MATRIX_H
#define MATRIX_H

#include <stdint.h>
#include <stddef.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_task_wdt.h>

#include "Arduino.h"
#include "app_constants.hpp"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_dma_parallel16.hpp"
#include "sdkconfig.h"
#include "spi_dma_seg_tx_loop.h"

#include <array>
#include <GFX_Lite.h>

class Matrix : public GFX {
 public:
  Matrix();
  ~Matrix();

  void initMatrix();
  void refreshMatrixConfig();
  void update();
  void updateRegisters();

  // Sets the MBI5153 LED drive current (config register 1, bits [5:0]).
  // Accepts a raw 0-63 value, or one of the named BRIGHTNESS_* constants in app_constants.hpp.
  void setBrightness(uint8_t newBrightness);
  uint8_t getBrightness() const;

  uint8_t getXResolution();
  uint8_t getYResolution();

  // Always implement basic AdaFruit_GFX virtual functions.
  void drawPixel(int16_t x, int16_t y, CRGB color);
  void drawPixel(uint8_t x, uint8_t y, uint8_t r_data, uint8_t g_data, uint8_t b_data);
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void writePixel(uint8_t x, uint8_t y, uint8_t r_data, uint8_t g_data, uint8_t b_data);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);

  // Pack 8-bit RGB into the 16-bit 565 format used by the uint16_t drawPixel()/fill*() overloads.
  static uint16_t color(uint8_t r, uint8_t g, uint8_t b);

 protected:
  bool initialized = false;
  uint8_t currentLevel = BRIGHTNESS_MIN;  // LED drive current, see setBrightness()

  // D<A Data to send.
  Bus_Parallel16 dma_bus;

  ESP32_GREY_DMA_STORAGE_TYPE *dma_grey_gpio_data = nullptr;

  // Length in bits of the buffer -> sequence of 13 x 16 bits (2 bytes) sent in parallel = length value of 13.
  size_t dma_grey_buffer_parallel_bit_length = 0;
  // Length of buffer in memory used -> sequence of 13 x 16 bits (2 bytes) sent in parallel = value of 26 bytes.
  size_t dma_grey_buffer_size = 0;

  void mbi_update_frame(bool configure_latches);
  void mbi_set_pixel(uint8_t x, uint8_t y, uint8_t _r_data, uint8_t _g_data, uint8_t _b_data);

  void mbi_pre_active_dma();
  void mbi_v_sync_dma();
  void mbi_soft_reset_dma();
  void mbi_set_config_dma(unsigned int &dma_output_pos, uint16_t config_reg_r, uint16_t config_reg_gb, bool latch, bool reg2);
  void mbi_send_config_reg1_dma();
  void mbi_send_config_reg2_dma();
};

#endif  // MATRIX_H
