#include <unity.h>
#include <ArduinoJson.h>
#include "peripheral_manager.h"
#include "../../src/peripheral_manager.cpp"

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

  void appendSenML(JsonArray& entries, time_t /*now*/) override {
    JsonObject e = entries.add<JsonObject>();
    e["n"] = _name;
    e["v"] = (float)tickCount;
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

// ── entry point ──────────────────────────────────────────────────────────────

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_not_ticked_before_interval);
  RUN_TEST(test_ticked_after_interval);
  RUN_TEST(test_two_peripherals_tick_independently);
  RUN_TEST(test_dispatch_command_routes_by_name);
  return UNITY_END();
}
