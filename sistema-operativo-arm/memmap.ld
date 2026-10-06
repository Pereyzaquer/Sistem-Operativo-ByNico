
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_reset_vector)

/* Definiciones de simbolos*/

/* Tamaño de los stacks del kernel*/
IRQ_STACK_SIZE = 1024;
USER_STACK_SIZE  = 1024;
SYC_STACK_SIZE   = 512;
FIQ_STACK_SIZE = 512;
SVC_STACK_SIZE = 512;
ABT_STACK_SIZE = 512;
UND_STACK_SIZE = 512;
SYS_STACK_SIZE = 512;

PIPE_LINE_STACK = 16;

STACK_IRQ_TAREA_1_SIZE = 0x1000;
STACK_IRQ_TAREA_2_SIZE = 0x1000;
STACK_IRQ_TAREA_3_SIZE = 512;
STACK_IRQ_IDLE_SIZE = 512;

STACK_USR_TAREA_1_SIZE = 0x1000;
STACK_USR_TAREA_2_SIZE = 0x1000;
STACK_SVC_TAREA_3_SIZE = 512;
STACK_USR_IDLE_SIZE = 512;

TABLE_L2_SIZE = 0x2000;

/* ------------------ Adress ------------------ */

/* --------- PHY --------- */
_PUBLIC_RAM_DEST        = 0x00000000;
_PUBLIC_RAM_INIT        = 0x70010000;
_PUBLIC_STACK_INIT      = 0x70020000;

/* Kernel */
_KERNEL_BSS_PHY         = 0x70022000;
_KERNEL_RODATA_PHY      = 0x70023000;
_KERNEL_TXT_PHY         = 0x70030000;
_KERNEL_DATA_PHY        = 0x70032000;

/* Tareas */
_IDLE_TXT_PHY         = 0x70034000;
_IDLE_STACK_INIT_     = 0x70040000;

_TAREA_1_TXT_PHY            = 0x80750000;
_TAREA_1_DATA_PHY           = 0x80751000;

_TAREA_1_BSS_PHY            = 0x80753000;
_TAREA_1_RODATA_PHY         = 0x80754000;
_TAREA_1_READ_AREA_PHY      = 0x80000000;

_TAREA_2_TXT_PHY            = 0x80740000;
_TAREA_2_DATA_PHY           = 0x80741000;

_TAREA_2_BSS_PHY            = 0x80743000;
_TAREA_2_RODATA_PHY         = 0x80744000;
_TAREA_2_READ_AREA_PHY      = 0x80010000;

/* Tablas de paginación */
_TTBR0_KERNEL           = 0x70080000;
_TTBR0_TAREA_1          = 0x70090000;
_TTBR0_TAREA_2          = 0x700A0000;

/* --------- VMA --------- */

/* Kernel */
_KERNEL_BSS_VMA         = 0x70022000;
_KERNEL_RODATA_VMA      = 0x70023000;
_KERNEL_TXT_VMA         = 0x70030000;
_KERNEL_DATA_VMA        = 0x70032000;

/* Tareas */
_IDLE_TXT_VMA     = 0x70034000;

_TAREA_1_TXT_VMA        = 0x70F50000;
_TAREA_1_DATA_VMA       = 0x70F51000;
_TAREA_1_STACK_INIT_    = 0x70F52000;
_TAREA_1_BSS_VMA        = 0x70F53000;
_TAREA_1_RODATA_VMA     = 0x70F54000;
_TAREA_1_READ_AREA_VMA  = 0x70A00000;

_TAREA_2_TXT_VMA        = 0x70F40000;
_TAREA_2_DATA_VMA       = 0x70F41000;
_TAREA_2_STACK_INIT_    = 0X70F42000;
_TAREA_2_BSS_VMA        = 0x70F43000;
_TAREA_2_RODATA_VMA     = 0x70F44000;
_TAREA_2_READ_AREA_VMA  = 0x70A10000;

