#include <unity.h>
#include <ArduinoJson.h>
#include <cstring>
#include <map>
#include <string>

#include "../../src/trigger_event_queue.cpp"
#include "../../src/trigger.cpp"

// ── helpers ───────────────────────────────────────────────────────────────────

static TriggerEvent makeEvent(const char* triggerId, time_t firedAt) {
  TriggerEvent ev = {};
  strncpy(ev.triggerId, triggerId, sizeof(ev.triggerId) - 1);
  ev.firedAt = firedAt;
  std::string id = makeTriggerEventId(triggerId, firedAt);
  strncpy(ev.triggerEventId, id.c_str(), sizeof(ev.triggerEventId) - 1);
  return ev;
}

// ── queue tests ───────────────────────────────────────────────────────────────

void test_queue_empty_on_init(void) {
  TriggerEventQueue q;
  TEST_ASSERT_TRUE(q.empty());
  TEST_ASSERT_EQUAL(0, q.size());
  TEST_ASSERT_NULL(q.front());
}

void test_queue_push_pop(void) {
  TriggerEventQueue q;
  TriggerEvent ev = makeEvent("abc-123", 1000);
  q.push(ev);

  TEST_ASSERT_FALSE(q.empty());
  TEST_ASSERT_EQUAL(1, q.size());

  const TriggerEvent* front = q.front();
  TEST_ASSERT_NOT_NULL(front);
  TEST_ASSERT_EQUAL_STRING("abc-123", front->triggerId);
  TEST_ASSERT_EQUAL(1000, front->firedAt);

  q.pop();
  TEST_ASSERT_TRUE(q.empty());
  TEST_ASSERT_NULL(q.front());
}

void test_queue_fifo_order(void) {
  TriggerEventQueue q;
  q.push(makeEvent("t1", 1000));
  q.push(makeEvent("t2", 2000));
  q.push(makeEvent("t3", 3000));

  TEST_ASSERT_EQUAL_STRING("t1", q.front()->triggerId);
  q.pop();
  TEST_ASSERT_EQUAL_STRING("t2", q.front()->triggerId);
  q.pop();
  TEST_ASSERT_EQUAL_STRING("t3", q.front()->triggerId);
  q.pop();
  TEST_ASSERT_TRUE(q.empty());
}

void test_queue_overflow_drops_oldest(void) {
  TriggerEventQueue q;

  // Fill to capacity
  for (size_t i = 0; i < TRIGGER_EVENT_QUEUE_CAPACITY; i++) {
    char id[8];
    snprintf(id, sizeof(id), "t%02zu", i);
    q.push(makeEvent(id, (time_t)i));
  }
  TEST_ASSERT_EQUAL(TRIGGER_EVENT_QUEUE_CAPACITY, q.size());
  TEST_ASSERT_EQUAL_STRING("t00", q.front()->triggerId);

  // One more — oldest (t00) should be dropped
  q.push(makeEvent("tXX", 999));
  TEST_ASSERT_EQUAL(TRIGGER_EVENT_QUEUE_CAPACITY, q.size());
  TEST_ASSERT_EQUAL_STRING("t01", q.front()->triggerId);

  // Last element must be the overflow one
  for (size_t i = 0; i < TRIGGER_EVENT_QUEUE_CAPACITY - 1; i++) q.pop();
  TEST_ASSERT_EQUAL_STRING("tXX", q.front()->triggerId);
}

// ── makeTriggerEventId tests ──────────────────────────────────────────────────

void test_make_trigger_event_id_deterministic(void) {
  std::string a = makeTriggerEventId("trigger-uuid-1234", 1700000000);
  std::string b = makeTriggerEventId("trigger-uuid-1234", 1700000000);
  TEST_ASSERT_EQUAL_STRING(a.c_str(), b.c_str());
}

void test_make_trigger_event_id_different_firedAt(void) {
  std::string a = makeTriggerEventId("trigger-uuid-1234", 1700000000);
  std::string b = makeTriggerEventId("trigger-uuid-1234", 1700000001);
  TEST_ASSERT_NOT_EQUAL(0, strcmp(a.c_str(), b.c_str()));
}

