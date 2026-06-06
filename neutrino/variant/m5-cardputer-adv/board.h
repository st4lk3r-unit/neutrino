#pragma once
#define NEU_BOARD_NAME        "m5-cardputer-adv"
#define NEU_UART_CONSOLE_IDX  0
#define NEU_UART_CONSOLE_BAUD 115200
/* Cardputer-ADV: TCA8418 keyboard + ES8311 audio. */

#ifdef NEU_USE_SIC
#  include "sic/sic_board.h"
#  define NEU_SIC_BOARD SIC_BOARD_CARDPUTER_ADV
#endif