MEMORY
{
    public_ram           : org = _PUBLIC_RAM_INIT, len = 512M        /* va de 0x70010000 a 0x8FFFFFFF*/
    public_stack         : org = _PUBLIC_STACK_INIT, len = 0x10000    /* va de 0x70020000 a 0x70020FFF */

    stack_idle           : org = _IDLE_STACK_INIT_, len = 4K
    stack_tarea1         : org = _TAREA_1_STACK_INIT_, len = 8K
    stack_tarea2         : org = _TAREA_2_STACK_INIT_, len = 8K
}

/* Definicion de las secciones */
SECTIONS
{
    . = _PUBLIC_RAM_INIT;
    /* ---------- Reset e Inicialisaciones ---------- */
    .text : 
    {               
        *(.reset_vector_text*)   /*el uso del `*` es como comodin osea usa todos los archivos q empiezan con text*/
        *(.startup_text*)   /*el uso del `*` es como comodin osea usa todos los archivos q empiezan con text*/
        *(.text)
        *(.init)
    } > public_ram
    __reset_size__ = SIZEOF(.text);

    .data : 
    {   /* Datos y variables globales inicializadas */
        *(.data*) 
    } > public_ram

    .bss (NOLOAD) : 
    {    /* Datos y variables globales no inicializadas */
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        __bss_end__ = .;
    } > public_ram

    /* ------------------------------ Kernel ------------------------------ */
    /* Código de kernel, quiero que este compacto por eso esta el KERMEL_LMA */    
    _KERNEL_TXT_LMA = .;
    .kernel_text _KERNEL_TXT_VMA : AT (_KERNEL_TXT_LMA)
    { 
        *(.kernel_text*);
        . = ALIGN(4); 
    } > public_ram
    __kernel_size__ = SIZEOF(.kernel_text);

    /* Código de tarea idle */    
    _IDLE_TXT_LMA = __kernel_size__ + LOADADDR(.kernel_text);
    .idle_text _IDLE_TXT_VMA : AT (_IDLE_TXT_LMA)
    { 
        *(.idle_text*);
        . = ALIGN(4); 
    } > public_ram
    __idle_size__ = SIZEOF(.idle_text);

    /* BSS de kernel */ 
    _KERNEL_BSS_LMA = __idle_size__ + LOADADDR(.idle_text);
    .kernel_bss _KERNEL_BSS_VMA : AT(_KERNEL_BSS_LMA)
    {
        *(.kernel_bss*);
        . = ALIGN(4); 
    } > public_ram
    __kernel_bss_size__ = SIZEOF(.kernel_bss);

    /* Data de kernel */ 
    _KERNEL_DATA_LMA = __kernel_bss_size__ + LOADADDR(.kernel_bss);
    .kernel_data _KERNEL_DATA_VMA : AT(_KERNEL_DATA_LMA)
    {
        *(.kernel_data*);
        . = ALIGN(4);
    } > public_ram
    __kernel_data_size__ = SIZEOF(.kernel_data);

    /* Read only data de kernel */ 
    _KERNEL_RODATA_LMA = __kernel_data_size__ + LOADADDR(.kernel_data);
    .kernel_rodata _KERNEL_RODATA_VMA : AT(_KERNEL_RODATA_LMA)
    {
        *(.kernel_rodata*);
        . = ALIGN(4);
    } > public_ram
    __kernel_rodata_size__ = SIZEOF(.kernel_rodata);

    /* ------------------------------ Tarea 1 ------------------------------ */

    /* Código de Tarea 1 */ 
    _TAREA_1_TXT_LMA = __kernel_rodata_size__ + LOADADDR(.kernel_rodata);
    .txt_tarea_1 _TAREA_1_TXT_VMA : AT(_TAREA_1_TXT_LMA)
    {
        *(.tarea_1_txt*);
        . = ALIGN(4);
    } > public_ram
    __tarea_1_txt_size__ = SIZEOF(.txt_tarea_1);    

    /* BSS de Tarea 1 */ 
    _TAREA_1_BSS_LMA = __tarea_1_txt_size__ + LOADADDR(.txt_tarea_1);
    .bss_tarea_1 _TAREA_1_BSS_VMA : AT(_TAREA_1_BSS_LMA)
    {
        *(.tarea_1_bss*);
        . = ALIGN(4); 
    } > public_ram
    __tarea_1_bss_size__ = SIZEOF(.bss_tarea_1);

    /* Data de Tarea 1 */ 
    _TAREA_1_DATA_LMA = __tarea_1_bss_size__ + LOADADDR(.bss_tarea_1);
    .data_tarea_1 _TAREA_1_DATA_VMA : AT(_TAREA_1_DATA_LMA)
    {
        *(.tarea_1_data*);
        . = ALIGN(4);
    } > public_ram
    __tarea_1_data_size__ = SIZEOF(.data_tarea_1);

    /* Read only data de la tarea 1 */ 
    _TAREA_1_RODATA_LMA = __tarea_1_data_size__ + LOADADDR(.data_tarea_1);
    .tarea_1_rodata _TAREA_1_RODATA_VMA : AT(_TAREA_1_RODATA_LMA)
    {
        *(.tarea_1_rodata*);
        . = ALIGN(4);
    } > public_ram
    __tarea_1_rodata_size__ = SIZEOF(.tarea_1_rodata);

    /* ------------------------------ Tarea 2 ------------------------------ */

    /* Código de Tarea 2 */ 
    _TAREA_2_TXT_LMA = __tarea_1_rodata_size__ + LOADADDR(.tarea_1_rodata);
    .txt_tarea_2 _TAREA_2_TXT_VMA : AT(_TAREA_2_TXT_LMA)
    {
        *(.tarea_2_txt*);
        . = ALIGN(4);
    } > public_ram
    __tarea_2_txt_size__ = SIZEOF(.txt_tarea_2);    

    /* BSS de Tarea 2 */ 
    _TAREA_2_BSS_LMA = __tarea_2_txt_size__ + LOADADDR(.txt_tarea_2);
    .bss_tarea_2 _TAREA_2_BSS_VMA : AT(_TAREA_2_BSS_LMA)
    {
        *(.tarea_2_bss*);
        . = ALIGN(4);
    } > public_ram
    __tarea_2_bss_size__ = SIZEOF(.bss_tarea_2);

    /* Data de Tarea 2 */ 
    _TAREA_2_DATA_LMA = __tarea_2_bss_size__ + LOADADDR(.bss_tarea_2);
    .data_tarea_2 _TAREA_2_DATA_VMA : AT(_TAREA_2_DATA_LMA)
    {
        *(.tarea_2_data*);
        . = ALIGN(4);
    } > public_ram
    __tarea_2_data_size__ = SIZEOF(.data_tarea_2);

    /* Read only data de la tarea 2 */ 
    _TAREA_2_RODATA_LMA = __tarea_2_data_size__ + LOADADDR(.data_tarea_2);
    .tarea_2_rodata _TAREA_2_RODATA_VMA : AT(_TAREA_2_RODATA_LMA)
    {
        *(.tarea_2_rodata*);
        . = ALIGN(4);
    } > public_ram
    __tarea_2_rodata_size__ = SIZEOF(.tarea_2_rodata);


    /* -------------------- STACK -------------------- */
    . = _PUBLIC_STACK_INIT;
    .stack (NOLOAD): 
    {

    /*Stack de memoria de las tareas (Dejo el unico que no uso) */

        *(.kernel_data*);
        . = ALIGN(4);
        /* Coloco aca los stacks de las interrupciones */

        __stack_pipe_line__ = .;
        . += PIPE_LINE_STACK;
        . = ALIGN(4);
        __pipe_line_stack_top__ = .;

        __stack_start__ = .;
        . += IRQ_STACK_SIZE;
        . = ALIGN(4);
        __irq_stack_top__ = .;

        . += FIQ_STACK_SIZE;
        . = ALIGN(4);
        __fiq_stack_top__ = .;

        . += SVC_STACK_SIZE;
        . = ALIGN(4);
        __svc_stack_top__ = .;

        . += ABT_STACK_SIZE;
        . = ALIGN(4);
        __abt_stack_top__ = .;

        . += UND_STACK_SIZE;
        . = ALIGN(4);
        __und_stack_top__ = .;

        . += SYC_STACK_SIZE;
        . = ALIGN(4);
        __syc_stack_top__ = .;

        . += USER_STACK_SIZE;
        . = ALIGN(4);
        __user_stack_top__ = .;

    } > public_stack

    /* ------------------------------ TTBR0 ------------------------------ */
    /* Tabla de primer nivel de tarea 1 */
    
    . = _TTBR0_KERNEL;
    .ttbr0_kernel (NOLOAD):
    {
        . = ALIGN(16384);
        __ttbr0_kernel_start__ = .;
        . = . + 0x4000;
        __ttbr0_kernel_end__ = .;
    } > public_ram

    . = _TTBR0_TAREA_1;
    .ttbr0_tarea_1 (NOLOAD):
    {
        . = ALIGN(16384);
        __ttbr0_tarea_1_start__ = .;
        . = . + 0x4000;
        __ttbr0_tarea_1_end__ = .;
    } > public_ram

    . = _TTBR0_TAREA_2;
    /* Tabla de primer nivel de tarea 2 */
    .ttbr0_tarea_2 (NOLOAD):
    {
        . = ALIGN(16384);
        __ttbr0_tarea_2_start__ = .;
        . = . + 0x4000;
        __ttbr0_tarea_2_end__ = .;
    } > public_ram

    /* ------------------------------ L2 ------------------------------ */
    .l2_tables_idel (NOLOAD):
    {
        . = ALIGN(1024);
        __base_l2_kernel_start__ = .;
        . += TABLE_L2_SIZE;
        __base_l2_kernel_end__ = .;
    } >public_ram

    .l2_tables_task1 (NOLOAD):
    {
        . = ALIGN(1024);
        __base_l2_tarea_1_start__ = .;
        . += TABLE_L2_SIZE;
        __base_l2_tarea_1_end__ = .;
    } >public_ram

    .l2_tables_task2 (NOLOAD):
    {
        . = ALIGN(1024);
        __base_l2_tarea_2_start__ = .;
        . += TABLE_L2_SIZE;
        __base_l2_tarea_2_end__ = .;
    } >public_ram

    /* -------------------------------------------------------------------------------------------------------------------------- */

    /* Stacks especificos para cada tarea, el idle no tiene un registro en especial separado porque va a utilizar el del modo svc */
    /* el cual es solo accesible desde el modo service */

    . = _TAREA_1_STACK_INIT_;
    .tarea_1_stack (NOLOAD):
    {
        . = . + STACK_USR_TAREA_1_SIZE;
        . = ALIGN(4);
        __usr_tarea1_top__ = .;

        . = . + STACK_IRQ_TAREA_1_SIZE;
        . = ALIGN(4);
        __irq_tarea1_top__ = .;
    } > stack_tarea1

    . = _TAREA_2_STACK_INIT_;
    .tarea_2_stack (NOLOAD):
    {
        . = . + STACK_USR_TAREA_2_SIZE;
        . = ALIGN(4);
        __usr_tarea2_top__ = .;

        . = . + STACK_IRQ_TAREA_2_SIZE;
        . = ALIGN(4);
        __irq_tarea2_top__ = .;
    } > stack_tarea2

    . = _IDLE_STACK_INIT_;
    .idle_stack (NOLOAD):
    {
        . = . + STACK_USR_IDLE_SIZE;
        . = ALIGN(4);
        __usr_idle_top__ = .;

        . = . + STACK_IRQ_IDLE_SIZE;
        . = ALIGN(4);
        __irq_idle_top__ = .;

        . = . + STACK_IRQ_IDLE_SIZE;
        . = ALIGN(4);
        __svc_idle_top__ = .;

    } > stack_idle

}