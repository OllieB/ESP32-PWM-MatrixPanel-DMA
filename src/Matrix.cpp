#include "Matrix.h"

#include <assert.h>
#include <string.h>

static const char *TAG = "Matrix.h";
static ESP32_GREY_DMA_STORAGE_TYPE g_cmd_dma_buf[700];

// CIE - Lookup table for converting between perceived LED brightness and PWM
// https://gist.github.com/mathiasvr/19ce1d7b6caeab230934080ae1f1380e
static const uint16_t CIE[256] = {
    0,    0,    0,    0,    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,    2,    2,
    2,    2,    2,    2,    2,    2,    2,    3,    3,    3,    3,    3,    3,    3,    3,    4,
    4,    4,    4,    4,    4,    5,    5,    5,    5,    5,    6,    6,    6,    6,    6,    7,
    7,    7,    7,    8,    8,    8,    8,    9,    9,    9,   10,   10,   10,   10,   11,   11,
   11,   12,   12,   12,   13,   13,   13,   14,   14,   15,   15,   15,   16,   16,   17,   17,
   17,   18,   18,   19,   19,   20,   20,   21,   21,   22,   22,   23,   23,   24,   24,   25,
   25,   26,   26,   27,   28,   28,   29,   29,   30,   31,   31,   32,   32,   33,   34,   34,
   35,   36,   37,   37,   38,   39,   39,   40,   41,   42,   43,   43,   44,   45,   46,   47,
   47,   48,   49,   50,   51,   52,   53,   54,   54,   55,   56,   57,   58,   59,   60,   61,
   62,   63,   64,   65,   66,   67,   68,   70,   71,   72,   73,   74,   75,   76,   77,   79,
   80,   81,   82,   83,   85,   86,   87,   88,   90,   91,   92,   94,   95,   96,   98,   99,
  100,  102,  103,  105,  106,  108,  109,  110,  112,  113,  115,  116,  118,  120,  121,  123,
  124,  126,  128,  129,  131,  132,  134,  136,  138,  139,  141,  143,  145,  146,  148,  150,
  152,  154,  155,  157,  159,  161,  163,  165,  167,  169,  171,  173,  175,  177,  179,  181,
  183,  185,  187,  189,  191,  193,  196,  198,  200,  202,  204,  207,  209,  211,  214,  216,
  218,  220,  223,  225,  228,  230,  232,  235,  237,  240,  242,  245,  247,  250,  252,  255,
};

Matrix::Matrix() : GFX(PANEL_PHY_RES_X, PANEL_PHY_RES_Y) {
}

Matrix::~Matrix() {
}

