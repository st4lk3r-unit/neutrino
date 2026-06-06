
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
static uint32_t io_millis(void *ctx) {
  struct io_ctx *c = (struct io_ctx*)ctx;
  return c->A->millis();
}

/* ---- Console instance (declared early — used by SGFX and SIC blocks below) ---- */
static struct konsole       g_ks;
static struct kon_line_state g_line;
static struct io_ctx         g_ioctx;

/* ---- Keyboard ring buffer (UART + hardware kbd → konsole) ---- */
#define KBD_BUF 32
static uint8_t s_kbd_buf[KBD_BUF];
static int     s_kbd_head = 0, s_kbd_tail = 0;

static void kbd_push(uint8_t c) {
  int next = (s_kbd_head + 1) % KBD_BUF;
  if (next != s_kbd_tail) { s_kbd_buf[s_kbd_head] = c; s_kbd_head = next; }
}
static int kbd_avail(void) {
  return (s_kbd_head - s_kbd_tail + KBD_BUF) % KBD_BUF;
}
static int kbd_pop(void) {
  if (s_kbd_head == s_kbd_tail) return -1;
  uint8_t c = s_kbd_buf[s_kbd_tail];
  s_kbd_tail = (s_kbd_tail + 1) % KBD_BUF;
  return c;
}

static size_t io_read_avail(void *ctx) {
  (void)ctx; return 1024; /* non-blocking; io_read returns 0 if nothing available */
}

static size_t io_read(void *ctx, uint8_t *buf, size_t len) {
  /* drain keyboard ring buffer first, then fall back to UART */
  size_t n = 0;
  while (n < len && kbd_avail()) {
    int c = kbd_pop();
    if (c >= 0) buf[n++] = (uint8_t)c;
  }
  if (n < len) {
    struct io_ctx *cx = (struct io_ctx*)ctx;
    int r = cx->A->uart_read(cx->uart_idx, buf + n, len - n);
    if (r > 0) n += (size_t)r;
  }
  return n;
}

/* ---- Optional SGFX screen terminal ---- */
#if defined(NEU_USE_SGFX)
#  include "sgfx.h"
#  include "sgfx_port.h"

#  ifndef SGFX_SCRATCH_BYTES
#    define SGFX_SCRATCH_BYTES 2048
#  endif
   static sgfx_device_t s_gfx_dev;
   static uint8_t       s_gfx_scratch[SGFX_SCRATCH_BYTES];
   static int           gfx_init_ok = 0;

/* Scale for the splash banner */
#  ifndef NEU_GFX_TEXT_SCALE
#    if SGFX_W >= 320
#      define NEU_GFX_TEXT_SCALE 3
#    else
#      define NEU_GFX_TEXT_SCALE 2
#    endif
#  endif

