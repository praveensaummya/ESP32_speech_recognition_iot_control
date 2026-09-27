# ESP32-S3 Voice-Controlled Inverter (English Speech Commands)

ESP32-S3 firmware that recognises the wake word **"HI ESP"** and voice
commands ("inverter on" / "inverter off") to switch relays. It exposes a REST
API on the LAN, advertises itself over mDNS for the JCON mobile app, and
mirrors relay/voice state to an MQTT cloud broker.

## JCON mobile app

The companion **JCON mobile app** controls this device over the local network:
it discovers the board via mDNS (`esp32-inverter.local`), verifies it is
reachable with `GET /api/status`, and drives relays, voice recognition, and
the cloud-MQTT configuration through the REST API on port 8080.

- 📄 **Full integration guide** (discovery, every endpoint with examples, CORS,
  troubleshooting): **[docs/JCON_APP.md](docs/JCON_APP.md)**
- 📦 App source / APK: **[github.com/praveensaummya/jcon](https://github.com/praveensaummya/jcon)** — ready-to-install APKs on the [Releases page](https://github.com/praveensaummya/jcon/releases) (built automatically by CI from every `v*` tag)

## Project layout (where to look)

| Path | Purpose |
|---|---|
| `main/main.c` | Entry point, Wi-Fi/mDNS/HTTP/MQTT startup, wake-word + MultiNet detection loop |
| `main/speech_commands_action.c` | Relays, WS2812 status LED, voice command → action mapping |
| `main/mqtt_server.c` | MQTT cloud client + REST API handlers (relay, MQTT config, voice toggle) |
| `main/cpu_monitor.c` | Optional CPU/task usage monitor (`cpu_monitor_start()`) |
| `main/include/app_config.h` | **ALL application tunables — start here** |
| `docs/JCON_APP.md` | JCON mobile app ↔ firmware integration guide |
| `main/secrets.h.example` | Template for local MQTT credentials (see below) |
| `components/hardware_driver/` | Board layer: INMP441 I2S microphone bring-up |
| `components/esp-wifi-manager/` | Third-party captive-portal Wi-Fi provisioning (creds stored in NVS by the user via the portal) |

## How to use

### 1. Activate the ESP-IDF environment

ESP-IDF **v5.2.7** is required. Adjust the path to your own installation —
do not copy a teammate's home directory path blindly:

```bash
. $HOME/.espressif/v5.2.7/esp-idf/export.sh
```

### 2. Set target and sdkconfig

```bash
idf.py set-target esp32s3
cp sdkconfig.defaults.esp32s3 sdkconfig   # only on first build / after clean
```

### 3. Configure local secrets (one time after cloning)

Real MQTT broker credentials are **not** stored in the repo. Each developer
keeps them in a local, git-ignored file:

```bash
cp main/secrets.h.example main/secrets.h   # then edit with YOUR broker URI/user/pass
```

* `main/secrets.h` is only a compile-time **fallback** — at runtime the
  device first reads the broker settings from NVS flash.
* A fresh clone without `secrets.h` still compiles (placeholder credentials
  + a `#warning`); MQTT simply won't connect until real credentials reach the
  device via `POST /api/config/mqtt` or a flashed `secrets.h`.

### 4. Build, flash and monitor

```bash
idf.py -b 2000000 flash monitor
```

(To exit the serial monitor, type `Ctrl-]`.)


---

## Configuration reference

### Compile-time settings — `main/include/app_config.h`

Every tunable lives in this one header with an explanatory comment. Summary:

| Define | Default | What it controls |
|---|---|---|
| `APP_HTTP_API_PORT` | `8080` | REST API server port (used by mDNS ad + mobile app + `/api/status` body) |
| `APP_HTTP_CTRL_PORT` | `32769` | esp_http_server internal control port (must differ from wifi_manager's 32768) |
| `APP_MDNS_HOSTNAME` | `"esp32-inverter"` | Device discovered as `esp32-inverter.local` — **changing breaks the mobile app** |
| `APP_MDNS_INSTANCE_NAME` | `"ESP32-S3 Smart Inverter Controller"` | Friendly name in mDNS browsers |
| `MQTT_TOPIC_RELAY_COMMAND` | `device/relays/command` | Subscribe: relay commands |
| `MQTT_TOPIC_RELAY_STATUS` | `device/relays/status` | Publish: relay state |
| `MQTT_TOPIC_VOICE_COMMAND` | `device/voice/command` | Subscribe: voice on/off |
| `MQTT_TOPIC_VOICE_STATUS` | `device/voice/status` | Publish: voice state |
| `MQTT_QOS` | `1` | QoS for all publishes/subscriptions |
| `NVS_NAMESPACE_MQTT` + keys | `mqtt_config` / `broker_uri`, `broker_user`, `broker_pass` | Where runtime broker settings persist. ⚠ Renaming loses saved settings on existing devices |
| `NVS_NAMESPACE_VOICE` + key | `voice_cfg` / `voice_enabled` | Voice-recognition on/off persists here |
| `RELAY_1_GPIO` / `RELAY_2_GPIO` / `RELAY_3_GPIO` | `GPIO4` / `GPIO5` / `GPIO13` | Relay outputs. **Relay 3 always mirrors Relay 1** (no independent ID) |
| `BUILTIN_PIXEL_LED_GPIO` | `48` | On-board WS2812 status LED |
| `WIFI_LED_SUCCESS_ON_TIME_MS` | `6000` | Green "connected" display time before auto-off |
| `WAKE_WORD_PHRASE` | `"HI ESP"` | Wake word (must match the flashed WakeNet model) |
| `MULTINET_COMMAND_TIMEOUT_MS` | `6000` | Listening window for a command after the wake word |
| `MIN_COMMAND_CONFIDENCE` | `0.12f` | Detection probability below which commands are ignored |
| `VOICE_CMD_*` enum | 1–4 | Command ID → relay mapping (see Voice commands table) |
| `ENABLE_AUDIO_METER` (in `main.c`) | `0` | `1` = print live audio VU meter in the serial monitor |

### Local secrets — `main/secrets.h` (not committed)

| Define | Meaning |
|---|---|
| `MQTT_DEFAULT_BROKER_URI` | Fallback broker URL, e.g. `mqtts://<cluster-id>.s1.eu.hivemq.cloud` |
| `MQTT_DEFAULT_BROKER_USER` | Fallback broker username |
| `MQTT_DEFAULT_BROKER_PASS` | Fallback broker password |

These are compile-time fallbacks, used only when NVS has no saved MQTT config.

### Runtime configuration (no rebuild needed)

* **MQTT broker** — `POST /api/config/mqtt` with `{"uri": "...", "username": "...", "password": "..."}` persists to NVS and restarts the client. `DELETE /api/config/mqtt` erases it and stops the client.
* **Voice recognition on/off** — `POST /api/voice/config` with `{"enabled": true|false}`, persisted to NVS, survives reboots.
* **Wi-Fi credentials** — provisioned through the `esp-wifi-manager` captive portal (first boot: connect to the ESP32's access point).

## Interfaces

### HTTP API (LAN, port 8080 — CORS enabled for the mobile app)

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/status` | Reachability ping — app expects HTTP 200 `{"status":"online",...}` |
| `POST` | `/api/relay` | `{"relay":1,"state":1}` → drive relay + publish to MQTT |
| `POST` | `/api/config/mqtt` | Save broker URI/user/pass to NVS, restart client |
| `DELETE` | `/api/config/mqtt` | Erase broker config, stop client |
| `POST` | `/api/voice/config` | `{"enabled":true|false}` toggle (persisted) |
| `GET` | `/api/voice/status` | `{"voice_enabled":true|false}` |
| `OPTIONS` | `/api/*` | CORS preflight |

Device discovery: the app resolves `http://esp32-inverter.local:8080` via mDNS
(details, examples and troubleshooting in [docs/JCON_APP.md](docs/JCON_APP.md)).

### MQTT (cloud)

| Direction | Topic | Payload |
|---|---|---|
| Subscribe | `device/relays/command` | `{"relay":1,"state":1}` |
| Subscribe | `device/voice/command` | `{"voice_enabled":true}` (accepts `true/false` or `1/0`) |
| Publish | `device/relays/status` | `{"relay":1,"state":1}` |
| Publish | `device/voice/status` | `{"voice_enabled":true}` |

### Voice commands

| ID | Phrase | Action |
|---|---|---|
| 1 | "inverter on" | Relay 1 ON (Relay 3 mirrors) |
| 2 | "inverter off" | Relay 1 OFF (Relay 3 mirrors) |
| 3 | *(reserved)* | Relay 2 ON |
| 4 | *(reserved)* | Relay 2 OFF |

To add a command: register the ID/phrase in `detect_Task()` (`main/main.c`)
**and** add a case in `speech_commands_action()` — both reference the
`VOICE_CMD_*` enum in `app_config.h`. See the
[MultiNet docs](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/speech_command_recognition/README.html)
for retraining / custom models.

MultiNet snippet for reference:

```c
// MultiNet6 — create the multinet handle before adding speech commands
esp_mn_commands_clear();                             // Clear existing commands
esp_mn_commands_add(1, "turn on the light");         // add a command
esp_mn_commands_add(2, "turn off the light");        // add a command
esp_mn_commands_update();                            // update commands
multinet->print_active_speech_commands(model_data);  // print active commands
```

### Status LED meanings

| Colour | Meaning |
|---|---|
| Orange | Wi-Fi connecting |
| Green (6 s, then off) | Wi-Fi connected |
| Red | Wi-Fi disconnected / failed |
| Blue | Wake word detected — listening for a command |
| Off | Idle |
