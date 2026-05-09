#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SDA_PIN 21
#define SCL_PIN 22

static Adafruit_SSD1306 display(128, 32, &Wire, -1);

static bool tryBegin(uint8_t vccMode, uint8_t addr)
{
  // Re-create the display object state and try
  display.~Adafruit_SSD1306();
  new (&display) Adafruit_SSD1306(128, 64, &Wire, -1);
  return display.begin(vccMode, addr);
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  // Wire.begin(SDA_PIN, SCL_PIN);
  // Wire.setClock(100000); // drop to 100 kHz

  // I2C scan
  Serial.println("=== I2C scan ===");
  // for (uint8_t addr = 1; addr < 127; addr++) {
  //   Wire.beginTransmission(addr);
  //   if (Wire.endTransmission() == 0)
  //     Serial.printf("  device at 0x%02X\n", addr);
  // }

  // Try every combination of VCC mode + address
  const uint8_t vccModes[] = {SSD1306_SWITCHCAPVCC, SSD1306_EXTERNALVCC};
  const char *vccNames[] = {"SWITCHCAPVCC", "EXTERNALVCC"};
  const uint8_t addrs[] = {0x3C, 0x3D};
  bool found = false;

  for (int v = 0; v < 2 && !found; v++)
  {
    for (int a = 0; a < 2 && !found; a++)
    {
      Serial.printf("Trying %s @ 0x%02X ... ", vccNames[v], addrs[a]);
      if (display.begin(vccModes[v], addrs[a]))
      {
        Serial.println("OK");
        found = true;

        // Fill entire framebuffer white and push
        Serial.println("Filling screen...");
        display.fillRect(0, 0, 128, 32, SSD1306_WHITE);
        display.display();
        delay(2000);

        // Text in the 32px height
        Serial.println("Drawing text...");
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("Hello FishHub!");
        display.setCursor(0, 16);
        display.println("OLED test OK");
        display.display();
      }
      else
      {
        Serial.println("failed");
      }
    }
  }

  if (!found)
    Serial.println("No SSD1306 found with any combination.");
}

void loop()
{
  delay(5000);
  Serial.println("(still alive)");
}
