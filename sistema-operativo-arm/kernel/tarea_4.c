#include "../inc/sys_tick.h"

/*
 * brief:   Espera una nueva interupcion
*/
__attribute__((section(".idle_text"))) void _idle(void)
{
    while(1)
    {
        asm volatile("wfi");
    }
}