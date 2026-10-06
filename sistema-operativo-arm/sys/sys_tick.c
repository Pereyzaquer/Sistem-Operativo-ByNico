#include "../inc/sys_tick.h"
#include "../inc/mmu.h"

// Traigo los sp creados en memmap para el manejo de memoria
extern const uint32_t  __usr_tarea1_top__;
extern const uint32_t  __usr_tarea2_top__;
//extern const uint32_t  __irq_tarea3_top__;
extern const uint32_t  __user_stack_top__;

extern const uint32_t  __irq_tarea1_top__;
extern const uint32_t  __irq_tarea2_top__;
//extern const uint32_t  __svc_tarea3_top__;
extern const uint32_t  __svc_idle_top__;
extern const uint32_t  __svc_stack_top__;
extern const uint32_t  __irq_stack_top__;

//volatile uint32_t globalVar1 __attribute__((section(".kernel_data"))) = 0;
//volatile uint32_t globalVar2 __attribute__((section(".kernel_data"))) = 0;

TCB task1 __attribute__((section(".kernel_data"))) = { .ticks = TICK_T1, .taskFunction = Tarea_1};
TCB task2 __attribute__((section(".kernel_data"))) = { .ticks = TICK_T2, .taskFunction = Tarea_2};
//TCB task3 __attribute__((section(".tarea_data"))) = { .ticks = TICK_T3, .taskFunction = Tarea_3};

TCB idleTask __attribute__((section(".kernel_data"))) = { .ticks = TICK_IDLE, .taskFunction = _idle};

//TCB* tasks[] __attribute__((section(".tarea_data"))) = { &task1, &task2, &task3, &idleTask };
TCB* tasks[] __attribute__((section(".kernel_data"))) = { &task1, &task2, &idleTask };

Sched_Manager sche __attribute__((section(".bss"))) ={.currentTask = 0, .tickCount = 0};

/*
 * brief: Handler del systick de Timer 0 y 1, en esta ocacion lo utilizaremos
 * como un scheduler.
 * Tarea 1:     8 Ticks, Incremento de variable local y otra global
 * Tarea 2:     12 Ticks, Decremento d una variable local y otra global
 * Tarea 3:     5 Ticks, Decrementa la variable de Tarea 1 y incrementa
 *              la variable de Tarea 2
 * Tarea _idle:  calcular Ticks
 * Tarea TET:   30 Ticks
 * 
*/
__attribute__((section(".kernel_text"))) uint32_t scheduler(uint32_t actualSP) //uint32_t*
{
    uint32_t newSP = actualSP; // Devuelvo el mimo sp por si no se cambia de tarea

    if (sche.tickCount >= tasks[sche.currentTask]->ticks)
    {
        // Guardo como variaron los sp
        tasks[sche.currentTask]->stack_usr = read_sp_usr();
        tasks[sche.currentTask]->stack_irq = actualSP;

        // Cambiar a la siguiente tarea
        sche.currentTask = (sche.currentTask + 1) % 3;
                
        //escribir_ttbr0((uint32_t)(uintptr_t)MY_TTBR0(sche.currentTask));             //Cambio la ttbr0
        asm volatile (
            "MCR p15, 0, %0, c2, c0, 0\n" // Escribe nuevo_ttbr0 en TTBR0
            :: "r" (MY_TTBR0(sche.currentTask))
            : "memory"
        );

        newSP = tasks[sche.currentTask]->stack_irq;
        write_sp_usr(tasks[sche.currentTask]->stack_usr); // Cambia el SP del modo Sys y User

        sche.tickCount = 0;
    }

    sche.tickCount++;

    return newSP;
}

/*
 * brief:       Por lo que lei en internet es un truco que se utiliza para el procesador cree que que vuelve del proceso que se encuentra en la
 *              primera ubicacion del sp. Por lo tanto luego de hacer el switch ctx y finalice la funcion el programa ira al nuevo programa.
 * ubicacion:   Dado a su funcionamiento se debe ejecutar dentro del switch context y luego de configurar el resto de las cosas.
 * initTask: prepara contextos (SPs y paginación) antes de arrancar scheduler
 */
