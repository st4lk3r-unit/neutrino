
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "neutrino/arch_api.h"

static uint32_t start_ms;

static uint32_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL));
}

static struct termios orig_tio;
static int tty_raw = 0;

static void set_stdin_raw(void) {
  if (tty_raw) return;
  struct termios tio;
  if (tcgetattr(STDIN_FILENO, &orig_tio) == 0) {
    tio = orig_tio;
    cfmakeraw(&tio);
    tcsetattr(STDIN_FILENO, TCSANOW, &tio);
    tty_raw = 1;
  }
}
static void restore_stdin(void) {
  if (tty_raw) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_tio);
    tty_raw = 0;
  }
}

static int posix_init(void) {
  start_ms = now_ms();
  set_stdin_raw();
  atexit(restore_stdin);
  return 0;
}

static void posix_delay_ms(uint32_t ms) {
  struct timespec ts;
  ts.tv_sec  = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

static uint32_t posix_millis(void) {
  return now_ms() - start_ms;
}

static int posix_uart_init(int idx, uint32_t baud) {
  (void)idx; (void)baud;
  // Using STDIN/STDOUT as "UART0"
  return 0;
}

static int posix_uart_write(int idx, const void* buf, size_t len) {
  (void)idx;
  if (!buf || !len) return 0;
  ssize_t w = write(STDOUT_FILENO, buf, len);
  return (int)(w < 0 ? 0 : w);
}

static int posix_uart_read(int idx, void* buf, size_t len) {
  (void)idx;
  if (!buf || !len) return 0;
  // Non-blocking read using select()
  fd_set rfds;
  struct timeval tv = {0, 0};
  FD_ZERO(&rfds);
  FD_SET(STDIN_FILENO, &rfds);
  int r = select(STDIN_FILENO+1, &rfds, NULL, NULL, &tv);
  if (r <= 0) return 0;
  ssize_t n = read(STDIN_FILENO, buf, len);
  return (int)(n < 0 ? 0 : n);
}

static const arch_api_t API = {
  .init      = posix_init,
  .delay_ms  = posix_delay_ms,
  .millis    = posix_millis,
  .uart_init = posix_uart_init,
  .uart_write= posix_uart_write,
  .uart_read = posix_uart_read,
};

const arch_api_t* arch_api(void) { return &API; }
