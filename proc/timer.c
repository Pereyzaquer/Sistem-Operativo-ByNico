
#include "../inc/timer.h"

/*
 * brief: Incializa el timer
 * Los comentarios sacados de este archivo son sacados del pdf DDI0271
 * Para facilitar el chequeo de informacion se adjuntara al comentraio el
 * numero de hoja
*/
__attribute__((section(".init"))) void __timer_init()
    {
        //Apunto a TIMER0
        _timer_t* const TIMER0 = ( _timer_t* )TIMER0_ADDR;

//(pag 36) Este registro define a partir de que numero el contador debe disminuir una vez comenzado el modo periodico
        TIMER0->Timer1Load     = 0x00010000;

//(pag 38) Establece un bit counter operation de 32bits (0x2 -> 10)
        TIMER0->Timer1Ctrl     = 0x00000002;

//(pag 37) TimerMode --> Modo periodico (0x40 -> 1000000)
        TIMER0->Timer1Ctrl    |= 0x00000040;

//(pag 37) IntEnable --> Habilito la interrupcion del timer (0x20 -> 100000)
        TIMER0->Timer1Ctrl    |= 0x00000020;

//(pag 37) TimerEn --> Habilito el timer (0x80 -> 10000000)
        TIMER0->Timer1Ctrl    |= 0x00000080;
    }