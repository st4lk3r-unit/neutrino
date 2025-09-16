#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stddef.h>
#include "neutrino/arch_api.h"

static uint64_t start_ms;

static uint32_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL));
}

static int posix_init(void) {
  start_ms = now_ms();
  return 0;
}

static void posix_delay_ms(uint32_t ms) { usleep(ms * 1000U); }
static uint32_t posix_millis(void) { return now_ms() - start_ms; }

static int posix_uart_init(int idx, uint32_t baud) { (void)idx; (void)baud; return 0; }

static int posix_uart_write(int idx, const void* buf, size_t len) {
  (void)idx;
  return (int)write(STDOUT_FILENO, buf, len);
}

static int posix_uart_read(int idx, void* buf, size_t len) {
  (void)idx;
  fd_set rfds;
  struct timeval tv = (struct timeval){0, 0}; /* non-blocking */
  FD_ZERO(&rfds);
  FD_SET(STDIN_FILENO, &rfds);
  int rv = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
  if (rv > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
    return (int)read(STDIN_FILENO, buf, len);
  }
  return 0;
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
