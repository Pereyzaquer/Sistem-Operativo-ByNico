#include "../inc/sys_tick.h"

#define TASK_2_MEMORY_CONTROL_ADDRESS_MIN   0x70A10000
#define TASK_2_MEMORY_CONTROL_ADDRESS_MAX   0x70A1FFFF
#define WORK_SPACE_SIZE                     TASK_2_MEMORY_CONTROL_ADDRESS_MAX - TASK_2_MEMORY_CONTROL_ADDRESS_MIN

/*
 * brief:   12 Ticks, Decrementa de variable local y otra global
 * Variable local: ubicada enla terceraposicion de memoria
*/
__attribute__((section(".txt_tarea_2"))) void Tarea_2(void)
{
    uint32_t *addres = (uint32_t *)TASK_2_MEMORY_CONTROL_ADDRESS_MIN;
    uint32_t indice = 0;
    uint32_t recivo;

    while(1)
    {
        if(indice <= WORK_SPACE_SIZE/4)
        {
            recivo = syscall_recv_pipeline(); // Recibo el indice de la tarea 1
            if(recivo)
            {
                *(addres + indice) = recivo;
            }
            else
            {
                *(addres + indice) ^= 0xFFFFFFFF;
            }
            indice++;
        }
        else addres = (uint32_t *)TASK_2_MEMORY_CONTROL_ADDRESS_MIN;    // Una vez llega al final vuelve a comenzar
        asm volatile("wfi");
    }
}

/*
 * brief:   Realiza una syscall para que escriba el valor en el sp de svc para comunicarlo con otra tarea que comparte el mismo sp.
 *          En caso que se necesite todas la funciones salvo idel utilizan el mismo sp por lo cual
 *          se podria utilizar como pipe line.
*/
__attribute__((section(".txt_tarea_2"))) uint32_t syscall_recv_pipeline(void)
{
    register uint32_t r0 __asm__("r0"); // resultado
    register uint32_t r7 __asm__("r7") = 1;
    asm volatile ("svc #0" : "=r"(r0) : "r"(r7) : "memory");
    return r0;
}
