#include "platform.h"

// Programs that do not install an interrupt handler still link the common
// trap entry. A real program supplies a strong machine_trap definition.
__attribute__((weak)) uint32_t *machine_trap(uint32_t *frame) {
  (void)frame;
  test_finish(1);
}
