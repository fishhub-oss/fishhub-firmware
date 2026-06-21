#include "ota_update.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>

static const int    OTA_CONNECT_TIMEOUT_MS = 30000;
static const int    OTA_READ_TIMEOUT_S     = 30;
static const size_t OTA_BUF_SIZE           = 4096;

bool performOtaUpdate(const String& url, const String& expectedSha256) {
    Serial.printf("[OTA] Downloading from %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure(); // TLS without CA verification — sha256 is the integrity check

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(OTA_CONNECT_TIMEOUT_MS);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP error: %d\n", code);
        http.end();
        return false;
    }

    int contentLen = http.getSize(); // -1 if server omits Content-Length
    Serial.printf("[OTA] Content-Length: %d bytes\n", contentLen);

    if (!Update.begin(contentLen > 0 ? (size_t)contentLen : UPDATE_SIZE_UNKNOWN)) {
        Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
        http.end();
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, /*is224=*/0);

    WiFiClient* stream = http.getStreamPtr();
    stream->setTimeout(OTA_READ_TIMEOUT_S);

    static uint8_t buf[OTA_BUF_SIZE];
    int remaining = contentLen; // negative means unknown
    int totalWritten = 0;
    bool error = false;

    while ((remaining != 0) && http.connected() && !error) {
        int toRead = (remaining > 0)
            ? min(remaining, (int)sizeof(buf))
            : (int)sizeof(buf);

        int n = stream->readBytes(buf, toRead);
        if (n <= 0) break; // timeout or connection closed

        mbedtls_sha256_update(&sha, buf, n);

        if (Update.write(buf, n) != (size_t)n) {
            Serial.println("[OTA] Flash write error");
            error = true;
            break;
        }

        totalWritten += n;
        if (remaining > 0) remaining -= n;
    }

    http.end();

    if (error || (contentLen > 0 && totalWritten < contentLen)) {
        Serial.printf("[OTA] Incomplete: received %d of %d bytes\n", totalWritten, contentLen);
        Update.abort();
        mbedtls_sha256_free(&sha);
        return false;
    }

    // Verify SHA-256
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(&hex[i * 2], 3, "%02x", digest[i]);
    hex[64] = '\0';

    if (!expectedSha256.isEmpty() && String(hex) != expectedSha256) {
        Serial.printf("[OTA] SHA-256 mismatch\n  got:      %s\n  expected: %s\n",
                      hex, expectedSha256.c_str());
        Update.abort();
        return false;
    }
    Serial.printf("[OTA] SHA-256 verified: %s\n", hex);

    if (!Update.end(/*evenIfRemaining=*/true)) {
        Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
        return false;
    }

    Serial.printf("[OTA] Flash complete — %d bytes written\n", totalWritten);
    return true;
}