__attribute__((section(".init"))) void initTask(void)
{
    // Inicial SP e SPSR en cada stack_irq
    //TCB* arr[NUM_TASKS] = { &task1, &task2, &task3, &idleTask };
    TCB* arr[NUM_TASKS] = { &task1, &task2, &idleTask };
    const uint32_t usr_tops[NUM_TASKS] = {
        (uint32_t)&__usr_tarea1_top__,
        (uint32_t)&__usr_tarea2_top__,
        //(uint32_t)&__irq_tarea3_top__,
        (uint32_t)&__svc_idle_top__
    };
    const uint32_t irq_tops[NUM_TASKS] = {
        (uint32_t)&__irq_tarea1_top__,
        (uint32_t)&__irq_tarea2_top__,
        //(uint32_t)&__svc_tarea3_top__,
        (uint32_t)&__irq_stack_top__
    };
    void (*funcs[NUM_TASKS])(void) = { Tarea_1, Tarea_2, _idle };

    for (int i = 0; i < 3; i++) {
        TCB* t = arr[i];                    //Creo q esta variable esta de mas
        t->stack_irq = irq_tops[i]- OFFSET;
        t->stack_usr = usr_tops[i];

        uint32_t *aux = (uint32_t*)t->stack_irq;
        *(aux + OFFSET_LR)  = (uint32_t)funcs[i];

        if(i == IDLE)
        {
            *(aux + OFFSET_SPSR)= MASK_SYS_CPSR;
        }
        else
        {
            *(aux + OFFSET_SPSR)= MASK_USER_CPSR;
        }
    }


}

/*
 * brief:       Esta funcion se utiliza para escribir el SP en modo usuario, dado que el sp de usuario es diferente al de IRQ y SVC
 *              por lo tanto se debe escribir en el registro SP del modo usuario.
 * ubicacion:   Dado a su funcionamiento se debe ejecutar dentro del switch context y luego de configurar el resto de las cosas.
 * @param val:   Valor que se escribira en el SP del modo usuario
*/  
__attribute__((section(".kernel_text"))) void write_sp_usr(uint32_t val)
{
    asm volatile("mrs r1, cpsr\n\t"
                 "cps #0x1F\n\t"
                 "mov sp, %0\n\t"
                 "msr cpsr_c, r1\n\t" ::"r"(val)
                 : "r1");
  }

/*
 * brief:       Esta funcion se utiliza para escribir el SP en modo SVC, dado que el sp de SVC es diferente al de IRQ y USER
 *              por lo tanto se debe escribir en el registro SP del modo SVC.
 * ubicacion:   Dado a su funcionamiento se debe ejecutar dentro del switch context y luego de configurar el resto de las cosas.
 * @param val: Valor que se escribira en el SP del modo SVC
*/
  __attribute__((section(".kernel_text"))) void write_sp_svc(uint32_t val)
{
    asm volatile("mrs r1, cpsr\n\t"
                 "cps #0x13\n\t"
                 "mov sp, %0\n\t"
                 "msr cpsr_c, r1\n\t" ::"r"(val)
                 : "r1");
}

/*
 * brief:       Esta funcion se utiliza para leer el SP del modo SVC, dado que el sp de SVC es diferente al de IRQ y USER
 *              por lo tanto se debe leer en el registro SP del modo SVC.
 * ubicacion:   Dado a su funcionamiento se debe ejecutar dentro del switch context y luego de configurar el resto de las cosas.
 * @return:     Valor del SP del modo SVC
*/
__attribute__((section(".kernel_text"))) uint32_t read_sp_svc(void)
{
    uint32_t val;
    asm volatile("mrs r1, cpsr\n\t"   // save current CPSR
                 "cps #0x13\n\t"      // switch to SVC mode
                 "mov %0, sp\n\t"     // read sp
                 "msr cpsr_c, r1\n\t" // restore CPSR
                 : "=r"(val)::"r1");
    return val;
}

