#include <unity.h>
#include <ArduinoJson.h>
#include <string>

// Standalone reimplementation for native testing (no Arduino String/time_t deps)
std::string buildSenMLPayload(float tempCelsius, long timestamp) {
  JsonDocument doc;
  JsonObject record = doc.add<JsonObject>();
  record["bn"] = "fishhub/device/";
  record["bt"] = timestamp;
  JsonObject entry = record["e"].add<JsonObject>();
  entry["n"] = "temperature";
  entry["u"] = "Cel";
  entry["v"] = tempCelsius;

  std::string output;
  serializeJson(doc, output);
  return output;
}

void test_happy_path() {
  std::string result = buildSenMLPayload(23.4f, 1713000000L);
  JsonDocument doc;
  deserializeJson(doc, result);
  TEST_ASSERT_EQUAL_STRING("fishhub/device/", doc[0]["bn"].as<const char*>());
  TEST_ASSERT_EQUAL(1713000000L, doc[0]["bt"].as<long>());
  TEST_ASSERT_EQUAL_STRING("temperature", doc[0]["e"][0]["n"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Cel", doc[0]["e"][0]["u"].as<const char*>());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.4f, doc[0]["e"][0]["v"].as<float>());
}

void test_negative_temperature() {
  std::string result = buildSenMLPayload(-2.5f, 1713000000L);
  JsonDocument doc;
  deserializeJson(doc, result);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -2.5f, doc[0]["e"][0]["v"].as<float>());
}

void test_zero_temperature() {
  std::string result = buildSenMLPayload(0.0f, 1713000000L);
  JsonDocument doc;
  deserializeJson(doc, result);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, doc[0]["e"][0]["v"].as<float>());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_happy_path);
  RUN_TEST(test_negative_temperature);
  RUN_TEST(test_zero_temperature);
  return UNITY_END();
}
