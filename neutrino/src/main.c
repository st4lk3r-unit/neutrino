#include "neutrino/neutrino.h"
int main(void) {
  neutrino_init();
  for(;;) neutrino_run();
  return 0;
}
