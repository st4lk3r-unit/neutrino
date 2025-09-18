
# Neutrino — platform template (Konsole + SGFX + RadioLib)

This template shows how to wire:
- **Konsole** (UART CLI)
- **SGFX** (tiny C99 graphics)
- **RadioLib** (LoRa SX127x demo, optional)

## Environments

- `native`: runs on your PC (stdin/stdout as UART). Great to hack CLI logic fast.
- `esp32-ssd1306`: ESP32 DevKit + SSD1306 (I2C on SDA=21, SCL=22). Shows SGFX banner and CLI on USB serial.
- *(optional)* `esp32-ssd1306-lora`: uncomment in `platformio.ini` and set your SX127x pins.

## Build

```sh
pio run -e native
pio run -e esp32-ssd1306 && pio device monitor -b 115200
```

## CLI

Built-ins: `help`, `clear`, `version`, `reboot`

Custom:

- `sys` — board, uptime, fw
- `echo <...>`
- `gfx` — draws a banner via SGFX (only if `NEU_USE_SGFX`)

Radio demo (when `NEU_USE_RADIOLIB` is defined):

- `rf freq <MHz>` — set frequency
- `rf tx <text>` — transmit text
- `rf rx <ms>` — poll for a packet for given ms

## Pins

In `variant/esp32dev/board.h` you have default SX127x pins. Override in `platformio.ini` if your board differs.

## Notes

- SGFX is configured entirely by build flags (`SGFX_*`) in `platformio.ini`.
- `native` env puts your terminal in raw mode for better interactivity; exit restores settings.
- Keep heavy modules in `src/mod_*.c(pp)` and register their commands in `neutrino_init()`.