void Matrix::initMatrix() {
  // Step 1) Allocate raw buffer space for MBI5153 greyscale / MBI chip command / pixel memory
  dma_grey_buffer_parallel_bit_length = ((PANEL_SCAN_LINES * PANEL_MBI_RES_X * 16));
  dma_grey_buffer_size = sizeof(ESP32_GREY_DMA_STORAGE_TYPE) * dma_grey_buffer_parallel_bit_length;
  ESP_LOGD(TAG, "Allocating greyscale DMA memory buffer. Size of memory required: %lu bytes.", dma_grey_buffer_size);

  // Malloc Greyscale / Command DMA Memory
  dma_grey_gpio_data = (ESP32_GREY_DMA_STORAGE_TYPE *)heap_caps_malloc(dma_grey_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  assert(dma_grey_gpio_data != nullptr);

  // Fill with zeros to start with
  memset(dma_grey_gpio_data, 0, dma_grey_buffer_size);

  // Setup SPI DMA Output for GCLK and Address Lines FIRST
  // (Must initialize before LCD DMA to avoid SPI2 resource conflict)
  spi_setup();

  // Setup LCD DMA and Output to GPIO
  auto bus_cfg = dma_bus.config();
  bus_cfg.pin_wr = MBI_DCLK;  // DCLK Pin
  bus_cfg.invert_pclk = false;
  bus_cfg.pin_d0 = MBI_G1;
  bus_cfg.pin_d1 = MBI_B1;
  bus_cfg.pin_d2 = MBI_R1;
  bus_cfg.pin_d3 = MBI_G2;
  bus_cfg.pin_d4 = MBI_B2;
  bus_cfg.pin_d5 = MBI_R2;
  bus_cfg.pin_d6 = MBI_G3;
  bus_cfg.pin_d7 = MBI_B3;
  bus_cfg.pin_d8 = MBI_R3;
  bus_cfg.pin_d9 = MBI_G4;
  bus_cfg.pin_d10 = MBI_B4;
  bus_cfg.pin_d11 = MBI_R4;
  bus_cfg.pin_d12 = MBI_LAT;  // Latch
  bus_cfg.pin_d13 = -1;       // DCLK potentially if we need to manually generate
  bus_cfg.pin_d14 = -1;
  bus_cfg.pin_d15 = -1;

  dma_bus.config(bus_cfg);
  dma_bus.setup_lcd_dma_periph();

  updateRegisters();

  initialized = true;
  update();
}

void Matrix::refreshMatrixConfig() {
  mbi_pre_active_dma();  // 14 clocks
  mbi_send_config_reg1_dma();

  // MBI Step 2) Some other register 2 hack to reduce ghosting.
  mbi_pre_active_dma();
  mbi_send_config_reg2_dma();
}

void Matrix::update() {
  assert(initialized);

  mbi_update_frame(true);
  spi_transfer_loop_stop();
  mbi_v_sync_dma();
  spi_transfer_loop_start();

  if (!imagePersistenceEnabled) {
    memset(dma_grey_gpio_data, 0, dma_grey_buffer_size);
  }
}

void Matrix::updateRegisters() {
  spi_transfer_loop_start();  // start GCLK + Adress toggling

  // MBI Step 1) Set key registers, such as number of rows
  mbi_soft_reset_dma();  // 10 clocks
  mbi_pre_active_dma();  // 14 clocks
  mbi_send_config_reg1_dma();

  // MBI Step 2) Some other register 2 hack to reduce ghosting.
  mbi_pre_active_dma();
  mbi_send_config_reg2_dma();

  // MBI Step 3) Clean out any crap in the greyscale buffer
  mbi_soft_reset_dma();  // 10 clocks

  memset(dma_grey_gpio_data, 0, dma_grey_buffer_size);
}

void Matrix::setBrightness(uint8_t newBrightness) {
  if (newBrightness > BRIGHTNESS_MAX) {
    newBrightness = BRIGHTNESS_MAX;
  }

  currentLevel = newBrightness;

  // Push the new current setting to the panel immediately if already running.
  if (initialized) {
    mbi_pre_active_dma();
    mbi_send_config_reg1_dma();
  }
}

uint8_t Matrix::getBrightness() const {
  return currentLevel;
}

void Matrix::setImagePersistence(bool enabled) {
  imagePersistenceEnabled = enabled;
}

bool Matrix::getImagePersistence() const {
  return imagePersistenceEnabled;
}

void Matrix::clearFrameBuffer() {
  if (dma_grey_gpio_data != nullptr && dma_grey_buffer_size > 0) {
    memset(dma_grey_gpio_data, 0, dma_grey_buffer_size);
  }
}

uint8_t Matrix::getXResolution() {
  return PANEL_PHY_RES_X;
}

uint8_t Matrix::getYResolution() {
  return PANEL_PHY_RES_Y;
}

void Matrix::drawPixel(int16_t x, int16_t y, CRGB color) {
  mbi_set_pixel(x, y, color.red, color.green, color.blue);
}

void Matrix::drawPixel(uint8_t x, uint8_t y, uint8_t r_data, uint8_t g_data, uint8_t b_data) {
  mbi_set_pixel(x, y, r_data, g_data, b_data);
}

void Matrix::drawPixel(int16_t x, int16_t y, uint16_t color565) {
  uint8_t r = (color565 >> 8) & 0xf8;
  uint8_t g = (color565 >> 3) & 0xfc;
  uint8_t b = (color565 << 3);
  r |= r >> 5;
  g |= g >> 6;
  b |= b >> 5;

  mbi_set_pixel(x, y, r, g, b);
}

void Matrix::writePixel(uint8_t x, uint8_t y, uint8_t r_data, uint8_t g_data, uint8_t b_data) {
  mbi_set_pixel(x, y, r_data, g_data, b_data);
}

void Matrix::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color565) {
  for (int16_t i = 0; i < w; i++) {
    drawPixel(x + i, y, color565);
  }
}

void Matrix::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color565) {
  for (int16_t i = 0; i < h; i++) {
    drawPixel(x, y + i, color565);
  }
}

