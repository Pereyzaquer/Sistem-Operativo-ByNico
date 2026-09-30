#include <stdint.h>
#include <stddef.h>

// Dado que se desconoce la velocidad del reloj normalizaremos todo a los siguientes valores en el cual 1 tick = 1ms
#define TICK_FRAME       100

#define TICK_T1     3
#define TICK_T2     3
//#define TICK_IDLE   TICK_FRAME-TICK_T1-TICK_T2
#define TICK_IDLE   3

#define NUM_TASKS   4

#define OFFSET      60
//#define OFFSET_LR   13
#define OFFSET_LR   1
//#define OFFSET_SPSR 14
#define OFFSET_SPSR 0

// Tamaño de la pipeline
#define PIPELINE_SIZE 16

#define SEND 0
#define RECV 1

//Tb hay que chequear raw data

typedef struct {
    uint32_t ticks;                 // Ticks que la tarea debe correr
    void (*taskFunction)(void);     // Función de la tarea
    uint32_t stack_irq;             // SP para modo IRQ
    uint32_t stack_usr;             // SP para modo SVC

    // Punteros resultantes tras init_task_paging
    uint32_t *pd;                   // Page Directory (L1)
    uint32_t *l2_ra;                // L2 read-only
    uint32_t *l2_cd;                // L2 read/write

    uint32_t sp;                    // Stack Pointer User
} TCB;

typedef struct {
    uint8_t currentTask;
    uint32_t tickCount;
} Sched_Manager;


// Funcion principal de manejo de interrupciones
__attribute__((section(".tarea_code"))) uint32_t scheduler(uint32_t);

// Tareas
__attribute__((section(".txt_tarea_1"))) void Tarea_1(void);
__attribute__((section(".txt_tarea_1"))) void syscall_send_pipeline(uint32_t mng);

__attribute__((section(".txt_tarea_2"))) void Tarea_2(void);
__attribute__((section(".txt_tarea_2"))) uint32_t syscall_recv_pipeline(void);

__attribute__((section(".idle_text"))) void _idle(void);

__attribute__((section(".init"))) void initTask(void);
__attribute__((section(".kernel_text"))) void write_sp_svc(uint32_t val);
__attribute__((section(".kernel_text"))) void write_sp_usr(uint32_t val);
__attribute__((section(".kernel_text"))) uint32_t read_sp_usr(void);
__attribute__((section(".kernel_text"))) uint32_t read_sp_svc(void);
__attribute__((section(".kernel_text"))) void write_sp_irq(uint32_t val);

__attribute__((section(".kernel_text"))) uint32_t syscall_pipeline_enqueue(uint32_t value);
__attribute__((section(".kernel_text"))) uint32_t syscall_pipeline_dequeue(void);

void init_task_paging(TCB *t,
    uint32_t vbase_ro, uint32_t pbase_ro, size_t size_ro,
    uint32_t vbase_rw, uint32_t pbase_rw, size_t size_rw,
    uint32_t vbase_stack, uint32_t pbase_stack, size_t size_stack);

void map_region(uint32_t *l2_table,
    uint32_t vbase, uint32_t pbase,
    size_t size, uint32_t flags);

//#define MASK_USER_CPSR  0x800001d2 ESTE ES EL VALOR QUE ESTABA ANTERIOREMTENTE en este define pero creo que no lo uso en otras cosas entonces lo saco
#define MASK_USER_CPSR  0x60000110      // Este es el valor que estaba en la variable del initTask() antes 
#define MASK_SYS_CPSR   0x6000011F