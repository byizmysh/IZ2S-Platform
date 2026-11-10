.section .text.izpsx_entry,"ax",%progbits
.global izpsx_engine
.type izpsx_engine,%function
izpsx_engine:
 bti jc
 stp x29,x30,[sp,#-16]!
 mov x29,sp
 ldr x2,[x0,#16]
 ldr x2,[x2]
 cbz x2,1f
 mov sp,x2
1:
 bl izpsx_engine_event
 mov sp,x29
 ldp x29,x30,[sp],#16
 ret
.size izpsx_engine,.-izpsx_engine
