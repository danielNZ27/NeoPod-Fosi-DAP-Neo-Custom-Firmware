    .syntax unified
    .thumb
    .section .hook_entry,"ax"
    .global stock_diag_entry
    .type stock_diag_entry,%function
stock_diag_entry:
    @ Stock scheduler/interrupts are still running for this original-driver call.
    sub sp, sp, #8
    movs r2, #255
    str r2, [sp]
    movw r0, #0
    movt r0, #1
    mov r1, sp
    ldr r3, =0x03060d29
    blx r3
    ldr r1, =stock_id_result
    str r0, [r1]
    cpsid i
    @ Stock execution ends here; use the proven diagnostic's private stack/BSS.
    ldr r0, =0x03004000
    msr msp, r0
    ldr r0, =device_reset
    bx r0
    .ltorg
