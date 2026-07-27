/******************************************************************************************
 * @file        main.cpp
 * @author      github.com/mrcodetastic
 * @date        2024
 * @brief       ESP32-S3 implementation for a MBI5135 PWM chip based LED Matrix Panel
 ******************************************************************************************/

#include <Arduino.h>
#include <Matrix.h>
#include <array>
#include <math.h>

Matrix matrix;

// -------------------------------------------------------------------------
// EMF Camp logo (2026 white/gradient variant) - https://www.emfcamp.org/about/branding
// -------------------------------------------------------------------------

static const uint16_t COLOR_BLACK = 0x0000;

static const int16_t LOGO_CX          = 39;  // bar/ring centerline, x
static const int16_t LOGO_BAR_TOP     = 0;
static const int16_t LOGO_BAR_BOTTOM  = PANEL_PHY_RES_Y;
static const int16_t LOGO_BAR_HALFW   = 1;   // bar width = 2*halfw + 1 = 3px
static const int16_t LOGO_RING_CY     = PANEL_PHY_RES_Y / 2 + 1;  // dead centre of the screen, nudged down 1px
static const int16_t LOGO_RING_R_OUT  = 14;
static const int16_t LOGO_RING_R_IN   = 10;

// Small tilde/wave squiggle inside the ring, scaled down from the source
// logo's decorative accent path (a filled S-curve sitting inside the orbit).
static const int16_t TILDE_CX = LOGO_CX;
static const int16_t TILDE_CY = LOGO_RING_CY - 1;
static const int8_t TILDE_OFFSETS[][2] = {
    {-6, 1}, {-5, -0}, {-4, -1}, {-3, -1}, {-2, -1}, {-1, 0},
    {0, 1}, {1, 2}, {2, 2}, {3, 2}, {4, 1}, {5, 0}, {6, -1},
};

struct Sparkle {
    int16_t x, y;
    uint8_t coreR, coreG, coreB;  // colour at the glyph centre
    uint8_t tipR,  tipG,  tipB;   // colour at the glyph's outer points
    float   period;               // twinkle period, seconds
    float   phase;                 // twinkle phase offset, radians
};

// Positions/order mirror the source logo: sparkles trail up and to the right
// away from the ring, cooling from coral -> orange -> yellow -> white.
static Sparkle sparkles[3] = {
    {54, 23, 247,127,2,   245, 81, 94, 2.4f, 0.0f}, // nearest ring: orange -> coral
    {48, 13, 249,226,0,   247,127,  2, 3.1f, 1.4f}, // middle:       yellow -> orange
    {45,  9, 255,255,255, 249,226,  0, 2.7f, 2.6f}, // farthest:     white  -> yellow
};

// Coloured "planetary ring" swoop - a wide, shallow, tilted ellipse around
// the orbit ring, like Saturn's rings seen nearly edge-on. Half of the loop
// (the "back" arc) is drawn *before* the white logo so it visibly disappears
// behind the disk; the other half (the "front" arc) is drawn on top, giving
// a real wrap-around-behind-the-sphere look instead of a flat swoosh.
static const float SWOOP_A         = 20.0f;                    // semi-major axis (px)
static const float SWOOP_B         = 6.0f;                     // semi-minor axis (px)
static const float SWOOP_TILT_RAD  = 18.0f * (float)PI / 180;  // tilt of the ellipse

// The ellipse is sampled at even *angle* steps, but the point moves fastest
// (in px) right at the waist near the ring centre - the thinnest, lowest-
// overlap part of the stamp. Too few samples there leaves visible notches
// between the small plus-shaped stamps, so this needs to be dense.
#define SWOOP_SAMPLES 220
struct SwoopDot {
    int16_t  x, y;
    uint16_t color;
    bool     front;
    uint8_t  thickness;  // 0 = thinnest (near ring centre) .. 2 = widest (at the tips)
};
static SwoopDot swoopDots[SWOOP_SAMPLES];

// Coral -> orange -> yellow -> orange -> coral around the loop, so the two
// tips where the front/back arcs meet (t=0 and t=PI) match colour exactly.
static uint16_t swoopColorForAngle(float t) {
    float tt = fmodf(t, 2.0f * PI);
    if (tt < 0) tt += 2.0f * PI;
    float s = (tt <= PI) ? (tt / PI) : ((2.0f * PI - tt) / PI);  // 0 (yellow tip) .. 1 (coral tip)

    uint8_t r, g, b;
    if (s < 0.5f) {
        float u = s / 0.5f;  // yellow -> orange
        r = 249 + (int8_t)((247 - 249) * u);
        g = 226 + (int8_t)((127 - 226) * u);
        b = 0   + (int8_t)((2   - 0)   * u);
    } else {
        float u = (s - 0.5f) / 0.5f;  // orange -> coral
        r = 247 + (int8_t)((245 - 247) * u);
        g = 127 + (int8_t)((81  - 127) * u);
        b = 2   + (int8_t)((94  - 2)   * u);
    }
    return Matrix::color(r, g, b);
}