/* Screen terminal geometry */
#  ifndef NEU_SCR_SCALE
#    define NEU_SCR_SCALE 2
#  endif
#  define SCR_CHAR_W  (6 * NEU_SCR_SCALE)   /* font advance_px × scale */
#  define SCR_CHAR_H  (8 * NEU_SCR_SCALE)   /* 7px glyph + 1px gap, scaled */
#  define SCR_COLS    (SGFX_W  / SCR_CHAR_W)
#  define SCR_ROWS    (SGFX_H  / SCR_CHAR_H)

   static char s_scr_buf[SCR_ROWS][SCR_COLS + 1];
   static int  s_scr_col = 0, s_scr_row = 0;
   static uint8_t s_scr_dirty[SCR_ROWS]; /* row needs hardware redraw */
   static uint32_t s_cursor_ms   = 0;    /* last cursor blink timestamp */
   static int      s_cursor_vis  = 1;    /* cursor currently visible */
   static int      s_cursor_prow = -1;   /* row cursor was on last flush */

   /* Minimal ANSI/VT100 escape-sequence strip state */
   typedef enum { SCR_NORM, SCR_ESC, SCR_CSI } scr_esc_t;
   static scr_esc_t s_esc = SCR_NORM;

   static void scr_redraw_line(int row) {
     int y = row * SCR_CHAR_H;
     sgfx_fill_rect(&s_gfx_dev, 0, y, SGFX_W, SCR_CHAR_H, (sgfx_rgba8_t){0,0,0,255});
     if (s_scr_buf[row][0])
       sgfx_text5x7_scaled(&s_gfx_dev, 0, y, s_scr_buf[row],
                           (sgfx_rgba8_t){200,200,200,255},
                           NEU_SCR_SCALE, NEU_SCR_SCALE);
     /* blinking cursor underline on the active row */
     if (row == s_scr_row && s_cursor_vis) {
       int cx = s_scr_col * SCR_CHAR_W;
       if (cx < SGFX_W)
         sgfx_fill_rect(&s_gfx_dev, cx, y + SCR_CHAR_H - 2, SCR_CHAR_W, 2,
                        (sgfx_rgba8_t){180,220,255,255});
     }
     s_scr_dirty[row] = 0;
   }

   static void scr_flush(void) {
     /* erase cursor from previous row when cursor moves to a new row */
     if (s_cursor_prow != s_scr_row) {
       if (s_cursor_prow >= 0 && s_cursor_prow < SCR_ROWS)
         s_scr_dirty[s_cursor_prow] = 1;
       s_cursor_prow = s_scr_row;
     }
     /* blink: toggle visibility every 500 ms */
     uint32_t now = A->millis();
     if (now - s_cursor_ms >= 500) {
       s_cursor_vis ^= 1;
       s_cursor_ms = now;
       s_scr_dirty[s_scr_row] = 1;
     }
     for (int r = 0; r < SCR_ROWS; r++)
       if (s_scr_dirty[r]) scr_redraw_line(r);
     sgfx_present(&s_gfx_dev);
   }

   static void scr_scroll(void) {
     memmove(s_scr_buf[0], s_scr_buf[1], (SCR_ROWS - 1) * (SCR_COLS + 1));
     memset(s_scr_buf[SCR_ROWS - 1], 0, SCR_COLS + 1);
     for (int i = 0; i < SCR_ROWS; i++) s_scr_dirty[i] = 1;
   }

   static void scr_newline(void) {
     s_scr_col = 0;
     if (++s_scr_row >= SCR_ROWS) { s_scr_row = SCR_ROWS - 1; scr_scroll(); }
   }

   static void scr_putc(char c) {
     if (s_esc == SCR_ESC) {
       s_esc = (c == '[') ? SCR_CSI : SCR_NORM;
       return;
     }
     if (s_esc == SCR_CSI) {
       if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~')
         s_esc = SCR_NORM;
       return;
     }
     if (c == '\033') { s_esc = SCR_ESC; return; }
     if (c == '\r')   { s_scr_col = 0;   return; }
     if (c == '\n')   { scr_newline();    return; }
     if (c == '\b' || c == 0x7f) {
       if (s_scr_col > 0) {
         s_scr_buf[s_scr_row][--s_scr_col] = '\0';
         s_scr_dirty[s_scr_row] = 1;
       }
       return;
     }
     if (c < 32 || c > 126) return;
     if (s_scr_col >= SCR_COLS) scr_newline();
     s_scr_buf[s_scr_row][s_scr_col++] = c;
     s_scr_buf[s_scr_row][s_scr_col]   = '\0';
     s_scr_dirty[s_scr_row] = 1;
   }

   static int neu_gfx_init(void) {
#if defined(SGFX_BUS_SPI)
     kon_printf(&g_ks, "[GFX] SPI sck=%d mosi=%d cs=%d dc=%d rst=%d bl=%d hz=%u\r\n",
       SGFX_PIN_SCK, SGFX_PIN_MOSI, SGFX_PIN_CS, SGFX_PIN_DC, SGFX_PIN_RST, SGFX_PIN_BL,
       (unsigned)SGFX_SPI_HZ);
#elif defined(SGFX_BUS_I2C)
     kon_printf(&g_ks, "[GFX] I2C sda=%d scl=%d rst=%d addr=0x%02x hz=%u\r\n",
       SGFX_PIN_SDA, SGFX_PIN_SCL, SGFX_PIN_RST, SGFX_I2C_ADDR, (unsigned)SGFX_I2C_HZ);
#else
     kon_printf(&g_ks, "[GFX] autoinit\r\n");
#endif
     int rc = sgfx_autoinit(&s_gfx_dev, s_gfx_scratch, sizeof s_gfx_scratch);
     kon_printf(&g_ks, "[GFX] autoinit rc=%d\r\n", rc);
     if (rc) return rc;
     /* Black screen, terminal starts clean */
     sgfx_clear(&s_gfx_dev, (sgfx_rgba8_t){0,0,0,255});
     memset(s_scr_buf, 0, sizeof(s_scr_buf));
     memset(s_scr_dirty, 0, sizeof(s_scr_dirty));
     s_scr_col = 0; s_scr_row = 0;
     gfx_init_ok = 1;
     return 0;
   }

   /* forward decl — neu_kbd_poll is defined in the SIC block below */
