#include <Arduino.h>

extern "C" {
  int  neutrino_init(void);
  void neutrino_run(void);
}

void setup() { neutrino_init(); }
void loop()  { neutrino_run(); }