static void initRingSwoop() {
    for (int i = 0; i < SWOOP_SAMPLES; i++) {
        float t = (float)i / SWOOP_SAMPLES * 2.0f * PI;
        float lx = SWOOP_A * cosf(t);
        float ly = SWOOP_B * sinf(t);
        float fx = lx * cosf(SWOOP_TILT_RAD) - ly * sinf(SWOOP_TILT_RAD);
        float fy = lx * sinf(SWOOP_TILT_RAD) + ly * cosf(SWOOP_TILT_RAD);

        swoopDots[i].x     = LOGO_CX + (int16_t)roundf(fx);
        swoopDots[i].y     = LOGO_RING_CY + (int16_t)roundf(fy);
        swoopDots[i].color = swoopColorForAngle(t);
        swoopDots[i].front = (fmodf(t, 2.0f * PI) <= PI);  // local-bottom half = in front of the disk

        // |cos t| == 1 at the tips (t=0/PI, distance from centre == SWOOP_A) and
        // == 0 at the closest approach to the ring centre (t=PI/2, distance == SWOOP_B).
        float taper = fabsf(cosf(t));
        swoopDots[i].thickness = (taper > 0.55f) ? 1 : 0;
    }
}

// front=false draws the arc that passes behind the ring (call before drawLogo());
// front=true draws the arc that passes in front (call after drawLogo()).
static void drawRingSwoop(bool front) {
    for (auto &d : swoopDots) {
        if (d.front != front) continue;
        matrix.drawPixel(d.x,     d.y,     d.color);
        matrix.drawPixel(d.x - 1, d.y,     d.color);
        matrix.drawPixel(d.x + 1, d.y,     d.color);
        matrix.drawPixel(d.x,     d.y - 1, d.color);
        matrix.drawPixel(d.x,     d.y + 1, d.color);
        if (d.thickness >= 1) {
            matrix.drawPixel(d.x - 1, d.y - 1, d.color);
            matrix.drawPixel(d.x + 1, d.y - 1, d.color);
            matrix.drawPixel(d.x - 1, d.y + 1, d.color);
            matrix.drawPixel(d.x + 1, d.y + 1, d.color);
        }
    }
}

#define NUM_STARS 20
struct Star {
    int16_t x, y;
    float   period;
    float   phase;
    bool    blue;
    float   nextRespawn;  // time (seconds) at which this star jumps to a new spot
};
static Star stars[NUM_STARS];

static inline uint8_t scale8(uint8_t v, float f) {
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return (uint8_t)(v * f);
}

// Smooth twinkle factor in [lo, hi], unique per element via period/phase.
static float twinkle(float t, float period, float phase, float lo, float hi) {
    float s = 0.5f + 0.5f * sinf((2.0f * PI) * (t / period) + phase);
    // Small fast wobble layered on top so it doesn't look like a plain sine pulse.
    s += 0.12f * sinf(t * 6.0f + phase * 3.0f);
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    return lo + (hi - lo) * s;
}

static bool pointInLogo(int16_t x, int16_t y) {
    if (x >= LOGO_CX - LOGO_BAR_HALFW && x <= LOGO_CX + LOGO_BAR_HALFW &&
        y >= LOGO_BAR_TOP && y <= LOGO_BAR_BOTTOM) {
        return true;
    }
    int16_t dx = x - LOGO_CX;
    int16_t dy = y - LOGO_RING_CY;
    int32_t d2 = (int32_t)dx * dx + (int32_t)dy * dy;
    return d2 <= (int32_t)LOGO_RING_R_OUT * LOGO_RING_R_OUT &&
           d2 >= (int32_t)LOGO_RING_R_IN * LOGO_RING_R_IN;
}

