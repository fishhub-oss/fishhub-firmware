#include <unity.h>
#include <ArduinoJson.h>
#include <cstdlib>
#include <cmath>
#include <string>
#include <map>

#include "peripheral_manager.h"
#include "../../src/peripheral_manager.cpp"
#include "../../src/trigger.cpp"

// ── helpers ───────────────────────────────────────────────────────────────────

// Build and evaluate a trigger whose condition is condJson.
// A fresh Trigger and PeripheralManager are created for each call.
static bool evalCond(const char* condJson,
                     const std::map<std::string, float>& values,
                     time_t now = 1000) {
  std::string json = R"({"id":"t","enabled":true,"actions":[],"condition":)";
  json += condJson;
  json += "}";

  JsonDocument doc;
  deserializeJson(doc, json.c_str());
  Trigger t;
  t.load(doc.as<JsonObjectConst>());
  PeripheralManager mgr;
  return t.evaluate(values, now, mgr);
}

static const char* COMPOUND =
  R"({"op":"or","left":{"op":"gt","left":{"op":"add","left":{"op":"value","measurement":"ds18b20-4/temperature"},"right":{"op":"value","measurement":"ds18b20-5/temperature"}},"right":{"op":"literal","value":38}},"right":{"op":"lt","left":{"op":"value","measurement":"ds18b20-4/temperature"},"right":{"op":"literal","value":18}}})";

// ── stub peripheral ───────────────────────────────────────────────────────────

struct RecordingPeripheral : public Peripheral {
  const char* _name;
  int         callCount = 0;
  JsonDocument lastCmd;

  explicit RecordingPeripheral(const char* name) : _name(name) {}

  const char* name() const override { return _name; }
  void begin() override {}
  uint32_t intervalMs() const override { return 1000; }
  bool tick(time_t) override { return false; }
  void appendSenML(JsonArray&, time_t) override {}

  void applyCommand(JsonObjectConst cmd) override {
    callCount++;
    lastCmd.set(cmd);
  }
};

// ── leaf nodes ────────────────────────────────────────────────────────────────

void test_value_hit(void) {
  bool r = evalCond(R"({"op":"gt","left":{"op":"value","measurement":"temp"},"right":{"op":"literal","value":0}})",
                    {{"temp", 25.0f}});
  TEST_ASSERT_TRUE(r);
}

void test_value_miss_nan_propagation(void) {
  // Missing key → NAN; NAN > 0 is false in IEEE 754
  bool r = evalCond(R"({"op":"gt","left":{"op":"value","measurement":"missing"},"right":{"op":"literal","value":0}})",
                    {});
  TEST_ASSERT_FALSE(r);
}

void test_literal(void) {
  bool r = evalCond(R"({"op":"gt","left":{"op":"literal","value":1},"right":{"op":"literal","value":0}})",
                    {});
  TEST_ASSERT_TRUE(r);
}

// ── arithmetic ────────────────────────────────────────────────────────────────

void test_add(void) {
  // 3 + 4 = 7 > 6
  bool r = evalCond(
    R"({"op":"gt","left":{"op":"add","left":{"op":"literal","value":3},"right":{"op":"literal","value":4}},"right":{"op":"literal","value":6}})",
    {});
  TEST_ASSERT_TRUE(r);
}

void test_sub(void) {
  // 10 - 3 = 7 > 6
  bool r = evalCond(
    R"({"op":"gt","left":{"op":"sub","left":{"op":"literal","value":10},"right":{"op":"literal","value":3}},"right":{"op":"literal","value":6}})",
    {});
  TEST_ASSERT_TRUE(r);
}

void test_mul(void) {
  // 3 * 4 = 12 > 11
  bool r = evalCond(
    R"({"op":"gt","left":{"op":"mul","left":{"op":"literal","value":3},"right":{"op":"literal","value":4}},"right":{"op":"literal","value":11}})",
    {});
  TEST_ASSERT_TRUE(r);
}

void test_div(void) {
  // 10 / 4 = 2.5 > 2
  bool r = evalCond(
    R"({"op":"gt","left":{"op":"div","left":{"op":"literal","value":10},"right":{"op":"literal","value":4}},"right":{"op":"literal","value":2}})",
    {});
  TEST_ASSERT_TRUE(r);
}

