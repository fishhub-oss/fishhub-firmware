# Configuration

## `config.h`

`include/config.h` is **gitignored** and must be created locally before building. Use the provided template:

```bash
cp include/config.h.example include/config.h
```

Then fill in the four required defines:

```cpp
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

#define SERVER_URL   "http://192.168.x.x:8080/readings"
#define DEVICE_TOKEN "your-64-char-hex-token"
```

| Define | Description |
|---|---|
| `WIFI_SSID` | SSID of the Wi-Fi network the ESP32 will join |
| `WIFI_PASSWORD` | Wi-Fi password |
| `SERVER_URL` | Full URL of the `POST /readings` endpoint on the FishHub backend. Use the local IP of the machine running the server — `localhost` won't resolve on device. |
| `DEVICE_TOKEN` | 64-char hex Bearer token issued by `POST /tokens` on the server |

## Provisioning a new device

1. Start the FishHub backend (`make dev` in `fishhub-server/`).
2. Issue a token:
   ```bash
   curl -s -X POST http://localhost:8080/tokens | jq
   ```
3. Copy the `"token"` value from the response into `config.h` as `DEVICE_TOKEN`.
4. Find your machine's local IP (the Makefile prints it on `make dev`):
   ```bash
   ipconfig getifaddr en0   # Wi-Fi
   ipconfig getifaddr en1   # Ethernet
   ```
5. Set `SERVER_URL` to `http://<your-ip>:8080/readings`.
6. Build and flash:
   ```bash
   pio run --target upload
   ```

## `include/pins.h`

Pin assignments are in `include/pins.h`:

```cpp
#define ONE_WIRE_PIN 4
```

GPIO 4 is the data line for the DS18B20 OneWire bus. Change this if you wire the sensor to a different pin.
