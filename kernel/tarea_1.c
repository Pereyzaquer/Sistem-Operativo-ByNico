#include "../inc/sys_tick.h"

#define TASK_1_MEMORY_CONTROL_ADDRESS_MIN   0x70A00000
#define TASK_1_MEMORY_CONTROL_ADDRESS_MAX   0x70A0FFFF
#define WORK_SPACE_SIZE                     TASK_1_MEMORY_CONTROL_ADDRESS_MAX - TASK_1_MEMORY_CONTROL_ADDRESS_MIN

/*
 * brief:   8 Ticks, Incremento de variable local y otra global
 * Variable global: ubicada en la segunda posicion de memoria
*/
__attribute__((section(".txt_tarea_1"))) void Tarea_1(void)
{
    //uint32_t save_vector[WORK_SPACE_SIZE+1];
    uint32_t volatile *addres = (uint32_t *)TASK_1_MEMORY_CONTROL_ADDRESS_MIN;
    uint32_t volatile indice = 0;

    while(1)
    {
        if(indice <= WORK_SPACE_SIZE/4)
        {
            if (*(addres + indice) == 0x55AA55AA)
            {
                *(addres + indice) = 0xAA55AA55;
            }
            else
            {
                *(addres + indice) = 0x55AA55AA;
            }

            syscall_send_pipeline(indice);
            
            indice++;
        }
        else addres = (uint32_t *)TASK_1_MEMORY_CONTROL_ADDRESS_MIN;

        asm volatile("wfi");
    }
}

/*
 * brief:   Realiza una syscall para que escriba el valor en el sp de svc para comunicarlo con otra tarea que comparte el mismo sp.
 *          En caso que se necesite todas la funciones salvo idel utilizan el mismo sp por lo cual
 *          se podria utilizar como pipe line.
*/
__attribute__((section(".txt_tarea_1"))) void syscall_send_pipeline(uint32_t val)
{
    register uint32_t r0 __asm__("r0") = val;
    register uint32_t r7 __asm__("r7") = 0;
    asm volatile ("svc #0" : : "r"(r0), "r"(r7) : "memory");
}

