#pragma once

// Developer override: create include/config.h (gitignored) to set credentials
// and connection details for local builds without provisioning. config.h is
// never required — the defaults below are used when it is absent (CI builds,
// fully-provisioned devices where NVS holds all values at runtime).
#if __has_include("config.h")
#include "config.h"
#endif

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
// Used only before the device is provisioned, or as a dev shortcut.
// After provisioning NVS keys "wifi_ssid" / "wifi_pass" take precedence.
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// ── MQTT broker ───────────────────────────────────────────────────────────────
// Used only before the device is provisioned, or as a dev shortcut.
// After provisioning the NVS key "mqtt_host" takes precedence.
// Define MQTT_TLS (no value needed) in config.h to enable TLS (port 8883).
// Leave it undefined for plain TCP (e.g. a self-hosted broker, port 1883).
#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif

#ifndef MQTT_PORT
#ifdef MQTT_TLS
#define MQTT_PORT 8883
#else
#define MQTT_PORT 1883
#endif
#endif

// ── Device identity ───────────────────────────────────────────────────────────
// Set by provisioning; override here for dev builds that skip provisioning.
// After provisioning the NVS key "device_id" takes precedence.
#ifndef DEVICE_ID
#define DEVICE_ID ""
#endif

// ── Server URL ────────────────────────────────────────────────────────────────
// Used only during the captive-portal provisioning/activation flow.
#ifndef SERVER_URL
#define SERVER_URL ""
#endif