// Pick a new random spot/period for a star, phasing it so its twinkle
// brightness starts (and, one period later, ends) at its dimmest point -
// so the jump to a new location happens while the star is essentially
// invisible instead of teleporting a bright pixel.
static void respawnStar(Star &s, float t) {
    do {
        s.x = random(0, PANEL_PHY_RES_X);
        s.y = random(0, PANEL_PHY_RES_Y);
    } while (pointInLogo(s.x, s.y));
    s.period      = 1.5f + random(0, 250) / 100.0f;  // 1.5 - 4.0s
    s.blue        = random(0, 3) == 0;
    s.phase       = -PI / 2.0f - (2.0f * PI) * (t / s.period);
    s.nextRespawn = t + s.period;
}

static void drawStarfield(float t) {
    for (auto &s : stars) {
        if (t >= s.nextRespawn) {
            respawnStar(s, t);
        }
        float b = twinkle(t, s.period, s.phase, 0.05f, 1.0f);
        uint8_t v = scale8(255, b);
        uint16_t c = s.blue ? Matrix::color(scale8(216, b), scale8(230, b), v)
                             : Matrix::color(v, v, v);
        matrix.drawPixel(s.x, s.y, c);
    }
}

static void drawLogo() {
    uint16_t white = Matrix::color(255, 255, 255);
    matrix.fillRect(LOGO_CX - LOGO_BAR_HALFW, LOGO_BAR_TOP,
                     LOGO_BAR_HALFW * 2 + 1, LOGO_BAR_BOTTOM - LOGO_BAR_TOP, white);
    matrix.fillCircle(LOGO_CX, LOGO_RING_CY, LOGO_RING_R_OUT, white);
    matrix.fillCircle(LOGO_CX, LOGO_RING_CY, LOGO_RING_R_IN, COLOR_BLACK);

    for (auto &o : TILDE_OFFSETS) {
        // Don't draw the top pixel for the last offset, so the squiggle tapers off nicely.
        if (o[0] != 6) {
            matrix.drawPixel(TILDE_CX + o[0], TILDE_CY + o[1], white);
        }
        matrix.drawPixel(TILDE_CX + o[0], TILDE_CY + o[1]+1, white);
        // don't draw the bottom pixlel for the first offset
        if (o[0] != -6) {
            matrix.drawPixel(TILDE_CX + o[0], TILDE_CY + o[1]+2, white);
        }
    }
}

static void drawSparkles(float t) {
    for (auto &s : sparkles) {
        float b = twinkle(t, s.period, s.phase, 0.35f, 1.0f);

        uint16_t core = Matrix::color(scale8(s.coreR, b), scale8(s.coreG, b), scale8(s.coreB, b));
        uint16_t mid  = Matrix::color(scale8((s.coreR + s.tipR) / 2, b * 0.75f),
                                       scale8((s.coreG + s.tipG) / 2, b * 0.75f),
                                       scale8((s.coreB + s.tipB) / 2, b * 0.75f));
        uint16_t tip  = Matrix::color(scale8(s.tipR, b * 0.45f),
                                       scale8(s.tipG, b * 0.45f),
                                       scale8(s.tipB, b * 0.45f));

        matrix.drawPixel(s.x,     s.y,     core);
        matrix.drawPixel(s.x - 1, s.y,     mid);
        matrix.drawPixel(s.x + 1, s.y,     mid);
        matrix.drawPixel(s.x,     s.y - 1, mid);
        matrix.drawPixel(s.x,     s.y + 1, mid);
        matrix.drawPixel(s.x - 2, s.y,     tip);
        matrix.drawPixel(s.x + 2, s.y,     tip);
        matrix.drawPixel(s.x,     s.y - 2, tip);
        matrix.drawPixel(s.x,     s.y + 2, tip);
    }
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
    matrix.setBrightness(BRIGHTNESS_MEDIUM); // Set brightness level (0-63)
    matrix.setRotation(1);  // 0=0°, 1=90°, 2=180°, 3=270°
    matrix.setImagePersistence(true); // Keep previous pixels across frames

    randomSeed(esp_random());
    for (auto &s : stars) {
        respawnStar(s, 0.0f);
    }

    initRingSwoop();

    matrix.update();
    delay(10);
    digitalWrite(MBI_SRCLK, LOW); // Enable display output
}

void loop() {

    unsigned long t0 = micros();

    float t = millis() / 1000.0f;

    matrix.fillScreen(COLOR_BLACK);
    drawStarfield(t);
    drawRingSwoop(false);  // back half of the ring arc - the white disk paints over it next
    drawLogo();             // static white orbit mark, drawn on top of the starfield + back arc
    drawRingSwoop(true);   // front half of the ring arc, drawn on top of the disk
    drawSparkles(t);        // twinkling gradient sparkles, drawn on top of everything else

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
