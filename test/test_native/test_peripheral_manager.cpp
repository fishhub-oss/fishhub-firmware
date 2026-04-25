#include <unity.h>
#include <ArduinoJson.h>
#include <cstdlib>
#include "peripheral_manager.h"
#include "schedule.h"
#include "../../src/peripheral_manager.cpp"
#include "../../src/schedule.cpp"

// ── mock peripheral ──────────────────────────────────────────────────────────

class MockPeripheral : public Peripheral {
public:
  explicit MockPeripheral(const char* n, uint32_t interval)
    : _name(n), _interval(interval), tickCount(0), lastCmd("") {}

  void        begin() override {}
  uint32_t    intervalMs() const override { return _interval; }
  const char* name() const override { return _name; }

  bool tick(time_t /*now*/) override {
    tickCount++;
    return true;
  }

  void appendSenML(JsonArray& records, time_t /*now*/) override {
    JsonObject r = records.add<JsonObject>();
    r["n"] = _name;
    r["v"] = (float)tickCount;
  }

  void applyCommand(JsonObjectConst cmd) override {
    lastCmd = cmd["action"].as<String>();
  }

  int    tickCount;
  String lastCmd;

private:
  const char* _name;
  uint32_t    _interval;
};

// ── tests ────────────────────────────────────────────────────────────────────

void test_not_ticked_before_interval(void) {
  PeripheralManager mgr;
  MockPeripheral p("temp", 1000);
  mgr.add(&p);
  mgr.beginAll();

  // t=0: first tick always fires (0 - 0 >= 1000 is false, but 0 >= 1000 is false)
  // Actually lastTickedAt starts at 0, nowMs=0: 0-0=0 < 1000 → no tick
  String out = mgr.tickAll(0, 0);
  TEST_ASSERT_EQUAL_STRING("", out.c_str());
  TEST_ASSERT_EQUAL_INT(0, p.tickCount);
}

void test_ticked_after_interval(void) {
  PeripheralManager mgr;
  MockPeripheral p("temp", 1000);
  mgr.add(&p);
  mgr.beginAll();

  String out = mgr.tickAll(0, 1000);
  TEST_ASSERT_FALSE(out.empty());
  TEST_ASSERT_EQUAL_INT(1, p.tickCount);
}

void test_two_peripherals_tick_independently(void) {
  PeripheralManager mgr;
  MockPeripheral fast("fast", 1000);
  MockPeripheral slow("slow", 5000);
  mgr.add(&fast);
  mgr.add(&slow);
  mgr.beginAll();

  // at 1000 ms: fast fires, slow does not
  mgr.tickAll(0, 1000);
  TEST_ASSERT_EQUAL_INT(1, fast.tickCount);
  TEST_ASSERT_EQUAL_INT(0, slow.tickCount);

  // at 3000 ms: fast fires again (2000 ms since last at 1000), slow does not
  mgr.tickAll(0, 3000);
  TEST_ASSERT_EQUAL_INT(2, fast.tickCount);
  TEST_ASSERT_EQUAL_INT(0, slow.tickCount);

  // at 5000 ms: slow fires for first time, fast fires again
  mgr.tickAll(0, 5000);
  TEST_ASSERT_EQUAL_INT(3, fast.tickCount);
  TEST_ASSERT_EQUAL_INT(1, slow.tickCount);
}

