#include "../inc/board_init.h"
#include "../inc/sys_tick.h"
#include "../inc/mmu.h"

/*
 * brief: Incializa la board llamando a la funion __gic_init y __timer_init
*/
__attribute__((section(".init"))) void __board_init() //Da nombre del funcion y le da el atributo de .text
{
    vma_copy();                         // Copia las secciones de LMA a VMA
    __gic_init();
    __timer_init();
    initTask();
    inicializar_mmu();                  // inicializa la mmu
}