void test_div_by_zero(void) {
  // 10 / 0 → 0.0; 0 > 5 → false
  bool r = evalCond(
    R"({"op":"gt","left":{"op":"div","left":{"op":"literal","value":10},"right":{"op":"literal","value":0}},"right":{"op":"literal","value":5}})",
    {});
  TEST_ASSERT_FALSE(r);
}

// ── comparisons ───────────────────────────────────────────────────────────────

void test_lt_true(void) {
  TEST_ASSERT_TRUE(evalCond(R"({"op":"lt","left":{"op":"literal","value":3},"right":{"op":"literal","value":4}})", {}));
}
void test_lt_false(void) {
  TEST_ASSERT_FALSE(evalCond(R"({"op":"lt","left":{"op":"literal","value":4},"right":{"op":"literal","value":3}})", {}));
}

void test_lte_true(void) {
  TEST_ASSERT_TRUE(evalCond(R"({"op":"lte","left":{"op":"literal","value":4},"right":{"op":"literal","value":4}})", {}));
}
void test_lte_false(void) {
  TEST_ASSERT_FALSE(evalCond(R"({"op":"lte","left":{"op":"literal","value":5},"right":{"op":"literal","value":4}})", {}));
}

void test_gt_true(void) {
  TEST_ASSERT_TRUE(evalCond(R"({"op":"gt","left":{"op":"literal","value":5},"right":{"op":"literal","value":4}})", {}));
}
void test_gt_false(void) {
  TEST_ASSERT_FALSE(evalCond(R"({"op":"gt","left":{"op":"literal","value":4},"right":{"op":"literal","value":5}})", {}));
}

void test_gte_true(void) {
  TEST_ASSERT_TRUE(evalCond(R"({"op":"gte","left":{"op":"literal","value":4},"right":{"op":"literal","value":4}})", {}));
}
void test_gte_false(void) {
  TEST_ASSERT_FALSE(evalCond(R"({"op":"gte","left":{"op":"literal","value":3},"right":{"op":"literal","value":4}})", {}));
}

void test_eq_true(void) {
  TEST_ASSERT_TRUE(evalCond(R"({"op":"eq","left":{"op":"literal","value":7},"right":{"op":"literal","value":7}})", {}));
}
void test_eq_false(void) {
  TEST_ASSERT_FALSE(evalCond(R"({"op":"eq","left":{"op":"literal","value":7},"right":{"op":"literal","value":8}})", {}));
}

// ── logical ───────────────────────────────────────────────────────────────────

void test_and_both_true(void) {
  TEST_ASSERT_TRUE(evalCond(
    R"({"op":"and","left":{"op":"literal","value":1},"right":{"op":"literal","value":1}})", {}));
}

void test_and_short_circuit_left_false(void) {
  // left is false → short-circuits to false without evaluating right
  TEST_ASSERT_FALSE(evalCond(
    R"({"op":"and","left":{"op":"literal","value":0},"right":{"op":"literal","value":1}})", {}));
}

void test_and_right_false(void) {
  TEST_ASSERT_FALSE(evalCond(
    R"({"op":"and","left":{"op":"literal","value":1},"right":{"op":"literal","value":0}})", {}));
}

void test_or_short_circuit_left_true(void) {
  // left is true → short-circuits to true without evaluating right
  TEST_ASSERT_TRUE(evalCond(
    R"({"op":"or","left":{"op":"literal","value":1},"right":{"op":"literal","value":0}})", {}));
}

void test_or_right_true(void) {
  TEST_ASSERT_TRUE(evalCond(
    R"({"op":"or","left":{"op":"literal","value":0},"right":{"op":"literal","value":1}})", {}));
}

void test_or_both_false(void) {
  TEST_ASSERT_FALSE(evalCond(
    R"({"op":"or","left":{"op":"literal","value":0},"right":{"op":"literal","value":0}})", {}));
}

void test_not_of_true(void) {
  TEST_ASSERT_FALSE(evalCond(R"({"op":"not","left":{"op":"literal","value":1}})", {}));
}

void test_not_of_false(void) {
  TEST_ASSERT_TRUE(evalCond(R"({"op":"not","left":{"op":"literal","value":0}})", {}));
}

// ── compound ──────────────────────────────────────────────────────────────────
// (ds18b20-4/temperature + ds18b20-5/temperature > 38) or (ds18b20-4/temperature < 18)

void test_compound_first_branch_true(void) {
  // 20 + 20 = 40 > 38 → true
  TEST_ASSERT_TRUE(evalCond(COMPOUND,
    {{"ds18b20-4/temperature", 20.0f}, {"ds18b20-5/temperature", 20.0f}}));
}

