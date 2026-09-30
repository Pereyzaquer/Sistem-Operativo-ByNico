#include <stddef.h>
#include <stdint.h>

// Datos Basicos
// Tamaño de cada pagina 4096 bytes (0x1000)
// Tamaño de cada tabla de primer nivel (L1) 16 KB (0x4000)
    // 4096 entradas y 4 bytes
// Tamaño de cada tabla de segundo nivel (L2) 1 KB (0x400)
    // 256 entradas y 4 bytes

/*          Direccion virtual
| 31     20 | 19     12 | 11       0 |
|   L1 idx  |  L2 idx   | page offset|
*/

#define TASK1   0
#define TASK2   1
#define IDLE    2

#define CANT_TASK       3
#define L1_ENTRIES      4096
#define L2_ENTRIES      256
#define CANT_L2_TABLES  8   // Cantidad de tablas L2

// Cambie la dirreccion de los TTBR0 a mano para que tengan sendido con el scheduler
/* Original
    #define TTBR0_IDLE              0x70080000
    #define TTBR0_TASK1             0x70090000
    #define TTBR0_TASK2             0x700A0000
*/
#define TTBR0_TASK1             0x70080000
#define TTBR0_TASK2             0x70090000
#define TTBR0_IDLE              0x700A0000

#define BASE_L2_IDLE            0x70100000
#define BASE_L2_TASK1           0x70120000
#define BASE_L2_TASK2           0x70140000

#define TTBR0_OFFSET            0x10000
#define MY_TTBR0(n)      ((uint32_t*)(TTBR0_TASK1 + ((n) * TTBR0_OFFSET)))

#define PAGE_SIZE               0x1000 // 4 KiB

// Dirreciones a paginar
// IMPORTANTE: ya que tomamos valores bajos de paginacion y todos se encuentran en la misma tabala L2 solo tendremos un valor en la tabla L1
#define V_ADDRS_HANDLERS        F_ADDRS_HANDLERS        // Identity mapping     - Inidce L1: 0      -L2: 0
#define V_ADDR_TIMER0           F_ADDR_TIMER0           // Identity mapping     - Inidce L1: 256    -L2: 17
#define V_ADDR_GICC0            F_ADDR_GICC0            // Identity mapping     - Inidce L1: 480    -L2: 0
#define V_ADDR_GICD0            F_ADDR_GICD0            // Identity mapping     - Inidce L1: 480    -L2: 1
#define V_ADDR_INIT             F_ADDR_INIT             // Identity mapping     - Inidce L1: 1792   -L2: 16
#define V_GENERAL_STAK_ADDR     F_GENERAL_STAK_ADDR     // Identity mapping     - Inidce L1: 1792   -L2: 32
#define V_ADDR_KERNEL           F_ADDR_KERNEL           // Identity mapping     - Inidce L1: 1792   -L2: 34

#define V_ADDR_IDLE             F_ADDR_IDLE             // Identity mapping     - Inidce L1: 1792   -L2: 52
#define V_STACK_IDLE            F_STACK_IDLE            // Identity mapping     - Inidce L1: 1792   -L2: 68

#define V_ADDR_TASK1_SPACE_WORK F_ADDR_TASK1_SPACE_WORK // Identity mapping     - Inidce L1: 1802   -L2:
#define V_ADDR_TASK1            F_ADDR_TASK1            // Identity mapping     - Inidce L1: 1807   -L2: 80
#define V_STACK_TASK1_USR       F_STACK_TASK1_USR       // Identity mapping     - Inidce L1: 1807   -L2: 96
#define V_STACK_TASK1_PRI       F_STACK_TASK1_PRI       // Identity mapping     - Inidce L1: 1807   -L2: 112

#define V_ADDR_TASK2_SPACE_WORK F_ADDR_TASK2_SPACE_WORK // Identity mapping     - Inidce L1: 1802   -L2:
#define V_ADDR_TASK2            F_ADDR_TASK2            // Identity mapping     - Inidce L1: 1807   -L2: 64
#define V_STACK_TASK2_USR       F_STACK_TASK2_USR       // Identity mapping     - Inidce L1: 1807   -L2: 80
#define V_STACK_TASK2_PRI       F_STACK_TASK2_PRI       // Identity mapping     - Inidce L1: 1807   -L2: 96

