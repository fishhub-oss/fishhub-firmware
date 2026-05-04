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
  std::string json = R"({"id":"t","enabled":true,"target":"","condition":)";
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
    R"({"id":"t","enabled":true,"target":"","cooldown_s":60,"condition":{"op":"literal","value":1}})");
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
    R"({"id":"t","enabled":false,"target":"","condition":{"op":"literal","value":1}})");
  Trigger t;
  t.load(doc.as<JsonObjectConst>());
  PeripheralManager mgr;

  TEST_ASSERT_FALSE(t.evaluate({}, 1000, mgr));
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

  return UNITY_END();
}