uint16_t Matrix::color(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void Matrix::mbi_update_frame(bool configure_latches) {
  int counter = 0;
  for (int row = 0; row < PANEL_SCAN_LINES; row++) {
    for (int chan = 0; chan < PANEL_MBI_LED_CHANS; chan++) {
      for (int ic = 0; ic < PANEL_MBI_CHAIN_LEN; ic++) {
        int bit_offset = 16;

        // Check if this is the last IC to determine latch bit.
        int latch = (ic == 4) ? 1 : 0;

        while (bit_offset > 0) {
          bit_offset--;

          // Set BIT_LAT only on the last bit of the last IC.
          if (latch == 1 && bit_offset == 0) {
            dma_grey_gpio_data[counter] |= BIT_LAT;
          }

          counter++;
        }
      }
    }
  }

  // Send the greyscale data buffer via DMA
  dma_bus.send_stuff_once(dma_grey_gpio_data, dma_grey_buffer_size, true);
}

void Matrix::mbi_set_pixel(uint8_t x, uint8_t y, uint8_t _r_data, uint8_t _g_data, uint8_t _b_data) {
  // Apply rotation transformation
  int16_t tx = x;
  int16_t ty = y;
  switch (getRotation()) {
    case 1:
      tx = PANEL_PHY_RES_Y - 1 - y;
      ty = x;
      break;
    case 2:
      tx = PANEL_PHY_RES_X - 1 - x;
      ty = PANEL_PHY_RES_Y - 1 - y;
      break;
    case 3:
      tx = y;
      ty = PANEL_PHY_RES_X - 1 - x;
      break;
    default:
      break;
  }
  x = tx;
  y = ty;

  if (x >= PANEL_PHY_RES_X || y >= PANEL_PHY_RES_Y) {
    return;
  }

  // CIE lookup.
  uint8_t r_data = CIE[_r_data];
  uint8_t g_data = CIE[_g_data];
  uint8_t b_data = CIE[_b_data];

  x += 2;  // offset for missing pixels on the left

  // Calculate bitmasks.
  uint16_t colourbitsoffset = (y / PANEL_SCAN_LINES) * 3;
  uint16_t colourbitsclear = ~(0b111 << colourbitsoffset);

  uint16_t g_gpio_bitmask = BIT_G1 << colourbitsoffset;
  uint16_t b_gpio_bitmask = BIT_B1 << colourbitsoffset;
  uint16_t r_gpio_bitmask = BIT_R1 << colourbitsoffset;

  // Row offset + channel offset + individual IC LED offset
  int y_normalised = y % PANEL_SCAN_LINES;  // Only have 20 rows of data...
  int bit_start_pos = (1280 * y_normalised) + ((x % 16) * 80) + ((x / 16) * 16);

  // RGB colour data provided is only 8bits, so we'll fill it from bit 16 down to bit 8
  int subpixel_colour_bit = 8;
  while (subpixel_colour_bit > 0) {
    subpixel_colour_bit--;
    dma_grey_gpio_data[bit_start_pos] &= colourbitsclear;

    uint8_t mask = 1 << subpixel_colour_bit;

    if (g_data & mask) {
      dma_grey_gpio_data[bit_start_pos] |= g_gpio_bitmask;
    }

    if (b_data & mask) {
      dma_grey_gpio_data[bit_start_pos] |= b_gpio_bitmask;
    }

    if (r_data & mask) {
      dma_grey_gpio_data[bit_start_pos] |= r_gpio_bitmask;
    }

    bit_start_pos++;
  }
}

void Matrix::mbi_pre_active_dma() {
  ESP_LOGD(TAG, "Send MBI Pre-Active.");

  int payload_length = 0;
  for (int i = 0; i < 14; i++) {
    g_cmd_dma_buf[payload_length] = BIT_LAT;
    payload_length++;
  }

  // LE/LAT should be low for any rising edge of DCLK.
  for (int i = 0; i < 2; i++) {
    g_cmd_dma_buf[payload_length] = 0x00;
    payload_length++;
  }

  dma_bus.send_stuff_once(g_cmd_dma_buf, payload_length * sizeof(ESP32_GREY_DMA_STORAGE_TYPE), false);
}

void Matrix::mbi_v_sync_dma() {
  ESP_LOGV(TAG, "Send MBI Vert Sync.");

  // Send the Vsync somewhere in the middle of the gclk data.
  int payload_length = 600;
  memset(g_cmd_dma_buf, 0, payload_length * sizeof(ESP32_GREY_DMA_STORAGE_TYPE));

  int start_pos = payload_length - (payload_length / 2);
  for (int i = 0; i < 3; i++) {
    g_cmd_dma_buf[start_pos++] = BIT_LAT;
  }

  g_cmd_dma_buf[start_pos++] = 0x00;

  dma_bus.send_stuff_once(g_cmd_dma_buf, payload_length * sizeof(ESP32_GREY_DMA_STORAGE_TYPE), false);
}

void Matrix::mbi_soft_reset_dma() {
  // Software reset returns outputs off until next Vsync while preserving config registers.
  ESP_LOGD(TAG, "Send MBI Soft Reset.");

  int payload_length = 0;
  for (int i = 0; i < 10; i++) {
    g_cmd_dma_buf[payload_length] = BIT_LAT;
    payload_length++;
  }

  g_cmd_dma_buf[payload_length] = 0x00;
  payload_length++;

  dma_bus.send_stuff_once(g_cmd_dma_buf, payload_length * sizeof(ESP32_GREY_DMA_STORAGE_TYPE), false);
}

void Matrix::mbi_set_config_dma(ESP32_GREY_DMA_STORAGE_TYPE *out_buf,
                                unsigned int &dma_output_pos,
                                uint16_t config_reg_r,
                                uint16_t config_reg_gb,
                                bool latch,
                                bool reg2) {
  ESP_LOGD(TAG, "Send MBI config");

  // Number of DCLK Rising Edge when LE is asserted.
  // Write Configuration 1 = 4
  // Write Configuration 2 = 8
  int latch_trigger_point = reg2 ? 8 : 4;

  for (int bit = 15; bit >= 0; bit--) {
    int r_bitval = ((config_reg_r >> bit) & 1);
    int gb_bitval = ((config_reg_gb >> bit) & 1);

    uint16_t mbi_rgb_sdi_val = 0;
    if (r_bitval) {
      mbi_rgb_sdi_val |= (BIT_R1 | BIT_R2 | BIT_R3 | BIT_R4);
    }
    if (gb_bitval) {
      mbi_rgb_sdi_val |= (BIT_G1 | BIT_B1 | BIT_G2 | BIT_B2 | BIT_G3 | BIT_B3 | BIT_G4 | BIT_B4);
    }

    if ((bit < latch_trigger_point) && latch) {
      mbi_rgb_sdi_val |= BIT_LAT;
    }

    out_buf[dma_output_pos++] = mbi_rgb_sdi_val;
  }
}

void Matrix::mbi_send_config_reg1_dma() {
  // Step 1) Send configuration for.
  uint16_t config_reg1_val = 0;

  int ghost_elimination = ghost_elimination_ON;
  int line_num = PANEL_SCAN_LINES - 1;
  int gray_scale = gray_scale_14;
  int gclk_multiplier = gclk_multiplier_OFF;
  int current = currentLevel;

  // Documentation says set bits E and F of Config1 Reg to 1.
  config_reg1_val =
      (config_reg1_val | (ghost_elimination << 14) | (line_num << 8) | (gray_scale << 7) | (gclk_multiplier << 6) | (current));

  unsigned int dma_output_pos = 0;
  for (int i = 0; i < PANEL_MBI_CHAIN_LEN; i++) {
    mbi_set_config_dma(g_cmd_dma_buf, dma_output_pos, config_reg1_val, config_reg1_val, (i == (PANEL_MBI_CHAIN_LEN - 1)), false);
  }

  dma_bus.send_stuff_once(g_cmd_dma_buf, dma_output_pos * sizeof(ESP32_GREY_DMA_STORAGE_TYPE), false);
}

void Matrix::mbi_send_config_reg2_dma() {
  // Register 2 uses different recommended values for R-loaded chains vs G/B-loaded chains.
  // Bit 10 (double refresh) is masked off to preserve the existing normal-GCLK behavior.
  uint16_t config_reg2_val_r = 0x4601 & ~(1 << 10);
  uint16_t config_reg2_val_gb = 0x6600 & ~(1 << 10);
  unsigned int dma_output_pos = 0;

  for (int i = 0; i < PANEL_MBI_CHAIN_LEN; i++) {
    mbi_set_config_dma(g_cmd_dma_buf, dma_output_pos, config_reg2_val_r, config_reg2_val_gb, (i == (PANEL_MBI_CHAIN_LEN - 1)), true);
  }

  dma_bus.send_stuff_once(g_cmd_dma_buf, dma_output_pos * sizeof(ESP32_GREY_DMA_STORAGE_TYPE), false);
}