void test_compound_second_branch_true(void) {
  // 10 + 10 = 20, not > 38; but 10 < 18 → true
  TEST_ASSERT_TRUE(evalCond(COMPOUND,
    {{"ds18b20-4/temperature", 10.0f}, {"ds18b20-5/temperature", 10.0f}}));
}

void test_compound_both_false(void) {
  // 20 + 15 = 35, not > 38; and 20 not < 18 → false
  TEST_ASSERT_FALSE(evalCond(COMPOUND,
    {{"ds18b20-4/temperature", 20.0f}, {"ds18b20-5/temperature", 15.0f}}));
}

// ── cooldown ──────────────────────────────────────────────────────────────────

void test_cooldown_prevents_refire(void) {
  JsonDocument doc;
  deserializeJson(doc,
    R"({"id":"t","enabled":true,"actions":[],"cooldown_s":60,"condition":{"op":"literal","value":1}})");
  Trigger t;
  t.load(doc.as<JsonObjectConst>());
  PeripheralManager mgr;

  TEST_ASSERT_TRUE(t.evaluate({}, 1000, mgr));   // first fire
  TEST_ASSERT_FALSE(t.evaluate({}, 1050, mgr));  // within cooldown
  TEST_ASSERT_TRUE(t.evaluate({}, 1061, mgr));   // beyond cooldown
}

// ── disabled ──────────────────────────────────────────────────────────────────

void test_disabled_does_not_fire(void) {
  JsonDocument doc;
  deserializeJson(doc,
    R"({"id":"t","enabled":false,"actions":[],"condition":{"op":"literal","value":1}})");
  Trigger t;
  t.load(doc.as<JsonObjectConst>());
  PeripheralManager mgr;

  TEST_ASSERT_FALSE(t.evaluate({}, 1000, mgr));
}

// ── actions array ─────────────────────────────────────────────────────────────

void test_load_single_action(void) {
  JsonDocument doc;
  deserializeJson(doc, R"({
    "id": "t1",
    "enabled": true,
    "cooldown_s": 0,
    "condition": {"op":"literal","value":1},
    "actions": [
      {"type":"peripheral_action","config":{"peripheral":"relay-16","command":"set","value":1}}
    ]
  })");

  Trigger t;
  t.load(doc.as<JsonObjectConst>());

  auto* p = new RecordingPeripheral("relay-16");
  PeripheralManager mgr;
  mgr.add(p);

  bool fired = t.evaluate({}, 1000, mgr);
  TEST_ASSERT_TRUE(fired);
  TEST_ASSERT_EQUAL(1, p->callCount);
  TEST_ASSERT_EQUAL_STRING("set", p->lastCmd["command"] | "");
  TEST_ASSERT_EQUAL_INT(1, p->lastCmd["value"] | 0);
}

void test_load_unknown_type_skipped(void) {
  JsonDocument doc;
  deserializeJson(doc, R"({
    "id": "t2",
    "enabled": true,
    "cooldown_s": 0,
    "condition": {"op":"literal","value":1},
    "actions": [
      {"type":"future_type","config":{"peripheral":"relay-16","command":"set","value":1}}
    ]
  })");

  Trigger t;
  t.load(doc.as<JsonObjectConst>());

  auto* p = new RecordingPeripheral("relay-16");
  PeripheralManager mgr;
  mgr.add(p);

  t.evaluate({}, 1000, mgr);
  // Unknown type must be silently skipped — peripheral never called
  TEST_ASSERT_EQUAL(0, p->callCount);
}