// Esto me gustaria saber si lo puedo hacer de otra manera
//#define V_ADDR_IDLE             V_ADDRS_HANDLERS+PAGE_SIZE*(CANT_SHARED_PAGES)  // 0x00027000   // L1 index: 0   - L2 index: 39
//#define V_ADDR_TASK1            V_ADDRS_HANDLERS+PAGE_SIZE*(CANT_SHARED_PAGES+CANT_IDLE)    // 0x00037000   // L1 index: 0   - L2 index: 55
//#define V_ADDR_TASK2            V_ADDRS_HANDLERS+PAGE_SIZE*(CANT_SHARED_PAGES+CANT_IDLE+CANT_TASK1) // 0x00047000   // L1 index: 0   - L2 index: 71

// Direcciones fisicas a paginar
#define F_ADDRS_HANDLERS        0x00000000
#define F_ADDR_TIMER0           0x10011000
#define F_ADDR_GICC0            0x1E000000
#define F_ADDR_GICD0            0x1E001000
#define F_ADDR_INIT             0x70010000
#define F_GENERAL_STAK_ADDR     0x70020000      //Estos serian stacks que los mapeo con privilegio
#define F_ADDR_KERNEL           0x70022000

#define F_ADDR_IDLE             0x70034000
#define F_STACK_IDLE            0x70040000

#define F_ADDR_TASK1_SPACE_WORK 0x70A00000
#define F_ADDR_TASK1            0x70F50000
#define F_STACK_TASK1_USR       0x70F52000
#define F_STACK_TASK1_PRI       0x70F53000

#define F_ADDR_TASK2_SPACE_WORK 0x70A10000
#define F_ADDR_TASK2            0x70F40000
#define F_STACK_TASK2_USR       0x70F42000
#define F_STACK_TASK2_PRI       0x70F43000

// Cantidad de paginas a paginar
// Con los numeros estos nos dan
    // Idel:    55 paginas
    // Task1:   55 paginas
    // Task2:   55 paginas
#define CANT_HANDLERS           16      // de 0x00000000 a 0x00010000
#define CANT_TIMER0             1       // de 0x10011000 a 0x10012000
// Mapeo todos los timers
#define CANT_TIME               16      // de 0x10011000 a 0x1001F000 (En realidad necesita 14 pero mapeo de mas)
#define CANT_GICC0              1       // de 0x1E000000 a 0x1E001000
#define CANT_GICD0              1       // de 0x1E001000 a 0x1E017000
// Mapeo todos los gic que hay hasta los que no utilizo
#define CANT_GIC                64      // de 0x1E000000 a 0x1E040000


#define CANT_INIT               18      // de 0x70010000 a 0x70022000
#define CANT_GENERAL_STACK      1       // de 0x70020000 a 0x70021000 (Como se ve aca vuelvo a remapear pero con privilegio)
#define CANT_KERNEL             18      // de 0x70022000 a 0x70034000

#define CANT_IDLE               16      // de 0x70034000 a 0x70044000  --> 48 --> 0x70064000
#define CANT_STACK_IDLE         1

#define SPACE_WORK_TASK1        16      // de 0x70A00000 a 0x70A10000
#define CANT_TASK1              16      // de 0x70F50000 a 0x70F60000  --> 48 --> 0x70F80000
#define STACK_SIZE_TASK1        1

#define SPACE_WORK_TASK2        16      // de 0x70A10000 a 0x70A20000
#define CANT_TASK2              16      // de 0x70F40000 a 0x70F50000  --> 48 --> 0x70F70000
#define STACK_SIZE_TASK2        1

/************************************************
 * Flags de las entradas de tabla L2 (Small Page)
 ************************************************/