#if defined(NEU_USE_SIC)
   static void neu_kbd_poll(void);
#endif

   /* hue (0-255) → RGB for the demo animation */
   static void hue_rgb(uint8_t t, uint8_t *r, uint8_t *g, uint8_t *b) {
     uint8_t s = t % 85;
     uint16_t p = (uint16_t)s * 3;
     switch (t / 85) {
       case 0: *r=255;     *g=(uint8_t)p;       *b=0;            break;
       case 1: *r=(uint8_t)(255-p); *g=255;      *b=0;            break;
       case 2: *r=0;       *g=(uint8_t)(255-p);  *b=(uint8_t)p;   break;
       default:*r=(uint8_t)p; *g=0;             *b=255;           break;
     }
   }

   static int cmd_gfx(struct konsole *ks, int argc, char **argv) {
     if (!gfx_init_ok) { kon_printf(ks, "gfx: not initialized\r\n"); return -1; }

     /* "gfx" or "gfx info" — show configuration */
     if (argc < 2 || strcmp(argv[1], "info") == 0) {
       kon_printf(ks, "size   : %dx%d\r\n", SGFX_W, SGFX_H);
       kon_printf(ks, "term   : %d cols x %d rows  (scale %d)\r\n",
                  SCR_COLS, SCR_ROWS, NEU_SCR_SCALE);
#if defined(SGFX_BUS_SPI)
       kon_printf(ks, "bus    : SPI hz=%u\r\n", (unsigned)SGFX_SPI_HZ);
       kon_printf(ks, "pins   : sck=%d mosi=%d cs=%d dc=%d rst=%d bl=%d\r\n",
                  SGFX_PIN_SCK, SGFX_PIN_MOSI, SGFX_PIN_CS,
                  SGFX_PIN_DC,  SGFX_PIN_RST,  SGFX_PIN_BL);
#elif defined(SGFX_BUS_I2C)
       kon_printf(ks, "bus    : I2C addr=0x%02x hz=%u\r\n",
                  SGFX_I2C_ADDR, (unsigned)SGFX_I2C_HZ);
       kon_printf(ks, "pins   : sda=%d scl=%d rst=%d\r\n",
                  SGFX_PIN_SDA, SGFX_PIN_SCL, SGFX_PIN_RST);
#endif
       kon_printf(ks, "scratch: %d bytes\r\n", SGFX_SCRATCH_BYTES);
       return 0;
     }

     /* "gfx demo" — hue-plasma animation with FPS counter, any key exits */
     if (strcmp(argv[1], "demo") == 0) {
       { uint8_t tmp; while (A->uart_read(uart_idx, &tmp, 1) > 0) {} }

       uint32_t frames = 0, fps_disp = 0, fps_t0 = A->millis(), frame = 0;

       for (;;) {
         uint8_t ch;
         if (A->uart_read(uart_idx, &ch, 1) > 0) break;
#if defined(NEU_USE_SIC)
         neu_kbd_poll();
#endif
         if (kbd_avail()) { kbd_pop(); break; }

         /* status bar (top 10px): drawn FIRST so it's in the first SPI chunk,
          * plasma never overwrites this area → no flicker */
#        define DEMO_BAR_H 10
         sgfx_fill_rect(&s_gfx_dev, 0, 0, SGFX_W, DEMO_BAR_H, (sgfx_rgba8_t){0,0,0,255});
         { char buf[20]; int n = 0;
           uint32_t v = fps_disp;
           if (v == 0) { buf[n++]='0'; }
           else { char tmp2[12]; int tl=0; while(v){tmp2[tl++]='0'+(int)(v%10);v/=10;}
                  for(int i=tl-1;i>=0;i--) buf[n++]=tmp2[i]; }
           buf[n++]=' '; buf[n++]='f'; buf[n++]='p'; buf[n++]='s'; buf[n]=0;
           sgfx_text5x7_scaled(&s_gfx_dev, SGFX_W - n*6 - 1, 1, buf,
                               (sgfx_rgba8_t){255,255,0,255}, 1, 1); }

         /* plasma: horizontal strips starting BELOW the status bar */
         for (int y = DEMO_BAR_H; y < SGFX_H; y += 3) {
           uint8_t r, g, b;
           hue_rgb((uint8_t)((y * 3 / 2 + frame) & 0xFF), &r, &g, &b);
           sgfx_fill_rect(&s_gfx_dev, 0, y, SGFX_W, 3, (sgfx_rgba8_t){r,g,b,255});
         }

         sgfx_present(&s_gfx_dev);
         frame++; frames++;

         uint32_t now = A->millis(), dt = now - fps_t0;
         if (dt >= 1000) { fps_disp = frames * 1000 / dt; frames = 0; fps_t0 = now; }
       }

       /* restore terminal */
       sgfx_clear(&s_gfx_dev, (sgfx_rgba8_t){0,0,0,255});
       memset(s_scr_dirty, 1, sizeof s_scr_dirty);
       return 0;
     }

     kon_printf(ks, "usage: gfx [info|demo]\r\n");
     return -1;
   }