void test_load_many_actions(void) {
  // 5 peripheral_action entries — all must be dispatched
  JsonDocument doc;
  deserializeJson(doc, R"({
    "id": "t3",
    "enabled": true,
    "cooldown_s": 0,
    "condition": {"op":"literal","value":1},
    "actions": [
      {"type":"peripheral_action","config":{"peripheral":"r1","command":"set","value":1}},
      {"type":"peripheral_action","config":{"peripheral":"r2","command":"set","value":1}},
      {"type":"peripheral_action","config":{"peripheral":"r3","command":"set","value":1}},
      {"type":"peripheral_action","config":{"peripheral":"r4","command":"set","value":1}},
      {"type":"peripheral_action","config":{"peripheral":"r5","command":"set","value":1}}
    ]
  })");

  Trigger t;
  bool ok = t.load(doc.as<JsonObjectConst>());
  TEST_ASSERT_TRUE(ok);

  auto* r1 = new RecordingPeripheral("r1");
  auto* r2 = new RecordingPeripheral("r2");
  auto* r3 = new RecordingPeripheral("r3");
  auto* r4 = new RecordingPeripheral("r4");
  auto* r5 = new RecordingPeripheral("r5");
  PeripheralManager mgr;
  mgr.add(r1); mgr.add(r2); mgr.add(r3); mgr.add(r4); mgr.add(r5);

  t.evaluate({}, 1000, mgr);

  // All 5 actions must be dispatched
  TEST_ASSERT_EQUAL(1, r1->callCount);
  TEST_ASSERT_EQUAL(1, r2->callCount);
  TEST_ASSERT_EQUAL(1, r3->callCount);
  TEST_ASSERT_EQUAL(1, r4->callCount);
  TEST_ASSERT_EQUAL(1, r5->callCount);
}

void test_serialize_round_trip(void) {
  const char* src = R"({
    "id": "t4",
    "enabled": true,
    "cooldown_s": 30,
    "condition": {"op":"literal","value":1},
    "actions": [
      {"type":"peripheral_action","config":{"peripheral":"relay-16","command":"set","value":1}}
    ]
  })";

  JsonDocument loadDoc;
  deserializeJson(loadDoc, src);
  Trigger t;
  t.load(loadDoc.as<JsonObjectConst>());

  // Serialize into a new document
  JsonDocument outDoc;
  t.serializeTo(outDoc.to<JsonObject>());

  TEST_ASSERT_EQUAL_STRING("upsert", outDoc["op"] | "");
  TEST_ASSERT_EQUAL_STRING("t4", outDoc["id"] | "");
  TEST_ASSERT_TRUE(outDoc["enabled"]);
  TEST_ASSERT_EQUAL_INT(30, outDoc["cooldown_s"] | 0);

  JsonArrayConst actions = outDoc["actions"].as<JsonArrayConst>();
  TEST_ASSERT_EQUAL(1, actions.size());

  JsonObjectConst entry = actions[0].as<JsonObjectConst>();
  TEST_ASSERT_EQUAL_STRING("peripheral_action", entry["type"] | "");

  JsonObjectConst cfg = entry["config"].as<JsonObjectConst>();
  TEST_ASSERT_EQUAL_STRING("relay-16", cfg["peripheral"] | "");
  TEST_ASSERT_EQUAL_STRING("set", cfg["command"] | "");
  TEST_ASSERT_EQUAL_INT(1, cfg["value"] | 0);
}

// ── entry point ───────────────────────────────────────────────────────────────

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_value_hit);
  RUN_TEST(test_value_miss_nan_propagation);
  RUN_TEST(test_literal);

  RUN_TEST(test_add);
  RUN_TEST(test_sub);
  RUN_TEST(test_mul);
  RUN_TEST(test_div);
  RUN_TEST(test_div_by_zero);

  RUN_TEST(test_lt_true);
  RUN_TEST(test_lt_false);
  RUN_TEST(test_lte_true);
  RUN_TEST(test_lte_false);
  RUN_TEST(test_gt_true);
  RUN_TEST(test_gt_false);
  RUN_TEST(test_gte_true);
  RUN_TEST(test_gte_false);
  RUN_TEST(test_eq_true);
  RUN_TEST(test_eq_false);

  RUN_TEST(test_and_both_true);
  RUN_TEST(test_and_short_circuit_left_false);
  RUN_TEST(test_and_right_false);
  RUN_TEST(test_or_short_circuit_left_true);
  RUN_TEST(test_or_right_true);
  RUN_TEST(test_or_both_false);
  RUN_TEST(test_not_of_true);
  RUN_TEST(test_not_of_false);

  RUN_TEST(test_compound_first_branch_true);
  RUN_TEST(test_compound_second_branch_true);
  RUN_TEST(test_compound_both_false);

  RUN_TEST(test_cooldown_prevents_refire);
  RUN_TEST(test_disabled_does_not_fire);

  RUN_TEST(test_load_single_action);
  RUN_TEST(test_load_unknown_type_skipped);
  RUN_TEST(test_load_many_actions);
  RUN_TEST(test_serialize_round_trip);

  return UNITY_END();
}
