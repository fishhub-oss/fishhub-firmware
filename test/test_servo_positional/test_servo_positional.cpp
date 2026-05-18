#include <unity.h>
#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>

#include "../../src/cron_trigger.cpp"
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
  ServoPositional servo("flap0", 17, 120, 0, 400);
  servo.begin();

  JsonDocument cmd;
  cmd["op"]         = "actuate";
  cmd["open_angle"] = 45;
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

void test_schedule_cron_entry_fires_when_due(void) {
  ServoPositional servo("flap0", 17, 120, 0, 400);
  servo.begin();

  struct tm* t = localtime(&T0);
  char cron[32];
  snprintf(cron, sizeof(cron), "%d %d * * *", t->tm_min, t->tm_hour);

  JsonDocument cmd;
  cmd["op"]   = "schedule";
  cmd["type"] = "cron";
  JsonArray entries = cmd["entries"].to<JsonArray>();
  JsonObject entry  = entries.add<JsonObject>();
  entry["id"]    = "e1";
  entry["cron"]  = cron;
  entry["value"] = 90;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  bool fired = servo.tick(T0);
  TEST_ASSERT_TRUE(fired);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);

  bool found = false;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n) == "flap0/angle") {
      TEST_ASSERT_EQUAL_INT(90, obj["v"] | 0);
      found = true;
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_schedule_cron_entry_does_not_refire_same_minute(void) {
  ServoPositional servo("flap0", 17, 120, 0, 400);
  servo.begin();

  struct tm* t = localtime(&T0);
  char cron[32];
  snprintf(cron, sizeof(cron), "%d %d * * *", t->tm_min, t->tm_hour);

  JsonDocument cmd;
  cmd["op"]   = "schedule";
  cmd["type"] = "cron";
  JsonArray entries = cmd["entries"].to<JsonArray>();
  JsonObject entry  = entries.add<JsonObject>();
  entry["id"]    = "e1";
  entry["cron"]  = cron;
  entry["value"] = 90;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  servo.tick(T0);
  JsonDocument tmp; JsonArray a = tmp.to<JsonArray>();
  servo.appendSenML(a, T0);

  bool firedAgain = servo.tick(T0 + 10);
  TEST_ASSERT_FALSE(firedAgain);
}

void test_schedule_cron_type_missing_defaults_to_windows_noop(void) {
  ServoPositional servo("flap0", 17, 120, 0, 400);
  servo.begin();

  JsonDocument cmd;
  cmd["op"] = "schedule";
  JsonArray entries = cmd["entries"].to<JsonArray>();
  JsonObject entry  = entries.add<JsonObject>();
  entry["id"]    = "e1";
  entry["cron"]  = "0 8 * * *";
  entry["value"] = 90;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  bool fired = servo.tick(T0);
  TEST_ASSERT_FALSE(fired);
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
  RUN_TEST(test_schedule_cron_entry_fires_when_due);
  RUN_TEST(test_schedule_cron_entry_does_not_refire_same_minute);
  RUN_TEST(test_schedule_cron_type_missing_defaults_to_windows_noop);
  return UNITY_END();
}
