# ArtNet / sACN → DMX Decoder
**v1.5.0** — ESP32 (generic) + auto-direction RS485 module + Web UI

## Hardware

RS485 module has 4 pins only (VCC/TXD/RXD/GND) — direction switching is
automatic on-board, there is no DE/RE pin to wire or drive from the MCU.
This module labels its pins from its own UART side, not the MCU side, so
wire straight across (TXD↔TXD, RXD↔RXD), not crossed.

```
ESP32 GPIO17  ──→  RS485 module TXD  (UART2 TX)
ESP32 GPIO16  ←──  RS485 module RXD  (UART2 RX)
Module A/B    ──→  DMX line (XLR pin 3 / pin 2)
Module VCC    ──→  3.3V or 5V (check datasheet)
120Ω terminator on DMX line far end
```

## Features

| Feature | Status |
|---------|--------|
| Art-Net DMX (OpCode 0x5000) | ✅ Full |
| sACN / E1.31 unicast + multicast | ✅ Full |
| sACN priority filtering | ✅ Basic (last-wins per priority) |
| Web config UI | ✅ Full |
| OTA (ArduinoOTA + web upload) | ✅ Full |
| Live DMX monitor (100ms refresh) | ✅ Full |
| RDM half-duplex switching | ✅ Correct |
| RDM DEVICE_INFO | ✅ GET |
| RDM DMX_START_ADDRESS | ✅ GET + SET |
| RDM IDENTIFY_DEVICE | ✅ GET + SET |
| RDM DEVICE_LABEL | ✅ GET + SET |
| RDM SOFTWARE_VERSION_LABEL | ✅ GET |
| RDM SUPPORTED_PARAMETERS | ✅ GET |
| RDM Discovery (DUB/mute) | 🚧 Stubbed |

## First Flash

```bash
# Edit platformio.ini if needed
pio run --target upload
pio device monitor
```

On first boot with no saved WiFi, an AP called `ArtDMX-Config` (password `artdmx123`) appears. Connect to it and open **http://192.168.4.1** to configure.

## Web UI

| Route | Description |
|-------|-------------|
| `GET /` | Config form + DMX monitor |
| `POST /save` | Save config, restart |
| `GET /status` | JSON status |
| `GET /dmx` | JSON DMX buffer |
| `POST /update` | OTA firmware binary upload |

## Art-Net Universe Numbering

Art-Net uses a 15-bit universe split into Net (7 bits) + Sub-Universe (8 bits). The UI accepts the combined integer 0–32767. For subnet 0, net 0: universe 0 = first universe.

## sACN Multicast

The node joins multicast group `239.255.X.Y` for the configured universe automatically.

## RDM Notes

RDM Discovery (DUB collision tree) is not yet implemented. The node will respond correctly to all addressed GET/SET commands from a controller that already knows its UID (printed on boot via Serial). Full discovery support is a significant undertaking (ANSI E1.20 is 200+ pages) and would be a separate development phase.

The RDM UID uses manufacturer prefix `0x7FF0` (development range). For a production device, register a manufacturer ID with ESTA.
