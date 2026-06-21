#pragma once

#include <Arduino.h>

// Downloads the firmware binary at `url` over HTTPS (TLS without CA verification —
// integrity is guaranteed by the sha256 comparison, not the cert chain), writes it
// to the inactive OTA slot, and verifies the SHA-256.
//
// Returns true on success — the caller is responsible for setting the NVS rollback
// guards and rebooting. Returns false on any error (HTTP failure, write error, hash
// mismatch); the OTA slot is aborted and the current firmware is unaffected.
bool performOtaUpdate(const String& url, const String& expectedSha256);
