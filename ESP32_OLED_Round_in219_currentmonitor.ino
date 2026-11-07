#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "INA219.h"
#include <math.h>

// === ESP32 Dev Kit pins ===
#define TFT_DC DF_GFX_DC
#define TFT_CS DF_GFX_CS
#define TFT_RST DF_GFX_RST
#define TFT_BL DF_GFX_BL

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC, TFT_CS, 18, 23, -1, VSPI
);

Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, false);

// === INA219 Sensor ===
INA219 INA(0x40);

// === Gauge Parameters ===
const int centerX = 122;
const int centerY = 128-5;

// Current gauge
const int currRadius = 90;
const int currNeedleLength = 80;
float currentI = 0;
float displayedI = 0;
const float Imax = 2500.0; // max current in mA (full-scale)

// Colored zones
void drawColoredZones(int radius){
    int outer = radius + 10; // extend 10 pixels outward
    for(int i=0; i<360; i++){
        uint16_t color;
        if(i < 120) color = 0x07FF;       // Cyan
        else if(i < 240) color = 0x001F;  // Magenta
        else color = 0xF81F;              // Dark Blue

        for(int r = radius-5; r<=outer; r++){
            int x = centerX + r*cos(radians(i));
            int y = centerY + r*sin(radians(i));
            gfx->drawPixel(x, y, color);
        }
    }
}

// Minor zones and major ticks with labels
void drawTickZones(int radius){
    int startAngle = 270; // rotate everything by 270°

    // Minor zones every 6°
    for(int i=0; i<360; i+=6){
        if(i % 30 != 0){
            for(int r = radius-8; r <= radius-3; r++){
                float angle = radians(i + startAngle);
                int x1 = centerX + r*cos(angle);
                int y1 = centerY + r*sin(angle);
                int x2 = centerX + r*cos(angle + radians(1));
                int y2 = centerY + r*sin(angle + radians(1));
                gfx->drawLine(x1, y1, x2, y2, RGB565_WHITE);
            }
        }
    }

    // Major ticks every 30° with labels
    int numMajor = 12;
    for(int t=0; t<numMajor; t++){
        float angleDeg = t * 30 + startAngle;
        int len = 20;
        int x1 = centerX + (radius - len) * cos(radians(angleDeg));
        int y1 = centerY + (radius - len) * sin(radians(angleDeg));
        int x2 = centerX + radius * cos(radians(angleDeg));
        int y2 = centerY + radius * sin(radians(angleDeg));
        gfx->drawLine(x1, y1, x2, y2, RGB565_WHITE);

        // Label value
        int tickCurrent = round(Imax * t / (numMajor-1)); // 0–2500 mA
        char buf[6];
        sprintf(buf, "%d", tickCurrent);

        int labelRadius = radius + 20;
        int lx = centerX + labelRadius * cos(radians(angleDeg)) - (strlen(buf)*3);
        int ly = centerY + labelRadius * sin(radians(angleDeg)) - 4;

        gfx->setCursor(lx, ly);
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565_WHITE);
        gfx->print(buf);
    }
}


// Triangular needle
void drawTriangleNeedle(int cx, int cy, float angleDeg, int length, int baseWidth, uint16_t color) {
    float angleRad = radians(angleDeg);
    float tipX = cx + length * cos(angleRad);
    float tipY = cy + length * sin(angleRad);

    float perpAngle = angleRad + PI/2;
    float halfBase = baseWidth / 2.0;
    float baseX1 = cx + halfBase * cos(perpAngle);
    float baseY1 = cy + halfBase * sin(perpAngle);
    float baseX2 = cx - halfBase * cos(perpAngle);
    float baseY2 = cy - halfBase * sin(perpAngle);

    for(int glow=0; glow<3; glow++){
        gfx->fillTriangle((int)tipX, (int)tipY, (int)baseX1, (int)baseY1, (int)baseX2, (int)baseY2, color);
    }
}

// Erase needle
void eraseTriangleNeedle(int cx, int cy, float angleDeg, int length, int baseWidth) {
    drawTriangleNeedle(cx, cy, angleDeg, length, baseWidth, RGB565_BLACK);
}

void setup() {
    Serial.begin(115200);

#ifdef GFX_BL
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
#endif

    if (!gfx->begin()) {
        Serial.println("gfx->begin() failed!");
        while(1);
    }

    // Rotate display 180° for proper orientation
    gfx->setRotation(2);
    gfx->fillScreen(RGB565_BLACK);

    drawColoredZones(currRadius);
    drawTickZones(currRadius);

    // INA219 setup
    Wire.begin();
    if (!INA.begin()) {
        Serial.println("INA219 not detected!");
        while(1);
    }
    INA.setMaxCurrentShunt(5, 0.002);
    delay(1000);
}

void loop() {
    // Read current and round
    currentI = round(INA.getCurrent_mA());

    // Clamp current within 0–Imax
    if(currentI < 0) currentI = 0;
    if(currentI > Imax) currentI = Imax;

    // Smooth animation for needle movement
    displayedI += (currentI - displayedI) * 0.1;

    // === Needle ===
    static float prevIangle = -1000;
    if(prevIangle > -999){
        eraseTriangleNeedle(centerX, centerY, prevIangle, currNeedleLength, 20);
    }

    // Needle starts at 270° for 0 mA, full 360° sweep clockwise
    float startAngle = 270;
    float Iangle = startAngle + (displayedI / Imax) * 360.0;
    if(Iangle >= 360) Iangle -= 360;

    drawTriangleNeedle(centerX, centerY, Iangle, currNeedleLength, 20, RGB565_CYAN);
    prevIangle = Iangle;

    // Centered numeric value 20px below center
    int displayVal = (int)currentI;
    int digits = 1;
    int tmp = displayVal;
    while(tmp >= 10) { tmp /= 10; digits++; }

    int textSizeVal = 4;
    int charWidth = 6 * textSizeVal;
    int textWidth = digits * charWidth;
    int cursorX = centerX - textWidth / 2;

    // Erase previous numeric value area
    gfx->fillRect(centerX-60, centerY+20, 120, 30, RGB565_BLACK);
    gfx->setCursor(cursorX, centerY+20);
    gfx->setTextColor(RGB565_CYAN);
    gfx->setTextSize(textSizeVal);
    gfx->print(displayVal);

    // "mA" label 50px below center
    gfx->fillRect(centerX-25, centerY+50, 50, 20, RGB565_BLACK);
    gfx->setCursor(centerX-15, centerY+50);
    gfx->setTextSize(2);
    gfx->print("mA");

    delay(50);
}
