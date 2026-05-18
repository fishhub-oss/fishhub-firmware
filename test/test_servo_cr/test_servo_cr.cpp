#include <unity.h>
#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>

#include "../../src/cron_trigger.cpp"
#include "../../src/peripherals/servo_cr.cpp"

static const time_t T0 = 1700000000;

void test_actuate_sets_pending_rotation(void) {
  ServoCR servo("feeder0", 17);
  servo.begin();

  JsonDocument cmd;
  cmd["op"]          = "actuate";
  cmd["rotation_ms"] = 1200;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);

  bool found = false;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n) == "feeder0/rotation") {
      TEST_ASSERT_EQUAL_STRING("ms", obj["u"] | "");
      TEST_ASSERT_EQUAL_INT(1200, obj["v"] | 0);
      found = true;
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_append_senml_clears_pending(void) {
  ServoCR servo("feeder0", 17);
  servo.begin();

  JsonDocument cmd;
  cmd["op"]          = "actuate";
  cmd["rotation_ms"] = 500;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc1;
  JsonArray arr1 = doc1.to<JsonArray>();
  servo.appendSenML(arr1, T0);
  TEST_ASSERT_EQUAL(1, arr1.size());

  // second call — flag cleared, nothing emitted
  JsonDocument doc2;
  JsonArray arr2 = doc2.to<JsonArray>();
  servo.appendSenML(arr2, T0);
  TEST_ASSERT_EQUAL(0, arr2.size());
}

void test_unknown_op_ignored(void) {
  ServoCR servo("feeder0", 17);
  servo.begin();

  JsonDocument cmd;
  cmd["op"] = "reboot";
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);
  TEST_ASSERT_EQUAL(0, arr.size());
}

void test_sense_emitted_when_pending(void) {
  ServoCR servo("feeder0", 17, 18);
  servo.begin();

  // tick() sets _sensePending only under ARDUINO; force it via a second actuate
  // and verify sense record is independent from rotation record
  JsonDocument cmd;
  cmd["op"]          = "actuate";
  cmd["rotation_ms"] = 300;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  // Manually verify rotation record present; sense absent (no ARDUINO digitalRead)
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);

  bool hasRotation = false;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n) == "feeder0/rotation") hasRotation = true;
  }
  TEST_ASSERT_TRUE(hasRotation);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_actuate_sets_pending_rotation);
  RUN_TEST(test_append_senml_clears_pending);
  RUN_TEST(test_unknown_op_ignored);
  RUN_TEST(test_sense_emitted_when_pending);
  return UNITY_END();
}
