#include "platform.h"

// Programs that do not install an interrupt handler still link the common
// trap entry. A real program supplies a strong machine_trap definition.
__attribute__((weak, noreturn)) void machine_trap(void) {
  test_finish(1);
}
