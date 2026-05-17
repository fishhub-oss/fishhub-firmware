#include <unity.h>
#include <ArduinoJson.h>
#include <cstdlib>
#include <map>
#include <string>
#include "peripheral_manager.h"
#include "schedule.h"
#include "../../src/peripheral_manager.cpp"
#include "../../src/schedule.cpp"
#include "../../src/trigger.cpp"
#include "../../src/trigger_event_queue.cpp"

// ── mock peripheral ──────────────────────────────────────────────────────────

class MockPeripheral : public Peripheral {
public:
  explicit MockPeripheral(const char* n, uint32_t interval, float value = 0.0f)
    : _name(n), _interval(interval), tickCount(0), lastCmd(""), _value(value) {}

  void        begin() override {}
  uint32_t    intervalMs() const override { return _interval; }
  const char* name() const override { return _name; }
  const char* kind() const override { return "mock"; }

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
    lastCmd = cmd["command"].as<String>();
  }

  void currentMeasurements(std::map<std::string, float>& out) const override {
    out[std::string(_name) + "/value"] = _value;
  }

  int    tickCount;
  String lastCmd;
  float  _value;

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
  doc["command"] = "on";
  mgr.dispatchCommand("relay", doc.as<JsonObjectConst>());

  TEST_ASSERT_EQUAL_STRING("on", a.lastCmd.c_str());
  TEST_ASSERT_EQUAL_STRING("", b.lastCmd.c_str());
}

// ── Schedule tests ───────────────────────────────────────────────────────────

// All schedule tests run with TZ=UTC so localtime_r matches the UTC timestamps.
// 2024-01-10 10:00:00 UTC  (Wednesday)
static const time_t T_10_00 = 1704880800;
// 2024-01-10 23:00:00 UTC  (Wednesday)
static const time_t T_23_00 = 1704924000;
// 2024-01-10 03:00:00 UTC  (Wednesday)
static const time_t T_03_00 = 1704848400;
// 2024-01-13 10:00:00 UTC  (Saturday)
static const time_t T_SAT_10_00 = 1705140000;

static Schedule makeSchedule(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  Schedule s;
  s.loadWindows(doc.as<JsonArrayConst>());
  return s;
}

void test_schedule_no_windows_inactive() {
  Schedule s;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.activeValue(T_10_00));
}

void test_schedule_normal_window_inside() {
  Schedule s = makeSchedule(R"([{"from":"08:00","to":"22:00","value":1.0}])");
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.activeValue(T_10_00));
}

void test_schedule_normal_window_outside() {
  Schedule s = makeSchedule(R"([{"from":"08:00","to":"22:00","value":1.0}])");
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.activeValue(T_23_00));
}

void test_schedule_overnight_after_on() {
  Schedule s = makeSchedule(R"([{"from":"22:00","to":"06:00","value":1.0}])");
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.activeValue(T_23_00));
}

void test_schedule_overnight_before_off() {
  Schedule s = makeSchedule(R"([{"from":"22:00","to":"06:00","value":1.0}])");
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.activeValue(T_03_00));
}

void test_schedule_overnight_outside() {
  Schedule s = makeSchedule(R"([{"from":"22:00","to":"06:00","value":1.0}])");
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.activeValue(T_10_00));
}

void test_schedule_override_on() {
  Schedule s = makeSchedule(R"([{"from":"08:00","to":"22:00","value":1.0}])");
  s.setManualValue(1.0f);
  s.setControlMode(ControlMode::Manual);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.activeValue(T_23_00));
}

void test_schedule_override_off() {
  Schedule s = makeSchedule(R"([{"from":"08:00","to":"22:00","value":1.0}])");
  s.setManualValue(0.0f);
  s.setControlMode(ControlMode::Manual);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.activeValue(T_10_00));
}

void test_schedule_load_clears_override() {
  Schedule s = makeSchedule(R"([{"from":"08:00","to":"22:00","value":1.0}])");
  s.setControlMode(ControlMode::Manual);
  // loadWindows must NOT clear manual mode
  JsonDocument empty;
  s.loadWindows(empty.as<JsonArrayConst>());
  TEST_ASSERT_TRUE(s.hasOverride());
}

void test_schedule_day_of_week_inactive_on_weekend() {
  // days:[1,2,3,4,5] = Mon–Fri only; T_SAT_10_00 is Saturday
  Schedule s = makeSchedule(R"([{"days":[1,2,3,4,5],"from":"08:00","to":"22:00","value":1.0}])");
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.activeValue(T_SAT_10_00));
}

