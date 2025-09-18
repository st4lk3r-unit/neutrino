
#include "konsole/konsole.h"

#if defined(NEU_USE_RADIOLIB)
#include <RadioLib.h>
#include "board.h"

static SX1276 radio = SX1276(
  RADIOLIB_SX127X_CS, RADIOLIB_SX127X_DIO0, RADIOLIB_SX127X_RST,
  RADIOLIB_SX127X_DIO1, RADIOLIB_SX127X_BUSY
);

static bool rl_ready = false;

static int rl_init_once(void) {
  if (rl_ready) return 0;
  int state = radio.begin(915.0); // default freq (override with 'rf freq')
  rl_ready = (state == RADIOLIB_ERR_NONE);
  return state;
}

static int cmd_rf(struct konsole *ks, int argc, char **argv) {
  if (!rl_ready && rl_init_once() != RADIOLIB_ERR_NONE) {
    kon_printf(ks, "radio init failed\r\n");
    return -1;
  }
  if (argc < 2) {
    kon_printf(ks, "rf freq <MHz> | tx <text> | rx <ms>\r\n");
    return 0;
  }
  if (strcmp(argv[1], "freq") == 0 && argc >= 3) {
    float mhz = atof(argv[2]);
    int st = radio.setFrequency(mhz);
    kon_printf(ks, (st==RADIOLIB_ERR_NONE) ? "ok\r\n" : "err %d\r\n", st);
    return 0;
  }
  if (strcmp(argv[1], "tx") == 0 && argc >= 3) {
    int st = radio.transmit(argv[2]);
    kon_printf(ks, (st==RADIOLIB_ERR_NONE) ? "tx ok\r\n" : "tx err %d\r\n", st);
    return 0;
  }
  if (strcmp(argv[1], "rx") == 0 && argc >= 3) {
    uint32_t ms = (uint32_t)strtoul(argv[2], NULL, 10);
    unsigned long t0 = millis();
    while (millis() - t0 < ms) {
      String str; int st = radio.receive(str);
      if (st == RADIOLIB_ERR_NONE) {
        kon_printf(ks, "rx: %s\r\n", str.c_str());
        break;
      }
      delay(5);
    }
    kon_printf(ks, "rx done\r\n");
    return 0;
  }
  kon_printf(ks, "bad args\r\n");
  return -1;
}

static const struct kon_cmd s_rf_cmds[] = {
  { "rf", "RadioLib demo: rf freq/tx/rx", cmd_rf },
};

extern "C" void mod_radio_register(struct konsole *ks) {
  kon_add_commands(ks, s_rf_cmds, sizeof(s_rf_cmds)/sizeof(s_rf_cmds[0]));
}
#else
extern "C" void mod_radio_register(struct konsole *ks) { (void)ks; }
#endif
