# Neutrino — cross-MCU PlatformIO firmware template

Neutrino is a minimal, architecture-agnostic firmware skeleton for embedded systems.
It wires together a UART CLI ([konsole](https://github.com/st4lk3r-unit/konsole)),
optional display ([SGFX](https://github.com/st4lk3r-unit/SGFX)),
optional hardware integration ([SIC](https://github.com/st4lk3r-unit/SIC)),
and optional LoRa radio ([RadioLib](https://github.com/jgromes/RadioLib)).

All platform-specific code is isolated behind an `arch_api_t` vtable — the application
core in `src/` compiles on both native (PC) and any supported MCU without changes.

---

## Clone and initialise

This repo uses **git submodules** (konsole, SGFX, SIC, RadioLib):

```sh
git clone --recurse-submodules https://github.com/st4lk3r-unit/neutrino.git
cd neutrino/neutrino
```

If you already cloned without `--recurse-submodules`:

```sh
git submodule update --init --recursive
```

To update all submodules to their latest tracked commit:

```sh
git submodule update --remote --merge
```

---

## Customise firmware name and version

The name and version shown in the CLI `version` command and `sys` output are set as
build flags in `platformio.ini`. Edit the `[env]` base section:

```ini
[env]
build_flags =
  -DNEU_VERSION=\"1.0.0\"
  -DKONSOLE_FW_NAME=\"my-firmware\"
  -DKONSOLE_FW_VERSION=\"1.0.0\"
```

---

## Environments

| Environment | Target | Display | SIC |
|-------------|--------|---------|-----|
| `native` | Linux/macOS PC | none | no |
| `esp32-vanilla` | ESP32 DevKit | none | no |
| `esp32s3-tdongle` | LilyGO T-Dongle S3 | ST7735 160×80 | no |
| `esp32s3-tpager` | LilyGO T-Pager | ST7796 480×222 | yes (kbd, mic, amp, encoder, battery) |
| `m5-cardputer` | M5Stack Cardputer | ST7789 240×135 | no |
| `esp32s3-heltecv3` | Heltec WiFi Kit V3 | SSD1306 128×64 | no |

---

## Build and flash

```sh
# Run on PC (no hardware needed)
pio run -e native

# Flash a T-Pager and open the serial monitor
pio run -e esp32s3-tpager -t upload && pio device monitor -b 115200

# Flash M5Stack Cardputer
pio run -e m5-cardputer -t upload && pio device monitor -b 115200
```

> **Note**: `monitor_raw = yes` is set in all embedded envs so that the ANSI terminal
> editing in konsole (cursor movement, history) works correctly. Use a raw-capable
> monitor such as `pio device monitor`, `screen`, or `minicom`.

---

## Built-in CLI commands

Available on all targets:

| Command | Description |
|---------|-------------|
| `help` | List available commands |
| `clear` | Clear the terminal |
| `version` | Firmware name and version |
| `reboot` | Reboot the MCU (no-op on native) |
| `sys` | Uptime, board name, firmware info |
| `echo <...>` | Echo arguments back |
| `gfx` | Draw SGFX banner (only if `NEU_USE_SGFX`) |

### SIC hardware commands (`NEU_USE_SIC` targets only)

| Command | Description |
|---------|-------------|
| `hw` | List probed peripherals (kbd, mic, amp, encoder, battery) |
| `bat` | Battery voltage and charge percent |
| `mic` | Live microphone VU meter — any key exits |
| `amp` | Play a 1kHz test tone for 500ms |
| `enc` | Live rotary encoder delta and button state — any key exits |
| `kmap` | Interactive keymap debug (prints key codes on press) |
| `i2c` | Scan I2C bus and print found addresses |

---

## Adding a command

Register commands in `neutrino_init()` inside `src/neutrino.c`:

```c
static int cmd_hello(struct konsole* ks, int argc, char** argv) {
    (void)argc; (void)argv;
    kon_printf(ks, "hello world\r\n");
    return 0;
}

/* inside neutrino_init(), append to the command table: */
static const kon_cmd g_extra[] = {
    { "hello", "say hello", cmd_hello },
};
```

Or keep module commands in separate `src/mod_*.c` files and register them from `neutrino_init()`.

---

## Architecture

```
src/
  neutrino.c       — CLI commands, main loop, arch_api_t wiring
  main.c           — native entry point (calls neutrino_init + loop)

arch/
  esp32/arduino/   — ESP32 arch_api_t implementation (Arduino framework)
  native/posix/    — native arch_api_t implementation (Linux/macOS)

variant/
  lilygo-tpager/   — board.h for T-Pager (SIC board descriptor + pin defs)
  lilygo-tdongle-s3/
  m5-cardputer/
  esp32dev/
  native/

lib/
  konsole/         — submodule: UART CLI
  SGFX/            — submodule: 2D graphics
  SIC/             — submodule: hardware abstraction (drivers, registry)
  RadioLib/        — submodule: LoRa / radio
```

The application code in `src/` only calls through `arch_api_t` vtable functions
(`A->millis()`, `A->uart_read()`, `A->delay_ms()`, …) and SIC vtable accessors
(`sic_mic(0)->v->start(…)`, `sic_kbd(0)->v->scan(…)`, …).
No platform headers ever appear in `src/`.

---

## Platform agnosticism

All four libraries follow the same rule:

- **Core / drivers**: pure C99, `stdlib.h` / `string.h` only — no Arduino, no FreeRTOS, no ESP-IDF.
- **Backends**: platform-specific code lives exclusively in `backends/arduino/` and `bus/*_arduino.*`,
  compiled only when `defined(ARDUINO) || defined(SIC_BACKEND_ARDUINO)`.

This means the core logic can be unit-tested on a PC with a C compiler, without any MCU SDK installed.