void test_make_trigger_event_id_different_triggerIds(void) {
  std::string a = makeTriggerEventId("trigger-uuid-aaaa", 1700000000);
  std::string b = makeTriggerEventId("trigger-uuid-bbbb", 1700000000);
  TEST_ASSERT_NOT_EQUAL(0, strcmp(a.c_str(), b.c_str()));
}

void test_make_trigger_event_id_non_empty(void) {
  std::string id = makeTriggerEventId("t1", 1000);
  TEST_ASSERT_TRUE(id.length() > 0);
}

// ── Trigger enqueues on fire ──────────────────────────────────────────────────

void test_trigger_evaluate_returns_fired(void) {
  JsonDocument doc;
  deserializeJson(doc, R"({
    "id": "trigger-abc",
    "enabled": true,
    "cooldown_s": 0,
    "condition": {"op":"literal","value":1},
    "actions": [
      {"type":"peripheral_action","config":{"peripheral":"relay","command":"set","value":1}}
    ]
  })");

  Trigger t;
  t.load(doc.as<JsonObjectConst>());

  TriggerFired result = t.evaluate({}, 1234);

  TEST_ASSERT_TRUE(result.fired);
  TEST_ASSERT_EQUAL_STRING("trigger-abc", result.triggerId.c_str());
  TEST_ASSERT_EQUAL(1234, result.firedAt);
  TEST_ASSERT_EQUAL(1, result.actions.size());
  TEST_ASSERT_EQUAL_STRING("relay", result.actions[0].first.c_str());
}

void test_trigger_evaluate_collects_condition_measurements(void) {
  JsonDocument doc;
  deserializeJson(doc, R"({
    "id": "t1",
    "enabled": true,
    "cooldown_s": 0,
    "condition": {
      "op": "and",
      "left":  {"op":"gt","left":{"op":"value","measurement":"temp"},"right":{"op":"literal","value":20}},
      "right": {"op":"lt","left":{"op":"value","measurement":"ph"},"right":{"op":"literal","value":8}}
    },
    "actions": []
  })");

  Trigger t;
  t.load(doc.as<JsonObjectConst>());

  std::map<std::string, float> values = {
    {"temp", 25.0f},
    {"ph",   7.5f},
    {"other", 99.0f}  // not in condition — must not appear in readings
  };

  TriggerFired result = t.evaluate(values, 1000);

  TEST_ASSERT_TRUE(result.fired);
  TEST_ASSERT_EQUAL(2, result.readings.size());
  TEST_ASSERT_TRUE(result.readings.count("temp") > 0);
  TEST_ASSERT_TRUE(result.readings.count("ph") > 0);
  TEST_ASSERT_FALSE(result.readings.count("other") > 0);
  TEST_ASSERT_EQUAL_FLOAT(25.0f, result.readings["temp"]);
  TEST_ASSERT_EQUAL_FLOAT(7.5f,  result.readings["ph"]);
}

void test_trigger_not_fired_when_condition_false(void) {
  JsonDocument doc;
  deserializeJson(doc, R"({
    "id": "t1",
    "enabled": true,
    "cooldown_s": 0,
    "condition": {"op":"literal","value":0},
    "actions": []
  })");

  Trigger t;
  t.load(doc.as<JsonObjectConst>());

  TriggerFired result = t.evaluate({}, 1000);
  TEST_ASSERT_FALSE(result.fired);
  TEST_ASSERT_EQUAL(0, result.actions.size());
  TEST_ASSERT_EQUAL(0, result.readings.size());
}

// ── entry point ───────────────────────────────────────────────────────────────

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_queue_empty_on_init);
  RUN_TEST(test_queue_push_pop);
  RUN_TEST(test_queue_fifo_order);
  RUN_TEST(test_queue_overflow_drops_oldest);

  RUN_TEST(test_make_trigger_event_id_deterministic);
  RUN_TEST(test_make_trigger_event_id_different_firedAt);
  RUN_TEST(test_make_trigger_event_id_different_triggerIds);
  RUN_TEST(test_make_trigger_event_id_non_empty);

  RUN_TEST(test_trigger_evaluate_returns_fired);
  RUN_TEST(test_trigger_evaluate_collects_condition_measurements);
  RUN_TEST(test_trigger_not_fired_when_condition_false);

  return UNITY_END();
}
