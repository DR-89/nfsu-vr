/* Only ABI callee-saved machine state lives here. RecompContext owns guest
 * registers, errno/last-error and native floating control/status separately. */
#if defined(__aarch64__)
.text
.p2align 2
.global xgf_native_swap
.hidden xgf_native_swap
.type xgf_native_swap, %function
xgf_native_swap:
    hint #34
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x9, sp
    str x9, [x0, #96]
    stp d8, d9, [x0, #104]
    stp d10, d11, [x0, #120]
    stp d12, d13, [x0, #136]
    stp d14, d15, [x0, #152]
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80]
    ldr x9, [x1, #96]
    ldp d8, d9, [x1, #104]
    ldp d10, d11, [x1, #120]
    ldp d12, d13, [x1, #136]
    ldp d14, d15, [x1, #152]
    mov sp, x9
    ret
.size xgf_native_swap, .-xgf_native_swap
.global xgf_native_bootstrap
.hidden xgf_native_bootstrap
.type xgf_native_bootstrap, %function
xgf_native_bootstrap:
    hint #34
    mov x0, x19
    bl xgf_fiber_entry
    brk #0
.size xgf_native_bootstrap, .-xgf_native_bootstrap
#elif defined(__x86_64__)
.text
.p2align 4
.global xgf_native_swap
.hidden xgf_native_swap
.type xgf_native_swap, @function
xgf_native_swap:
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)
    leaq 8(%rsp), %rax
    movq %rax, 48(%rdi)
    movq (%rsp), %rax
    movq %rax, 56(%rdi)
    movq 0(%rsi), %rbx
    movq 8(%rsi), %rbp
    movq 16(%rsi), %r12
    movq 24(%rsi), %r13
    movq 32(%rsi), %r14
    movq 40(%rsi), %r15
    movq 48(%rsi), %rsp
    jmp *56(%rsi)
.size xgf_native_swap, .-xgf_native_swap
.global xgf_native_bootstrap
.hidden xgf_native_bootstrap
.type xgf_native_bootstrap, @function
xgf_native_bootstrap:
    movq %r12, %rdi
    subq $8, %rsp
    call xgf_fiber_entry
    ud2
.size xgf_native_bootstrap, .-xgf_native_bootstrap
#else
#error Unsupported guest fiber ABI
#endif
.hidden xgf_fiber_entry
.section .note.GNU-stack,"",%progbits
