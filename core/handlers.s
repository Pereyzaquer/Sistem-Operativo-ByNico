.section .kernel_text
.global UND_handler
.global SVC_handler
.global PREFETCH_handler
.global ABT_handler
.global _reserved
.global IRQ_handler
.global FIQ_handler

// Traigo la dirreccion de memoria de la funcion que atiende el irq
.extern kernel_handler_irq

// Funciones para el manejo de las pipe lines
.extern syscall_pipeline_enqueue
.extern syscall_pipeline_dequeue
.extern svc_table

// ------------------------- HANDLERS -----------------------------------

UND_handler:
    LDR R10, =#0x0100
    RFEFD   SP!

SVC_handler:
    stmfd   sp!, {r1-r11, lr}       // Guardo r1-r11 y lr
    mrs     r1, spsr
    stmfd   sp!, {r1}               // Guardo SPSR

    bl      svc_table               // Llama a tabla de syscalls con r0 y r7

    ldmfd   sp!, {r1}               // Recupera SPSR
    msr     spsr_fsxc, r1
    ldmfd   sp!, {r1-r11, lr}       // Recupera resto del contexto (r0 intacto)
    movs    pc, lr                  // Vuelve a modo usuario

PREFETCH_handler:
    LDR R10, =#0x0300
    RFEFD   SP!

ABT_handler:
    LDR R10, =#0x0400
    RFEFD   SP!

_reserved:
    LDR R10, =#0x0500
    RFEFD   SP!

IRQ_handler:
    sub     lr, lr, #4              // Ajusto el valor de retorno
    stmfd   sp!, {r0-r11}
    mrs     r1, spsr
    stmfd   sp!, {r1, lr}

    mov     r0, sp
    bl      kernel_handler_irq
    mov     sp, r0

    ldmfd   sp!, {r1, lr}
    msr     spsr_fsxc, r1
    ldmfd   sp!, {r0-r11}
    movs    pc, lr

FIQ_handler:
    LDR R10, =#0x0600
    RFEFD   SP!

// ---------------- SYSCALL 0: Enviar ----------------
svc_send_pipeline:
    ldr     r1, =syscall_pipeline_enqueue
    blx     r1

    // Forzar retorno r0 = 0 (éxito)
    mov     r0, #0
    b       svc_return

// ---------------- SYSCALL 1: Recibir ----------------
svc_recv_pipeline:
    // Llamar a la función que coloca en r0 el dato recibido o error
    ldr     r1, =syscall_pipeline_dequeue
    blx     r1        // r0 = valor leído o -1
    b       svc_return

// ---------------- RESTAURACIÓN GENERAL ----------------
svc_return:
    ldmfd   sp!, {r1}               // Restaurar SPSR
    msr     spsr_fsxc, r1
    ldmfd   sp!, {r1-r11, lr}       // Restaurar todos menos r0
    movs    pc, lr                  // Vuelve al usuario (CPSR = SPSR)