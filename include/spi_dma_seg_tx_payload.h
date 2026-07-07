/******************************************************************************************
 * @file        spi_dma_seg_tx_payload.h
 * @author      github.com/mrcodetastic
 * @date        2024
 * @brief       ESP32-S3 implementation for a MBI5135 PWM chip based LED Matrix Panel
 ******************************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const int BYTES_PER_REPEAT;
extern const int REPEATS;
extern const int SEQUENCE_SIZE;
extern const int REPEAT_COUNT;
extern const int PADDING_BEFORE_SIZE;
extern const int PADDING_AFTER_SIZE;
extern const int GCLK_TOTAL_SIZE;

extern uint8_t *spi_tx_octal_payload;

void allocate_gclk_dma_memory(void);

#ifdef __cplusplus
}
#endif
