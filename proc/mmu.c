#include "../inc/mmu.h"
#include "../inc/sys_tick.h"
#include <string.h>

//-------------------------------------------------
// Variables utilizadas para la paginacion
//-------------------------------------------------

// Tabla para el proceso IDLE
__attribute__((section(".l2_tables_idel"), aligned(1024))) volatile uint32_t l2_table_idle[CANT_L2_TABLES][L2_ENTRIES];

// Tabla para la tarea 0
__attribute__((section(".l2_tables_task1"), aligned(1024))) volatile uint32_t l2_table_task1[CANT_L2_TABLES][L2_ENTRIES];

// Tabla para la tarea 1
__attribute__((section(".l2_tables_task2"), aligned(1024))) volatile uint32_t l2_table_task2[CANT_L2_TABLES][L2_ENTRIES];


// Cuento que tablas L2 tengo ocupadas
__attribute__((section(".kernel_data"))) uint32_t volatile l2_register_count[CANT_TASK] = {0};


//-------------------------------------------------
// FUNCIONES DE PAGINACION
//-------------------------------------------------

/************************************************
 * Función para escribir en el registro TTBR0
 * (establece la base de la tabla de primer nivel)
 ************************************************/
__attribute__((section(".kernel_text"))) void escribir_ttbr0(uint32_t addr)// La coloco en la seccion de tareas para ternla ya mapeada
{
    asm volatile (
        "MCR p15, 0, %0, c2, c0, 0\n" // Escribe nuevo_ttbr0 en TTBR0
        :: "r" (addr)
        : "memory"
    );
}

/************************************************
 * Función para escribir en el registro DACR
 * Define los permisos de acceso. El valor n es 0 a 15.
 *      00 No Access.Su acceso generará Domain Fault. --> Todo acceso produce un error de permiso.
 *      01 Client*. --> Se respeta el acceso de acuerdo a los permisos en las tablas de paginación (AP bits).
 *      10 Reservado. Efecto impredecible. --> 	(No se usa).
 *      11 Manager*. --> Todo acceso permitido automáticamente (sin chequear AP bits).
 ************************************************/
__attribute__((section(".init"))) void escribir_dacr(uint32_t value)
{
    asm volatile ("MCR p15, 0, %0, c3, c0, 0" :: "r" (value));
}

/************************************************
 * Función para habilitar la MMU
 * (modifica el registro de control del sistema)
 ************************************************/
__attribute__((section(".init"))) void habilitar_mmu(void) {
    uint32_t sctlr;

    // Leer el registro SCTLR
    __asm__ volatile (
        "mrc p15, 0, %[sctlr], c1, c0, 0\n\t"
        : [sctlr] "=r" (sctlr)
    );

    // Establecer bit 0 (MMU enable)
    sctlr |= (1 << 0);

    // Escribir el nuevo valor de SCTLR
    __asm__ volatile (
        "mcr p15, 0, %[sctlr], c1, c0, 0\n\t"
        :
        : [sctlr] "r" (sctlr)
        : "memory"
    );
}

/************************************************
 * Inicializa las tablas de paginación:
 *  - Borra todas las entradas (pone 0)
 *  - Configura el mapeo de dos tablas de segundo nivel
 *  - Configura entradas en tablas de segundo nivel para mapear DIR_FISICA1 y DIR_FISICA2
 ************************************************/
/************************************************
 * Inicialización de tablas L1 y L2 (Idle)
 ************************************************/
