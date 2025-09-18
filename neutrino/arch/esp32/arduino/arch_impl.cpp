
#include <Arduino.h>
#include "neutrino/arch_api.h"
#include "board.h"

extern "C" {

static uint32_t start_ms = 0;

static int arduino_init(void) {
  start_ms = millis();
#ifdef NEU_UART_CONSOLE_IDX
  (void)Serial; // hint that we use Serial
  if (NEU_UART_CONSOLE_IDX == 0) {
    Serial.begin(NEU_UART_CONSOLE_BAUD);
    // Give USB CDC some time on S3 etc.
    unsigned long t0 = millis();
    while (!Serial && (millis() - t0) < 1500) { delay(10); }
  }
#endif
  return 0;
}

static void arduino_delay_ms(uint32_t ms) { delay(ms); }
static uint32_t arduino_millis(void) { return millis() - start_ms; }

static int arduino_uart_init(int idx, uint32_t baud) {
  (void)baud;
  if (idx == 0) {
    if (!Serial) Serial.begin(baud);
    return 0;
  }
  return -1;
}

static int arduino_uart_write(int idx, const void* buf, size_t len) {
  if (idx != 0 || !buf || !len) return 0;
  return (int)Serial.write((const uint8_t*)buf, len);
}

static int arduino_uart_read(int idx, void* buf, size_t len) {
  if (idx != 0 || !buf || !len) return 0;
  size_t n = 0;
  while (Serial.available() && n < len) {
    ((uint8_t*)buf)[n++] = (uint8_t)Serial.read();
  }
  return (int)n;
}

static const arch_api_t API = {
  .init      = arduino_init,
  .delay_ms  = arduino_delay_ms,
  .millis    = arduino_millis,
  .uart_init = arduino_uart_init,
  .uart_write= arduino_uart_write,
  .uart_read = arduino_uart_read,
};

const arch_api_t* arch_api(void) { return &API; }

} // extern "C"
