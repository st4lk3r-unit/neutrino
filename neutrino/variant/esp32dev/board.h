
#pragma once
#define NEU_BOARD_NAME        "esp32dev"
#define NEU_UART_CONSOLE_IDX  0
#define NEU_UART_CONSOLE_BAUD 115200

/* RadioLib SX127x pin defaults (override in platformio.ini if needed) */
#ifndef RADIOLIB_SX127X_CS
#define RADIOLIB_SX127X_CS   5
#endif
#ifndef RADIOLIB_SX127X_DIO0
#define RADIOLIB_SX127X_DIO0 26
#endif
#ifndef RADIOLIB_SX127X_RST
#define RADIOLIB_SX127X_RST  14
#endif
#ifndef RADIOLIB_SX127X_DIO1
#define RADIOLIB_SX127X_DIO1 -1
#endif
#ifndef RADIOLIB_SX127X_BUSY
#define RADIOLIB_SX127X_BUSY -1
#endif
