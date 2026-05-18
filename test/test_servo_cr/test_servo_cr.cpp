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
  cmd["command"]          = "actuate";
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
  cmd["command"]          = "actuate";
  cmd["rotation_ms"] = 500;
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
  ServoCR servo("feeder0", 17);
  servo.begin();

  JsonDocument cmd;
  cmd["command"] = "reboot";
  servo.applyCommand(cmd.as<JsonObjectConst>());

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);
  TEST_ASSERT_EQUAL(0, arr.size());
}

void test_sense_emitted_when_pending(void) {
  ServoCR servo("feeder0", 17, 18);
  servo.begin();

  JsonDocument cmd;
  cmd["command"]          = "actuate";
  cmd["rotation_ms"] = 300;
  servo.applyCommand(cmd.as<JsonObjectConst>());

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

void test_schedule_cron_entry_fires_when_due(void) {
  ServoCR servo("feeder0", 17);
  servo.begin();

  // Build a schedule command with a cron that matches T0 exactly
  struct tm* t = localtime(&T0);
  char cron[32];
  snprintf(cron, sizeof(cron), "%d %d * * *", t->tm_min, t->tm_hour);

  JsonDocument cmd;
  cmd["command"]   = "schedule";
  cmd["type"] = "cron";
  JsonArray entries = cmd["entries"].to<JsonArray>();
  JsonObject entry  = entries.add<JsonObject>();
  entry["id"]    = "e1";
  entry["cron"]  = cron;
  entry["value"] = 800;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  bool fired = servo.tick(T0);
  TEST_ASSERT_TRUE(fired);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  servo.appendSenML(arr, T0);

  bool found = false;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n) == "feeder0/rotation") {
      TEST_ASSERT_EQUAL_INT(800, obj["v"] | 0);
      found = true;
    }
  }
  TEST_ASSERT_TRUE(found);
}

void test_schedule_cron_entry_does_not_refire_same_minute(void) {
  ServoCR servo("feeder0", 17);
  servo.begin();

  struct tm* t = localtime(&T0);
  char cron[32];
  snprintf(cron, sizeof(cron), "%d %d * * *", t->tm_min, t->tm_hour);

  JsonDocument cmd;
  cmd["command"]   = "schedule";
  cmd["type"] = "cron";
  JsonArray entries = cmd["entries"].to<JsonArray>();
  JsonObject entry  = entries.add<JsonObject>();
  entry["id"]    = "e1";
  entry["cron"]  = cron;
  entry["value"] = 800;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  servo.tick(T0);

  JsonDocument doc1;
  JsonArray arr1 = doc1.to<JsonArray>();
  servo.appendSenML(arr1, T0);

  // tick again within same minute — must not re-fire
  bool firedAgain = servo.tick(T0 + 10);
  TEST_ASSERT_FALSE(firedAgain);
}

void test_schedule_cron_type_missing_defaults_to_windows_noop(void) {
  ServoCR servo("feeder0", 17);
  servo.begin();

  // Missing type — should not load cron entries, no crash
  JsonDocument cmd;
  cmd["command"] = "schedule";
  // no "type" field
  JsonArray entries = cmd["entries"].to<JsonArray>();
  JsonObject entry  = entries.add<JsonObject>();
  entry["id"]    = "e1";
  entry["cron"]  = "0 8 * * *";
  entry["value"] = 500;
  servo.applyCommand(cmd.as<JsonObjectConst>());

  // Nothing should fire — entries not loaded
  bool fired = servo.tick(T0);
  TEST_ASSERT_FALSE(fired);
}

void test_schedule_replace_clears_entries_for_removed_ids(void) {
  ServoCR servo("feeder0", 17);
  servo.begin();

  struct tm* t = localtime(&T0);
  char cron[32];
  snprintf(cron, sizeof(cron), "%d %d * * *", t->tm_min, t->tm_hour);

  // Load e1 — should fire at T0
  JsonDocument cmd1;
  cmd1["command"]   = "schedule";
  cmd1["type"] = "cron";
  JsonArray e1 = cmd1["entries"].to<JsonArray>();
  JsonObject en1 = e1.add<JsonObject>();
  en1["id"] = "e1"; en1["cron"] = cron; en1["value"] = 500;
  servo.applyCommand(cmd1.as<JsonObjectConst>());
  TEST_ASSERT_TRUE(servo.tick(T0));

  JsonDocument tmp; JsonArray a = tmp.to<JsonArray>(); servo.appendSenML(a, T0);

  // Replace with e2 only — e1 is gone, e2 has a non-matching cron so nothing fires
  JsonDocument cmd2;
  cmd2["command"]   = "schedule";
  cmd2["type"] = "cron";
  JsonArray e2 = cmd2["entries"].to<JsonArray>();
  JsonObject en2 = e2.add<JsonObject>();
  en2["id"] = "e2"; en2["cron"] = "59 23 31 12 *"; en2["value"] = 500;
  servo.applyCommand(cmd2.as<JsonObjectConst>());

  bool fired = servo.tick(T0 + 5);
  TEST_ASSERT_FALSE(fired);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_actuate_sets_pending_rotation);
  RUN_TEST(test_append_senml_clears_pending);
  RUN_TEST(test_unknown_op_ignored);
  RUN_TEST(test_sense_emitted_when_pending);
  RUN_TEST(test_schedule_cron_entry_fires_when_due);
  RUN_TEST(test_schedule_cron_entry_does_not_refire_same_minute);
  RUN_TEST(test_schedule_cron_type_missing_defaults_to_windows_noop);
  RUN_TEST(test_schedule_replace_clears_entries_for_removed_ids);
  return UNITY_END();
}