/*
 * brief:       Esta funcion se utiliza para leer el SP del modo USER, dado que el sp de USER es diferente al de IRQ y SVC
 *              por lo tanto se debe leer en el registro SP del modo USER.
 * ubicacion:   Dado a su funcionamiento se debe ejecutar dentro del switch context y luego de configurar el resto de las cosas.
 * @return:     Valor del SP del modo USER
*/
__attribute__((section(".kernel_text"))) uint32_t read_sp_usr(void)
{
uint32_t val;
asm volatile("mrs r1, cpsr\n\t"   // save current CPSR
                "cps #0x1F\n\t"      // switch to SYSTEM mode (usr regs)
                "mov %0, sp\n\t"     // read sp_usr
                "msr cpsr_c, r1\n\t" // restore CPSR
                : "=r"(val)::"r1");
return val;
}

/*
 * brief:       Esta funcion se utiliza para escribir el SP del modo IRQ, dado que el sp de IRQ es diferente al de USER y SVC
 *              por lo tanto se debe escribir en el registro SP del modo IRQ.
 * ubicacion:   Dado a su funcionamiento se debe ejecutar dentro del switch context y luego de configurar el resto de las cosas.
 * @param val:   Valor que se escribira en el SP del modo IRQ
*/
__attribute__((section(".kernel_text"))) void write_sp_irq(uint32_t val)
{
    asm volatile(
        "mrs r1, cpsr\n\t"     // Guarda el CPSR actual
        "cps #0x12\n\t"        // Cambia a modo IRQ
        "mov sp, %0\n\t"       // Escribe el nuevo valor en SP (IRQ)
        "msr cpsr_c, r1\n\t"   // Restaura el CPSR anterior
        :
        : "r"(val)
        : "r1"
    );
}

//###############################################
// PIPELINE functions
//###############################################

uint32_t __attribute__((section(".bss"))) pipeline[PIPELINE_SIZE];
volatile uint32_t __attribute__((section(".bss"))) pipeline_head = 0;
volatile uint32_t __attribute__((section(".bss"))) pipeline_tail = 0;

/*
 * brief:       Esta funcion se utiliza para encolar un valor en la pipeline.
 *              Si la pipeline esta llena devuelve 0, si no devuelve 1.
 * @param value: Valor que se encolara
 * @return:     1 si se pudo encolar, 0 si estaba llena
*/
__attribute__((section(".kernel_text"))) uint32_t syscall_pipeline_enqueue(uint32_t value)
{
    uint32_t next = (pipeline_tail + 1) % PIPELINE_SIZE;
    if (next > PIPELINE_SIZE) return 0; // full
    pipeline[pipeline_tail] = value;
    pipeline_tail = next;
    return 1;
}

/*
 * brief:       Esta funcion se utiliza para desencolar un valor de la pipeline.
 *              Si la pipeline esta vacia devuelve 0, si no devuelve 1 y el valor desencolado en out.
 * @param out:   Puntero donde se guardara el valor desencolado
 * @return:     1 si se pudo desencolar, 0 si estaba vacia
*/
__attribute__((section(".kernel_text"))) uint32_t syscall_pipeline_dequeue(void)
{
    if (pipeline_head == pipeline_tail) return 0; // vacío

    pipeline_head = (pipeline_head + 1) % PIPELINE_SIZE;
    return pipeline[pipeline_head-1];;
}

/*
 * @brief Tabla de despacho de syscalls para SVC
 * @return El valor de retorno depende de la syscall invocada
 */
__attribute__((section(".kernel_text")))  uint32_t svc_table(void)
{
    register uint32_t r0 __asm__("r0"); // argumento
    register uint32_t r7 __asm__("r7"); // syscall id

    switch (r7)
    {
    case SEND:
        return syscall_pipeline_enqueue(r0);

    case RECV:
        return syscall_pipeline_dequeue();

    default:
        return 0xFFFFFFFF; // syscall inválida
    }
}