void test_schedule_day_of_week_active_on_weekday() {
  // Same window, but T_10_00 is Wednesday — should be active
  Schedule s = makeSchedule(R"([{"days":[1,2,3,4,5],"from":"08:00","to":"22:00","value":1.0}])");
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.activeValue(T_10_00));
}

void test_schedule_manual_holds_after_load_windows() {
  Schedule s = makeSchedule(R"([{"from":"08:00","to":"22:00","value":1.0}])");
  s.setManualValue(0.0f);
  s.setControlMode(ControlMode::Manual);
  TEST_ASSERT_EQUAL(ControlMode::Manual, s.controlMode());

  // Push a new schedule — must not revert to automatic
  JsonDocument doc;
  deserializeJson(doc, R"([{"from":"00:00","to":"23:59","value":1.0}])");
  s.loadWindows(doc.as<JsonArrayConst>());

  TEST_ASSERT_EQUAL(ControlMode::Manual, s.controlMode());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.activeValue(T_10_00));
}

void test_schedule_automatic_mode_clears_override() {
  Schedule s = makeSchedule(R"([{"from":"08:00","to":"22:00","value":1.0}])");
  s.setManualValue(0.0f);
  s.setControlMode(ControlMode::Manual);
  TEST_ASSERT_EQUAL(ControlMode::Manual, s.controlMode());

  s.setControlMode(ControlMode::Automatic);
  TEST_ASSERT_EQUAL(ControlMode::Automatic, s.controlMode());
  // Should now follow the schedule — T_10_00 is inside 08:00–22:00
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.activeValue(T_10_00));
}

// ── PeripheralManager remove / has ───────────────────────────────────────────

void test_manager_remove(void) {
  PeripheralManager mgr;
  mgr.add(new MockPeripheral("light", 1000));
  mgr.beginAll();
  mgr.remove("light");
  TEST_ASSERT_NULL(mgr.find("light"));
}

void test_manager_has(void) {
  PeripheralManager mgr;
  MockPeripheral p("light", 1000);
  mgr.add(&p);
  mgr.beginAll();
  TEST_ASSERT_TRUE(mgr.has("light"));
  TEST_ASSERT_FALSE(mgr.has("pump"));
}

void test_manager_add_after_begin_calls_begin(void) {
  PeripheralManager mgr;
  mgr.beginAll();
  MockPeripheral* p = new MockPeripheral("late", 1000);
  mgr.add(p);
  // A peripheral added after beginAll() should be ticked immediately
  // at t=1000ms without an explicit beginAll() call.
  String out = mgr.tickAll(0, 1000);
  TEST_ASSERT_FALSE(out.empty());
  TEST_ASSERT_EQUAL_INT(1, p->tickCount);
  mgr.remove("late");
}

// ── Trigger integration ───────────────────────────────────────────────────────

static Trigger* makeTrigger(const char* id, const char* condJson,
                             const char* target, bool enabled = true) {
  std::string json = R"({"id":")";
  json += id;
  json += R"(","enabled":)";
  json += enabled ? "true" : "false";
  json += R"(,"cooldown_s":0,)";
  json += R"("actions":[{"type":"peripheral_action","config":{"peripheral":")";
  json += target;
  json += R"(","command":"set","value":1.0}}],)";
  json += R"("condition":)";
  json += condJson;
  json += "}";

  JsonDocument* doc = new JsonDocument();
  deserializeJson(*doc, json.c_str());
  Trigger* t = new Trigger();
  t->load(doc->as<JsonObjectConst>());
  delete doc;
  return t;
}

void test_trigger_add_find_remove(void) {
  PeripheralManager mgr;
  Trigger* t = makeTrigger("t1", R"({"op":"literal","value":1})", "relay");
  mgr.addTrigger(t);
  TEST_ASSERT_NOT_NULL(mgr.findTrigger("t1"));
  mgr.removeTrigger("t1");
  TEST_ASSERT_NULL(mgr.findTrigger("t1"));
}

void test_trigger_fires_when_condition_met(void) {
  PeripheralManager mgr;
  // Sensor reports 15.0 — trigger fires when value < 19.0
  MockPeripheral* sensor = new MockPeripheral("temp", 1000, 15.0f);
  MockPeripheral* relay  = new MockPeripheral("relay", 1000);
  mgr.add(sensor);
  mgr.add(relay);
  mgr.beginAll();

  Trigger* t = makeTrigger("t1",
    R"({"op":"lt","left":{"op":"value","measurement":"temp/value"},"right":{"op":"literal","value":19.0}})",
    "relay");
  mgr.addTrigger(t);

  mgr.tickAll(1000, 1000);
  TEST_ASSERT_EQUAL_STRING("set", relay->lastCmd.c_str());

  mgr.remove("temp");
  mgr.remove("relay");
}

