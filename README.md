# Diy Led Lights

A guide to make smart LEDs from cheap AliExpress ones.
Two-wire LED strips light one half for one polarity and the other half for the opposite polarity.
An H-bridge flips the polarity thousands of times per second, so to the eye everything is lit.

You have to host ur own MQTT SERVER!! Using https://mosquitto.org/ 
Default settings are fine!

```
Home Assistant ─┐
                ├──▶ MQTT broker (your server) ──▶ ESP32 ──▶ L293D ──▶ LEDs
Web page ───────┘
```

The second ESP32 / CYD / SD card from the old version is gone. The web page is a single static file
on your server and talks to the broker directly. Home Assistant finds the light by itself.

## Repo layout

```
LED-Box/            ESP32 firmware (Arduino) + config.example.h
web/index.html      the control page
compose.yaml        Mosquitto + nginx for the web page (Docker / Dockge)
.env.example        broker login for compose.yaml
.github/workflows/  CI: firmware compile check, stack smoke test
```

## What you need

- The LED lights, example [here](https://de.aliexpress.com/item/1005006125200079)
- 1x ESP32, [here](https://de.aliexpress.com/item/1005006370388002)
- L293D, [here](https://de.aliexpress.com/item/1005009621838611)
- A server with Docker (Dockge works great)
- Optional: Home Assistant with the MQTT integration

## Why this is still flicker free

The ESP32 flips the polarity with its LEDC hardware PWM, not in `loop()`.
Two channels run at 2 kHz, the second shifted by half a period, so IN1 and IN2 alternate
by themselves. Brightness is the duty cycle of each half. Wi-Fi, MQTT, or a slow server
cannot cause flicker, because they only ever send power, effect and brightness, never the switching itself.

## Setup

### 1. Server (Docker / Dockge)

1. Put this repo (or just `compose.yaml` and the `web` folder) in a Dockge stack folder.
2. Copy `.env.example` to `.env` and set `MQTT_USER` and `MQTT_PASS`. In Dockge, use the stack's env editor.
3. Start the stack.

You get:

| Service | Port | What |
|---|---|---|
| mosquitto | 1883 | MQTT, login required (ESP32, Home Assistant) |
| mosquitto | 9001 | MQTT over websockets, no login, but only `led/box/#` (the web page) |
| web | 8087 | the control page |

Anyone on your network can use the web page and the 9001 listener to change the lights.
If you put the page behind an https reverse proxy, change `BROKER` at the top of the script
in `web/index.html` to `wss://` and proxy the websocket port as well.

### 2. ESP32

1. Arduino IDE with the arduino-esp32 core 3.x and the library `PubSubClient`.
2. Copy `LED-Box/config.example.h` to `LED-Box/config.h`, fill in Wi-Fi, server IP and the broker login.
3. Flash `LED-Box/LED-Box.ino`.

### 3. Home Assistant

1. In Home Assistant add the **MQTT** integration and point it at the same broker (port 1883, same login).
2. Make sure "Enable discovery" is on (default). Leave the discovery prefix at `homeassistant`
   or change `HA_DISCOVERY_PREFIX` in `config.h`.
3. Boot the ESP32. A device **LED Box** with a light entity shows up on its own: on/off, brightness,
   and effects `static` and `fade`. Nothing to write in YAML.

The entity goes unavailable when the ESP is offline (MQTT last will), and everything is retained,
so Home Assistant, the web page and the box always agree.

## MQTT topics

| Topic | Values | Notes |
|---|---|---|
| `led/box/power` | `ON` / `OFF` | retained |
| `led/box/effect` | `static` / `fade` | retained |
| `led/box/brightness` | `0`-`255` | retained |
| `led/box/status` | `online` / `offline` | retained, set by the ESP (last will) |

Test from a shell: `mosquitto_pub -h <server> -u <user> -P <pass> -t led/box/power -m ON -r`

If you used the first MQTT version, remove its old retained topic once:
`mosquitto_pub -h <server> -u <user> -P <pass> -t led/box/mode -r -n`

## GitHub Actions

- **Firmware**: compiles `LED-Box.ino` for the ESP32 with arduino-cli on every change.
  It only checks that the code builds; it does not publish binaries, because Wi-Fi and MQTT
  credentials are compiled in.
- **Stack**: validates `compose.yaml`, starts the stack, checks that the broker accepts the login,
  keeps retained messages, rejects anonymous clients on 1883, and that the web page is served.

## Wires / Soldering

```
ESP32                              L293D
─────                              ─────

GPIO 27 (IN1)  ────────────────▶  pin 2  (1A)

GPIO 26 (IN2)  ────────────────▶  pin 7  (2A)

3.3V or GPIO HIGH ─────────────▶  pin 1  (1,2EN)

GND ───────────────────────────▶  pin 4
                                   pin 5
                                   pin 12
                                   pin 13

LOAD WIRE 1 ◀──────────────────▶ pin 3  (1Y)
LOAD WIRE 2 ◀──────────────────▶ pin 6  (2Y)

Load Supply + (LED / driver) ───▶ pin 8  (Vs)

5V (VIN) ───────────────────────▶ pin 16 (Vss)  ← logic power
 │
 └──────────────────────────────▶ pin 8  (Vs)   ← load power
```

Wiring is unchanged from the old version.

Have fun with it!
~Kira
