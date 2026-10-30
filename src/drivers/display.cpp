#include "display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "perf/profiler.h"
#include "pins.h"

namespace {
constexpr int WIDTH = 128;
constexpr int HEIGHT = 64;
Adafruit_SSD1306 oled(WIDTH, HEIGHT, &Wire, -1);
bool isReady = false;
}  // namespace

namespace display {

bool begin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL, 400000);  // 400 kHz: ~25 ms por quadro.
  isReady = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
  if (isReady) {
    oled.setTextColor(SSD1306_WHITE);
    oled.clearDisplay();
    oled.display();
  }
  return isReady;
}

bool ready() { return isReady; }

void render(const DisplayModel& m) {
  if (!isReady) return;
  PROFILE_SCOPE(ProfId::OledRender);
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(m.title);
  oled.drawFastHLine(0, 11, WIDTH, SSD1306_WHITE);
  oled.setCursor(0, 18);
  oled.print(m.line1);
  oled.setCursor(0, 32);
  oled.print(m.line2);
  oled.drawFastHLine(0, 52, WIDTH, SSD1306_WHITE);
  oled.setCursor(0, 56);
  oled.print(m.footer);
  oled.display();
}

}  // namespace display
