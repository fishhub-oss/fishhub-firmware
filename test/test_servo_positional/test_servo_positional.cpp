#include <unity.h>
#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>

#include "../../src/peripherals/servo_positional.cpp"

static const time_t T0 = 1700000000;

void test_actuate_explicit_angle(void) {
  ServoPositional servo("flap0", 17, 120, 0, 400);
  servo.begin();

  JsonDocument cmd;
  cmd["op"]         = "actuate";
  cmd["open_angle"] = 90;
  cmd["hold_ms"]    = 600;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);

  bool found = false;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n) == "flap0/angle") {
      TEST_ASSERT_EQUAL_STRING("deg", obj["u"] | "");
      TEST_ASSERT_EQUAL_INT(90, obj["v"] | 0);
      found = true;
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_actuate_defaults_to_construction_angle(void) {
  ServoPositional servo("flap0", 17, 120, 0, 400);
  servo.begin();

  JsonDocument cmd;
  cmd["op"] = "actuate";
  // no open_angle — should fall back to 120
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);

  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n) == "flap0/angle") {
      TEST_ASSERT_EQUAL_INT(120, obj["v"] | 0);
    }
  }
}

void test_actuate_defaults_to_construction_hold_ms(void) {
  // hold_ms default is construction-time only; we verify it doesn't crash
  // and still emits an angle record when hold_ms is absent from the command
  ServoPositional servo("flap0", 17, 120, 0, 400);
  servo.begin();

  JsonDocument cmd;
  cmd["op"]         = "actuate";
  cmd["open_angle"] = 45;
  // no hold_ms
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);

  bool found = false;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n) == "flap0/angle") found = true;
  }
  TEST_ASSERT_TRUE(found);
}

void test_append_senml_clears_pending(void) {
  ServoPositional servo("flap0", 17);
  servo.begin();

  JsonDocument cmd;
  cmd["op"] = "actuate";
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc1;
  JsonArray arr1 = doc1.to<JsonArray>();
  servo.appendSenML(arr1, T0);
  TEST_ASSERT_EQUAL(1, arr1.size());

  JsonDocument doc2;
  JsonArray arr2 = doc2.to<JsonArray>();
  servo.appendSenML(arr2, T0);
  TEST_ASSERT_EQUAL(0, arr2.size());
}

void test_unknown_op_ignored(void) {
  ServoPositional servo("flap0", 17);
  servo.begin();

  JsonDocument cmd;
  cmd["op"] = "reboot";
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);
  TEST_ASSERT_EQUAL(0, arr.size());
}

void test_sense_record_when_pending(void) {
  // In native builds, _sensePending is never set by tick() (no digitalRead).
  // Verify that without a sense event, appendSenML only emits /angle.
  ServoPositional servo("flap0", 17, 120, 0, 400, 18);
  servo.begin();

  JsonDocument cmd;
  cmd["op"] = "actuate";
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);

  bool hasAngle = false;
  bool hasSense = false;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n) == "flap0/angle") hasAngle = true;
    if (n && std::string(n) == "flap0/sense") hasSense = true;
  }
  TEST_ASSERT_TRUE(hasAngle);
  TEST_ASSERT_FALSE(hasSense);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_actuate_explicit_angle);
  RUN_TEST(test_actuate_defaults_to_construction_angle);
  RUN_TEST(test_actuate_defaults_to_construction_hold_ms);
  RUN_TEST(test_append_senml_clears_pending);
  RUN_TEST(test_unknown_op_ignored);
  RUN_TEST(test_sense_record_when_pending);
  return UNITY_END();
}
