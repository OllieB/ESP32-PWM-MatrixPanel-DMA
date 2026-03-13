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

void hsvToRgb(float h, float s, float v, uint16_t &ret_r, uint16_t &ret_g, uint16_t &ret_b) {
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

    ret_r = (uint16_t)(r * 255);
    ret_g = (uint16_t)(g * 255);
    ret_b = (uint16_t)(b * 255);
}


// Start the App
void setup(void) 
{
    Serial.begin(115200);

    pinMode(MBI_SRCLK, OUTPUT);
    digitalWrite(MBI_SRCLK, HIGH); // Disable display output during setup

    //esp_log_level_set("*", ESP_LOG_VERBOSE); // Set all components to debug level
    Serial.println("Starting....");
    Serial.print("setup() running on core ");
    Serial.println(xPortGetCoreID());
    esp_task_wdt_deinit();

    matrix.initMatrix();
    //matrix.setRotation(1);  // 0=0°, 1=90°, 2=180°, 3=270°

    matrix.update();
    delay(10);
    digitalWrite(MBI_SRCLK, LOW); // Enable display output
}

void loop() {

    static uint16_t r,g,b = 0;
    static float angle = 0.0f;

    unsigned long t0 = micros();
    
    
    // Rainbow pinwheel
    for (int y = 0; y < PANEL_PHY_RES_Y; y++) {
        for (int x = 0; x < PANEL_PHY_RES_X; x++) {
            float dx = x - PANEL_PHY_RES_X / 2;
            float dy = y - PANEL_PHY_RES_Y / 2;
            float distance = sqrt(dx * dx + dy * dy);
            float theta = atan2(dy, dx) + angle;
            float hue = fmod((theta / (2 * PI)) + 1.0f, 1.0f);
            hsvToRgb(hue, 1.0f, 1.0f,r,g,b);
            
            matrix.drawPixel(x, y, r,g,b);
        }
    }
    
    angle += 0.1f;
    
    static unsigned long lastFrameTime = 0;
    unsigned long currentTime = millis();
    
    // Target frame time for 24 FPS (41.67ms per frame)
    const unsigned long targetFrameTime = 42;
    unsigned long deltaTime = currentTime - lastFrameTime;
    
    // Only process frame if enough time has passed
    if (deltaTime < targetFrameTime) {
        Serial.printf("Frame time %lu ms, sleeping for %lu ms\n", deltaTime, targetFrameTime - deltaTime);
        delay(targetFrameTime - deltaTime);
    }

    lastFrameTime = millis();

    static unsigned long lastFPSPrintTime = 0;
    static int frame_count = 0;

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
