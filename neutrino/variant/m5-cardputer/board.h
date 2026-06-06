#pragma once
#define NEU_BOARD_NAME        "m5-cardputer"
#define NEU_UART_CONSOLE_IDX  0
#define NEU_UART_CONSOLE_BAUD 115200
/* Display: ST7789 240x135 over SPI on ESP32-S3. */

#ifdef NEU_USE_SIC
#  include "sic/sic_board.h"
#  define NEU_SIC_BOARD SIC_BOARD_CARDPUTER
#endif