#endif /* NEU_USE_SGFX */

/* io_write: UART output + optional screen mirror */
static size_t io_write(void *ctx, const uint8_t *buf, size_t len) {
  struct io_ctx *c = (struct io_ctx*)ctx;
  int n = c->A->uart_write(c->uart_idx, buf, len);
#if defined(NEU_USE_SGFX)
  if (gfx_init_ok)
    for (size_t i = 0; i < len; i++) scr_putc((char)buf[i]);
#endif
  return (n > 0) ? (size_t)n : 0;
}

/* ---- SIC keyboard integration ---- */
#if defined(NEU_USE_SIC)
#  include <math.h>
#  include "sic/sic.h"
#  include "sic/audio/mic.h"    /* mic_t vtable */
#  include "sic/audio/amp.h"    /* amp_t vtable */
#  include "sic/bus/i2c_bus.h"  /* sic_i2c_writeread for raw register access */

   static void kbd_push_csi(char final) {
     kbd_push(0x1b); kbd_push('['); kbd_push((uint8_t)final);
   }

   static void kbd_push_key_event(const sic_key_event_t* ev) {
     if (!ev || !ev->pressed) return;

     /* Let SIC own all board-specific keymap, modifier, caps, and Fn logic.
      * Neutrino only translates abstract key events into console bytes. */
     switch (ev->code) {
       case SIC_KEY_BACKSPACE: kbd_push(0x08); return;
       case SIC_KEY_TAB:       kbd_push(0x09); return;
       case SIC_KEY_ENTER:     kbd_push(0x0d); return;
       case SIC_KEY_ESC:       kbd_push(0x1b); return;
       case SIC_KEY_LEFT:      kbd_push_csi('D'); return;
       case SIC_KEY_RIGHT:     kbd_push_csi('C'); return;
       case SIC_KEY_UP:        kbd_push_csi('A'); return;
       case SIC_KEY_DOWN:      kbd_push_csi('B'); return;
       case SIC_KEY_DEL:       kbd_push(0x1b); kbd_push('['); kbd_push('3'); kbd_push('~'); return;
       default: break;
     }

     if (ev->ascii) kbd_push((uint8_t)ev->ascii);
   }

   static void neu_kbd_poll(void) {
     sic_key_event_t ev;
     /* Drain a bounded burst so quick chords don't starve the console loop. */
     for (int i = 0; i < 8; ++i) {
       int rc = sic_key_poll(&ev);
       if (rc <= 0) break;
       kbd_push_key_event(&ev);
     }
   }

   /* kmap: raw keyboard debug — prints each key event until 'q' on UART */
   static int cmd_kmap(struct konsole *ks, int argc, char **argv) {
     (void)argc; (void)argv;
     /* I2C bus scan */
     { uint8_t addrs[16]; int n = sic_i2c_scan(addrs, 16);
       kon_printf(ks, "i2c scan: %d device(s)", n);
       for (int i = 0; i < n; i++) kon_printf(ks, " 0x%02x", addrs[i]);
       kon_printf(ks, "\r\n"); }
     const kscan_t* kbd = sic_kbd(0);
     if (!kbd) { kon_printf(ks, "no keyboard\r\n"); return -1; }

     /* drain any leftover bytes from the Enter keypress that launched this cmd */
     { uint8_t tmp; while (A->uart_read(uart_idx, &tmp, 1) > 0) {} }

     kon_printf(ks, "kbd=%p v=%p rb=%p km=%p impl=%p\r\n",
                (void*)kbd, (void*)kbd->v,
                (void*)kbd->v->read_bitmap,
                (void*)kbd->keymap,
                (void*)kbd->impl);
     kon_printf(ks, "kbd ok — press keys ('q' over UART to exit):\r\n");
     unsigned long long bm_prev = 0;
     int last_rc = 0;
     uint32_t heartbeat = A->millis();
     for (;;) {
       uint8_t ch;
       if (A->uart_read(uart_idx, &ch, 1) > 0 && ch == 'q') break;

       /* --- driver path --- */
       unsigned long long bm = 0;
       last_rc = kscan_read_bitmap(kbd, &bm);
       if (last_rc == 0) {
         unsigned long long pressed  = (bm ^ bm_prev) & bm;
         unsigned long long released = (bm ^ bm_prev) & bm_prev;
         bm_prev = bm;
         for (int b = 0; b < 64; b++) {
           if (pressed & (1ULL << b)) {
             char mapped = kbd->keymap ? kbd->keymap(b) : 0;
             kon_printf(ks, "PRESS   bit=%-2d mapped='%c' (0x%02x)\r\n",
                        b, mapped > 31 ? mapped : '.', (unsigned char)mapped);
           }
           if (released & (1ULL << b))
             kon_printf(ks, "RELEASE bit=%-2d\r\n", b);
         }
       }

       /* --- raw FIFO drain (bypass TCA8418 driver, verify I2C) --- */
       #define TCA8418_ADDR  0x34
       #define TCA8418_KEY_LCK_EC  0x03  /* event count register */
       #define TCA8418_KEY_EVENT_A 0x04  /* event FIFO register  */
       #define TCA8418_INT_STAT    0x02  /* interrupt status register */
       { uint8_t r = TCA8418_KEY_LCK_EC, ec = 0;
         sic_i2c_writeread(0, TCA8418_ADDR, &r, 1, &ec, 1);
         int cnt = (int)(ec & 0x0F);
         for (int i = 0; i < cnt; i++) {
           uint8_t er = TCA8418_KEY_EVENT_A, evt = 0;
           sic_i2c_writeread(0, TCA8418_ADDR, &er, 1, &evt, 1);
           int code  = (int)(evt & 0x7F);
           int press = (evt & 0x80) ? 1 : 0;
           char mapped = (code >= 1 && kbd->keymap) ? kbd->keymap(code - 1) : 0;
           if (press)
             kon_printf(ks, "RAW PRESS  code=%d bit=%d map='%c'(0x%02x)\r\n",
                        code, code - 1, mapped > 31 ? mapped : '.', (unsigned char)mapped);
           else
             kon_printf(ks, "RAW REL   code=%d\r\n", code);
         }
         if (cnt > 0) {
           uint8_t clr[2] = {TCA8418_INT_STAT, 0x01};
           sic_i2c_write(0, TCA8418_ADDR, clr, 2);  /* clear INT_STAT */
         }
       }
       #undef TCA8418_ADDR
       #undef TCA8418_KEY_LCK_EC
       #undef TCA8418_KEY_EVENT_A
       #undef TCA8418_INT_STAT

       /* heartbeat every 2 s */
       uint32_t now = A->millis();
       if ((now - heartbeat) >= 2000) {
         uint8_t reg, ec=0, istat=0;
         reg=0x02; sic_i2c_writeread(0,0x34,&reg,1,&istat,1);
         reg=0x03; sic_i2c_writeread(0,0x34,&reg,1,&ec,   1);
         kon_printf(ks, "... ISTAT=%02x EC=%02x bm=%016llx rc=%d\r\n",
                    istat, ec, bm, last_rc);
         heartbeat = now;
       }
       A->delay_ms(5);
     }
     kon_printf(ks, "done\r\n");
     return 0;
   }
   /* hw: list detected SIC hardware */
   static int cmd_hw(struct konsole *ks, int argc, char **argv) {
     (void)argc; (void)argv;
     static const struct { sic_func_id_t fn; const char* label; } kF[] = {
       { SIC_F_KSCAN,   "kbd    " }, { SIC_F_MIC,     "mic    " },
       { SIC_F_AMP,     "amp    " }, { SIC_F_PWR_SW,  "pwr_sw " },
       { SIC_F_CHARGER, "charger" }, { SIC_F_IR_TX,   "ir_tx  " },
       { SIC_F_SD,      "sd     " }, { SIC_F_ENCODER, "encoder" },
     };
     for (int i = 0; i < (int)(sizeof kF / sizeof kF[0]); i++) {
       int n = sic_count_fn(kF[i].fn);
       kon_printf(ks, "%s: ", kF[i].label);
       if (n == 0) { kon_printf(ks, "none\r\n"); continue; }
       for (int j = 0; j < n; j++)
         kon_printf(ks, "%s%s", sic_name_fn(kF[i].fn, j), j+1<n?", ":"");
       kon_printf(ks, "\r\n");
     }
     return 0;
   }

   /* mic: live VU meter (any key exits) */
   static int cmd_mic(struct konsole *ks, int argc, char **argv) {
     (void)argc; (void)argv;
     const mic_t* mic = sic_mic(0);
     if (!mic) { kon_printf(ks, "mic: not available\r\n"); return -1; }
     { uint8_t tmp; while (A->uart_read(uart_idx, &tmp, 1) > 0) {} }
     if (mic->v->start(mic, 16000) < 0) {
       kon_printf(ks, "mic: start failed\r\n"); return -1;
     }
     kon_printf(ks, "mic @ 16kHz — any key to exit\r\n");
     short buf[64];
     for (;;) {
       uint8_t ch;
       if (A->uart_read(uart_idx, &ch, 1) > 0) break;
       neu_kbd_poll(); if (kbd_avail()) { kbd_pop(); break; }
       int n = mic->v->read(mic, buf, 64);
       if (n <= 0) { A->delay_ms(10); continue; }
       int peak = 0;
       for (int i = 0; i < n; i++) {
         int v = buf[i] < 0 ? -buf[i] : buf[i]; if (v > peak) peak = v;
       }
       int bar = peak * 20 / 32767; if (bar > 20) bar = 20;
       kon_printf(ks, "\r[");
       for (int i = 0; i < 20; i++) kon_printf(ks, "%s", i < bar ? "#" : " ");
       kon_printf(ks, "] %5d", peak);
     }
     kon_printf(ks, "\r\ndone\r\n");
     return 0;
   }

   /* amp: play 1kHz tone for 500ms smoke test */
   static int cmd_amp(struct konsole *ks, int argc, char **argv) {
     (void)argc; (void)argv;
     const amp_t* amp = sic_amp(0);
     if (!amp || !amp->v) { kon_printf(ks, "amp: not available\r\n"); return -1; }
     kon_printf(ks, "amp: 1kHz 500ms... ");

     /* Keep codec/I2S details inside SIC.  Neutrino should only use the
      * abstract amp contract; board-specific routing belongs in SIC drivers.
      */
     if (amp->v->beep_ms) {
       int r = amp->v->beep_ms(amp, 500);
       kon_printf(ks, "%s\r\n", r >= 0 ? "off" : "failed");
       return r >= 0 ? 0 : -1;
     }

     if (amp->v->play_mono) {
       const int sr = 16000;
       const int total = sr / 2;  /* 500ms worth of samples */
       static int16_t buf[8000];
       float w = 2.0f * 3.14159265f * 1000.0f / (float)sr;
       for (int i = 0; i < total; i++)
         buf[i] = (int16_t)(sinf(w * i) * 12000);
       int n = amp->v->play_mono(amp, buf, total, sr);
       kon_printf(ks, "%s\r\n", n > 0 ? "off" : "failed");
       return n > 0 ? 0 : -1;
     }

     if (amp->v->enable) {
       amp->v->enable(amp, 1);
       A->delay_ms(500);
       amp->v->enable(amp, 0);
       kon_printf(ks, "toggle-only\r\n");
       return 0;
     }

     kon_printf(ks, "unsupported\r\n");
     return -1;
   }

   /* bat: battery voltage + charger state */
   static int cmd_bat(struct konsole *ks, int argc, char **argv) {
     (void)argc; (void)argv;
     sic_battery_t bat = {0.0f, -1};
     int r = sic_battery_read(&bat);
     if (r < 0) {
       kon_printf(ks, "battery: not available (rc=%d)\r\n", r);
     } else {
       int mv = (int)(bat.voltage_v * 1000.0f);
       kon_printf(ks, "battery: %d.%03dV  %d%%\r\n",
                  mv / 1000, mv % 1000, bat.percent);
     }
     const charger_t* chg = sic_charger(0);
     if (!chg) { kon_printf(ks, "charger: not available\r\n"); return 0; }
     static const char* kStates[] = {"not present","charging","full","fault"};
     int st = chg->v->get_state(chg->impl);
     kon_printf(ks, "charger: %s\r\n",
                (st >= 0 && st < 4) ? kStates[st] : "?");
     return 0;
   }

   /* ---- Rotary encoder: CW=history-up, CCW=history-down, btn=enter ---- */
   static int s_enc_btn_prev = 0;

   static void neu_enc_poll(void) {
     const encoder_t* enc = sic_encoder(0);
     if (!enc) return;

     int delta = enc->v->read_delta(enc);
     /* CW = up in history (\x1b[A), CCW = down (\x1b[B) */
     for (int i = 0; i < delta; i++)  { kbd_push(0x1b); kbd_push('['); kbd_push('A'); }
     for (int i = 0; i < -delta; i++) { kbd_push(0x1b); kbd_push('['); kbd_push('B'); }

     /* button: press edge → send Enter */
     int btn = enc->v->read_btn(enc);
     if (btn == 1 && s_enc_btn_prev == 0) kbd_push('\r');
     if (btn >= 0) s_enc_btn_prev = btn;
   }

   /* enc: live encoder delta display (any key exits) */
   static int cmd_enc(struct konsole *ks, int argc, char **argv) {
     (void)argc; (void)argv;
     const encoder_t* enc = sic_encoder(0);
     if (!enc) { kon_printf(ks, "encoder: not available\r\n"); return -1; }
     { uint8_t tmp; while (A->uart_read(uart_idx, &tmp, 1) > 0) {} }
     kon_printf(ks, "encoder test — turn knob, any key to exit\r\n");
     int total = 0;
     for (;;) {
       uint8_t ch;
       if (A->uart_read(uart_idx, &ch, 1) > 0) break;
       if (kbd_avail()) { kbd_pop(); break; }
       int d = enc->v->read_delta(enc);
       if (d) {
         total += d;
         kon_printf(ks, "\r  delta=%+d  total=%+d   ", d, total);
       }
       int btn = enc->v->read_btn(enc);
       if (btn == 1 && s_enc_btn_prev == 0)
         kon_printf(ks, "\r\n  [CLICK]\r\n");
       if (btn >= 0) s_enc_btn_prev = btn;
       A->delay_ms(5);
     }
     kon_printf(ks, "\r\ndone\r\n");
     return 0;
   }

