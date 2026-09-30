#include "../inc/irq_handler.h"
#include "../inc/sys_tick.h"
#include "../inc/gic.h"
#include "../inc/timer.h"

/*
 * brief: En esta funncion atiende los llamados de irq, aqui entre otras
 * funciones se deberia atender el systick
*/
//__irq


//LEER
// Para cambiar de tarea de manera correcta podriamos poner un return
// en esta funcion quedando el prototipo de la siguiente manera
// uint32_t * kernel_handler_irq(uint32_t *stackPointer)
// en el cual el programa devuelve el nuevo stack pointer
// el codigo en Assembly se tendria que modificar de la siguiente manera:
// MOV SP,R0 //Esto ira despues de volver


__attribute__((section(".kernel_text"))) uint32_t kernel_handler_irq(uint32_t actualSP)
{
    _gicc_t* const GICC0 = (_gicc_t*)GICC0_ADDR;
    _timer_t* const TIMER0 = ( _timer_t* )TIMER0_ADDR;

    uint32_t id = GICC0->IAR;

    uint32_t newSP;

    //El registro IAR nos brinda el numero del registro con mayor prioridad
    switch (id)
    {
        case TIMER_IRQ:
            TIMER0->Timer1IntClr = 0x01;    //Avisamos que atendimos la interrupcion
            newSP = scheduler(actualSP);
            break;
    
        default:
            break;
    }
    GICC0->EOIR = id;
    return newSP;
}