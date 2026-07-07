/******************************************************************************************
 * @file        main.cpp
 * @author      github.com/mrcodetastic
 * @date        2024
 * @brief       ESP32-S3 implementation for a MBI5135 PWM chip based LED Matrix Panel
 ******************************************************************************************/

#include <Arduino.h>
#include <Matrix.h>
#include <array>

Matrix matrix;

// Convert HSV (h 0-1, s 0-1, v 0-1) to RGB565
static uint16_t hsv(float h, float s, float v) {
    float r, g, b;
    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        case 5: r = v, g = p, b = q; break;
    }

    return Matrix::color((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

// Start the App
void setup(void)
{
    Serial.begin(115200);

    pinMode(MBI_SRCLK, OUTPUT);
    digitalWrite(MBI_SRCLK, HIGH); // Disable display output during setup

    Serial.println("Starting....");
    Serial.print("setup() running on core ");
    Serial.println(xPortGetCoreID());
    esp_task_wdt_deinit();

    matrix.initMatrix();
    matrix.setBrightness(BRIGHTNESS_LOW); // Set brightness level (0-63)
    matrix.setRotation(1);  // 0=0°, 1=90°, 2=180°, 3=270°
    matrix.setImagePersistence(true); // Keep previous pixels across frames

    matrix.update();
    delay(10);
    digitalWrite(MBI_SRCLK, LOW); // Enable display output
}

void loop() {

    unsigned long t0 = micros();

    // Horizontal scrolling rainbow demo
    static float hueOffset = 0.0f;

    for (int x = 0; x < PANEL_PHY_RES_X; x++) {
        float hue = fmodf(hueOffset + (float)x / PANEL_PHY_RES_X, 1.0f);
        uint16_t c = hsv(hue, 1.0f, 1.0f);
        matrix.drawFastVLine(x, 0, PANEL_PHY_RES_Y, c);
    }

    hueOffset += 0.01f;
    if (hueOffset >= 1.0f) hueOffset -= 1.0f;

    static unsigned long lastFPSPrintTime = 0;
    static int frame_count = 0;
    unsigned long currentTime = millis();

    unsigned long t1 = micros();
    matrix.update();
    unsigned long t2 = micros();

    if ((currentTime - lastFPSPrintTime) > 1000) {
        Serial.print("FPS: ");
        Serial.println(frame_count, DEC);
        Serial.printf("draw=%lu µs, update=%lu µs\n", t1-t0, t2-t1);

        frame_count = 0;
        lastFPSPrintTime = currentTime;
    }

    frame_count++;
}
