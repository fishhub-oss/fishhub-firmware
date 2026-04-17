#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial.println("FishHub firmware booting...");
}

void loop() {
  Serial.println("alive");
  delay(5000);
}
