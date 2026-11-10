.section .text.iz_net_main,"ax",%progbits
.global iz_net_main
.type iz_net_main,%function
iz_net_main:
 bti jc
 stp x29,x30,[sp,#-16]!
 mov x29,sp
 ldr x1,[x0,#56]
 ldr x2,[x0,#64]
 add x1,x1,x2
 and x1,x1,#-16
 mov sp,x1
 bl iz_net_run
 mov sp,x29
 ldp x29,x30,[sp],#16
 ret
.size iz_net_main,.-iz_net_main
