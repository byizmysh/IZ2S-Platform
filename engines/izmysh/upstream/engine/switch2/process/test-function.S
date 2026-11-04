// Runs in the target process and records native kernel identity and time.
.text
.global test_function
test_function:
    bti c
    stp x29, x30, [sp, #-32]!
    stp x19, x20, [sp, #16]
    mov x19, x0
    mov x20, x1
    mov w1, #0xffff8001
    svc #0x24
    stp x0, x1, [x19]
    mov w1, #0xffff8000
    svc #0x25
    stp x0, x1, [x19, #16]
    svc #0x1e
    str x0, [x19, #32]
    eor x0, x20, x20, lsl #1
    str x0, [x19, #40]
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #32
    ret
