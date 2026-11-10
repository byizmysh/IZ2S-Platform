.section .text.iz_main,"ax",%progbits
.global iz_main
.type iz_main,%function
iz_main:
 bti jc
 stp x29,x30,[sp,#-16]!
 mov x29,sp
 /* The final two ABI fields are located by a C-generated assembly include. */
 ldr x1,[x0,#IZ_STACK]
 ldr x2,[x0,#IZ_STACK_BYTES]
 add x1,x1,x2
 and x1,x1,#-16
 mov sp,x1
 bl iz_run
 mov sp,x29
 ldp x29,x30,[sp],#16
 ret
.size iz_main,.-iz_main
