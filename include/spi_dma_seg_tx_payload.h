/******************************************************************************************
 * @file        spi_dma_seg_tx_payload.c
 * @author      github.com/mrcodetastic
 * @date        2024
 * @brief       ESP32-S3 implementation for a MBI5135 PWM chip based LED Matrix Panel
 ******************************************************************************************/
#pragma once

/******** GCLK Calculation Bit Pulse Length and Data - ChatGPT generated **********
 >> LLM Prompt

Task Description: The goal is to create a function that is given an index value, calculates and returns a byte value based on the following:
	* Each row-sequence consists of 513 repeats of a three byte sequence.
	* In each sequence, the first two bytes are the same and the third byte has its Most Significant Bit (MSB) set to 1. This byte is called the GCLK byte.
	* Repeat the row-sequence 20 times, embedding the current row-sequence value (starting from 0 to 19) in the two bits to the left of the least significant bit (LSB) of each byte in the row-sequence.
	* Add 8 bytes of padding before and after each row-sequence, that only contains the row-sequence value.
	* Instead of pre-allocating memory for the overall sequence, individual byte values for a given position should be calculated on the fly.

For any extra code that is generated to print the output of this propsoed code should print the value of each byte in binary, and also print the overall sequence size.	

*/

const int BYTES_PER_REPEAT  = 3; // gclk pulse on 3rd byte, gives us enough time to send frame data then.
const int REPEATS           = 513; // 513 gclks per the documentation
const int SEQUENCE_SIZE     = BYTES_PER_REPEAT * REPEATS;
const int REPEAT_COUNT      = 20; // 20 rows

// Padding bytes form the dead time between rows, during which the row address switches.
// The MBI5153's built-in lower-ghost-elimination circuit runs in the window between the falling
// edge of the 513th GCLK and the scan/address switch (app note V1.02 Section 9, Figure 14) - so
// inter-row padding must hold GCLK LOW for that window to exist at all (see getByteValue()).
// PADDING_AFTER_SIZE sets the ghost-elimination running time (8 bytes x 200ns = 1.6us);
// PADDING_BEFORE_SIZE is address settle time before the next row's pulses. The dead time
// (513th GCLK rising edge to next row's first rising edge) must also exceed 1500ns (datasheet).
const int PADDING_BEFORE_SIZE = 8;
const int PADDING_AFTER_SIZE  = 8;
const int GCLK_TOTAL_SIZE   = (PADDING_BEFORE_SIZE + SEQUENCE_SIZE + PADDING_AFTER_SIZE) * REPEAT_COUNT;

// GCLK_TOTAL_SIZE = 31100 which is just under the SPI max transfer size of 32kB per SEGMENT

DMA_ATTR static uint8_t *spi_tx_octal_payload   = NULL; // data for gclk

// Generate MBI5153 GCLK DATA for Byte 0 of parallel output.
// Function to calculate the value of a byte at a specific position
uint8_t getByteValue(int position) {
    int blockSize = PADDING_BEFORE_SIZE + SEQUENCE_SIZE + PADDING_AFTER_SIZE;
    int sequenceIndex = position / blockSize;
    int offset = position % blockSize;

    // Calculate position within the sequence
    int sequencePosition = offset - PADDING_BEFORE_SIZE;
    int byteIndex = sequencePosition % BYTES_PER_REPEAT;

    uint8_t repeatValue = sequenceIndex & 0x1F;  // Repeat value embedded in the LSBs

    // Handle padding
    if (offset < PADDING_BEFORE_SIZE || offset >= PADDING_BEFORE_SIZE + SEQUENCE_SIZE) {

		// The last row's trailing padding holds GCLK HIGH. This is what the old "0x80 in all
		// padding" hack was really fixing: the SPI CONF gap between looped DMA segments sits
		// right after these bytes, and the line holds its last driven level through that gap -
		// leaving it high then falling into row 0's LOW padding produces a falling edge only,
		// never a spurious rising edge, so the chip's 513-rising-edge count per row stays in
		// sync with the address lines (a spurious edge per loop made the image slide/roll).
		if (sequenceIndex == REPEAT_COUNT - 1 && offset >= PADDING_BEFORE_SIZE + SEQUENCE_SIZE) {
			return 0x80 | (repeatValue << 2);
		}

		// All inter-row paddings hold GCLK LOW. The 513th GCLK's falling edge lands at the start
		// of this padding, giving the MBI5153's lower-ghost-elimination circuit its running window
		// (513th falling edge -> address switch, app note Fig 14) on every row transition. With
		// GCLK held high here (as previously), that window never existed, so the ghost elimination
		// enabled in the config registers never actually ran - hence load-dependent red ghosting
		// that no register setting could touch.
		return (repeatValue << 2);
    }

    // Calculate the byte value based on its position in the repeat
    if (byteIndex == 2) {
        return 0x80 | (repeatValue << 2);  // Third byte with MSB set
    } else {
        return (repeatValue << 2);  // First and second bytes
    }
}

void allocate_gclk_dma_memory()
{
	// We don't want to do this twice
	assert(spi_tx_octal_payload == NULL);

	// Allocate
          size_t alloc_size_bytes  = (GCLK_TOTAL_SIZE) * sizeof(uint8_t);
          size_t actual_size = 0;

#ifdef USE_PSRAM
          ESP_LOGI("spi_dma_seg_tx_payload", "Allocating PSRAM DMA memory for global_buffer_gclk_cdata.");  
          esp_err_t err = esp_dma_malloc(alloc_size_bytes, ESP_DMA_MALLOC_FLAG_PSRAM, (void **) &global_buffer_gclk_cdata, &actual_size);
          assert(err == ESP_OK);
#else
          ESP_LOGI("spi_dma_seg_tx_payload", "Allocating SRAM DMA memory for global_buffer_gclk_cdata.");  
          spi_tx_octal_payload = (uint8_t *)(heap_caps_malloc(alloc_size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
          assert(spi_tx_octal_payload != NULL);

          actual_size = alloc_size_bytes;
#endif

	ESP_LOGI("spi_dma_seg_tx_payload", "GCLK data total size: %d bytes.", GCLK_TOTAL_SIZE);  

	// Populate based on algo
	for (int i = 0; i < alloc_size_bytes; i++) {
		spi_tx_octal_payload[i] = getByteValue(i);
	}
} // end allocate_gclk_dma_memory