void test_tickAll_produces_flat_senml(void) {
  PeripheralManager mgr;
  MockPeripheral p("temperature", 1000);
  mgr.add(&p);
  mgr.beginAll();

  String out = mgr.tickAll(1745000000, 1000);
  TEST_ASSERT_FALSE(out.empty());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, out);
  TEST_ASSERT_EQUAL_INT(DeserializationError::Ok, err.code());

  JsonArray arr = doc.as<JsonArray>();
  // First element must be base record with bn and bt
  TEST_ASSERT_EQUAL_STRING("fishhub/device/", arr[0]["bn"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(1745000000, arr[0]["bt"].as<long>());
  // Second element must be a measurement record (no bn/bt)
  TEST_ASSERT_EQUAL_STRING("temperature", arr[1]["n"].as<const char*>());
  TEST_ASSERT_TRUE(arr[1]["v"].is<float>());
  // No "e" key anywhere
  TEST_ASSERT_TRUE(arr[0]["e"].isNull());
}

void test_dispatch_command_routes_by_name(void) {
  PeripheralManager mgr;
  MockPeripheral a("relay", 1000);
  MockPeripheral b("pump", 1000);
  mgr.add(&a);
  mgr.add(&b);
  mgr.beginAll();

  JsonDocument doc;
  doc["action"] = "on";
  mgr.dispatchCommand("relay", doc.as<JsonObjectConst>());

  TEST_ASSERT_EQUAL_STRING("on", a.lastCmd.c_str());
  TEST_ASSERT_EQUAL_STRING("", b.lastCmd.c_str());
}

// ── Schedule tests ───────────────────────────────────────────────────────────

// All schedule tests run with TZ=UTC so localtime_r matches the UTC timestamps.
// 2024-01-10 10:00:00 UTC
static const time_t T_10_00 = 1704880800;
// 2024-01-10 23:00:00 UTC
static const time_t T_23_00 = 1704924000;
// 2024-01-10 03:00:00 UTC
static const time_t T_03_00 = 1704848400;

static Schedule makeSchedule(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  Schedule s;
  s.loadWindows(doc.as<JsonArrayConst>());
  return s;
}

void test_schedule_no_windows_inactive() {
  Schedule s;
  TEST_ASSERT_FALSE(s.isActive(T_10_00));
}

void test_schedule_normal_window_inside() {
  Schedule s = makeSchedule("[[\"08:00\",\"22:00\"]]");
  TEST_ASSERT_TRUE(s.isActive(T_10_00));
}

void test_schedule_normal_window_outside() {
  Schedule s = makeSchedule("[[\"08:00\",\"22:00\"]]");
  TEST_ASSERT_FALSE(s.isActive(T_23_00));
}

void test_schedule_overnight_after_on() {
  Schedule s = makeSchedule("[[\"22:00\",\"06:00\"]]");
  TEST_ASSERT_TRUE(s.isActive(T_23_00));
}

void test_schedule_overnight_before_off() {
  Schedule s = makeSchedule("[[\"22:00\",\"06:00\"]]");
  TEST_ASSERT_TRUE(s.isActive(T_03_00));
}

void test_schedule_overnight_outside() {
  Schedule s = makeSchedule("[[\"22:00\",\"06:00\"]]");
  TEST_ASSERT_FALSE(s.isActive(T_10_00));
}

void test_schedule_override_true() {
  Schedule s = makeSchedule("[[\"08:00\",\"22:00\"]]");
  s.setOverride(true);
  TEST_ASSERT_TRUE(s.isActive(T_23_00));
}

void test_schedule_override_false() {
  Schedule s = makeSchedule("[[\"08:00\",\"22:00\"]]");
  s.setOverride(false);
  TEST_ASSERT_FALSE(s.isActive(T_10_00));
}

void test_schedule_load_clears_override() {
  Schedule s = makeSchedule("[[\"08:00\",\"22:00\"]]");
  s.setOverride(false);
  JsonDocument empty;
  s.loadWindows(empty.as<JsonArrayConst>());
  TEST_ASSERT_FALSE(s.hasOverride());
}

// ── entry point ──────────────────────────────────────────────────────────────

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  // Schedule tests use timestamps in UTC; force localtime_r to match.
  setenv("TZ", "UTC0", 1);
  tzset();

  UNITY_BEGIN();
  RUN_TEST(test_not_ticked_before_interval);
  RUN_TEST(test_ticked_after_interval);
  RUN_TEST(test_two_peripherals_tick_independently);
  RUN_TEST(test_tickAll_produces_flat_senml);
  RUN_TEST(test_dispatch_command_routes_by_name);
  RUN_TEST(test_schedule_no_windows_inactive);
  RUN_TEST(test_schedule_normal_window_inside);
  RUN_TEST(test_schedule_normal_window_outside);
  RUN_TEST(test_schedule_overnight_after_on);
  RUN_TEST(test_schedule_overnight_before_off);
  RUN_TEST(test_schedule_overnight_outside);
  RUN_TEST(test_schedule_override_true);
  RUN_TEST(test_schedule_override_false);
  RUN_TEST(test_schedule_load_clears_override);
  return UNITY_END();
}
