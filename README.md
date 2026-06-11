# Resilient IoT Control System — ESP32

An ESP32-based home automation system with cloud control via SinricPro and a custom local fallback mode that keeps the system operational during network or cloud outages.

Built with hardware reliability as the primary design constraint — not just software convenience.

---

## Design Decisions Worth Noting

**Local Fallback Mode** — When cloud/WiFi is unavailable, the ESP32 switches to AP mode and hosts a local web server for direct device control. This is not a library feature; it's a custom state machine handling the cloud→local transition cleanly.

**CGNAT Workaround** — Consumer ISPs in India typically assign private IPs via CGNAT, blocking inbound connections. Worked around this using SinricPro's outbound WebSocket model. Roadmap includes a fully local MQTT broker on a home Linux server to eliminate cloud dependency entirely.

**Hardware Protection** — Flyback diodes on all inductive relay loads. AC supply via HLK-5M05 (isolated AC-to-DC). Li-ion backup with MCP73831-based charge management to handle power flickers without dropping the system.

---

## Hardware

| Component | Role |
|---|---|
| ESP32-WROOM-32 | Dual-core MCU, central controller |
| 2× 2-Channel Relay Module | Switches up to 4 AC loads |
| HLK-5M05 | Isolated 5V AC-to-DC supply |
| MCP73831 | Li-ion charge management IC |
| TSOP1838 IR Sensor | Manual override / IR remote input |
| Flyback Diodes | Inductive load protection on relay coils |

Circuit schematic: [`schematics/Schematic_Exp-gr-82_2026-01-02.png`](schematics/Schematic_Exp-gr-82_2026-01-02.png)

---

## System Behavior

```
Power On
   └─► Connect to WiFi + SinricPro
         ├─► Success → Normal Mode (cloud control, LED solid)
         └─► Fail    → Fallback Mode (AP + local web UI, LED slow blink)
```

**Normal Mode** — SinricPro cloud, voice control via Alexa/Google Assistant, remote access from anywhere.

**Fallback Mode** — ESP32 creates its own WiFi AP. Connect to it, open `192.168.4.1`, control devices directly via web UI. No internet required.

---

## LED Status

| Pattern | State |
|---|---|
| Fast blink | Connecting to WiFi / cloud |
| Solid on | Connected, normal operation |
| Slow blink | Fallback mode active |

---

## Stack

- **Firmware**: Embedded C (Arduino framework via PlatformIO)
- **Cloud**: SinricPro (WebSocket-based, outbound connection)
- **Libraries**: SinricPro 3.5.2, ArduinoJson 7.x, IRremoteESP8266

---

## Build

```bash
git clone https://github.com/rudra-patell/esp32-home-automation.git
cd esp32-home-automation
pio run --target upload
pio device monitor
```

Requires PlatformIO and a configured `credentials.h` with your SinricPro App Key, Secret, and Device IDs.

---

## Roadmap

- [ ] Replace SinricPro with local MQTT broker (Mosquitto on home Linux server)
- [ ] Edge Voice AI running fully locally
- [ ] OTA firmware updates
- [ ] Energy monitoring per relay channel

---

## Safety

This project switches mains AC voltage. Ensure relay ratings match your load, all AC wiring is properly insulated, and a qualified electrician handles any mains connections.

---

## License

MIT