__attribute__((section(".init"))) void init_ttbr0(void)
{
    
    //Tarea Idel ---------------------------------------------------------------------------
    paginacion(IDLE , F_ADDRS_HANDLERS  , V_ADDRS_HANDLERS  , CANT_HANDLERS, PRIVILEGE);
    l2_register_count[IDLE]++;
    paginacion(IDLE , F_ADDR_TIMER0     , V_ADDR_TIMER0     , CANT_TIME, PRIVILEGE);
    l2_register_count[IDLE]++;
    paginacion(IDLE , F_ADDR_GICC0      , V_ADDR_GICC0      , CANT_GIC, PRIVILEGE);
    l2_register_count[IDLE]++;
    paginacion(IDLE , F_ADDR_INIT       , V_ADDR_INIT       , CANT_INIT, PRIVILEGE);
    paginacion(IDLE , F_ADDR_KERNEL     , V_ADDR_KERNEL     , CANT_KERNEL, PRIVILEGE);
    paginacion(IDLE , F_ADDR_IDLE       , V_ADDR_IDLE       , CANT_IDLE, PRIVILEGE);
    paginacion(IDLE , F_STACK_IDLE       , V_STACK_IDLE       , CANT_STACK_IDLE, PRIVILEGE);
    l2_register_count[IDLE]++;

    // Pagino las tablas de segundo nivel

    //Tarea 1 ---------------------------------------------------------------------------
    paginacion(TASK1    , F_ADDRS_HANDLERS          , V_ADDRS_HANDLERS  , CANT_HANDLERS, PRIVILEGE);
    l2_register_count[TASK1]++;
    paginacion(TASK1    , F_ADDR_TIMER0             , V_ADDR_TIMER0     , CANT_TIME, PRIVILEGE);
    l2_register_count[TASK1]++;
    paginacion(TASK1    , F_ADDR_GICC0              , V_ADDR_GICC0      , CANT_GIC, PRIVILEGE);
    l2_register_count[TASK1]++;
    paginacion(TASK1    , F_ADDR_INIT               , V_ADDR_INIT       , CANT_INIT, PRIVILEGE);
    //paginacion(TASK1    , F_GENERAL_STAK_ADDR       , V_GENERAL_STAK_ADDR     , CANT_GENERAL_STACK, PRIVILEGE);
    paginacion(TASK1    , F_ADDR_KERNEL             , V_ADDR_KERNEL     , CANT_KERNEL, PRIVILEGE);
    l2_register_count[TASK1]++;
    paginacion(TASK1    , F_ADDR_TASK1_SPACE_WORK   ,V_ADDR_TASK1_SPACE_WORK, SPACE_WORK_TASK1, USER);
    l2_register_count[TASK1]++;
    paginacion(TASK1    , F_ADDR_TASK1              , V_ADDR_TASK1      , CANT_TASK1, USER);
    paginacion(TASK1    , F_STACK_TASK1_USR         ,V_STACK_TASK1_USR  , STACK_SIZE_TASK1, USER);
    paginacion(TASK1    , F_STACK_TASK1_PRI         ,V_STACK_TASK1_PRI  , STACK_SIZE_TASK1, PRIVILEGE);
    l2_register_count[TASK1]++;

    //Tarea 2 ---------------------------------------------------------------------------
    paginacion(TASK2    , F_ADDRS_HANDLERS  , V_ADDRS_HANDLERS, CANT_HANDLERS, PRIVILEGE);
    l2_register_count[TASK2]++;
    paginacion(TASK2    , F_ADDR_TIMER0     , V_ADDR_TIMER0 , CANT_TIME, PRIVILEGE);
    l2_register_count[TASK2]++;
    paginacion(TASK2    , F_ADDR_GICC0      , V_ADDR_GICC0  , CANT_GIC, PRIVILEGE);
    l2_register_count[TASK2]++;
    paginacion(TASK2    , F_ADDR_INIT       , V_ADDR_INIT   , CANT_INIT, PRIVILEGE);
    //paginacion(TASK1    , F_GENERAL_STAK_ADDR       , V_GENERAL_STAK_ADDR     , CANT_GENERAL_STACK, PRIVILEGE);
    paginacion(TASK2    , F_ADDR_KERNEL     , V_ADDR_KERNEL , CANT_KERNEL, PRIVILEGE);
    l2_register_count[TASK2]++;
    paginacion(TASK2    , F_ADDR_TASK2_SPACE_WORK,V_ADDR_TASK2_SPACE_WORK, SPACE_WORK_TASK2, USER);
    l2_register_count[TASK2]++;
    paginacion(TASK2    , F_ADDR_TASK2      , V_ADDR_TASK2  , CANT_TASK2, USER);
    paginacion(TASK2    , F_STACK_TASK2_USR         ,V_STACK_TASK2_USR  , STACK_SIZE_TASK2, USER);
    paginacion(TASK2    , F_STACK_TASK2_PRI         ,V_STACK_TASK2_PRI  , STACK_SIZE_TASK2, PRIVILEGE);
    l2_register_count[TASK2]++;

}

