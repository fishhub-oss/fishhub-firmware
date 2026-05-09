#include "oled_display.h"
#include "pins.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static constexpr int SCREEN_WIDTH  = 128;
static constexpr int SCREEN_HEIGHT = 64;
static constexpr int OLED_RESET    = -1; // shared with MCU reset

// 2 seconds per peripheral slide in MEASUREMENTS mode
static constexpr unsigned long SLIDE_INTERVAL_MS = 2000;

// ~21 chars fit per line at text size 1 (6×8 px font)
static constexpr int LINE_CHARS = 21;

// Visible log lines in DEBUG mode (top line reserved for state)
static constexpr int LOG_LINES = 6;

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

OledDisplay oledDisplay;

// ─── helpers ─────────────────────────────────────────────────────────────────

static String truncate(const String& s, int maxLen)
{
  if ((int)s.length() <= maxLen) return s;
  return s.substring(0, maxLen - 1) + "~";
}

// ─── public API ──────────────────────────────────────────────────────────────

void OledDisplay::begin()
{
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED: SSD1306 not detected — display disabled");
    _available = false;
    return;
  }

  _available = true;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("FishHub booting...");
  display.display();
  Serial.println("OLED: SSD1306 initialised");
}

void OledDisplay::tick(unsigned long nowMs)
{
  if (!_available) return;

  if (_mode == DisplayMode::MEASUREMENTS) {
    if (!_readings.empty() && nowMs - _lastSlideMs >= SLIDE_INTERVAL_MS) {
      _lastSlideMs = nowMs;
      _slideIndex  = (_slideIndex + 1) % (int)_readings.size();
      drawMeasurements();
    } else if (_readings.empty()) {
      drawMeasurements();
    }
  }
  // DEBUG mode is redrawn immediately on each logEvent / setCurrentState call.
}

void OledDisplay::setMode(DisplayMode m)
{
  if (!_available) return;
  _mode = m;
  _slideIndex  = 0;
  _lastSlideMs = 0;
  if (m == DisplayMode::MEASUREMENTS)
    drawMeasurements();
  else
    drawDebug();
}

void OledDisplay::setCurrentState(const char* s)
{
  strncpy(_currentState, s, sizeof(_currentState) - 1);
  _currentState[sizeof(_currentState) - 1] = '\0';
  if (_available && _mode == DisplayMode::DEBUG)
    drawDebug();
}

void OledDisplay::logEvent(const String& line)
{
  // Overwrite the oldest slot when full
  int slot = (_ringHead + _ringCount) % RING_SIZE;
  if (_ringCount < RING_SIZE) {
    _ringCount++;
  } else {
    // Buffer full — advance head to discard oldest
    _ringHead = (_ringHead + 1) % RING_SIZE;
    slot = (_ringHead + _ringCount - 1) % RING_SIZE;
  }
  _ring[slot] = line;

  if (_available && _mode == DisplayMode::DEBUG)
    drawDebug();
}

void OledDisplay::updateReadings(const std::vector<ReadingEntry>& readings)
{
  _readings = readings;
  if (_slideIndex >= (int)_readings.size())
    _slideIndex = 0;
  if (_available && _mode == DisplayMode::MEASUREMENTS)
    drawMeasurements();
}

// ─── rendering ───────────────────────────────────────────────────────────────

void OledDisplay::drawMeasurements()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (_readings.empty()) {
    display.println("MEASUREMENTS");
    display.println("");
    display.println("Waiting for data...");
    display.display();
    return;
  }

  const ReadingEntry& r = _readings[_slideIndex];
  // Header: peripheral name
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(truncate(r.name, LINE_CHARS));

  // Divider
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  // Value — large text in the centre
  display.setTextSize(2);
  display.setCursor(0, 22);
  display.println(truncate(r.value, 10));

  // Slide indicator dots at the bottom
  if (_readings.size() > 1) {
    int dotSpacing = 8;
    int totalWidth = (int)_readings.size() * dotSpacing - 2;
    int startX     = (SCREEN_WIDTH - totalWidth) / 2;
    for (int i = 0; i < (int)_readings.size(); i++) {
      int x = startX + i * dotSpacing;
      if (i == _slideIndex)
        display.fillCircle(x, 60, 2, SSD1306_WHITE);
      else
        display.drawCircle(x, 60, 2, SSD1306_WHITE);
    }
  }

  display.display();
}

void OledDisplay::drawDebug()
{
  display.clearDisplay();
  display.setTextSize(1);

  // Top line: current state (inverted colours for emphasis)
  display.fillRect(0, 0, SCREEN_WIDTH, 9, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(1, 1);
  display.print(truncate(String(_currentState), LINE_CHARS));
  display.setTextColor(SSD1306_WHITE);

  // Log lines: show the most recent LOG_LINES entries, newest at top
  int y = 11;
  for (int i = 0; i < LOG_LINES && i < _ringCount; i++) {
    // Read from newest → oldest
    int idx = (_ringHead + _ringCount - 1 - i + RING_SIZE) % RING_SIZE;
    display.setCursor(0, y);
    display.print(truncate(_ring[idx], LINE_CHARS));
    y += 9;
  }

  display.display();
}
