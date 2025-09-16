#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int      (*init)(void);
  void     (*delay_ms)(uint32_t);
  uint32_t (*millis)(void);

  /* UART for console */
  int  (*uart_init)(int idx, uint32_t baud);
  int  (*uart_write)(int idx, const void* buf, size_t len);
  int  (*uart_read)(int idx, void* buf, size_t len); /* non-blocking, returns bytes read */
} arch_api_t;

/* Returns HAL vtable for the current architecture/port. */
const arch_api_t* arch_api(void);

#ifdef __cplusplus
}
#endif