/************************************************
 * Función para escribir un valor de prueba en DIR_FISICA2
 * (para validar luego que el mapeo esté funcionando)
 ************************************************/
__attribute__((section(".init"))) void escribir_valor_prueba(void)
{
    volatile uint32_t *addr = (volatile uint32_t *)DIR_FISICA2;
    *addr = 0x12345678; // Escribe un valor conocido
}

/************************************************
 * AFE (Access Flag Enable) es una opción para que el sistema valide los permisos de acceso.
*************************************************/
__attribute__((section(".init"))) void habilitar_afe(void) {
    uint32_t sctlr;

    // Leer el registro SCTLR
    __asm__ volatile (
        "mrc p15, 0, %[sctlr], c1, c0, 0\n\t"
        : [sctlr] "=r" (sctlr)
    );

    // Establecer bit 29 (AFE enable)
    sctlr |= (1 << 29);

    // Escribir el nuevo valor de SCTLR
    __asm__ volatile (
        "mcr p15, 0, %[sctlr], c1, c0, 0\n\t"
        :
        : [sctlr] "r" (sctlr)
        : "memory"
    );
}

/**
 * Paginación de dos niveles (ARMv7) con número limitado de tablas L2.
 * @param my_ttbr0       Índice de tarea (0=IDLE,1=TASK1,2=TASK2)
 * @param phys_addrs     Dirección física inicial a mapear
 * @param virtual_addrs  Dirección virtual inicial a mapear
 * @param cant           Cantidad de páginas contiguas (4 KiB) a mapear
 * @param flag           Indica si es para el usuario (USER) o para el kernel (PRIVILEGE)
 * @return               0 si se mapeó todo; nunca retorna si falla (wfi)
 */
__attribute__((section(".init"))) void paginacion(uint32_t task_id, uint32_t phys_addrs, uint32_t virtual_addrs, uint32_t cant, uint32_t flag)
{
    // Dirección base de la tabla L1 para esta tarea
    uint32_t* l1_table = MY_TTBR0(task_id);
    uint32_t volatile *l2_table;

    // Tabla L2 para esta tarea: es una matriz [L2_ENTRIES][CANT_L2_TABLES]
    switch (task_id)
    {
        case IDLE:
            l2_table = l2_table_idle[l2_register_count[IDLE]];
            break;
        case TASK1:
            l2_table = l2_table_task1[l2_register_count[TASK1]];
            break;
        case TASK2:
            l2_table = l2_table_task2[l2_register_count[TASK2]];
            break;
        default:
            while (1) asm volatile("wfi");  // Tarea no válida
    }

    for (uint32_t i = 0; i < cant; i++) {
        uint32_t vaddr = virtual_addrs + i * PAGE_SIZE;
        uint32_t paddr = phys_addrs + i * PAGE_SIZE;

        // Índices
        uint32_t l1_index = vaddr >> 20;              // Bits [31:20]
        uint32_t l2_index = (vaddr >> 12) & 0xFF;     // Bits [19:12]

        // Verifico los siguientes casos: id valido, espacio en tablas L2 y si se lleno la tabla L2
        if (task_id >= CANT_TASK || l2_register_count[task_id] >= CANT_L2_TABLES || i >= L2_ENTRIES) {
            while (1) asm volatile("wfi");  // Sin espacio en ninguna tabla L2
        }

        l1_table[l1_index] = ((uint32_t)&l2_table[0] & 0xFFFFFC00) | 0x1;

        switch (flag)
        {
        case PRIVILEGE:
            // Asignar entrada en la tabla L2 correspondiente
            l2_table[l2_index] = (paddr & 0xFFFFF000) | 0x12;
            break;
        case USER:
            l2_table[l2_index] = (paddr & 0xFFFFF000) | 0x32;
            break;
        default:
            l2_table[l2_index] = (paddr & 0xFFFFF000) | 0x32; //En caso de que no se especifique el flag, se asigna como USER
            break;
        }

    }
    return;  // Salir de la función si todo salió bien
}



