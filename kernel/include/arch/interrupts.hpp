#ifndef OMEGA_HAL_INTERRUPTS_HPP
#define OMEGA_HAL_INTERRUPTS_HPP

#include "std/cstdint.hpp"

namespace hal {

void interrupts_init();
void timer_init(uint32_t frequency_hz);
void interrupts_enable();
void interrupts_disable();

} // namespace hal

#endif // OMEGA_HAL_INTERRUPTS_HPP
