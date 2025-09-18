
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
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
struct io_ctx { const arch_api_t* A; int uart_idx; };
static size_t io_read_avail(void *ctx) {
  (void)ctx; return 1024; /* we use non-blocking read() anyway */
}
static size_t io_read(void *ctx, uint8_t *buf, size_t len) {
  struct io_ctx *c = (struct io_ctx*)ctx;
  int n = c->A->uart_read(c->uart_idx, buf, len);
  return (n > 0) ? (size_t)n : 0;
}
static size_t io_write(void *ctx, const uint8_t *buf, size_t len) {
  struct io_ctx *c = (struct io_ctx*)ctx;
  int n = c->A->uart_write(c->uart_idx, buf, len);
  return (n > 0) ? (size_t)n : 0;
}
static uint32_t io_millis(void *ctx) {
  struct io_ctx *c = (struct io_ctx*)ctx;
  return c->A->millis();
}

/* ---- Optional SGFX banner & tiny 'gfx' command ---- */
#if defined(NEU_USE_SGFX)
#  include "sgfx.h"
#  include "sgfx_port.h"
   static sgfx_device_t s_gfx_dev;
   static uint8_t       s_gfx_scratch[2048];
   static int gfx_init_ok = 0;

   static int neu_gfx_init(void) {
     int rc = sgfx_autoinit(&s_gfx_dev, s_gfx_scratch, sizeof s_gfx_scratch);
     if (rc) return rc;
     sgfx_clear(&s_gfx_dev, (sgfx_rgba8_t){0,0,0,255});
     sgfx_text8x8(&s_gfx_dev, 2, 2, "NEUTRINO", (sgfx_rgba8_t){255,255,255,255});
     sgfx_text8x8(&s_gfx_dev, 2, 12, NEU_VERSION, (sgfx_rgba8_t){255,255,255,255});
     gfx_init_ok = 1;
     return 0;
   }
   static int cmd_gfx(struct konsole *ks, int argc, char **argv) {
     (void)argc; (void)argv;
     if (!gfx_init_ok) {
       kon_printf(ks, "gfx not inited\r\n");
       return -1;
     }
     sgfx_clear(&s_gfx_dev, (sgfx_rgba8_t){0,0,0,255});
     sgfx_text8x8(&s_gfx_dev, 2, 2, "Hello SGFX!", (sgfx_rgba8_t){255,255,255,255});
     kon_printf(ks, "ok\r\n");
     return 0;
   }
#endif

/* ---- Custom commands ---- */
static int cmd_echo(struct konsole *ks, int argc, char **argv) {
  for (int i=1;i<argc;i++) {
    kon_printf(ks, "%s%s", argv[i], (i+1<argc)?" ":"");
  }
  kon_printf(ks, "\r\n");
  return 0;
}

static int cmd_sys(struct konsole *ks, int argc, char **argv) {
  (void)argc; (void)argv;
  kon_printf(ks, "arch  : %s\r\n", NEU_BOARD_NAME);
  kon_printf(ks, "uptime: %u ms\r\n", (unsigned)A->millis());
  kon_printf(ks, "fw    : neutrino %s\r\n", NEU_VERSION);
  return 0;
}

static const struct kon_cmd g_cmds[] = {
    { "echo",  "echo arguments",       cmd_echo },
    { "sys",   "system info",          cmd_sys  },
#if defined(NEU_USE_SGFX)
    #endif
};

/* ---- Console instance ---- */
static struct konsole g_ks;
static struct kon_line_state g_line;
static struct io_ctx g_ioctx;

/* Optional registration of RadioLib CLI (implemented in C++ file) */
void mod_radio_register(struct konsole *ks); /* weak */
__attribute__((weak)) void mod_radio_register(struct konsole *ks) { (void)ks; }

int neutrino_init(void) {
  A = arch_api();
  if (!A) return -1;
  if (A->init) A->init();

  /* init console over UART */
  uart_idx = NEU_UART_CONSOLE_IDX;
  A->uart_init(uart_idx, NEU_UART_CONSOLE_BAUD);

  struct konsole_io io = {
    .read_avail = io_read_avail,
    .read       = io_read,
    .write      = io_write,
    .millis     = io_millis,
    .ctx        = &g_ioctx
  };
  g_ioctx.A = A; g_ioctx.uart_idx = uart_idx;

  /* Use static storage to avoid heap usage on MCUs */
  konsole_init_with_storage(&g_ks, &g_line, &io,
                            g_cmds, sizeof(g_cmds)/sizeof(g_cmds[0]),
                            "> ", /*vt100*/ true);

#if defined(NEU_USE_SGFX)
  neu_gfx_init();
#endif

  /* let Radio module add its commands if compiled in */
  mod_radio_register(&g_ks);

  kon_banner(&g_ks, "Neutrino ready");
  return 0;
}

void neutrino_run(void) {
  konsole_poll(&g_ks);
  A->delay_ms(1);
}