#define PRIVILEGE   0
#define USER        1
#define L2_SMALL_PAGE_FLAGS  ( (3 << 4) /* AP1-0 = 11: RW */ \
                             | (1 << 1) /* bit reservado debe ser 1 */ )

#define L2_USER_FLAG        ((3<<4)||(1<<1))
#define L2_KERNEL_FLAG      ((1<<4) | (1<<1))

//Dirrecion fisica de ejemplo
#define DIR_FISICA2          0x70F00000  // Físico mapeado por L2_2

// Prototipo de funciones
__attribute__((section(".kernel_text"))) void escribir_ttbr0(uint32_t addr);
__attribute__((section(".init"))) void escribir_dacr(uint32_t value);
__attribute__((section(".init"))) void habilitar_mmu(void);
__attribute__((section(".init"))) void init_ttbr0(void);
__attribute__((section(".init"))) void escribir_valor_prueba(void);
__attribute__((section(".init"))) void habilitar_afe(void);
__attribute__((section(".init"))) void paginacion (uint32_t my_ttbr0, uint32_t phys_addrs, uint32_t virtual_addrs, uint32_t cant, uint32_t flag);
__attribute__((section(".init"))) void inicializar_mmu(void);

__attribute__((section(".init"))) void vma_copy(void);
__attribute__((section(".init"))) void *mi_memcpy(void *destino, const void *origen, size_t n);


// Datos para inicilaizar la LMA y VMA
/* Kernel */
#define _KERNEL_BSS_VMA             ((void *)0x70022000)
#define _KERNEL_RODATA_VMA          ((void *)0x70023000)
#define _KERNEL_TXT_VMA             ((void *)0x70030000)
#define _KERNEL_DATA_VMA            ((void *)0x70032000)

/* Tareas */
#define _IDLE_TXT_VMA               ((void *)0x70034000)

#define _TAREA_1_TXT_VMA            ((void *)0x70F50000)
#define _TAREA_1_DATA_VMA           ((void *)0x70F51000)
#define _TAREA_1_STACK_INIT_VMA     ((void *)0x70F52000)
#define _TAREA_1_BSS_VMA            ((void *)0x70F53000)
#define _TAREA_1_RODATA_VMA         ((void *)0x70F54000)
#define _TAREA_1_READ_AREA_VMA      ((void *)0x70A00000)

#define _TAREA_2_TXT_VMA            ((void *)0x70F40000)
#define _TAREA_2_DATA_VMA           ((void *)0x70F41000)
#define _TAREA_2_STACK_INIT_VMA     ((void *)0X70F42000)
#define _TAREA_2_BSS_VMA            ((void *)0x70F43000)
#define _TAREA_2_RODATA_VMA         ((void *)0x70F44000)
#define _TAREA_2_READ_AREA_VMA      ((void *)0x70A10000)

extern void*     _KERNEL_TXT_LMA;
extern void*     __kernel_size__;

extern void*    _IDLE_TXT_LMA;
extern void*     __idle_size__;

extern void*    _KERNEL_BSS_LMA;
extern void*     __kernel_bss_size__;

extern void*    _KERNEL_DATA_LMA;
extern void*     __kernel_data_size__;

extern void*    _KERNEL_RODATA_LMA;
extern void*     __kernel_rodata_size__;

extern void*    _TAREA_1_TXT_LMA;
extern void*     __tarea_1_txt_size__;

extern void*    _TAREA_1_BSS_LMA;
extern void*     __tarea_1_bss_size__;

extern void*    _TAREA_1_DATA_LMA;
extern void*     __tarea_1_data_size__;

extern void*    _TAREA_1_RODATA_LMA;
extern void*     __tarea_1_rodata_size__;

extern void*    _TAREA_2_TXT_LMA;
extern void*     __tarea_2_txt_size__;

extern void*    _TAREA_2_BSS_LMA;
extern void*     __tarea_2_bss_size__;

extern void*    _TAREA_2_DATA_LMA;
extern void*     __tarea_2_data_size__;

extern void*    _TAREA_2_RODATA_LMA;
extern void*     __tarea_2_rodata_size__;