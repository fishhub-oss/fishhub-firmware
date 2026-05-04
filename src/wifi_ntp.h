#pragma once

// Returns true on success, false on failure (all retries exhausted / timeout).
bool connectWifi();
bool waitForNtp();
