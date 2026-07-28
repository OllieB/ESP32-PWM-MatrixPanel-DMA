## Driving DMG1083, a 78 x 78 pixel MBI5153 based PWM LED MatrixPanel with an ESP32-S3

Uses PWM based chip that takes 16 bits sent in a series for every pixel in the relevant chain.

There are 12 channels, as the panel is broken into 4 x RGB sections for each 1/4 of the panel (20 pixel in height each, for a total of 80px of which only 78 physical pixels exist).

The code compensates for the 2 'ghost' pixels.

More info on the panels here: https://led.limehouselabs.org/docs/tiles/dmg1083/

## Building

This is a pioarduino project. Open the repo root in pioarduino (VS Code extension or CLI) and build/upload the `esp32-s3-n16r8` environment defined in [platformio.ini](platformio.ini).

This fork has been specifically created for a PCB that I have designed and I hope to it publish here.

## Credits

This project is a fork of [mrcodetastic/ESP32-PWM-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-PWM-MatrixPanel-DMA). All credit for the original MBI5153 DMA driver, GCLK/PWM approach, and [GFX_Lite](https://github.com/mrcodetastic/GFX_Lite) library goes to [mrcodetastic](https://github.com/mrcodetastic) — many thanks for the original work this builds on.

The ESP32-S3 LCD-peripheral DMA technique ([lcd_dma_parallel16.hpp](include/lcd_dma_parallel16.hpp) / [lcd_dma_parallel16.cpp](src/lcd_dma_parallel16.cpp)) is adapted from [Adafruit's ESP32-S3 LCD peripheral hacking writeup](https://blog.adafruit.com/2022/06/21/esp32uesday-more-s3-lcd-peripheral-hacking-with-code/) and the [Adafruit_Protomatter](https://github.com/adafruit/Adafruit_Protomatter) project — please support Adafruit if you find it useful.
