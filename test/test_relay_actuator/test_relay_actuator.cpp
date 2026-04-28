#include <unity.h>
#include <ArduinoJson.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

// ── subject under test ────────────────────────────────────────────────────────

#include "../../src/schedule.cpp"
#include "../../src/peripherals/relay_actuator.cpp"

// ── helpers ───────────────────────────────────────────────────────────────────

// Force the relay into a known state without triggering the schedule.
// We drive tick() with a time that makes _schedule.isActive() return false
// (empty schedule → always inactive), then manipulate via applyCommand.
static const time_t T0 = 1700000000;

// Returns a freshly ticked RelayActuator whose appendSenML result is in `doc`.
// `changed` indicates whether this tick should represent a state change.
static void tickAndAppend(RelayActuator& relay, bool triggerChange,
                          JsonDocument& doc) {
  JsonArray arr = doc.to<JsonArray>();

  if (triggerChange) {
    // Apply an override to flip state, then advance time just enough that
    // tick() sees a change.
    JsonDocument cmd;
    cmd["action"] = "set";
    cmd["state"]  = true;
    relay.applyCommand(cmd.as<JsonObjectConst>());
  }

  // Advance time past heartbeat so tick always fires when no change occurred.
  static time_t t = T0 + ACTUATOR_HEARTBEAT_S;
  t += ACTUATOR_HEARTBEAT_S;
  relay.tick(t);
  relay.appendSenML(arr, t);
}

// ── tests ─────────────────────────────────────────────────────────────────────

void test_state_change_emits_change_source(void) {
  RelayActuator relay("light", 1);
  relay.begin();

  // Override to ON → tick sees a state change.
  JsonDocument cmd;
  cmd["action"] = "set";
  cmd["state"]  = true;
  relay.applyCommand(cmd.as<JsonObjectConst>());

  time_t t = T0 + ACTUATOR_HEARTBEAT_S;
  relay.tick(t);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  relay.appendSenML(arr, t);

  // Find the source entry.
  const char* src = nullptr;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n).find("/source") != std::string::npos) {
      src = obj["vs"];
    }
  }
  TEST_ASSERT_NOT_NULL(src);
  TEST_ASSERT_EQUAL_STRING("change", src);
}

void test_heartbeat_emits_heartbeat_source(void) {
  RelayActuator relay("light", 1);
  relay.begin();

  // Tick once to initialise _lastSentAt, with no state change (empty schedule).
  time_t t1 = T0 + ACTUATOR_HEARTBEAT_S;
  relay.tick(t1);

  // Second tick: no state change, heartbeat fires.
  time_t t2 = t1 + ACTUATOR_HEARTBEAT_S;
  relay.tick(t2);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  relay.appendSenML(arr, t2);

  const char* src = nullptr;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n).find("/source") != std::string::npos) {
      src = obj["vs"];
    }
  }
  TEST_ASSERT_NOT_NULL(src);
  TEST_ASSERT_EQUAL_STRING("heartbeat", src);
}

void test_state_entry_always_present(void) {
  RelayActuator relay("light", 1);
  relay.begin();

  time_t t = T0 + ACTUATOR_HEARTBEAT_S;
  relay.tick(t);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  relay.appendSenML(arr, t);

  bool foundState = false;
  for (JsonObject obj : arr) {
    const char* n = obj["n"];
    if (n && std::string(n).find("/state") != std::string::npos) {
      foundState = true;
      TEST_ASSERT_TRUE(obj["vb"].is<bool>());
    }
  }
  TEST_ASSERT_TRUE(foundState);
}

// ── entry point ───────────────────────────────────────────────────────────────

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_state_change_emits_change_source);
  RUN_TEST(test_heartbeat_emits_heartbeat_source);
  RUN_TEST(test_state_entry_always_present);
  return UNITY_END();
}
