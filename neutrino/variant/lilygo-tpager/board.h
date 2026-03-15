#pragma once
#define NEU_BOARD_NAME        "lilygo-tpager"
#define NEU_UART_CONSOLE_IDX  0
#define NEU_UART_CONSOLE_BAUD 115200

#ifdef NEU_USE_SIC
#  include "sic/sic_board.h"
#  define NEU_SIC_BOARD SIC_BOARD_TPAGER
#endif
