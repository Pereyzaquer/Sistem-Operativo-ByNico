#include <stdint.h>

#define TIMER_IRQ   0x24

__attribute__((section(".kernel_text"))) uint32_t kernel_handler_irq(uint32_t);