/************************************************
 * Funcion desarrollada unicamente par colocar en el board init, engloba todo lo de la mmu 
*************************************************/
__attribute__((section(".init"))) void inicializar_mmu(void) 
{
    escribir_valor_prueba();         // Escribe un valor en memoria física antes de habilitar MMU

    init_ttbr0();       // Construir L1 y L2 para idle y el resto de las tareas

    escribir_valor_prueba();// Esto lo tengo que sacar despues lo dejo para tener un stop

    asm volatile(
        /* 1) Cargar TTBR0 = 0x70080000 */
        "LDR   r0, =0x70080000    \n\t"
        "MCR   p15, 0, r0, c2, c0, 0\n\t"
        /* 2) Poner DACR = 0xFFFFFFFF */
        "LDR   r0, =0x55555555    \n\t"
        "MCR   p15, 0, r0, c3, c0, 0\n\t"
        /* 3) Leer SCTLR, habilitar bit 0 y escribirlo */
        "MRC   p15, 0, r1, c1, c0, 0\n\t"
        "ORR   r1, r1, #1         \n\t"
        "MCR   p15, 0, r1, c1, c0, 0\n\t"
        :
        :
        : "r0", "r1", "memory"
    );
}

/**
 * Realiza una memcopy en todas las dirreciones LMA a VMA
 * Esta se hace cuando se tiene el codigo guardado en un memoria flash y lo queres traer a la memoria RAM
 * @note: Ya que tanto la ttbr0 como L2 son dirreciones que se desarrollan en el board_init no hay que realizar una distincion
 * entre LMA y VMA, sin embargo en el linker script aparece pero solo para definir un espacio de memoria para que se desarrolle.
 */
__attribute__((section(".init"))) void vma_copy(void)
{
    mi_memcpy((void*)_KERNEL_TXT_VMA,       (void*)&_KERNEL_TXT_LMA,        (uint32_t)&__kernel_size__);
    mi_memcpy((void*)_IDLE_TXT_VMA,         (void*)&_IDLE_TXT_LMA,          (uint32_t)&__idle_size__ );
    mi_memcpy((void*)_KERNEL_BSS_VMA,       (void*)&_KERNEL_BSS_LMA,        (uint32_t)&__kernel_bss_size__ );
    mi_memcpy((void*)_KERNEL_DATA_VMA,      (void*)&_KERNEL_DATA_LMA,       (uint32_t)&__kernel_data_size__);
    mi_memcpy((void*)_KERNEL_RODATA_VMA,    (void*)&_KERNEL_RODATA_LMA,     (uint32_t)&__kernel_rodata_size__ );
    mi_memcpy((void*)_TAREA_1_TXT_VMA,      (void*)&_TAREA_1_TXT_LMA,       (uint32_t)&__tarea_1_txt_size__);
    mi_memcpy((void*)_TAREA_1_BSS_VMA,      (void*)&_TAREA_1_BSS_LMA,       (uint32_t)&__tarea_1_bss_size__);
    mi_memcpy((void*)_TAREA_1_DATA_VMA,     (void*)&_TAREA_1_DATA_LMA,      (uint32_t)&__tarea_1_data_size__);
    mi_memcpy((void*)_TAREA_1_RODATA_VMA,   (void*)&_TAREA_1_RODATA_LMA,    (uint32_t)&__tarea_1_rodata_size__);
    mi_memcpy((void*)_TAREA_2_TXT_VMA,      (void*)&_TAREA_2_TXT_LMA,       (uint32_t)&__tarea_2_txt_size__);
    mi_memcpy((void*)_TAREA_2_BSS_VMA,      (void*)&_TAREA_2_BSS_LMA,       (uint32_t)&__tarea_2_bss_size__);
    mi_memcpy((void*)_TAREA_2_DATA_VMA,     (void*)&_TAREA_2_DATA_LMA,      (uint32_t)&__tarea_2_data_size__);
    mi_memcpy((void*)_TAREA_2_RODATA_VMA,   (void*)&_TAREA_2_RODATA_LMA,    (uint32_t)&__tarea_2_rodata_size__);

}

/*
 * Función para copiar memoria de un lugar a otro (No se porque pero memcopy no funcionaba correctamente)
 * @param destino: Puntero al destino donde se copiará la memoria
 * @param origen: Puntero al origen desde donde se copiará la memoria
 * @param n: Número de bytes a copiar
 * @return: Puntero al destino
*/
__attribute__((section(".init"))) void *mi_memcpy(void *destino, const void *origen, size_t n)
{
    unsigned char *d = (unsigned char *)destino;
    const unsigned char *s = (const unsigned char *)origen;

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return destino;
}