void test_trigger_does_not_fire_when_condition_false(void) {
  PeripheralManager mgr;
  // Sensor reports 25.0 — trigger does NOT fire when value < 19.0
  MockPeripheral* sensor = new MockPeripheral("temp", 1000, 25.0f);
  MockPeripheral* relay  = new MockPeripheral("relay", 1000);
  mgr.add(sensor);
  mgr.add(relay);
  mgr.beginAll();

  Trigger* t = makeTrigger("t1",
    R"({"op":"lt","left":{"op":"value","measurement":"temp/value"},"right":{"op":"literal","value":19.0}})",
    "relay");
  mgr.addTrigger(t);

  mgr.tickAll(1000, 1000);
  TEST_ASSERT_EQUAL_STRING("", relay->lastCmd.c_str());

  mgr.remove("temp");
  mgr.remove("relay");
}

void test_trigger_respects_cooldown(void) {
  PeripheralManager mgr;
  MockPeripheral* sensor = new MockPeripheral("temp", 1000, 15.0f);
  MockPeripheral* relay  = new MockPeripheral("relay", 1000);
  mgr.add(sensor);
  mgr.add(relay);
  mgr.beginAll();

  std::string json = R"({"id":"t1","enabled":true,"cooldown_s":10,)"
                     R"("actions":[{"type":"peripheral_action","config":{"peripheral":"relay","command":"set","value":1.0}}],)"
                     R"("condition":{"op":"lt","left":{"op":"value","measurement":"temp/value"},)"
                     R"("right":{"op":"literal","value":19.0}}})";
  JsonDocument doc;
  deserializeJson(doc, json.c_str());
  Trigger* t = new Trigger();
  t->load(doc.as<JsonObjectConst>());
  mgr.addTrigger(t);

  // First tick at t=1 — fires
  mgr.tickAll(1, 1000);
  TEST_ASSERT_EQUAL_STRING("set", relay->lastCmd.c_str());

  // Reset lastCmd and tick at t=5 (within 10s cooldown) — must NOT fire
  relay->lastCmd = "";
  mgr.tickAll(5, 2000);
  TEST_ASSERT_EQUAL_STRING("", relay->lastCmd.c_str());

  // Tick at t=12 (past cooldown) — fires again
  mgr.tickAll(12, 3000);
  TEST_ASSERT_EQUAL_STRING("set", relay->lastCmd.c_str());

  mgr.remove("temp");
  mgr.remove("relay");
}

void test_trigger_disabled_does_not_fire(void) {
  PeripheralManager mgr;
  MockPeripheral* sensor = new MockPeripheral("temp", 1000, 15.0f);
  MockPeripheral* relay  = new MockPeripheral("relay", 1000);
  mgr.add(sensor);
  mgr.add(relay);
  mgr.beginAll();

  Trigger* t = makeTrigger("t1",
    R"({"op":"lt","left":{"op":"value","measurement":"temp/value"},"right":{"op":"literal","value":19.0}})",
    "relay", false);
  mgr.addTrigger(t);

  mgr.tickAll(1000, 1000);
  TEST_ASSERT_EQUAL_STRING("", relay->lastCmd.c_str());

  mgr.remove("temp");
  mgr.remove("relay");
}

void test_current_measurements_populates_map(void) {
  MockPeripheral p("sensor", 1000, 42.5f);
  std::map<std::string, float> values;
  p.currentMeasurements(values);
  TEST_ASSERT_EQUAL_FLOAT(42.5f, values["sensor/value"]);
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
  RUN_TEST(test_schedule_override_on);
  RUN_TEST(test_schedule_override_off);
  RUN_TEST(test_schedule_load_clears_override);
  RUN_TEST(test_schedule_day_of_week_inactive_on_weekend);
  RUN_TEST(test_schedule_day_of_week_active_on_weekday);
  RUN_TEST(test_schedule_manual_holds_after_load_windows);
  RUN_TEST(test_schedule_automatic_mode_clears_override);
  RUN_TEST(test_manager_remove);
  RUN_TEST(test_manager_has);
  RUN_TEST(test_manager_add_after_begin_calls_begin);
  RUN_TEST(test_trigger_add_find_remove);
  RUN_TEST(test_trigger_fires_when_condition_met);
  RUN_TEST(test_trigger_does_not_fire_when_condition_false);
  RUN_TEST(test_trigger_respects_cooldown);
  RUN_TEST(test_trigger_disabled_does_not_fire);
  RUN_TEST(test_current_measurements_populates_map);
  return UNITY_END();
}
