
#include "../inc/gic.h"

/*
 * brief: Incializa el GIC
 * Los comentarios de este archivo se basaron en los datos encontrados en
 * la pagina armDeveloper, luego de cada comentario se pondra un enlace de
 * donde se obtuvo la informacion
*/
__attribute__((section(".init"))) void __gic_init() //Da nombre del funcion y le da el atributo de .text
    {
//Apunto a GICC0 (GICD --> Generic Interrupt Contruller CPU)
        _gicc_t* const GICC0 = (_gicc_t*)GICC0_ADDR;
//Apunto a GICD0 (GICD --> Generic Interrupt Contruller Distributor)
        _gicd_t* const GICD0 = (_gicd_t*)GICD0_ADDR;

//En la siguiente mascara estableceremos el numero de prioridad minimo, osea todos los procesos que posean un numero menor seran atendidos por la interrupcion
//Dado que se eligio 0xF0 como mascara se comtara unicamente con 240 valores distintos de prioridades (0xF0 > 11110000)
//Link -> https://developer.arm.com/documentation/ihi0048/b/Programmers--Model/CPU-interface-register-descriptions/Interrupt-Priority-Mask-Register--GICC-PMR?lang=en
        GICC0->PMR  = 0x000000F0;

//(0x10 -> 10000) Habilito la interrupcion numero 36, segun ARM debeloper esta interrupcion corresponde al Timer 0 y 1
//Link -> https://developer.arm.com/documentation/ddi0595/2021-03/External-Registers/GICD-ISENABLER-n---Interrupt-Set-Enable-Registers?lang=en#fieldset_0-31_0
//Link -> https://developer.arm.com/documentation/dui0417/d/programmer-s-reference/generic-interrupt-controller--gic/interrupt-signals?lang=en
        GICD0->ISENABLER[1] |= 0x00000010;

//(0x10 -> 1000000000000) Habilito la interrupcion numero 44, segun ARM debeloper esta interrupcion corresponde al UART0
//Link ->  https://developer.arm.com/documentation/ddi0595/2021-03/External-Registers/GICD-ISENABLER-n---Interrupt-Set-Enable-Registers?lang=en#fieldset_0-31_0
//Link -> https://developer.arm.com/documentation/dui0417/d/programmer-s-reference/generic-interrupt-controller--gic/interrupt-signals?lang=en
        GICD0->ISENABLER[1] |= 0x00001000;

//Habilitacion de interrupciones por la interfaz de la cpu
//Link -> https://developer.arm.com/documentation/ihi0048/b/Programmers--Model/CPU-interface-register-descriptions/CPU-Interface-Control-Register--GICC-CTLR
        GICC0->CTLR         = 0x00000001;

//Habilitacion de interrupciones pendientes del distribuidor
//Link -> https://developer.arm.com/documentation/ihi0048/b/Programmers--Model/Distributor-register-descriptions/Distributor-Control-Register--GICD-CTLR?lang=en
        GICD0->CTLR         = 0x00000001;

    }