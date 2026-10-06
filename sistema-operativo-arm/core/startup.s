.global _start

//Tomo las dirrecciones de los handlers
.extern _reset_vector
.extern UND_handler
.extern SVC_handler
.extern PREFETCH_handler
.extern ABT_handler
.extern _reserved
.extern IRQ_handler
.extern FIQ_handler

//Tomo del memmap los valores del top del mmemap
.extern __irq_stack_top__
.extern __fiq_stack_top__
.extern __svc_stack_top__
.extern __abt_stack_top__
.extern __und_stack_top__
.extern __syc_stack_top__
.extern __user_stack_top__

//Para el cambio de contexto
.extern __usr_tarea1_top__

//Traigo la inicializacion del GIC y el timer
.extern __board_init

//Funcion final en C
.extern _idle
.extern Tarea_1

//Tabla de paginacion
.extern __page_table__

.section .startup_text
.code 32

.equ USR_MODE, 0x10     //User
.equ FIQ_MODE, 0x11     //FIQ
.equ IRQ_MODE, 0x12     //IRQ
.equ SVC_MODE, 0x13     //Supervisor
.equ ABT_MODE, 0x17     //Abort
.equ UND_MODE, 0x18     //Undefined
.equ SYS_MODE, 0x1F     //System
.equ I_BIT, 0x80        //Mask bit I    10000000
.equ F_BIT, 0x40        //Masl bit F    01000000

/*
Lo que tenemos que hacer con el table start es setear esa funcion en la direccion 0x00000000
Para que se vaya guardando en cada espacio de memoria la instruccion para llenar todas las interupciones
Si no entendiste preguntale a franco
 */
_table_start:
    LDR PC, add_reset_vector
    LDR PC, add_UND_handler
    LDR PC, add_SVC_handler
    LDR PC, add_PREFETCH_handler
    LDR PC, add_ABT_handler
    LDR PC, add_reserved
    LDR PC, add_IRQ_handler
    LDR PC, add_FIQ_handler

add_reset_vector:       .word _reset_vector
add_UND_handler:        .word UND_handler
add_SVC_handler:        .word SVC_handler
add_PREFETCH_handler:   .word PREFETCH_handler
add_ABT_handler:        .word ABT_handler
add_reserved:           .word _reserved
add_IRQ_handler:        .word IRQ_handler
add_FIQ_handler:        .word FIQ_handler

_start:

_table_copy:
    MOV R0, #0
    LDR R1,=_table_start
    LDR R2,=_start

_table_loop:
    LDR R3, [R1],#4
    STR R3, [R0],#4     //El sentido de movimiento de los datos en esta funcion es el parametro uno es el parametro destino, osea R0 es destin
                        // R3 = R1+4 ---> R3 = R1
                        // R0 = R3+4
    CMP R1, R2
    BNE _table_loop


_STACK_INIT:
//Inicializamos los stack pointer para los diferentes modos de funcionamiento
//Primero se establecen los modos de funcionamiento y interupcion (MSR)
// y luego se apunta a su determinado stack pointer (LDR)

//IRQ
    MSR cpsr_c, #(IRQ_MODE | I_BIT | F_BIT)
    LDR SP, =__irq_tarea1_top__
    //LDR r12, =__usr_tarea1_top__
    //stmfd   sp!, {r12}          //Guardo r12 en el sp de IRQ

//FIQ
    MSR cpsr_c, #(FIQ_MODE | I_BIT | F_BIT)
    LDR SP, =__fiq_stack_top__

//Abort
    MSR cpsr_c, #(ABT_MODE | I_BIT | F_BIT)
    LDR SP, =__abt_stack_top__

//Undefined
    MSR cpsr_c, #(UND_MODE | I_BIT | F_BIT)
    LDR SP, =__und_stack_top__

//System
    MSR cpsr_c, #(SYS_MODE | I_BIT | F_BIT)
    LDR SP, =__usr_tarea1_top__         //System y user comparten sp y en un inicio voy a utilizar este sp entonces lo dejo seteado

//Supervisor
    MSR cpsr_c, #(SVC_MODE | I_BIT | F_BIT)
    LDR SP, =__svc_stack_top__


//Inicializa GIC y el timer inicializando asi el stick
    LDR R10, =__board_init        
    MOV LR, PC              //Almaceno la dirrecion de retorno
    BX R10

//En a siguiente parte del codigo se modifica cpsr para habilitar las interrupciones
habilitar_interrupciones:
    MSR SPSR_cxsf, #USR_MODE     // Entrás al modo usuario

entro_al_ciclo:
    LDR R10, =Tarea_1        
    MOVS PC, R10

.end