#endif /* NEU_USE_SIC */

/* ---- Custom commands ---- */
static int cmd_echo(struct konsole *ks, int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    kon_printf(ks, "%s%s", argv[i], (i+1<argc)?" ":"");
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
    { "echo", "echo arguments",            cmd_echo },
    { "sys",  "system info",               cmd_sys  },
#if defined(NEU_USE_SGFX)
    { "gfx",  "gfx [info|demo]",          cmd_gfx  },
#endif
#if defined(NEU_USE_SIC)
    { "hw",   "list SIC hardware",         cmd_hw   },
    { "kmap", "raw keyboard debug",        cmd_kmap },
    { "mic",  "mic VU meter (key=exit)",   cmd_mic  },
    { "amp",  "amp enable test",           cmd_amp  },
    { "bat",  "battery + charger status",  cmd_bat  },
    { "enc",  "encoder live test",         cmd_enc  },
#endif
};

/* Optional registration of RadioLib CLI (implemented in C++ file) */
void mod_radio_register(struct konsole *ks);
__attribute__((weak)) void mod_radio_register(struct konsole *ks) { (void)ks; }

int neutrino_init(void) {
  A = arch_api();
  if (!A) return -1;
  if (A->init) A->init();

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

  konsole_init_with_storage(&g_ks, &g_line, &io,
                            g_cmds, sizeof(g_cmds)/sizeof(g_cmds[0]),
                            "> ", /*vt100*/ true);

#if defined(NEU_USE_SIC)
#  if defined(I2C_SDA_PIN) && defined(I2C_SCL_PIN)
  sic_i2c_begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  kon_printf(&g_ks, "[SIC] I2C bus 0 init SDA=%d SCL=%d\r\n", I2C_SDA_PIN, I2C_SCL_PIN);
#  endif
  { const sic_board_t* board = sic_board_default();
#  if defined(NEU_SIC_BOARD)
    if (!board) board = &NEU_SIC_BOARD;
#  endif
    int r = sic_begin_legacy(board, NULL);
    kon_printf(&g_ks, "[SIC] board=%s begin rc=%d kbd=%s\r\n",
               board ? board->name : "none", r, sic_kbd(0) ? "ok" : "NONE"); }
#endif

#if defined(NEU_USE_SGFX)
  neu_gfx_init();
#endif

  mod_radio_register(&g_ks);
  kon_banner(&g_ks, "Neutrino ready");
  return 0;
}

void neutrino_run(void) {
#if defined(NEU_USE_SIC)
  neu_kbd_poll();
  neu_enc_poll();
#endif
  konsole_poll(&g_ks);
#if defined(NEU_USE_SGFX)
  if (gfx_init_ok) scr_flush();
#endif
  A->delay_ms(1);
}
