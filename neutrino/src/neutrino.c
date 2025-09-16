#include <stdio.h>
#include <string.h>
#include "neutrino/arch_api.h"
#include "neutrino/neutrino.h"
#include "board.h"
#include "konsole/konsole.h"
#include "konsole/static.h"

#ifndef NEU_VERSION
#define NEU_VERSION "0.1.0"
#endif

static const arch_api_t* A = 0;
static int uart_idx = 0;

/* ---- konsole I/O bridge over arch_api UART ---- */
struct io_ctx {
    const arch_api_t* A;
    int idx;
};

static size_t k_read_avail(void* ctx) {
    (void)ctx;
    /* Optional optimization: return bytes available if your HAL exposes it.
       Our arch_api UART is non-blocking; return 1 to attempt a read. */
    return 1;
}
static size_t k_read(void* ctx, uint8_t* buf, size_t len) {
    struct io_ctx* c = (struct io_ctx*)ctx;
    int n = c->A->uart_read(c->idx, buf, len);
    return n > 0 ? (size_t)n : 0;
}
static size_t k_write(void* ctx, const uint8_t* buf, size_t len) {
    struct io_ctx* c = (struct io_ctx*)ctx;
    int n = c->A->uart_write(c->idx, buf, len);
    return n > 0 ? (size_t)n : 0;
}
static uint32_t k_millis(void* ctx) {
    struct io_ctx* c = (struct io_ctx*)ctx;
    return c->A->millis();
}

/* ---- Firmware commands (in addition to builtins: help/clear/version/reboot) ---- */
static int cmd_echo(struct konsole* ks, int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        kon_printf(ks, "%s%s", argv[i], (i + 1 < argc) ? " " : "\r\n");
    }
    return 0;
}
static int cmd_sys(struct konsole* ks, int argc, char** argv) {
    (void)argc; (void)argv;
    unsigned long ms = (unsigned long)A->millis();
    kon_printf(ks, "uptime: %lu ms\r\n", ms);
    kon_printf(ks, "board : %s\r\n", NEU_BOARD_NAME);
    kon_printf(ks, "fw    : neutrino %s\r\n", NEU_VERSION);
    return 0;
}

static const struct kon_cmd g_cmds[] = {
    { "echo", "echo arguments", cmd_echo },
    { "sys",  "system info",   cmd_sys  },
};

/* ---- Console instance ---- */
static struct konsole g_ks;
static struct kon_line_state g_line;
static struct io_ctx g_ioctx;

int neutrino_init(void) {
    A = arch_api();
    if (!A) return -1;
    if (A->init() != 0) return -2;

    uart_idx = NEU_UART_CONSOLE_IDX;
    A->uart_init(uart_idx, NEU_UART_CONSOLE_BAUD);

    g_ioctx.A = A;
    g_ioctx.idx = uart_idx;

    struct konsole_io io = {
        .read_avail = k_read_avail,
        .read  = k_read,
        .write = k_write,
        .millis= k_millis,
        .ctx   = &g_ioctx
    };

    /* Use static storage to avoid heap usage on MCUs */
    konsole_init_with_storage(&g_ks, &g_line, &io,
                              g_cmds, sizeof(g_cmds)/sizeof(g_cmds[0]),
                              "> ", /*vt100*/ true);

    kon_banner(&g_ks, "Neutrino ready");
    return 0;
}

void neutrino_run(void) {
    konsole_poll(&g_ks);
    A->delay_ms(1);
}
