# JCON Mobile App ↔ Firmware Integration Guide

This is the reference for how the **JCON mobile app** talks to this
ESP32-S3 firmware: how the app finds the device, every HTTP endpoint it
uses (with real request/response examples taken from the firmware source),
CORS behaviour, and troubleshooting.

> **Where is the app?** The JCON app lives in its own repository:
> **https://github.com/praveensaummya/jcon**
> Prebuilt release APKs are attached to that repo's
> [Releases page](https://github.com/praveensaummya/jcon/releases) (built
> automatically by CI — see `.github/workflows/release.yml` there).
> Latest release:
> **[jcon-v1.0.0.apk](https://github.com/praveensaummya/jcon/releases/download/v1.0.0/jcon-v1.0.0.apk)**
> — [v1.0.0 release notes](https://github.com/praveensaummya/jcon/releases/tag/v1.0.0)

**Firmware-side code:** `main/mqtt_server.c` (all routes + CORS),
`main/main.c` (server startup + mDNS), `main/include/app_config.h`
(ports, hostname, payloads). Everything below reflects the actual
implementation — if you change the code, update this document.

## At a glance

| | |
|---|---|
| Transport | Plain HTTP REST over the local Wi-Fi network (device control needs no cloud) |
| Discovery | mDNS — device answers as **`esp32-inverter.local`**, advertises service `_http._tcp` on port **8080** |
| Base URL | `http://esp32-inverter.local:8080` (or `http://<device-ip>:8080`) |
| Payload format | JSON, `Content-Type: application/json` |
| CORS | Open: `Access-Control-Allow-Origin: *` on every `/api/*` response + `OPTIONS` preflight handler |
| Cloud mirror | Relay/voice state is also published/subscribed via MQTT (see [README](../README.md#mqtt-cloud)) |

## 1. How the app finds the device (mDNS)

After Wi-Fi connects, the firmware starts an mDNS responder:

| Item | Value | Firmware define |
|---|---|---|
| Hostname | `esp32-inverter.local` | `APP_MDNS_HOSTNAME` |
| Instance name | `ESP32-S3 Smart Inverter Controller` | `APP_MDNS_INSTANCE_NAME` |
| Service | `ESP32-WebServer` / `_http._tcp` | `APP_MDNS_SERVICE_*` |
| Advertised port | `8080` | `APP_MDNS_SERVICE_PORT` (= `APP_HTTP_API_PORT`) |

The app resolves the hostname, then probes the API port. Requirements:

* Phone and ESP32 must be on the **same network**. The ESP32-S3 is
  **2.4 GHz Wi-Fi only** — it will never appear on a 5 GHz-only SSID.
* Guest networks / "AP/client isolation" block mDNS and local traffic —
  the device will be invisible there.
* If `.local` resolution fails (common on some Android/network combos),
  fall back to the device IP: get it from the router's DHCP list, or from
  the serial monitor (the boot log prints the resolved URL).

## 2. Reachability check — `GET /api/status`

The app calls this first (its `_checkDeviceReachability()` hits
`http://<ip>:8080/api/status` and treats **any HTTP 200** as "device online").

```bash
curl -i http://esp32-inverter.local:8080/api/status
```

```http
HTTP/1.1 200 OK
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type
Connection: close
Content-Type: application/json

{"status":"online","device":"ESP32-S3 Inverter","port":8080}
```

Notes for app developers:

* `port` in the body is the live API port (`APP_HTTP_API_PORT`) — read it
  instead of hard-coding 8080 if you support custom firmware builds.
* The firmware answers with `Connection: close`; the app should keep
  sending/expecting that header — without it, sockets leak on the ESP32 and
  the server eventually refuses connections until reboot.

## 3. Relay control — `POST /api/relay`

```bash
curl -X POST http://esp32-inverter.local:8080/api/relay \
     -H "Content-Type: application/json" \
     -d '{"relay":1,"state":1}'
```

```json
{"status":"ok"}
```

| Field | Type | Meaning |
|---|---|---|
| `relay` | number | Relay ID: `1` or `2` (Relay 3 mirrors Relay 1 in hardware; it has no ID) |
| `state` | number | `1` = ON, `0` = OFF |

Behaviour:

* The relay GPIO switches **immediately**, and the new state is also
  published to the cloud topic `device/relays/status` (if MQTT is connected)
  so other clients stay in sync.
* The endpoint replies `{"status":"ok"}` whenever the connection succeeds —
  invalid payloads/IDs are logged on the device serial console but are **not**
  reported back to the app. Validate IDs (1–2) and states (0/1) app-side.
* Voice commands, HTTP, and MQTT all converge on the same
  `set_relay_state()` — state is always consistent no matter which channel
  triggered it.

## 4. Voice recognition toggle

Voice control can be muted/unmuted remotely; the setting persists in NVS
flash and survives reboots.

**Read current state:**

```bash
curl http://esp32-inverter.local:8080/api/voice/status
```
```json
{"voice_enabled":true}
```

**Set state:**

```bash
curl -X POST http://esp32-inverter.local:8080/api/voice/config \
     -H "Content-Type: application/json" \
     -d '{"enabled":false}'
```
```json
{"status":"ok"}
```

* `enabled` **must be a JSON boolean** (`true`/`false`) — anything else gets
  `HTTP 400 "Invalid JSON payload"`.
* When toggled (from the app **or** via MQTT `device/voice/command`), the new
  state is published to `device/voice/status` so all clients stay in sync.
* With voice recognition OFF the wake word ("HI ESP") is ignored; HTTP/MQTT
  relay control keeps working.

## 5. MQTT cloud configuration from the app

The app can point the device at any MQTT broker at runtime — no reflash
needed. Settings are stored in NVS and survive reboots.

**Save + connect:**

```bash
curl -X POST http://esp32-inverter.local:8080/api/config/mqtt \
     -H "Content-Type: application/json" \
     -d '{"uri":"mqtts://<cluster>.s1.eu.hivemq.cloud:8883","username":"dev1","password":"secret"}'
```
```json
{"status":"ok","message":"MQTT configuration saved and client restarted successfully!"}
```

**Erase + disconnect:**

```bash
curl -X DELETE http://esp32-inverter.local:8080/api/config/mqtt
```
```json
{"status":"ok","message":"MQTT configuration erased and client stopped!"}
```

Behaviour and precedence:

| Source | When it is used |
|---|---|
| NVS (saved via the app) | Always wins — read on every client (re)start |
| `main/secrets.h` fallbacks | Only when NVS has no saved config (e.g. after `DELETE`, on next reboot) |
| `CHANGE-ME` placeholders | Fresh clone without `secrets.h` → MQTT stays offline until configured |

* `uri` is **required** — omitting it returns `HTTP 400 "Missing 'uri' field"`.
  `username`/`password` are optional (empty = anonymous connect attempt).
* Use `mqtts://` + TLS port (e.g. 8883 for HiveMQ Cloud); the firmware
  validates the broker certificate against the ESP-IDF certificate bundle.
* `DELETE` stops the client immediately; after a reboot the device falls
  back to the flashed `secrets.h` defaults (if any).

## 6. CORS

Every `/api/*` handler attaches:

```
Access-Control-Allow-Origin:  *
Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type
```

A wildcard `OPTIONS /api/*` preflight handler is registered, so the app can
call the API from any web view/origin (e.g. a PWA) without proxying.

## 7. Endpoint summary

| Method | Path | Purpose | Success response |
|---|---|---|---|
| `GET` | `/api/status` | Reachability ping (expects HTTP 200) | `{"status":"online","device":"ESP32-S3 Inverter","port":<port>}` |
| `POST` | `/api/relay` | Switch relay `{"relay":1,"state":1}` | `{"status":"ok"}` |
| `POST` | `/api/config/mqtt` | Save broker config (NVS) + restart client | `{"status":"ok","message":"..."}` |
| `DELETE` | `/api/config/mqtt` | Erase broker config + stop client | `{"status":"ok","message":"..."}` |
| `POST` | `/api/voice/config` | `{"enabled":true\|false}` (persisted) | `{"status":"ok"}` |
| `GET` | `/api/voice/status` | Read voice state | `{"voice_enabled":true}` |
| `OPTIONS` | `/api/*` | CORS preflight | empty 200 |

## 8. Firmware settings that affect the app

All in `main/include/app_config.h` (documented there in detail):

| Define | Default | Effect on the app |
|---|---|---|
| `APP_HTTP_API_PORT` | `8080` | The port the app talks to (also advertised via mDNS and in `/api/status`) |
| `APP_MDNS_HOSTNAME` | `esp32-inverter` | The `.local` name the app resolves — changing it breaks discovery unless the app is updated |
| `APP_HTTP_CTRL_PORT` | `32769` | Internal; must differ from the Wi-Fi provisioning portal's 32768 |

## 9. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| App can't find the device | Phone & device on different SSIDs/bands (ESP32-S3 is 2.4 GHz only); guest network with AP isolation; mDNS blocked — use the device IP directly |
| `GET /api/status` times out | HTTP server only starts after Wi-Fi connects (see LED colours in [README](../README.md#status-led-meanings)); wrong port — confirm with `/api/status` body or serial log |
| Relay doesn't click | Only IDs 1–2 exist; Relay 3 mirrors Relay 1; check serial log for `[RELAY ERROR] Invalid relay ID` |
| Voice toggle returns 400 | Body must be exactly `{"enabled":<bool>}` — booleans, not `"true"` strings |
| MQTT settings saved but no cloud connection | URI must be `mqtts://…` with a TLS port; broker certificate must be trusted by the ESP-IDF bundle; check serial `MQTT_CLIENT` logs |
| Device stopped reaching cloud after `DELETE /api/config/mqtt` | Expected — client stopped; it will reconnect on next reboot from `secrets.h` fallbacks, or reconfigure via `POST` |

## Related docs

* [README.md](../README.md) — build/flash, full configuration reference, MQTT topics
* [main/include/app_config.h](../main/include/app_config.h) — every tunable with comments

